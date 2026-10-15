// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin

#include <algorithm>
#include <cstring>

#include "convert.h"
#include "player.h"

namespace webmpb {

namespace {

// Decoded frames are kept up to this much memory per animation; beyond
// that they are decoded again when needed.
constexpr uint64_t kCacheBudget = 128ull << 20;

void
set_error(GError **error, ParseResult r, const std::string &msg)
{
	GdkPixbufError code = GDK_PIXBUF_ERROR_CORRUPT_IMAGE;

	switch (r) {
	case ParseResult::Unsupported:
		code = GDK_PIXBUF_ERROR_UNKNOWN_TYPE;
		break;
	case ParseResult::TooLarge:
		code = GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY;
		break;
	default:
		break;
	}
	g_set_error_literal(error, GDK_PIXBUF_ERROR, code, msg.c_str());
}

bool
size_ok(uint64_t w, uint64_t h)
{
	return (w > 0 && h > 0 && w <= WEBM_MAX_DIMENSION
	    && h <= WEBM_MAX_DIMENSION && w * h <= WEBM_MAX_PIXELS);
}

} // namespace

int
Player::probe(const guint8 *data, gsize len, int *width, int *height,
    GError **error)
{
	Stream s;
	std::string msg;
	uint32_t w, h;

	std::vector<uint8_t> buf(data, data + len);
	ParseResult r = demux_headers(std::move(buf), &s, &msg);
	if (r == ParseResult::NeedMoreData)
		return (0);
	if (r != ParseResult::Ok) {
		set_error(error, r, msg);
		return (-1);
	}
	if (!codec_supported(s.codec)) {
		set_error(error, ParseResult::Unsupported,
		    "WebM codec support was not compiled in");
		return (-1);
	}
	stream_output_size(s, &w, &h);
	*width = (int)w;
	*height = (int)h;
	return (1);
}

Player *
Player::open(const guint8 *data, gsize len, GError **error)
{
	std::unique_ptr<Stream> s(new Stream());
	std::string msg;

	std::vector<uint8_t> buf(data, data + len);
	ParseResult r = demux(std::move(buf), s.get(), &msg);
	if (r == ParseResult::Ok && !codec_supported(s->codec)) {
		r = ParseResult::Unsupported;
		msg = "WebM codec support was not compiled in";
	}
	if (r != ParseResult::Ok) {
		set_error(error, r, msg);
		return (nullptr);
	}
	std::unique_ptr<Player> p(new Player(std::move(s)));
	if (!p->init(&msg)) {
		set_error(error, ParseResult::Corrupt, msg);
		return (nullptr);
	}
	return (p.release());
}

Player::Player(std::unique_ptr<Stream> s)
    : s_(std::move(s))
{
	uint32_t w, h;

	stream_output_size(*s_, &w, &h);
	width_ = (int)w;
	height_ = (int)h;
	cache_.assign(s_->frames.size(), nullptr);
}

Player::~Player()
{
	for (GdkPixbuf *p : cache_)
		if (p != nullptr)
			g_object_unref(p);
	if (first_ != nullptr)
		g_object_unref(first_);
	if (last_ != nullptr)
		g_object_unref(last_);
}

bool
Player::init(std::string *error)
{
	if (!size_ok(width_, height_)) {
		*error = "WebM image has an unsupported size";
		return (false);
	}
	dec_ = make_decoder(s_->codec, s_->codec_private, error);
	if (!dec_)
		return (false);
	if (s_->has_alpha) {
		alpha_dec_ = make_decoder(s_->codec, s_->codec_private, error);
		if (!alpha_dec_)
			return (false);
	}
	GdkPixbuf *f = frame(0);
	if (f == nullptr) {
		*error = error_.empty() ? "Cannot decode the first WebM frame" :
					  error_;
		return (false);
	}
	g_object_unref(f);
	return (true);
}

size_t
Player::frame_at(int64_t pos_ns) const
{
	const std::vector<Frame> &f = s_->frames;
	size_t lo = 0, hi = f.size();

	// Binary search for the last frame that starts at or before pos_ns.
	while (hi - lo > 1) {
		size_t mid = (lo + hi) / 2;
		if (f[mid].start_ns <= pos_ns)
			lo = mid;
		else
			hi = mid;
	}
	return (lo);
}

void
Player::set_size(int w, int h)
{
	std::lock_guard<std::mutex> lock(mu_);

	if (w <= 0 || h <= 0 || (w == width_ && h == height_))
		return;
	width_ = w;
	height_ = h;
	// Throw everything away; frames are re-decoded on demand.
	for (GdkPixbuf *&p : cache_) {
		if (p != nullptr)
			g_object_unref(p);
		p = nullptr;
	}
	cache_bytes_ = 0;
	if (first_ != nullptr)
		g_object_unref(first_);
	first_ = nullptr;
	if (last_ != nullptr)
		g_object_unref(last_);
	last_ = nullptr;
	next_ = 0;
	need_reset_ = true;
}

GdkPixbuf *
Player::first()
{
	if (first_ == nullptr) {
		GdkPixbuf *f = frame(0);
		std::lock_guard<std::mutex> lock(mu_);
		if (first_ == nullptr)
			first_ = f;
		else if (f != nullptr)
			g_object_unref(f);
	}
	return (first_);
}

GdkPixbuf *
Player::frame(size_t i)
{
	std::lock_guard<std::mutex> lock(mu_);
	GdkPixbuf *result = nullptr;

	if (i >= cache_.size())
		return (nullptr);
	if (cache_[i] != nullptr)
		return ((GdkPixbuf *)g_object_ref(cache_[i]));

	if (need_reset_ || i < next_) {
		// Start over at the nearest keyframe.
		size_t k = i;
		std::string err;

		while (k > 0 && !s_->frames[k].key)
			k--;
		dec_->reset(&err);
		if (alpha_dec_)
			alpha_dec_->reset(&err);
		have_alpha_ = false;
		next_ = k;
		need_reset_ = false;
	}
	for (size_t f = next_; f <= i; f++) {
		GdkPixbuf *pb = decode_frame(f);

		if (pb == nullptr) {
			// Corrupt frame: keep showing the previous picture.
			if (last_ != nullptr)
				pb = (GdkPixbuf *)g_object_ref(last_);
		} else {
			if (last_ != nullptr)
				g_object_unref(last_);
			last_ = (GdkPixbuf *)g_object_ref(pb);
		}
		if (pb != nullptr && cache_[f] == nullptr
		    && cache_bytes_ + frame_bytes() <= kCacheBudget) {
			cache_[f] = (GdkPixbuf *)g_object_ref(pb);
			cache_bytes_ += frame_bytes();
		}
		if (f == i)
			result = pb;
		else if (pb != nullptr)
			g_object_unref(pb);
	}
	next_ = i + 1;
	return (result);
}

GdkPixbuf *
Player::decode_frame(size_t f)
{
	const Frame &fr = s_->frames[f];
	const Picture *pic = nullptr;
	std::string err;

	for (size_t k = 0; k < fr.n_packets; k++) {
		const Packet &pk = s_->packets[fr.first_packet + k];

		if (!dec_->decode(pk.data.data(), pk.data.size(), &pic, &err)) {
			error_ = err;
			return (nullptr);
		}
		if (alpha_dec_ && !pk.alpha.empty()) {
			const Picture *ap = nullptr;

			if (alpha_dec_->decode(pk.alpha.data(), pk.alpha.size(),
				&ap, &err)
			    && ap != nullptr)
				copy_alpha(*ap);
		}
	}
	if (pic == nullptr) {
		error_ = "WebM packet produced no picture";
		return (nullptr);
	}
	if (!size_ok(pic->width, pic->height)) {
		error_ = "Decoded WebM frame has an unsupported size";
		return (nullptr);
	}

	// Work out how to interpret the samples: the bitstream wins, then the
	// container, then a guess.
	int matrix = pic->matrix;
	if (matrix == 2)
		matrix = s_->color.matrix;
	if (matrix == 2)
		matrix = default_matrix(pic->height);
	int range = pic->range;
	if (s_->codec == Codec::VP8 && s_->color.range != 0)
		range = s_->color.range;
	if (range == 0)
		range = s_->color.range != 0 ? s_->color.range : 1;

	bool alpha = s_->has_alpha;
	GdkPixbuf *pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, pic->width,
	    pic->height);
	if (pb == nullptr) {
		error_ = "Not enough memory for a WebM frame";
		return (nullptr);
	}
	convert_to_rgb(*pic, matrix, range,
	    (alpha && have_alpha_) ? &alpha_pic_ : nullptr,
	    gdk_pixbuf_get_pixels(pb), gdk_pixbuf_get_rowstride(pb),
	    alpha ? 4 : 3);
	if (pic->width != width_ || pic->height != height_) {
		GdkPixbuf *sc = gdk_pixbuf_scale_simple(pb, width_, height_,
		    GDK_INTERP_BILINEAR);

		g_object_unref(pb);
		pb = sc;
		if (pb == nullptr)
			error_ = "Not enough memory for a WebM frame";
	}
	return (pb);
}

// Keep our own copy of the alpha plane; the decoder's buffer dies on the
// next call.
void
Player::copy_alpha(const Picture &ap)
{
	int shift = ap.bit_depth > 8 ? ap.bit_depth - 8 : 0;

	alpha_plane_.resize((size_t)ap.width * ap.height);
	for (int y = 0; y < ap.height; y++) {
		uint8_t *dst = &alpha_plane_[(size_t)y * ap.width];

		if (ap.wide) {
			const uint16_t *src = (const uint16_t *)(ap.plane[0]
			    + y * ap.stride[0]);

			for (int x = 0; x < ap.width; x++)
				dst[x] = (uint8_t)(src[x] >> shift);
		} else {
			memcpy(dst, ap.plane[0] + y * ap.stride[0], ap.width);
		}
	}
	alpha_pic_ = Picture();
	alpha_pic_.width = ap.width;
	alpha_pic_.height = ap.height;
	alpha_pic_.mono = true;
	alpha_pic_.plane[0] = alpha_plane_.data();
	alpha_pic_.stride[0] = ap.width;
	have_alpha_ = true;
}

} // namespace webmpb
