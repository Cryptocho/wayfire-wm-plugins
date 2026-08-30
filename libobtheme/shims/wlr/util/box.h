/* SPDX-License-Identifier: GPL-2.0-only */
/* obtheme shim: minimal <wlr/util/box.h> replacement (struct only). */
#ifndef OBTHEME_SHIM_WLR_BOX_H
#define OBTHEME_SHIM_WLR_BOX_H

struct wlr_box {
	int x, y;
	int width, height;
};

struct wlr_fbox {
	double x, y;
	double width, height;
};

#endif
