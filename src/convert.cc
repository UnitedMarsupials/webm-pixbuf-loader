// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin

#include <algorithm>
#include <vector>

#include "convert.h"

namespace webmpb {

namespace {

struct Coefs {
	int y_off;     // subtracted from luma
	int y_scale;   // 16.16
	int rv, bu;    // chroma -> R, B (16.16)
	int gu, gv;    // chroma -> G (16.16)
	bool identity; // GBR: G = Y, B = U, R = V
};

int
fix(double v)
{
	return (int)(v * 65536.0 + (v < 0 ? -0.5 : 0.5));
}

Coefs
make_coefs(int matrix, bool full)
{
	double kr, kb;
	Coefs c = { };
	switch (matrix) {
	case 0:
		c.identity = true;
		kr = kb = 0;
		break;
	case 1:
		kr = 0.2126;
		kb = 0.0722;
		break; // BT.709
	case 4:
		kr = 0.30;
		kb = 0.11;
		break; // FCC
	case 7:
		kr = 0.212;
		kb = 0.087;
		break; // SMPTE 240M
	case 9:
	case 10:
		kr = 0.2627;
		kb = 0.0593;
		break; // BT.2020
	case 5:
	case 6:
	default:
		kr = 0.299;
		kb = 0.114;
		break; // BT.601
	}
	double ys = full ? 1.0 : 255.0 / 219.0;
	double cs = full ? 1.0 : 255.0 / 224.0;
	c.y_off = full ? 0 : 16;
	c.y_scale = fix(ys);
	if (c.identity)
		return c;
	double kg = 1.0 - kr - kb;
	c.rv = fix(2.0 * (1.0 - kr) * cs);
	c.bu = fix(2.0 * (1.0 - kb) * cs);
	c.gu = fix(2.0 * kb * (1.0 - kb) / kg * cs);
	c.gv = fix(2.0 * kr * (1.0 - kr) / kg * cs);
	return c;
}

inline uint8_t
clamp8(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

// Copy one row of samples, reduced to 8 bit.
template <typename T>
void
load_row(const uint8_t *base, ptrdiff_t stride, int row, int n, int shift,
    uint8_t *dst)
{
	const T *src = reinterpret_cast<const T *>(
	    base + (ptrdiff_t)row * stride);
	if (shift == 0) {
		for (int i = 0; i < n; i++)
			dst[i] = (uint8_t)src[i];
	} else {
		for (int i = 0; i < n; i++)
			dst[i] = (uint8_t)(src[i] >> shift);
	}
}

void
load(const Picture &p, int plane, int row, int n, uint8_t *dst)
{
	int shift = p.bit_depth > 8 ? p.bit_depth - 8 : 0;
	if (p.wide)
		load_row<uint16_t>(p.plane[plane], p.stride[plane], row, n,
		    shift, dst);
	else
		load_row<uint8_t>(p.plane[plane], p.stride[plane], row, n, 0,
		    dst);
}

// Horizontally upsample a chroma row of |cw| samples to |w| pixels with
// a 3:1 triangle filter (centred siting) when xs == 1.
void
upsample_h(const uint8_t *src, int cw, int w, int xs, uint8_t *dst)
{
	if (!xs) {
		std::copy(src, src + w, dst);
		return;
	}
	for (int x = 0; x < w; x++) {
		int c = x >> 1;
		int n = (x & 1) ? std::min(c + 1, cw - 1) : std::max(c - 1, 0);
		dst[x] = (uint8_t)((3 * src[c] + src[n] + 2) >> 2);
	}
}

} // namespace

int
default_matrix(int height)
{
	return height >= 720 ? 1 : 6;
}

void
convert_to_rgb(const Picture &p, int matrix, int range, const Picture *alpha,
    uint8_t *out, size_t rowstride, int nch)
{
	const int w = p.width, h = p.height;
	const Coefs k = make_coefs(matrix, range == 2);
	const int cw = (w + (1 << p.xs) - 1) >> p.xs;
	const int ch = (h + (1 << p.ys) - 1) >> p.ys;

	std::vector<uint8_t> yrow(w), urow(w), vrow(w);
	std::vector<uint8_t> uraw(cw), vraw(cw), uraw2(cw), vraw2(cw);
	std::vector<uint8_t> arow(alpha ? alpha->width : 0);

	for (int y = 0; y < h; y++) {
		load(p, 0, y, w, yrow.data());

		if (p.mono) {
			std::fill(urow.begin(), urow.end(), 128);
			std::fill(vrow.begin(), vrow.end(), 128);
		} else {
			int cy = y >> p.ys;
			load(p, 1, cy, cw, uraw.data());
			load(p, 2, cy, cw, vraw.data());
			upsample_h(uraw.data(), cw, w, p.xs, urow.data());
			upsample_h(vraw.data(), cw, w, p.xs, vrow.data());
			if (p.ys) {
				int ny = (y & 1) ? std::min(cy + 1, ch - 1) :
						   std::max(cy - 1, 0);
				load(p, 1, ny, cw, uraw2.data());
				load(p, 2, ny, cw, vraw2.data());
				std::vector<uint8_t> t(w);
				upsample_h(uraw2.data(), cw, w, p.xs, t.data());
				for (int x = 0; x < w; x++)
					urow[x] = (uint8_t)((3 * urow[x] +
								t[x] + 2) >>
					    2);
				upsample_h(vraw2.data(), cw, w, p.xs, t.data());
				for (int x = 0; x < w; x++)
					vrow[x] = (uint8_t)((3 * vrow[x] +
								t[x] + 2) >>
					    2);
			}
		}

		if (alpha) {
			int ay = alpha->height == h ?
			    y :
			    (int)((int64_t)y * alpha->height / h);
			load(*alpha, 0, ay, alpha->width, arow.data());
		}

		uint8_t *o = out + (size_t)y * rowstride;
		for (int x = 0; x < w; x++, o += nch) {
			int Y = yrow[x], U = urow[x], V = vrow[x];
			int r, g, b;
			if (k.identity) {
				g = ((Y - k.y_off) * k.y_scale + 32768) >> 16;
				b = ((U - k.y_off) * k.y_scale + 32768) >> 16;
				r = ((V - k.y_off) * k.y_scale + 32768) >> 16;
			} else {
				int yy = (Y - k.y_off) * k.y_scale + 32768;
				int u = U - 128, v = V - 128;
				r = (yy + k.rv * v) >> 16;
				g = (yy - k.gu * u - k.gv * v) >> 16;
				b = (yy + k.bu * u) >> 16;
			}
			o[0] = clamp8(r);
			o[1] = clamp8(g);
			o[2] = clamp8(b);
			if (nch == 4) {
				if (alpha) {
					int ax = alpha->width == w ?
					    x :
					    (int)((int64_t)x * alpha->width /
						w);
					o[3] = arow[ax];
				} else {
					o[3] = 255;
				}
			}
		}
	}
}

} // namespace webmpb
