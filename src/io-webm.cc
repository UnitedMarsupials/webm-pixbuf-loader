/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mikhail Teterin
 */
/*
 * gdk-pixbuf loader for WebM (VP8, VP9 and AV1 video).
 *
 * A file is shown as an image: a single frame is a still picture, anything
 * longer is an animation that loops forever.  The first frame is what
 * gdk_pixbuf_new_from_file() and friends return.
 */

#include <stdio.h>
#include <string.h>

#include "io-webm-animation.h"

/* Largest file we are willing to buffer. */
#define WEBM_MAX_FILE (256UL * 1024 * 1024)
/* Do not keep probing for the headers beyond this much data. */
#define WEBM_MAX_PROBE (4UL * 1024 * 1024)

typedef struct {
	GdkPixbufModuleSizeFunc size_func;
	GdkPixbufModulePreparedFunc prepared_func;
	GdkPixbufModuleUpdatedFunc updated_func;
	gpointer user_data;
	GByteArray *data;
	gsize next_probe;
	gboolean size_known;
	gboolean stop; /* size_func said "no more" */
	int width, height;
} WebmContext;

static void
set_file_too_large(GError **error)
{
	g_set_error_literal(error, GDK_PIXBUF_ERROR,
	    GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY, "WebM file is too large");
}

/*
 * Tell the application the size and let it ask for a different one.  Returns
 * FALSE if it asked us to stop (size 0).
 */
static gboolean
report_size(WebmContext *ctx, int width, int height)
{
	int w = width, h = height;

	ctx->size_known = TRUE;
	ctx->width = width;
	ctx->height = height;
	if (ctx->size_func == NULL)
		return (TRUE);
	ctx->size_func(&w, &h, ctx->user_data);
	if (w == 0 || h == 0) {
		ctx->stop = TRUE;
		return (FALSE);
	}
	if (w > 0 && h > 0) {
		ctx->width = w;
		ctx->height = h;
	}
	return (TRUE);
}

/* Build the animation for a complete file and hand it to the callbacks. */
static GdkPixbufAnimation *
build_animation(const guint8 *data, gsize len, GError **error)
{
	webmpb::Player *player;

	player = webmpb::Player::open(data, len, error);
	if (player == NULL)
		return (NULL);
	return (webm_animation_new(player));
}

static gpointer
webm_begin_load(GdkPixbufModuleSizeFunc size_func,
    GdkPixbufModulePreparedFunc prepared_func,
    GdkPixbufModuleUpdatedFunc updated_func, gpointer user_data, GError **)
{
	WebmContext *ctx;

	ctx = g_new0(WebmContext, 1);
	ctx->size_func = size_func;
	ctx->prepared_func = prepared_func;
	ctx->updated_func = updated_func;
	ctx->user_data = user_data;
	ctx->data = g_byte_array_new();
	return (ctx);
}

static gboolean
webm_load_increment(gpointer data, const guchar *buf, guint size,
    GError **error)
{
	WebmContext *ctx = (WebmContext *)data;
	int w, h, r;

	if (ctx->stop)
		return (TRUE);
	if ((gsize)ctx->data->len + size > WEBM_MAX_FILE) {
		set_file_too_large(error);
		return (FALSE);
	}
	g_byte_array_append(ctx->data, buf, size);

	/*
	 * Report the size as soon as the headers are complete, so that
	 * gdk_pixbuf_get_file_info() does not have to read the whole file.
	 * Probe again only when the amount of data has doubled.
	 */
	if (ctx->size_known || ctx->data->len < ctx->next_probe ||
	    ctx->data->len > WEBM_MAX_PROBE)
		return (TRUE);
	r = webmpb::Player::probe(ctx->data->data, ctx->data->len, &w, &h,
	    error);
	if (r < 0)
		return (FALSE);
	if (r == 0) {
		ctx->next_probe = MAX(4096, 2 * (gsize)ctx->data->len);
		return (TRUE);
	}
	report_size(ctx, w, h);
	return (TRUE);
}

static gboolean
webm_stop_load(gpointer data, GError **error)
{
	WebmContext *ctx = (WebmContext *)data;
	webmpb::Player *player;
	GdkPixbufAnimation *anim;
	GdkPixbuf *pixbuf;
	gboolean ok = FALSE;

	if (ctx->stop) {
		ok = TRUE;
		goto out;
	}
	player = webmpb::Player::open(ctx->data->data, ctx->data->len, error);
	if (player == NULL)
		goto out;
	if (!ctx->size_known &&
	    !report_size(ctx, player->width(), player->height())) {
		delete player;
		ok = TRUE;
		goto out;
	}
	if (ctx->width != player->width() || ctx->height != player->height())
		player->set_size(ctx->width, ctx->height);

	anim = webm_animation_new(player);
	pixbuf = player->first();
	if (pixbuf == NULL) {
		g_object_unref(anim);
		g_set_error_literal(error, GDK_PIXBUF_ERROR,
		    GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
		    "Cannot decode the first WebM frame");
		goto out;
	}
	if (ctx->prepared_func != NULL)
		ctx->prepared_func(pixbuf, anim, ctx->user_data);
	if (ctx->updated_func != NULL)
		ctx->updated_func(pixbuf, 0, 0, gdk_pixbuf_get_width(pixbuf),
		    gdk_pixbuf_get_height(pixbuf), ctx->user_data);
	g_object_unref(anim);
	ok = TRUE;
out:
	g_byte_array_free(ctx->data, TRUE);
	g_free(ctx);
	return (ok);
}

static GdkPixbufAnimation *
webm_load_animation(FILE *f, GError **error)
{
	GdkPixbufAnimation *anim;
	GByteArray *bytes;
	guint8 buf[65536];
	size_t n;

	bytes = g_byte_array_new();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (bytes->len + n > WEBM_MAX_FILE) {
			set_file_too_large(error);
			g_byte_array_free(bytes, TRUE);
			return (NULL);
		}
		g_byte_array_append(bytes, buf, n);
	}
	if (ferror(f)) {
		g_set_error_literal(error, GDK_PIXBUF_ERROR,
		    GDK_PIXBUF_ERROR_FAILED, "Error reading WebM file");
		g_byte_array_free(bytes, TRUE);
		return (NULL);
	}
	anim = build_animation(bytes->data, bytes->len, error);
	g_byte_array_free(bytes, TRUE);
	return (anim);
}

extern "C" G_MODULE_EXPORT void
fill_vtable(GdkPixbufModule *module)
{
	/* Our GObject types live in this module and must outlive it. */
	if (module->module != NULL)
		g_module_make_resident(module->module);
	module->begin_load = webm_begin_load;
	module->load_increment = webm_load_increment;
	module->stop_load = webm_stop_load;
	module->load_animation = webm_load_animation;
}

extern "C" G_MODULE_EXPORT void
fill_info(GdkPixbufFormat *info)
{
	/* The EBML magic; Matroska files share it and are rejected later. */
	static GdkPixbufModulePattern signature[] = {
		{ (char *)"\x1a\x45\xdf\xa3", NULL, 100 }, { NULL, NULL, 0 }
	};
	static gchar *mime_types[] = { (gchar *)"video/webm", NULL };
	static gchar *extensions[] = { (gchar *)"webm", NULL };

	info->name = (gchar *)"webm";
	info->signature = signature;
	info->description = (gchar *)"WebM video (VP8, VP9 and AV1)";
	info->mime_types = mime_types;
	info->extensions = extensions;
	info->flags = GDK_PIXBUF_FORMAT_THREADSAFE;
	info->license = (gchar *)"BSD";
}
