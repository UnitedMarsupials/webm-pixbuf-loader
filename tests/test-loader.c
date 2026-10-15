/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Exercise the WebM loader through the public gdk-pixbuf API.
 *
 * usage: test-loader <data-dir>
 *
 * GDK_PIXBUF_MODULE_FILE must point to a loaders cache that lists the
 * freshly built module.
 */

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOLERANCE 10

struct sample {
	const char *file;
	gboolean still; /* one frame only */
	gboolean alpha;
};

static const struct sample samples[] = {
	{ "vp8.webm", FALSE, FALSE },
	{ "vp9-420.webm", FALSE, FALSE },
	{ "vp9-444.webm", FALSE, FALSE },
	{ "vp9-10bit.webm", FALSE, FALSE },
	{ "av1.webm", FALSE, FALSE },
	{ "still.webm", TRUE, FALSE },
	{ "alpha.webm", TRUE, TRUE },
};

static int failures;

#define CHECK(cond, ...)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			failures++;                                            \
			fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);   \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (0)

static gboolean
near(int a, int b)
{
	return (abs(a - b) <= TOLERANCE);
}

/* Is the middle pixel (r, g, b)? */
static gboolean
is_color(GdkPixbuf *pixbuf, int r, int g, int b)
{
	const guchar *p;
	int x, y;

	x = gdk_pixbuf_get_width(pixbuf) / 2;
	y = gdk_pixbuf_get_height(pixbuf) / 2;
	p = gdk_pixbuf_get_pixels(pixbuf) + y * gdk_pixbuf_get_rowstride(pixbuf)
	    + x * gdk_pixbuf_get_n_channels(pixbuf);
	if (!near(p[0], r) || !near(p[1], g) || !near(p[2], b)) {
		fprintf(stderr, "  pixel is (%d, %d, %d), want (%d, %d, %d)\n",
		    p[0], p[1], p[2], r, g, b);
		return (FALSE);
	}
	return (TRUE);
}

static int
alpha_of(GdkPixbuf *pixbuf)
{
	const guchar *p;
	int x, y;

	x = gdk_pixbuf_get_width(pixbuf) / 2;
	y = gdk_pixbuf_get_height(pixbuf) / 2;
	p = gdk_pixbuf_get_pixels(pixbuf) + y * gdk_pixbuf_get_rowstride(pixbuf)
	    + x * 4;
	return (p[3]);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
/* The picture an iterator shows ms milliseconds after it was created. */
static GdkPixbuf *
frame_at(GdkPixbufAnimationIter *iter, const GTimeVal *start, int ms)
{
	GTimeVal now = *start;

	g_time_val_add(&now, (glong)ms * 1000);
	gdk_pixbuf_animation_iter_advance(iter, &now);
	return (gdk_pixbuf_animation_iter_get_pixbuf(iter));
}

static void
test_animation(const char *path, const struct sample *s)
{
	GdkPixbufAnimation *anim;
	GdkPixbufAnimationIter *iter;
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	GTimeVal start;

	anim = gdk_pixbuf_animation_new_from_file(path, &error);
	CHECK(anim != NULL, "%s: animation: %s", s->file,
	    error != NULL ? error->message : "?");
	if (anim == NULL) {
		g_clear_error(&error);
		return;
	}
	CHECK(gdk_pixbuf_animation_is_static_image(anim) == s->still,
	    "%s: static image mismatch", s->file);
	CHECK(gdk_pixbuf_animation_get_width(anim) == 64
		&& gdk_pixbuf_animation_get_height(anim) == 48,
	    "%s: animation size", s->file);
	if (!s->still) {
		g_get_current_time(&start);
		iter = gdk_pixbuf_animation_get_iter(anim, &start);
		pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(iter);
		CHECK(is_color(pixbuf, 255, 0, 0), "%s: 0 ms is not red",
		    s->file);
		CHECK(gdk_pixbuf_animation_iter_get_delay_time(iter) > 0,
		    "%s: no delay", s->file);
		CHECK(is_color(frame_at(iter, &start, 300), 0, 255, 0),
		    "%s: 300 ms is not green", s->file);
		CHECK(is_color(frame_at(iter, &start, 500), 0, 0, 255),
		    "%s: 500 ms is not blue", s->file);
		/* Total length is 600 ms; it loops. */
		CHECK(is_color(frame_at(iter, &start, 700), 255, 0, 0),
		    "%s: 700 ms is not red", s->file);
		/* Going back in time makes the decoder start over. */
		CHECK(is_color(frame_at(iter, &start, 100), 255, 0, 0),
		    "%s: back to 100 ms is not red", s->file);
		CHECK(is_color(frame_at(iter, &start, 1100), 0, 0, 255),
		    "%s: 1100 ms is not blue", s->file);
		g_object_unref(iter);
	}
	g_object_unref(anim);
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void
test_sample(const char *dir, const struct sample *s)
{
	GdkPixbufFormat *format;
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	char *path;
	int w = 0, h = 0;

	path = g_build_filename(dir, s->file, NULL);

	/* Size and format without decoding anything. */
	format = gdk_pixbuf_get_file_info(path, &w, &h);
	CHECK(format != NULL, "%s: format not recognised", s->file);
	if (format != NULL)
		CHECK(strcmp(gdk_pixbuf_format_get_name(format), "webm") == 0,
		    "%s: format is %s", s->file,
		    gdk_pixbuf_format_get_name(format));
	CHECK(w == 64 && h == 48, "%s: file info says %dx%d", s->file, w, h);

	/* The first frame as a plain picture. */
	pixbuf = gdk_pixbuf_new_from_file(path, &error);
	CHECK(pixbuf != NULL, "%s: load: %s", s->file,
	    error != NULL ? error->message : "?");
	if (pixbuf != NULL) {
		CHECK(gdk_pixbuf_get_width(pixbuf) == 64
			&& gdk_pixbuf_get_height(pixbuf) == 48,
		    "%s: size", s->file);
		CHECK(gdk_pixbuf_get_has_alpha(pixbuf) == s->alpha,
		    "%s: alpha channel mismatch", s->file);
		CHECK(is_color(pixbuf, 255, 0, 0), "%s: first frame is not red",
		    s->file);
		if (s->alpha)
			CHECK(near(alpha_of(pixbuf), 128), "%s: alpha is %d",
			    s->file, alpha_of(pixbuf));
		g_object_unref(pixbuf);
	}
	g_clear_error(&error);

	/* The loader may be asked for another size. */
	pixbuf = gdk_pixbuf_new_from_file_at_size(path, 32, 24, &error);
	CHECK(pixbuf != NULL, "%s: scaled load failed", s->file);
	if (pixbuf != NULL) {
		CHECK(gdk_pixbuf_get_width(pixbuf) == 32
			&& gdk_pixbuf_get_height(pixbuf) == 24,
		    "%s: scaled size is %dx%d", s->file,
		    gdk_pixbuf_get_width(pixbuf),
		    gdk_pixbuf_get_height(pixbuf));
		CHECK(is_color(pixbuf, 255, 0, 0), "%s: scaled colour",
		    s->file);
		g_object_unref(pixbuf);
	}
	g_clear_error(&error);

	test_animation(path, s);
	g_free(path);
}

/* Feed a file to a GdkPixbufLoader in small pieces. */
static void
test_chunked(const char *dir)
{
	GdkPixbufLoader *loader;
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	gchar *path, *contents;
	gsize len, i, n;

	path = g_build_filename(dir, "vp9-420.webm", NULL);
	CHECK(g_file_get_contents(path, &contents, &len, NULL), "read %s",
	    path);
	loader = gdk_pixbuf_loader_new();
	for (i = 0; i < len; i += n) {
		n = MIN((gsize)7, len - i);
		CHECK(gdk_pixbuf_loader_write(loader, (guchar *)contents + i, n,
			  &error),
		    "chunked write at %zu: %s", i,
		    error != NULL ? error->message : "?");
		g_clear_error(&error);
	}
	CHECK(gdk_pixbuf_loader_close(loader, &error), "chunked close: %s",
	    error != NULL ? error->message : "?");
	g_clear_error(&error);
	pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
	CHECK(pixbuf != NULL && is_color(pixbuf, 255, 0, 0),
	    "chunked: first frame");
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	CHECK(gdk_pixbuf_loader_get_animation(loader) != NULL,
	    "chunked: no animation");
	G_GNUC_END_IGNORE_DEPRECATIONS
	g_object_unref(loader);
	g_free(contents);
	g_free(path);
}

/* Broken input must produce an error or a picture, never a crash. */
static void
test_broken(const char *dir)
{
	GdkPixbufLoader *loader;
	GError *error = NULL;
	gchar *path, *contents;
	guchar junk[256];
	gsize len, cut;
	int i;

	path = g_build_filename(dir, "vp9-420.webm", NULL);
	CHECK(g_file_get_contents(path, &contents, &len, NULL), "read %s",
	    path);

	for (cut = 0; cut < len; cut += 23) {
		loader = gdk_pixbuf_loader_new_with_mime_type("video/webm",
		    &error);
		g_clear_error(&error);
		if (loader == NULL)
			continue;
		(void)gdk_pixbuf_loader_write(loader, (guchar *)contents, cut,
		    &error);
		g_clear_error(&error);
		(void)gdk_pixbuf_loader_close(loader, &error);
		g_clear_error(&error);
		g_object_unref(loader);
	}

	/* Every byte flipped, one at a time. */
	for (i = 0; i < (int)MIN(len, (gsize)400); i++) {
		contents[i] ^= 0x5a;
		loader = gdk_pixbuf_loader_new_with_mime_type("video/webm",
		    &error);
		g_clear_error(&error);
		if (loader != NULL) {
			(void)gdk_pixbuf_loader_write(loader,
			    (guchar *)contents, len, &error);
			g_clear_error(&error);
			(void)gdk_pixbuf_loader_close(loader, &error);
			g_clear_error(&error);
			g_object_unref(loader);
		}
		contents[i] ^= 0x5a;
	}

	/* EBML magic followed by garbage. */
	memset(junk, 0xff, sizeof(junk));
	memcpy(junk, "\x1a\x45\xdf\xa3", 4);
	loader = gdk_pixbuf_loader_new();
	(void)gdk_pixbuf_loader_write(loader, junk, sizeof(junk), &error);
	g_clear_error(&error);
	CHECK(!gdk_pixbuf_loader_close(loader, &error), "garbage was accepted");
	g_clear_error(&error);
	g_object_unref(loader);
	g_free(contents);
	g_free(path);
}

int
main(int argc, char **argv)
{
	size_t i;

	if (argc != 2) {
		fprintf(stderr, "usage: %s data-dir\n", argv[0]);
		return (2);
	}
	for (i = 0; i < G_N_ELEMENTS(samples); i++)
		test_sample(argv[1], &samples[i]);
	test_chunked(argv[1]);
	test_broken(argv[1]);
	if (failures != 0)
		fprintf(stderr, "%d check(s) failed\n", failures);
	return (failures != 0);
}
