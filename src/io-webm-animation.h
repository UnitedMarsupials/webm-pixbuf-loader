/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Mikhail Teterin
 */
/*
 * GdkPixbufAnimation for WebM; frames are decoded on demand.
 */

#ifndef IO_WEBM_ANIMATION_H
#define IO_WEBM_ANIMATION_H

#define GDK_PIXBUF_ENABLE_BACKEND
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "player.h"

#define WEBM_TYPE_ANIMATION (webm_animation_get_type())
#define WEBM_ANIMATION(o)                                                      \
	(G_TYPE_CHECK_INSTANCE_CAST((o), WEBM_TYPE_ANIMATION, WebmAnimation))

#define WEBM_TYPE_ANIMATION_ITER (webm_animation_iter_get_type())
#define WEBM_ANIMATION_ITER(o)                                                 \
	(G_TYPE_CHECK_INSTANCE_CAST((o), WEBM_TYPE_ANIMATION_ITER,             \
	    WebmAnimationIter))

typedef struct _WebmAnimation {
	GdkPixbufAnimation parent;
	webmpb::Player *player;
} WebmAnimation;

typedef struct _WebmAnimationClass {
	GdkPixbufAnimationClass parent_class;
} WebmAnimationClass;

typedef struct _WebmAnimationIter {
	GdkPixbufAnimationIter parent;
	WebmAnimation *anim;
	gint64 start_us;
	gint64 pos_ns;
	gsize index;
	GdkPixbuf *pixbuf;
} WebmAnimationIter;

typedef struct _WebmAnimationIterClass {
	GdkPixbufAnimationIterClass parent_class;
} WebmAnimationIterClass;

GType
webm_animation_get_type(void);
GType
webm_animation_iter_get_type(void);

/* Takes ownership of the player. */
GdkPixbufAnimation *
webm_animation_new(webmpb::Player *player);

#endif /* IO_WEBM_ANIMATION_H */
