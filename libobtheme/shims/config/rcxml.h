/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * obtheme shim for labwc's include/config/rcxml.h — defines only the fields
 * and symbols the vendored theme code actually touches. Populated by
 * ob_theme_init() (see obtheme.h) so vendored `rc.*` references compile
 * without labwc's rcxml machinery.
 */
#ifndef OBTHEME_SHIM_RCXML_H
#define OBTHEME_SHIM_RCXML_H

#include <stdbool.h>
#include "common/font.h"
#include "common/node-type.h"
#include "theme.h"

#define TITLE_BUTTONS_MAX 6

enum ssd_active_state {
	SSD_INACTIVE = 0,
	SSD_ACTIVE = 1,
};

#define FOR_EACH_ACTIVE_STATE(active) \
	for (active = SSD_INACTIVE; active <= SSD_ACTIVE; active++)

/* from labwc include/ssd.h — used by theme.c create_shadow() */
#define SSD_SHADOW_INSET 0.3

struct rcxml {
	bool merge_config;
	/* The theme currently being initialized; theme.c helpers read
	 * already-computed fields from it (labwc's rc.theme back-pointer). */
	struct theme *theme;
	char *theme_name;
	char *config_dir;
	int corner_radius;
	int resize_corner_range;
	enum lab_node_type title_buttons_left[TITLE_BUTTONS_MAX];
	enum lab_node_type title_buttons_right[TITLE_BUTTONS_MAX];
	int nr_title_buttons_left;
	int nr_title_buttons_right;
	struct font font_activewindow;
	struct font font_inactivewindow;
	struct font font_menuheader;
	struct font font_menuitem;
	struct font font_osd;
};

extern struct rcxml rc;

static inline int ssd_get_corner_width(void)
{
	return rc.corner_radius > 0 ? rc.corner_radius : 0;
}

#endif
