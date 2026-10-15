// SPDX-License-Identifier: LGPL-2.1-or-later
//
// A parsed WebM file whose frames are decoded on demand.

#ifndef WEBM_PLAYER_H
#define WEBM_PLAYER_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "decoder.h"
#include "demux.h"

#define WEBM_MAX_DIMENSION 16384
#define WEBM_MAX_PIXELS (100ULL * 1000 * 1000)

namespace webmpb {

class Player {
public:
	// Look at the beginning of a file.  Returns 1 and the image size once
	// the headers are complete, 0 if more data is needed, -1 (with *error
	// set) if the file cannot be handled.
	static int
	probe(const guint8 *data, gsize len, int *width, int *height,
	    GError **error);

	// Parse a whole file and decode its first frame.
	static Player *
	open(const guint8 *data, gsize len, GError **error);

	~Player();

	int
	width() const
	{
		return (width_);
	}
	int
	height() const
	{
		return (height_);
	}
	size_t
	n_frames() const
	{
		return (s_->frames.size());
	}
	int64_t
	total_ns() const
	{
		return (s_->total_ns);
	}
	int64_t
	start_ns(size_t i) const
	{
		return (s_->frames[i].start_ns);
	}

	// Index of the frame shown at pos_ns (0 <= pos_ns < total_ns()).
	size_t
	frame_at(int64_t pos_ns) const;

	// Scale all frames to this size; call before the first frame is used.
	void
	set_size(int width, int height);

	// Frame 0, owned by the player.
	GdkPixbuf *
	first();

	// New reference to a frame, decoded on demand; NULL on failure.
	GdkPixbuf *
	frame(size_t i);

private:
	explicit Player(std::unique_ptr<Stream> s);
	bool
	init(std::string *error);
	uint64_t
	frame_bytes() const
	{
		return ((uint64_t)width_ * height_ * 4);
	}
	GdkPixbuf *
	decode_frame(size_t f);
	void
	copy_alpha(const Picture &ap);

	std::unique_ptr<Stream> s_;
	std::unique_ptr<Decoder> dec_, alpha_dec_;
	std::mutex mu_;
	std::vector<GdkPixbuf *> cache_;
	uint64_t cache_bytes_ = 0;
	size_t next_ = 0; // next frame the decoders will produce
	bool need_reset_ = false;
	GdkPixbuf *first_ = nullptr;
	GdkPixbuf *last_ = nullptr;
	int width_ = 0, height_ = 0;
	std::string error_;
	std::vector<uint8_t> alpha_plane_;
	Picture alpha_pic_;
	bool have_alpha_ = false;
};

} // namespace webmpb

#endif
