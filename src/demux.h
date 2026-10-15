// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin
//
// WebM demuxing on top of libwebm's webm_parser.

#ifndef WEBM_PIXBUF_DEMUX_H
#define WEBM_PIXBUF_DEMUX_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace webmpb {

enum class Codec { VP8, VP9, AV1 };

// Colour description from the container (Matroska numbering, same as
// ISO/IEC 23091-2).  Only used when the bitstream does not say.
struct ColorInfo {
	int matrix = 2; // 2 == unspecified
	int range = 0;	// 0 unspecified, 1 limited ("broadcast"), 2 full
};

// One block frame as stored in the file.
struct Packet {
	std::vector<uint8_t> data;
	std::vector<uint8_t> alpha; // BlockAdditional with BlockAddID 1, if any
	bool key = false;
	bool visible = true;
};

// One picture that is shown to the user.  It is produced by feeding
// packets [first_packet, first_packet + n_packets) to the decoder; all
// but the last one are invisible (VP8 alt-ref frames and the like).
struct Frame {
	size_t first_packet = 0;
	size_t n_packets = 0;
	bool key = false;
	int64_t start_ns = 0; // position on the (gap-free) timeline
	int64_t duration_ns = 0;
};

struct Stream {
	Codec codec = Codec::VP9;
	std::vector<uint8_t> codec_private;
	uint32_t pixel_width = 0, pixel_height = 0;
	uint32_t display_width = 0, display_height = 0; // 0 if not given
	bool has_alpha = false; // VP8/VP9 with a BlockAdditional alpha channel
	ColorInfo color;
	std::vector<Packet> packets;
	std::vector<Frame> frames;
	int64_t total_ns = 0; // sum of all frame durations
};

// The size an application should expect for the image: DisplayWidth/Height
// when given, otherwise the coded size.
void stream_output_size(const Stream &s, uint32_t *w, uint32_t *h);

enum class ParseResult {
	Ok,
	NeedMoreData, // headers_only: no complete Tracks yet
	Unsupported, // well-formed WebM/Matroska but no VP8/VP9/AV1 video track
	Corrupt,
	TooLarge,
};

// Parse |data| completely.  Truncated files are accepted as long as at
// least one frame is present.
ParseResult demux(std::vector<uint8_t> data, Stream *out, std::string *error);

// Parse only up to the first Cluster; fills everything in Stream except
// packets and frames.  Used to report the image size early.
ParseResult demux_headers(std::vector<uint8_t> data, Stream *out,
    std::string *error);

} // namespace webmpb

#endif
