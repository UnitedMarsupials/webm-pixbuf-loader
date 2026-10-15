// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin

#include <algorithm>
#include <cstring>
#include <memory>
#include <webm/buffer_reader.h>
#include <webm/callback.h>
#include <webm/status.h>
#include <webm/webm_parser.h>

#include "demux.h"

namespace webmpb {

namespace {

constexpr uint64_t kMaxTotalBytes = 512ull << 20; // of packet payload
constexpr int kAbortHeadersDone = 1;		  // user status code

bool
codec_from_id(const std::string &id, Codec *c)
{
	if (id == "V_VP8")
		*c = Codec::VP8;
	else if (id == "V_VP9")
		*c = Codec::VP9;
	else if (id == "V_AV1")
		*c = Codec::AV1;
	else
		return false;
	return true;
}

class Collector : public webm::Callback {
    public:
	Collector(Stream *out, bool headers_only)
	    : out_(out)
	    , headers_only_(headers_only)
	{
	}

	bool seen_ebml = false;
	bool have_track = false;
	bool too_large = false;
	bool saw_video_track = false;

	webm::Status OnEbml(const webm::ElementMetadata &,
	    const webm::Ebml &ebml) override
	{
		seen_ebml = true;
		(void)ebml;
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnInfo(const webm::ElementMetadata &,
	    const webm::Info &info) override
	{
		uint64_t s = info.timecode_scale.value();
		scale_ = s ? s : 1000000;
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnTrackEntry(const webm::ElementMetadata &,
	    const webm::TrackEntry &t) override
	{
		if (have_track ||
		    t.track_type.value() != webm::TrackType::kVideo)
			return webm::Status(webm::Status::kOkCompleted);
		saw_video_track = true;
		Codec c;
		if (!codec_from_id(t.codec_id.value(), &c) ||
		    !t.video.is_present())
			return webm::Status(webm::Status::kOkCompleted);
		// Content encodings (compression, encryption) are not
		// supported.
		if (t.content_encodings.is_present() &&
		    !t.content_encodings.value().encodings.empty())
			return webm::Status(webm::Status::kOkCompleted);

		const webm::Video &v = t.video.value();
		uint64_t w = v.pixel_width.value(), h = v.pixel_height.value();
		if (w > UINT32_MAX || h > UINT32_MAX)
			return webm::Status(webm::Status::kOkCompleted);
		have_track = true;
		track_ = t.track_number.value();
		default_duration_ = t.default_duration.value();
		out_->codec = c;
		out_->codec_private = t.codec_private.value();
		out_->pixel_width = (uint32_t)w;
		out_->pixel_height = (uint32_t)h;
		if (v.display_unit.value() == webm::DisplayUnit::kPixels &&
		    v.display_width.is_present() &&
		    v.display_height.is_present()) {
			uint64_t dw = v.display_width.value(),
				 dh = v.display_height.value();
			if (dw && dh && dw <= UINT32_MAX && dh <= UINT32_MAX) {
				out_->display_width = (uint32_t)dw;
				out_->display_height = (uint32_t)dh;
			}
		}
		out_->has_alpha = (c != Codec::AV1) &&
		    v.alpha_mode.value() == 1;
		if (v.colour.is_present()) {
			const webm::Colour &col = v.colour.value();
			out_->color.matrix =
			    (int)col.matrix_coefficients.value();
			out_->color.range = (int)col.range.value();
		}
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnClusterBegin(const webm::ElementMetadata &,
	    const webm::Cluster &cluster, webm::Action *action) override
	{
		*action = webm::Action::kRead;
		if (headers_only_)
			return webm::Status(kAbortHeadersDone);
		cluster_tc_ = cluster.timecode.value();
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnSimpleBlockBegin(const webm::ElementMetadata &,
	    const webm::SimpleBlock &b, webm::Action *action) override
	{
		begin_block(b, action);
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnSimpleBlockEnd(const webm::ElementMetadata &,
	    const webm::SimpleBlock &b) override
	{
		if (!active_)
			return webm::Status(webm::Status::kOkCompleted);
		active_ = false;
		return finish_block(b, b.is_key_frame, nullptr, 0);
	}

	webm::Status OnBlockGroupBegin(const webm::ElementMetadata &,
	    webm::Action *action) override
	{
		*action = webm::Action::kRead;
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnBlockBegin(const webm::ElementMetadata &,
	    const webm::Block &b, webm::Action *action) override
	{
		begin_block(b, action);
		return webm::Status(webm::Status::kOkCompleted);
	}

	webm::Status OnBlockGroupEnd(const webm::ElementMetadata &,
	    const webm::BlockGroup &g) override
	{
		if (!active_)
			return webm::Status(webm::Status::kOkCompleted);
		active_ = false;
		if (!g.block.is_present())
			return webm::Status(webm::Status::kOkCompleted);
		// A block without references is a keyframe.
		bool key = g.references.empty();
		const std::vector<uint8_t> *alpha = nullptr;
		if (g.additions.is_present()) {
			for (const auto &m : g.additions.value().block_mores) {
				if (m.value().id.value() == 1) {
					alpha = &m.value().data.value();
					break;
				}
			}
		}
		uint64_t dur = g.duration.is_present() ? g.duration.value() : 0;
		return finish_block(g.block.value(), key, alpha, dur);
	}

	webm::Status OnFrame(const webm::FrameMetadata &m, webm::Reader *reader,
	    std::uint64_t *remaining) override
	{
		if (!active_) {
			uint64_t skipped = 0;
			webm::Status s = reader->Skip(*remaining, &skipped);
			*remaining -= skipped;
			return s;
		}
		if (frames_.empty() || m.position != last_frame_pos_) {
			frames_.emplace_back();
			last_frame_pos_ = m.position;
		}
		std::vector<uint8_t> &dst = frames_.back();
		while (*remaining > 0) {
			if (total_bytes_ + dst.size() > kMaxTotalBytes) {
				too_large = true;
				return webm::Status(
				    webm::Status::kNotEnoughMemory);
			}
			size_t chunk = (size_t)std::min<uint64_t>(*remaining,
			    1 << 16);
			size_t old = dst.size();
			dst.resize(old + chunk);
			uint64_t got = 0;
			webm::Status s = reader->Read(chunk, dst.data() + old,
			    &got);
			dst.resize(old + (size_t)got);
			*remaining -= got;
			if (!s.completed_ok())
				return s;
			if (got == 0)
				return webm::Status(webm::Status::kEndOfFile);
		}
		return webm::Status(webm::Status::kOkCompleted);
	}

    private:
	template <class B> void begin_block(const B &b, webm::Action *action)
	{
		frames_.clear();
		active_ = have_track && b.track_number == track_;
		*action = active_ ? webm::Action::kRead : webm::Action::kSkip;
		(void)b;
	}

	webm::Status finish_block(const webm::Block &b, bool key,
	    const std::vector<uint8_t> *alpha, uint64_t duration_units)
	{
		int64_t tc;
		if (__builtin_add_overflow((int64_t)cluster_tc_,
			(int64_t)b.timecode, &tc) ||
		    tc < 0 || tc > (int64_t)1 << 48)
			return webm::Status(webm::Status::kInvalidElementValue);
		int64_t base_ns;
		if (__builtin_mul_overflow(tc, (int64_t)scale_, &base_ns))
			return webm::Status(webm::Status::kInvalidElementValue);

		for (size_t i = 0; i < frames_.size(); i++) {
			Packet p;
			total_bytes_ += frames_[i].size();
			p.data = std::move(frames_[i]);
			// Lacing is not used for video in practice;
			// space laced frames out by the default
			// duration.
			if (i == 0 && alpha) {
				p.alpha = *alpha;
				total_bytes_ += alpha->size();
			}
			p.key = key;
			p.visible = b.is_visible;
			out_->packets.push_back(std::move(p));
			pts_.push_back(
			    base_ns + (int64_t)i * (int64_t)default_duration_);
			durs_.push_back(i + 1 == frames_.size() ?
				duration_units * scale_ :
				0);
		}
		frames_.clear();
		return webm::Status(webm::Status::kOkCompleted);
	}

    public:
	// Build out_->frames and the timeline once the file has been
	// read.
	void finalize()
	{
		std::vector<Packet> &pk = out_->packets;
		std::vector<int64_t> pts; // per frame
		std::vector<int64_t> explicit_dur;
		size_t group = 0;
		for (size_t j = 0; j < pk.size(); j++) {
			if (!pk[j].visible)
				continue;
			Frame f;
			f.first_packet = group;
			f.n_packets = j - group + 1;
			f.key = false;
			for (size_t k = group; k <= j; k++)
				f.key = f.key || pk[k].key;
			out_->frames.push_back(f);
			pts.push_back(pts_[j]);
			explicit_dur.push_back((int64_t)durs_[j]);
			group = j + 1;
		}
		size_t n = out_->frames.size();
		int64_t t = 0, prev = 0;
		for (size_t i = 0; i < n; i++) {
			int64_t d = 0;
			if (i + 1 < n)
				d = pts[i + 1] - pts[i];
			if (d <= 0)
				d = explicit_dur[i];
			if (d <= 0)
				d = (int64_t)default_duration_;
			if (d <= 0)
				d = prev;
			if (d <= 0)
				d = 33333333;
			out_->frames[i].start_ns = t;
			out_->frames[i].duration_ns = d;
			t += d;
			prev = d;
		}
		out_->total_ns = t;
	}

    private:
	Stream *out_;
	bool headers_only_;
	uint64_t scale_ = 1000000;
	uint64_t track_ = 0;
	uint64_t default_duration_ = 0;
	uint64_t cluster_tc_ = 0;
	uint64_t total_bytes_ = 0;
	uint64_t last_frame_pos_ = 0;
	bool active_ = false;
	std::vector<std::vector<uint8_t>> frames_;
	std::vector<int64_t> pts_;
	std::vector<uint64_t> durs_;
};

ParseResult
run(std::vector<uint8_t> &&data, Stream *out, std::string *error,
    bool headers_only)
{
	*out = Stream();
	Collector cb(out, headers_only);
	webm::BufferReader reader(std::move(data));
	webm::WebmParser parser;
	webm::Status st = parser.Feed(&cb, &reader);

	if (cb.too_large) {
		*error = "WebM file contains too much data";
		return ParseResult::TooLarge;
	}
	if (!cb.seen_ebml) {
		*error = "Not a WebM file";
		return ParseResult::Corrupt;
	}
	if (headers_only) {
		if (cb.have_track && st.code == kAbortHeadersDone)
			return ParseResult::Ok;
		if (cb.have_track && st.ok())
			return ParseResult::Ok;
		if (st.is_parsing_error()) {
			*error = "Corrupt WebM header";
			return ParseResult::Corrupt;
		}
		return ParseResult::NeedMoreData;
	}

	// EOF in the middle of the stream is fine (truncated download):
	// use whatever was collected.  Genuine syntax errors are not.
	if (st.is_parsing_error() && out->packets.empty()) {
		*error = "Corrupt WebM file";
		return ParseResult::Corrupt;
	}
	if (!cb.have_track) {
		*error = cb.saw_video_track ?
		    "WebM video track uses an unsupported codec" :
		    "WebM file has no video track";
		return ParseResult::Unsupported;
	}
	cb.finalize();
	if (out->frames.empty()) {
		*error = "WebM file contains no video frames";
		return ParseResult::Corrupt;
	}
	return ParseResult::Ok;
}

} // namespace

void
stream_output_size(const Stream &s, uint32_t *w, uint32_t *h)
{
	if (s.display_width && s.display_height) {
		*w = s.display_width;
		*h = s.display_height;
	} else {
		*w = s.pixel_width;
		*h = s.pixel_height;
	}
}

ParseResult
demux(std::vector<uint8_t> data, Stream *out, std::string *error)
{
	return run(std::move(data), out, error, false);
}

ParseResult
demux_headers(std::vector<uint8_t> data, Stream *out, std::string *error)
{
	return run(std::move(data), out, error, true);
}

} // namespace webmpb
