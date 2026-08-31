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

#ifdef __cplusplus
extern "C" {
#endif

enum ob_font_place
{
	OB_FONT_TITLEBAR = 0,
	OB_FONT_MENUITEM,
	OB_FONT_MENUHEADER,
};

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
	/* Menu fonts; NULL = same as @font. */
	const char *font_menuitem;
	const char *font_menuheader;
	int corner_radius; /* <=0 = square corners */
};

/* Initialize `rc` shims + parse the theme. Returns 0 on success.
 * NOTE: the vendored theme code reads colors/layouts from the GLOBAL rc
 * shim, so only ONE theme instance may exist per process. Later callers
 * that only need menu/dimension accessors should reuse ob_theme_current()
 * instead of initializing a second theme (a second init repoints rc.theme
 * and crashes the first consumer's hover rendering). */
int ob_theme_init(struct theme **out, struct ob_theme_config cfg);

/* The most recently initialized theme (NULL before the first init).
 * Borrowed pointer; shared between ob-decor and ob-menu. */
struct theme *ob_theme_current(void);

/* Free all theme resources (labwc theme_finish + rc strings). */
void ob_theme_finish(struct theme *theme);

/* ---- accessors (borrowed data; lifetime = theme) ---- */
/* Index into the per-state data (theme->window[]); 1 = active. */
#define OB_THEME_INACTIVE 0
#define OB_THEME_ACTIVE 1
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

/* Parsed "window.{active,inactive}.button.unpressed.image.color"
 * (per-button overrides included), baked into the xbm at load time. */
static inline const float *ob_theme_button_color(const struct theme *t,
	int active, enum lab_node_type type)
{
	return t->window[active].button_colors[type];
}

/* 1px-wide surface encoding the titlebar background (solid or vertical /
 * split-vertical gradient); stretch horizontally when drawing. Borrowed. */
static inline cairo_surface_t *ob_theme_titlebar_fill(const struct theme *t,
	int active)
{
	return t->window[active].titlebar_fill ?
		t->window[active].titlebar_fill->surface : NULL;
}

/* Copy the parsed titlebar buttons (from titlebar_layout) for the given
 * side (0 = left, 1 = right). Returns the count (<= max). */
int ob_theme_title_buttons(const struct theme *t, int side,
	enum lab_node_type *out, int max);

/* ---- menu theme accessors (themerc menu.* keys) ---- */
static inline int ob_theme_menu_item_height(const struct theme *t)
{
	return t->menu_item_height;
}

static inline int ob_theme_menu_header_height(const struct theme *t)
{
	return t->menu_header_height;
}

static inline int ob_theme_menu_overlap_x(const struct theme *t)
{
	return t->menu_overlap_x;
}

static inline int ob_theme_menu_overlap_y(const struct theme *t)
{
	return t->menu_overlap_y;
}

static inline int ob_theme_menu_min_width(const struct theme *t)
{
	return t->menu_min_width;
}

static inline int ob_theme_menu_max_width(const struct theme *t)
{
	return t->menu_max_width;
}

static inline int ob_theme_menu_border_width(const struct theme *t)
{
	return t->menu_border_width;
}

static inline const float *ob_theme_menu_border_color(const struct theme *t)
{
	return t->menu_border_color;
}

static inline int ob_theme_menu_items_padding_x(const struct theme *t)
{
	return t->menu_items_padding_x;
}

static inline int ob_theme_menu_items_padding_y(const struct theme *t)
{
	return t->menu_items_padding_y;
}

static inline const float *ob_theme_menu_items_bg(const struct theme *t)
{
	return t->menu_items_bg_color;
}

static inline const float *ob_theme_menu_items_text(const struct theme *t)
{
	return t->menu_items_text_color;
}

static inline const float *ob_theme_menu_items_active_bg(const struct theme *t)
{
	return t->menu_items_active_bg_color;
}

static inline const float *ob_theme_menu_items_active_text(
	const struct theme *t)
{
	return t->menu_items_active_text_color;
}

static inline int ob_theme_menu_separator_thickness(const struct theme *t)
{
	return t->menu_separator_line_thickness;
}

static inline int ob_theme_menu_separator_padding_width(const struct theme *t)
{
	return t->menu_separator_padding_width;
}

static inline int ob_theme_menu_separator_padding_height(const struct theme *t)
{
	return t->menu_separator_padding_height;
}

static inline const float *ob_theme_menu_separator_color(const struct theme *t)
{
	return t->menu_separator_color;
}

static inline const float *ob_theme_menu_title_bg(const struct theme *t)
{
	return t->menu_title_bg_color;
}

static inline const float *ob_theme_menu_title_text(const struct theme *t)
{
	return t->menu_title_text_color;
}

static inline enum lab_justification ob_theme_menu_title_justify(
	const struct theme *t)
{
	return t->menu_title_text_justify;
}

static inline enum lab_justification ob_theme_menu_items_justify(
	const struct theme *t)
{
	return t->menu_items_text_justify;
}

/* Width (px) reserved on the left of an item row for the submenu
 * indicator (openbox draws arrows on the right, we draw a small arrow
 * on the left per user preference). */
static inline int ob_theme_menu_submenu_indent(const struct theme *t)
{
	return t->menu_submenu_indent;
}

/* Height (px) of the font for the given place (OB_FONT_*). */
int ob_theme_font_height(int place);

/* Pango font description string for the given place, e.g. "sans 10".
 * Static buffer, valid until the next call. */
const char *ob_theme_font_string(int place);

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

#ifdef __cplusplus
}
#endif

#endif /* OBTHEME_H */
