/* SPDX-License-Identifier: GPL-2.0-only */
/* obtheme shim: minimal <wlr/version.h>. The vendored code does not call
 * LAB_WLR_VERSION_AT_LEAST (which would need labwc's `server` global);
 * only the version macros are provided for completeness. */
#ifndef OBTHEME_SHIM_WLR_VERSION_H
#define OBTHEME_SHIM_WLR_VERSION_H

#define WLR_VERSION_MAJOR 0
#define WLR_VERSION_MINOR 19
#define WLR_VERSION_MICRO 0
#define WLR_VERSION_NUM ((WLR_VERSION_MAJOR << 16) | \
	(WLR_VERSION_MINOR << 8) | WLR_VERSION_MICRO)
#define WLR_VERSION_STR "0.19.0 (obtheme shim)"

#endif
