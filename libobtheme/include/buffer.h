/* Vendored from labwc 5aaa5da0571b include/buffer.h; local changes: see
 * libobtheme/VENDORED-DIFFS.md (wlr_buffer dependency removed — the buffer is
 * a plain cairo-surface holder now; buffer_create_from_wlr_buffer omitted). */
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on wlroots/include/types/wlr_buffer.c
 *
 * Copyright (c) 2017, 2018 Drew DeVault
 * Copyright (c) 2018-2021 Simon Ser, Simon Zeni
 */
#ifndef LABWC_BUFFER_H
#define LABWC_BUFFER_H

#include <cairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct lab_data_buffer {
	bool surface_owns_data;
	cairo_surface_t *surface;
	void *data;
	uint32_t format; /* currently always DRM_FORMAT_ARGB8888 */
	size_t stride;
	/*
	 * The logical size of the surface in layout pixels.
	 * The raw pixel data may be larger or smaller.
	 */
	uint32_t logical_width;
	uint32_t logical_height;
	float scale;
};

/*
 * Create a buffer which holds (and takes ownership of) an existing
 * CAIRO_FORMAT_ARGB32 image surface.
 *
 * The logical size is set to the surface size in pixels, ignoring
 * device scale.
 */
struct lab_data_buffer *buffer_adopt_cairo_surface(cairo_surface_t *surface);

/*
 * Create a buffer which holds a new CAIRO_FORMAT_ARGB32 image surface.
 * Additionally create a cairo context for drawing to the surface.
 */
struct lab_data_buffer *buffer_create_cairo(uint32_t logical_width,
	uint32_t logical_height, float scale);

/*
 * Create a buffer which holds (and takes ownership of) raw pixel data
 * in pre-multiplied ARGB32 format.
 *
 * The logical size is set to the width and height of the pixel data.
 */
struct lab_data_buffer *buffer_create_from_data(void *pixel_data, uint32_t width,
	uint32_t height, uint32_t stride);

/*
 * Resize a buffer to the given size. The source buffer is rendered at the
 * center of the output buffer and shrunk if it overflows from the output
 * buffer. Returns the new buffer; the source buffer is destroyed.
 */
struct lab_data_buffer *buffer_resize(struct lab_data_buffer *src_buffer,
	int width, int height, double scale);

/* Free the buffer and its cairo surface. NULL is allowed. */
void buffer_destroy(struct lab_data_buffer *buffer);

#endif /* LABWC_BUFFER_H */
