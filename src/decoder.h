// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin
//
// Thin wrapper around libvpx (VP8/VP9) and dav1d (AV1).

#ifndef WEBM_PIXBUF_DECODER_H
#define WEBM_PIXBUF_DECODER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "demux.h"

namespace webmpb {

// A decoded picture, planar YUV.  The pointers are owned by the decoder and
// valid until the next call to decode() or reset().
struct Picture {
	int width = 0, height = 0;
	int bit_depth = 8;  // 8, 10 or 12
	bool wide = false;  // samples are 16 bit
	int xs = 1, ys = 1; // log2 chroma subsampling
	bool mono = false;  // no chroma planes
	const uint8_t *plane[3] = { nullptr, nullptr, nullptr };
	ptrdiff_t stride[3] = { 0, 0, 0 }; // bytes
	int matrix = 2; // Matroska/ISO 23091-2 numbering, 2 == unspecified
	int range = 0;	// 0 unspecified, 1 limited, 2 full
};

class Decoder {
    public:
	virtual ~Decoder() = default;

	// Decode one packet.  On success *pic is the picture that became
	// visible or nullptr if the packet did not produce one.
	virtual bool decode(const uint8_t *data, size_t size,
	    const Picture **pic, std::string *error) = 0;

	// Forget all reference state; the next packet must be a keyframe.
	virtual bool reset(std::string *error) = 0;
};

// Whether support for the codec was compiled in.
bool codec_supported(Codec codec);

std::unique_ptr<Decoder> make_decoder(Codec codec,
    const std::vector<uint8_t> &codec_private, std::string *error);

} // namespace webmpb

#endif
