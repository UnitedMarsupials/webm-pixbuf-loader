# webm-pixbuf-loader

## Goal

WebM is no longer just video: stickers, custom emoji, reactions and short
loops in chat applications are WebM files, usually VP9 with an alpha
channel.  GTK applications get their pictures through
[gdk-pixbuf](https://gitlab.gnome.org/GNOME/gdk-pixbuf), which has no idea
what to do with them.  This project is a gdk-pixbuf loader plugin that
teaches it: any program that can show a PNG or a GIF through gdk-pixbuf
(Pidgin, image viewers, file managers, thumbnailers, ...) can show a `.webm`
file as an image, or as an animation, without knowing anything about video
codecs.

The immediate use is Pidgin, through tdlib-purple, showing Telegram's WebM
stickers and custom emoji.  The goal is general, though: all the WebM video
codecs (VP8, VP9 and AV1), alpha, animation, and sane behaviour on hostile
input.

Frames are demuxed with libwebm and decoded with libvpx and dav1d.

## What it does

* A one-frame file is a still picture; anything longer is an animation that
  loops forever. `gdk_pixbuf_new_from_file()` returns the first frame.
* Codecs: VP8 and VP9 through libvpx, AV1 through dav1d. Either can be
  disabled at build time (`-Dvpx=disabled`, `-Ddav1d=disabled`), but this is
  only transitional: both are expected to become mandatory, so packagers
  should depend on both.
* VP8/VP9 with alpha (the `BlockAdditional` side channel) gives RGBA.
* 8, 10 and 12 bit samples; 4:0:0, 4:2:0, 4:2:2, 4:4:0 and 4:4:4;
  BT.601/709/2020/SMPTE 240M/FCC and RGB (identity) matrices; limited and
  full range. Bitstream colour information wins, then the container's
  `Colour` element, then BT.709 for HD and BT.601 otherwise.
* `DisplayWidth`/`DisplayHeight` (in pixels) set the image size.
* Frames are demuxed up front but decoded on demand, so a thumbnail costs one
  frame, not the whole clip. Decoded frames are cached up to 128 MiB per
  animation; seeking back restarts at the nearest keyframe.
* The image size is reported as soon as the headers are in, so
  `gdk_pixbuf_get_file_info()` and size-limited loading stay cheap.
* Truncated files load as far as they go. Input is untrusted: sizes are
  capped (16384 per side, 100 Mpixel, 256 MiB file).

Not supported: AV1 alpha, multiple video tracks (the first usable one is
used), content encodings (compression/encryption), cropping and rotation.

## Building

Needs gdk-pixbuf 2.36+, meson, [libwebm](https://www.webmproject.org/code/)
(the `webm_parser` library; there is no pkg-config file, so make the headers
and library visible through `CXXFLAGS`/`LDFLAGS`), libvpx and/or dav1d.

    meson setup build
    meson compile -C build
    meson test -C build
    meson install -C build      # also refreshes the loaders cache

The tests load the files in `tests/data` through a private loaders cache;
`tests/gen-data.sh` regenerates them with ffmpeg.

## License

BSD 2-clause; see [LICENSE.md](LICENSE.md).  Copyright (c) 2026 Mikhail
Teterin.  Developers: please read [AGENTS.md](AGENTS.md) first.
