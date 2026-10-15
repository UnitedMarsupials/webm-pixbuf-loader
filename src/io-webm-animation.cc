/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "io-webm-animation.h"

G_DEFINE_TYPE(WebmAnimation, webm_animation, GDK_TYPE_PIXBUF_ANIMATION)
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
G_DEFINE_TYPE(WebmAnimationIter, webm_animation_iter,
    GDK_TYPE_PIXBUF_ANIMATION_ITER)
G_GNUC_END_IGNORE_DEPRECATIONS

static void
webm_animation_init(WebmAnimation *)
{
}

static void
webm_animation_finalize(GObject *object)
{
	WebmAnimation *anim = WEBM_ANIMATION(object);

	delete anim->player;
	anim->player = NULL;
	G_OBJECT_CLASS(webm_animation_parent_class)->finalize(object);
}

static gboolean
webm_animation_is_static_image(GdkPixbufAnimation *animation)
{
	return (WEBM_ANIMATION(animation)->player->n_frames() == 1);
}

static GdkPixbuf *
webm_animation_get_static_image(GdkPixbufAnimation *animation)
{
	return (WEBM_ANIMATION(animation)->player->first());
}

static void
webm_animation_get_size(GdkPixbufAnimation *animation, int *width, int *height)
{
	webmpb::Player *player = WEBM_ANIMATION(animation)->player;

	if (width != NULL)
		*width = player->width();
	if (height != NULL)
		*height = player->height();
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static GdkPixbufAnimationIter *
webm_animation_get_iter(GdkPixbufAnimation *animation,
    const GTimeVal *start_time)
{
	WebmAnimation *anim = WEBM_ANIMATION(animation);
	WebmAnimationIter *iter;

	iter
	    = (WebmAnimationIter *)g_object_new(WEBM_TYPE_ANIMATION_ITER, NULL);
	iter->anim = (WebmAnimation *)g_object_ref(anim);
	iter->start_us
	    = (gint64)start_time->tv_sec * G_USEC_PER_SEC + start_time->tv_usec;
	iter->index = 0;
	iter->pos_ns = 0;
	iter->pixbuf = anim->player->frame(0);
	return (GDK_PIXBUF_ANIMATION_ITER(iter));
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void
webm_animation_class_init(WebmAnimationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GdkPixbufAnimationClass *anim_class = GDK_PIXBUF_ANIMATION_CLASS(klass);

	object_class->finalize = webm_animation_finalize;
	anim_class->is_static_image = webm_animation_is_static_image;
	anim_class->get_static_image = webm_animation_get_static_image;
	anim_class->get_size = webm_animation_get_size;
	anim_class->get_iter = webm_animation_get_iter;
}

static void
webm_animation_iter_init(WebmAnimationIter *)
{
}

static void
webm_animation_iter_finalize(GObject *object)
{
	WebmAnimationIter *iter = WEBM_ANIMATION_ITER(object);

	if (iter->pixbuf != NULL)
		g_object_unref(iter->pixbuf);
	if (iter->anim != NULL)
		g_object_unref(iter->anim);
	G_OBJECT_CLASS(webm_animation_iter_parent_class)->finalize(object);
}

static int
webm_animation_iter_get_delay_time(GdkPixbufAnimationIter *anim_iter)
{
	WebmAnimationIter *iter = WEBM_ANIMATION_ITER(anim_iter);
	webmpb::Player *player = iter->anim->player;
	gint64 next, ms;

	if (player->n_frames() == 1)
		return (-1);
	if (iter->index + 1 < player->n_frames())
		next = player->start_ns(iter->index + 1);
	else
		next = player->total_ns();
	/* Round up, so that we never wake up before the next frame is due. */
	ms = (next - iter->pos_ns + 999999) / 1000000;
	return (ms < 1 ? 1 : (int)ms);
}

static GdkPixbuf *
webm_animation_iter_get_pixbuf(GdkPixbufAnimationIter *anim_iter)
{
	return (WEBM_ANIMATION_ITER(anim_iter)->pixbuf);
}

static gboolean
webm_animation_iter_on_currently_loading_frame(GdkPixbufAnimationIter *)
{
	return (FALSE);
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static gboolean
webm_animation_iter_advance(GdkPixbufAnimationIter *anim_iter,
    const GTimeVal *current_time)
{
	WebmAnimationIter *iter = WEBM_ANIMATION_ITER(anim_iter);
	webmpb::Player *player = iter->anim->player;
	GdkPixbuf *pixbuf;
	gint64 now_us, elapsed_us, pos;
	gsize index;

	if (player->n_frames() == 1 || player->total_ns() <= 0)
		return (FALSE);

	now_us = (gint64)current_time->tv_sec * G_USEC_PER_SEC
	    + current_time->tv_usec;
	elapsed_us = now_us - iter->start_us;
	if (elapsed_us < 0) {
		/* The system clock was probably set backwards. */
		iter->start_us = now_us;
		elapsed_us = 0;
	}

	/* The animation loops forever. */
	pos = (elapsed_us * 1000) % player->total_ns();
	index = player->frame_at(pos);
	iter->pos_ns = pos;
	if (index == iter->index)
		return (FALSE);

	pixbuf = player->frame(index);
	iter->index = index;
	if (pixbuf == NULL)
		return (FALSE);
	if (iter->pixbuf != NULL)
		g_object_unref(iter->pixbuf);
	iter->pixbuf = pixbuf;
	return (TRUE);
}
G_GNUC_END_IGNORE_DEPRECATIONS

static void
webm_animation_iter_class_init(WebmAnimationIterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	GdkPixbufAnimationIterClass *iter_class
	    = GDK_PIXBUF_ANIMATION_ITER_CLASS(klass);
	G_GNUC_END_IGNORE_DEPRECATIONS

	object_class->finalize = webm_animation_iter_finalize;
	iter_class->get_delay_time = webm_animation_iter_get_delay_time;
	iter_class->get_pixbuf = webm_animation_iter_get_pixbuf;
	iter_class->on_currently_loading_frame
	    = webm_animation_iter_on_currently_loading_frame;
	iter_class->advance = webm_animation_iter_advance;
}

GdkPixbufAnimation *
webm_animation_new(webmpb::Player *player)
{
	WebmAnimation *anim;

	anim = (WebmAnimation *)g_object_new(WEBM_TYPE_ANIMATION, NULL);
	anim->player = player;
	return (GDK_PIXBUF_ANIMATION(anim));
}
