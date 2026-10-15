// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin

#include <cerrno>
#include <cstring>

#include "decoder.h"

#ifdef HAVE_VPX
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#endif
#ifdef HAVE_DAV1D
#include <dav1d/dav1d.h>
#endif

namespace webmpb {

#ifdef HAVE_VPX
namespace {

class VpxDecoder : public Decoder {
    public:
	explicit VpxDecoder(Codec c)
	    : codec_(c)
	{
	}
	~VpxDecoder() override { close(); }

	bool open(std::string *error)
	{
		vpx_codec_iface_t *iface = codec_ == Codec::VP8 ?
		    vpx_codec_vp8_dx() :
		    vpx_codec_vp9_dx();
		vpx_codec_dec_cfg_t cfg = { };
		cfg.threads = 1;
		if (vpx_codec_dec_init(&ctx_, iface, &cfg, 0) != VPX_CODEC_OK) {
			*error = std::string("Cannot initialise libvpx: ") +
			    vpx_codec_error(&ctx_);
			return false;
		}
		open_ = true;
		return true;
	}

	bool reset(std::string *error) override
	{
		close();
		return open(error);
	}

	bool decode(const uint8_t *data, size_t size, const Picture **pic,
	    std::string *error) override
	{
		*pic = nullptr;
		if (!open_ && !open(error))
			return false;
		if (vpx_codec_decode(&ctx_, data, (unsigned)size, nullptr, 0) !=
		    VPX_CODEC_OK) {
			*error = std::string("VPx decode error: ") +
			    vpx_codec_error(&ctx_);
			return false;
		}
		vpx_codec_iter_t it = nullptr;
		const vpx_image_t *img = vpx_codec_get_frame(&ctx_, &it);
		if (!img)
			return true;

		int ok_fmt = 1;
		switch (img->fmt) {
		case VPX_IMG_FMT_I420:
		case VPX_IMG_FMT_I422:
		case VPX_IMG_FMT_I440:
		case VPX_IMG_FMT_I444:
		case VPX_IMG_FMT_I42016:
		case VPX_IMG_FMT_I42216:
		case VPX_IMG_FMT_I44016:
		case VPX_IMG_FMT_I44416:
			break;
		default:
			ok_fmt = 0;
		}
		if (!ok_fmt) {
			*error = "Unsupported VPx pixel format";
			return false;
		}
		pic_ = Picture();
		pic_.width = (int)img->d_w;
		pic_.height = (int)img->d_h;
		pic_.bit_depth = (int)img->bit_depth;
		pic_.wide = (img->fmt & VPX_IMG_FMT_HIGHBITDEPTH) != 0;
		pic_.xs = (int)img->x_chroma_shift;
		pic_.ys = (int)img->y_chroma_shift;
		for (int i = 0; i < 3; i++) {
			pic_.plane[i] = img->planes[i];
			pic_.stride[i] = img->stride[i];
		}
		switch (img->cs) {
		case VPX_CS_BT_601:
		case VPX_CS_SMPTE_170:
			pic_.matrix = 6;
			break;
		case VPX_CS_BT_709:
			pic_.matrix = 1;
			break;
		case VPX_CS_SMPTE_240:
			pic_.matrix = 7;
			break;
		case VPX_CS_BT_2020:
			pic_.matrix = 9;
			break;
		case VPX_CS_SRGB:
			pic_.matrix = 0;
			break;
		default:
			pic_.matrix = 2;
			break;
		}
		pic_.range = img->range == VPX_CR_FULL_RANGE ? 2 : 1;
		*pic = &pic_;
		return true;
	}

    private:
	void close()
	{
		if (open_)
			vpx_codec_destroy(&ctx_);
		open_ = false;
	}
	Codec codec_;
	vpx_codec_ctx_t ctx_ = { };
	bool open_ = false;
	Picture pic_;
};

} // namespace
#endif // HAVE_VPX

#ifdef HAVE_DAV1D
namespace {

class Dav1dDecoder : public Decoder {
    public:
	explicit Dav1dDecoder(const std::vector<uint8_t> &priv)
	{
		// av1C: 4 bytes of header, then optional configOBUs
		// (sequence header).
		if (priv.size() > 4)
			config_.assign(priv.begin() + 4, priv.end());
	}
	~Dav1dDecoder() override { close(); }

	bool open(std::string *error)
	{
		Dav1dSettings s;
		dav1d_default_settings(&s);
		s.n_threads = 1;
		s.max_frame_delay = 1;
		s.logger.callback = nullptr;
		s.all_layers = 0;
		if (dav1d_open(&ctx_, &s) < 0) {
			*error = "Cannot initialise dav1d";
			return false;
		}
		if (!config_.empty())
			feed(config_.data(), config_.size(), nullptr);
		return true;
	}

	// dav1d_flush() also forgets the sequence header, which may exist
	// only in the configOBUs, so start from scratch like open() does.
	bool reset(std::string *error) override
	{
		close();
		return open(error);
	}

	bool decode(const uint8_t *data, size_t size, const Picture **pic,
	    std::string *error) override
	{
		*pic = nullptr;
		if (!ctx_ && !open(error))
			return false;
		release();
		int r = feed(data, size, &pic_raw_);
		if (r < 0) {
			*error = "AV1 decode error";
			return false;
		}
		if (r == 0)
			return true; // no picture

		const Dav1dPicture &p = pic_raw_;
		pic_ = Picture();
		pic_.width = p.p.w;
		pic_.height = p.p.h;
		pic_.bit_depth = p.p.bpc;
		pic_.wide = p.p.bpc > 8;
		switch (p.p.layout) {
		case DAV1D_PIXEL_LAYOUT_I400:
			pic_.mono = true;
			pic_.xs = pic_.ys = 1;
			break;
		case DAV1D_PIXEL_LAYOUT_I420:
			pic_.xs = pic_.ys = 1;
			break;
		case DAV1D_PIXEL_LAYOUT_I422:
			pic_.xs = 1;
			pic_.ys = 0;
			break;
		case DAV1D_PIXEL_LAYOUT_I444:
			pic_.xs = pic_.ys = 0;
			break;
		}
		pic_.plane[0] = (const uint8_t *)p.data[0];
		pic_.stride[0] = p.stride[0];
		if (!pic_.mono) {
			pic_.plane[1] = (const uint8_t *)p.data[1];
			pic_.plane[2] = (const uint8_t *)p.data[2];
			pic_.stride[1] = pic_.stride[2] = p.stride[1];
		}
		if (p.seq_hdr) {
			pic_.matrix = (int)p.seq_hdr->mtrx;
			pic_.range = p.seq_hdr->color_range ? 2 : 1;
		}
		*pic = &pic_;
		return true;
	}

    private:
	void release()
	{
		if (have_pic_)
			dav1d_picture_unref(&pic_raw_);
		have_pic_ = false;
	}
	void close()
	{
		release();
		if (ctx_)
			dav1d_close(&ctx_);
		ctx_ = nullptr;
	}

	// Returns 1 if a picture was produced, 0 if not, -1 on error.
	int feed(const uint8_t *data, size_t size, Dav1dPicture *out)
	{
		Dav1dData d;
		uint8_t *buf = dav1d_data_create(&d, size);
		if (!buf)
			return -1;
		memcpy(buf, data, size);
		int got = 0;
		int ret = 0;
		do {
			if (d.sz > 0) {
				int r = dav1d_send_data(ctx_, &d);
				if (r < 0 && r != DAV1D_ERR(EAGAIN)) {
					dav1d_data_unref(&d);
					return -1;
				}
			}
			if (out && !got) {
				int r = dav1d_get_picture(ctx_, out);
				if (r == 0) {
					got = 1;
					have_pic_ = true;
				} else if (r != DAV1D_ERR(EAGAIN)) {
					ret = -1;
				}
			} else if (!out) {
				Dav1dPicture tmp = { };
				if (dav1d_get_picture(ctx_, &tmp) == 0)
					dav1d_picture_unref(&tmp);
			}
		} while (d.sz > 0 && ret == 0);
		if (d.sz > 0)
			dav1d_data_unref(&d);
		return ret < 0 ? -1 : got;
	}

	std::vector<uint8_t> config_;
	Dav1dContext *ctx_ = nullptr;
	Dav1dPicture pic_raw_ = { };
	bool have_pic_ = false;
	Picture pic_;
};

} // namespace
#endif // HAVE_DAV1D

bool
codec_supported(Codec codec)
{
	switch (codec) {
#ifdef HAVE_VPX
	case Codec::VP8:
	case Codec::VP9:
		return true;
#endif
#ifdef HAVE_DAV1D
	case Codec::AV1:
		return true;
#endif
	default:
		return false;
	}
}

std::unique_ptr<Decoder>
make_decoder(Codec codec, const std::vector<uint8_t> &priv, std::string *error)
{
	(void)priv;
	switch (codec) {
#ifdef HAVE_VPX
	case Codec::VP8:
	case Codec::VP9: {
		std::unique_ptr<VpxDecoder> d(new VpxDecoder(codec));
		if (!d->open(error))
			return nullptr;
		return std::unique_ptr<Decoder>(d.release());
	}
#endif
#ifdef HAVE_DAV1D
	case Codec::AV1: {
		std::unique_ptr<Dav1dDecoder> d(new Dav1dDecoder(priv));
		if (!d->open(error))
			return nullptr;
		return std::unique_ptr<Decoder>(d.release());
	}
#endif
	default:
		*error = "Codec support was not compiled in";
		return nullptr;
	}
}

} // namespace webmpb
