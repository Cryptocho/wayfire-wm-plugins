/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * obtheme — public API implementation: populates the rcxml shim, wraps
 * labwc's theme_init/theme_finish and renders button images. Written for
 * the wayfire wm-plugins project (not vendored).
 */
#include "obtheme.h"
#include "buffer.h"

#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/font.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "img/img.h"
#include <wlr/util/log.h>

struct rcxml rc;

/* Parse labwc titlebar-layout syntax into rc.title_buttons_*.
 * Exactly one ':' separates left:right (a missing colon means all buttons
 * are on the right side); tokens are comma-separated. Mirrors labwc's
 * rcxml.c fill_title_layout(), but trims whitespace. */
static void parse_titlebar_layout(const char *layout)
{
	static const struct {
		const char *name;
		enum lab_node_type type;
	} tokens[] = {
		{ "iconify", LAB_NODE_BUTTON_ICONIFY },
		{ "max",     LAB_NODE_BUTTON_MAXIMIZE },
		{ "close",   LAB_NODE_BUTTON_CLOSE },
		{ "shade",   LAB_NODE_BUTTON_SHADE },
		{ "desk",    LAB_NODE_BUTTON_OMNIPRESENT },
		{ "menu",    LAB_NODE_BUTTON_WINDOW_MENU },
		{ "icon",    LAB_NODE_BUTTON_WINDOW_ICON }, /* no libsfdo: like labwc, treat as menu */
	};

	char *dup = strdup(layout ? layout : "menu:iconify,max,close");

	char *left  = dup;
	char *right = strchr(dup, ':');
	if (right) {
		*right = '\0';
		right++;
	} else {
		right  = left;
		left   = NULL;
	}

	struct {
		char *spec;
		enum lab_node_type *out;
		int *nr;
	} sides[] = {
		{ left,  rc.title_buttons_left,  &rc.nr_title_buttons_left  },
		{ right, rc.title_buttons_right, &rc.nr_title_buttons_right },
	};

	for (size_t i = 0; i < 2; i++) {
		*sides[i].nr = 0;
		if (!sides[i].spec) {
			continue;
		}

		char *save = NULL;
		for (char *tok = strtok_r(sides[i].spec, ",", &save); tok;
				tok = strtok_r(NULL, ",", &save)) {
			while (*tok == ' ' || *tok == '\t') {
				tok++;
			}

			char *end = tok + strlen(tok);
			while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) {
				*--end = '\0';
			}

			if (!*tok || *sides[i].nr >= TITLE_BUTTONS_MAX) {
				continue;
			}

			enum lab_node_type type = -1;
			for (size_t j = 0; j < sizeof(tokens) / sizeof(tokens[0]); j++) {
				if (!strcmp(tok, tokens[j].name)) {
					type = tokens[j].type;
					if (!strcmp(tok, "icon")) {
						wlr_log(WLR_ERROR,
							"icon button unavailable, substituting menu");
						type = LAB_NODE_BUTTON_WINDOW_MENU;
					}

					break;
				}
			}

			if (type == -1) {
				wlr_log(WLR_ERROR, "invalid titlebar button '%s'", tok);
				continue;
			}

			sides[i].out[(*sides[i].nr)++] = type;
		}
	}

	free(dup);
}

static void set_fonts(const char *font)
{
	PangoFontDescription *desc =
		pango_font_description_from_string(font ? font : "sans 10");

	struct font *targets[] = {
		&rc.font_activewindow,
		&rc.font_inactivewindow,
		&rc.font_menuheader,
		&rc.font_menuitem,
		&rc.font_osd,
	};

	const char *family = pango_font_description_get_family(desc);
	int size = pango_font_description_get_size(desc) / PANGO_SCALE;
	if (size <= 0) {
		size = 10;
	}

	for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
		free(targets[i]->name);
		targets[i]->name   = strdup(family ? family : "sans");
		targets[i]->size   = size;
		targets[i]->slant  = pango_font_description_get_style(desc);
		targets[i]->weight = pango_font_description_get_weight(desc);
	}

	pango_font_description_free(desc);
}

int ob_theme_init(struct theme **out, struct ob_theme_config cfg)
{
	if (!out) {
		return -1;
	}

	struct theme *theme = calloc(1, sizeof(*theme));
	if (!theme) {
		return -1;
	}

	/* Populate the rcxml shim (labwc reads these from rc.xml). */
	free(rc.theme_name);
	free(rc.config_dir);
	rc.theme_name   = strdup(cfg.theme_name && *cfg.theme_name ?
		cfg.theme_name : "");
	rc.config_dir   = NULL; /* dir.c: NULL skips themerc-override lookup */
	rc.merge_config = false;
	rc.corner_radius = cfg.corner_radius > 0 ? cfg.corner_radius : 0;
	parse_titlebar_layout(cfg.titlebar_layout);
	set_fonts(cfg.font);

	rc.theme = theme;

	/* rc.theme_name == "" keeps theme_name NULL → built-in defaults. */
	theme_init(theme, *rc.theme_name ? rc.theme_name : NULL);
	if (!rc.merge_config) {
		/* single-read semantics are handled inside theme_read */
	}

	*out = theme;
	return 0;
}

void ob_theme_finish(struct theme *theme)
{
	if (!theme) {
		return;
	}

	theme_finish(theme);
	rc.theme = NULL;
	free(theme);
}

cairo_surface_t *ob_theme_render_button(struct theme *theme,
	enum lab_node_type type, unsigned state_bits, int active,
	int width, int height, double scale)
{
	if (!theme || active < SSD_INACTIVE || active > SSD_ACTIVE) {
		return NULL;
	}

	struct lab_img **imgs = theme->window[active].button_imgs[type];
	struct lab_img *img   = imgs ? imgs[state_bits & LAB_BS_ALL] : NULL;

	/* labwc-style fallbacks: rounded -> base, hovered -> base. */
	if (!img) {
		img = imgs[state_bits & ~(LAB_BS_ROUNDED | LAB_BS_HOVERED)];
	}

	if (!img) {
		return NULL;
	}

	struct lab_data_buffer *buffer =
		lab_img_render(img, width, height, scale);
	if (!buffer) {
		return NULL;
	}

	cairo_surface_t *surface = buffer->surface;
	cairo_surface_reference(surface);
	buffer_destroy(buffer);
	return surface;
}
