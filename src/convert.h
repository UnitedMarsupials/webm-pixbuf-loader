// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2026 Mikhail Teterin
//
// Planar YUV (8-12 bit, 4:0:0 / 4:2:0 / 4:2:2 / 4:4:0 / 4:4:4) to RGB(A).

#ifndef WEBM_PIXBUF_CONVERT_H
#define WEBM_PIXBUF_CONVERT_H

#include <cstddef>
#include <cstdint>

#include "decoder.h"

namespace webmpb {

// Write |pic| as packed RGB (n_channels == 3) or RGBA (n_channels == 4) into
// |out|, which has pic.width x pic.height pixels.  |matrix| and |range|
// (see Picture) must be resolved, i.e. matrix != 2 and range != 0.  If
// |alpha| is not null, the luma plane of that picture provides the alpha
// channel (n_channels must be 4); otherwise alpha is opaque.
void convert_to_rgb(const Picture &pic, int matrix, int range,
    const Picture *alpha, uint8_t *out, size_t rowstride, int n_channels);

// Pick a sensible matrix for streams that do not say.
int default_matrix(int height);

} // namespace webmpb

#endif
