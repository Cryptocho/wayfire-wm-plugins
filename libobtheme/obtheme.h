/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * obtheme — public C API of libobtheme: openbox themerc parsing + button
 * image loading, ported from labwc's theme engine. The heavy lifting lives
 * in the vendored theme.c; this header only exposes what ob-decor needs.
 */
#ifndef OBTHEME_H
#define OBTHEME_H

#include <cairo.h>
#include <stddef.h>
#include "buffer.h"
#include "theme.h"

struct ob_theme_config {
	/* Theme name searched in $XDG_DATA_HOME/themes, ~/.themes and
	 * $XDG_DATA_DIRS/themes as <name>/labwc/themerc then
	 * <name>/openbox-3/themerc (labwc semantics). NULL/"" = built-in
	 * labwc defaults. */
	const char *theme_name;
	/* Titlebar button layout, labwc rc.xml syntax:
	 * "[left]:right" sides split on a single colon, buttons split on
	 * commas; tokens: icon|menu|iconify|max|close|shade|desk.
	 * NULL = "menu:iconify,max,close". */
	const char *titlebar_layout;
	/* Pango font description string for the titlebar, e.g. "sans 10".
	 * NULL = "sans 10". themerc carries no window fonts (same as labwc). */
	const char *font;
	int corner_radius; /* <=0 = square corners */
};

/* Initialize `rc` shims + parse the theme. Returns 0 on success. */
int ob_theme_init(struct theme **out, struct ob_theme_config cfg);

/* Free all theme resources (labwc theme_finish + rc strings). */
void ob_theme_finish(struct theme *theme);

/* ---- accessors (borrowed data; lifetime = theme) ---- */
static inline int ob_theme_border_width(const struct theme *t)
{
	return t->border_width;
}

static inline int ob_theme_titlebar_height(const struct theme *t)
{
	return t->titlebar_height;
}

static inline int ob_theme_titlebar_padding_width(const struct theme *t)
{
	return t->window_titlebar_padding_width;
}

static inline int ob_theme_titlebar_padding_height(const struct theme *t)
{
	return t->window_titlebar_padding_height;
}

static inline void ob_theme_button_size(const struct theme *t,
	int *width, int *height, int *spacing)
{
	*width = t->window_button_width;
	*height = t->window_button_height;
	*spacing = t->window_button_spacing;
}

static inline enum lab_justification ob_theme_justify(const struct theme *t)
{
	return t->window_label_text_justify;
}

/* active: 1 = SSD_ACTIVE, 0 = SSD_INACTIVE. Premultiplied RGBA floats. */
static inline const float *ob_theme_border_color(const struct theme *t,
	int active)
{
	return t->window[active].border_color;
}

static inline const float *ob_theme_label_text_color(const struct theme *t,
	int active)
{
	return t->window[active].label_text_color;
}

static inline const float *ob_theme_button_hover_bg(const struct theme *t,
	int active)
{
	(void)active;
	return t->window_button_hover_bg_color;
}

static inline int ob_theme_button_hover_bg_corner_radius(const struct theme *t,
	int active)
{
	(void)active;
	return t->window_button_hover_bg_corner_radius;
}

/* 1px-wide surface encoding the titlebar background (solid or vertical /
 * split-vertical gradient); stretch horizontally when drawing. Borrowed. */
static inline cairo_surface_t *ob_theme_titlebar_fill(const struct theme *t,
	int active)
{
	return t->window[active].titlebar_fill ?
		t->window[active].titlebar_fill->surface : NULL;
}

/* ---- button rendering ---- */

/* State bits (subset of labwc's LAB_BS_*; see include/theme.h). */
#define OB_BTN_DEFAULT  0
#define OB_BTN_HOVERED  1
#define OB_BTN_TOGGLED  2

/*
 * Render a button image at the requested size/scale.
 * Returns a CAIRO_FORMAT_ARGB32 cairo_surface_t owned by the caller, or
 * NULL if the theme has no image for this button (e.g. unsupported type).
 * Missing hover/toggled variants fall back like labwc does.
 */
cairo_surface_t *ob_theme_render_button(struct theme *theme,
	enum lab_node_type type, unsigned state_bits, int active,
	int width, int height, double scale);

#endif /* OBTHEME_H */
