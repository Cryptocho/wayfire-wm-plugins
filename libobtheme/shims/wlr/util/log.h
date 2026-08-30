/* SPDX-License-Identifier: GPL-2.0-only */
/* obtheme shim: minimal <wlr/util/log.h> replacement (fprintf-based).
 * Only what the vendored labwc theme code uses. */
#ifndef OBTHEME_SHIM_WLR_LOG_H
#define OBTHEME_SHIM_WLR_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

enum wlr_log_importance
{
	WLR_SILENT = 0,
	WLR_DEBUG,
	WLR_INFO,
	WLR_ERROR,
};

static inline void obtheme_shim_log(enum wlr_log_importance importance,
	const char *fmt, ...)
{
	static const char *names[] = { "", "DEBUG", "INFO", "ERROR" };
	if (importance < WLR_DEBUG)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "[%s] ", names[importance]);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

#define wlr_log(verb, fmt, ...) \
	obtheme_shim_log(verb, fmt, ##__VA_ARGS__)

static inline void obtheme_shim_log_errno(enum wlr_log_importance importance,
	const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "[ERROR] (errno=%d) ", errno);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

#define wlr_log_errno(verb, fmt, ...) \
	obtheme_shim_log_errno(verb, fmt, ##__VA_ARGS__)

#endif
