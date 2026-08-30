/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * obtheme shim implementation for labwc's buffer.h — lab_data_buffer holds a
 * cairo ARGB32 surface instead of a wlr_buffer. Written for the wayfire
 * wm-plugins project (not vendored).
 */
#include "buffer.h"

#include <assert.h>
#include <cairo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* DRM_FORMAT_ARGB8888 == CAIRO_FORMAT_ARGB32 layout (premultiplied). */
#define OB_FORMAT_ARGB8888 0

static struct lab_data_buffer *buffer_wrap(cairo_surface_t *surface,
	uint32_t logical_width, uint32_t logical_height, float scale)
{
	struct lab_data_buffer *buffer = calloc(1, sizeof(*buffer));
	buffer->surface_owns_data = false;
	buffer->surface = surface;
	buffer->data = cairo_image_surface_get_data(surface);
	buffer->format = OB_FORMAT_ARGB8888;
	buffer->stride = cairo_image_surface_get_stride(surface);
	buffer->logical_width = logical_width;
	buffer->logical_height = logical_height;
	buffer->scale = scale;
	return buffer;
}

struct lab_data_buffer *buffer_adopt_cairo_surface(cairo_surface_t *surface)
{
	assert(cairo_image_surface_get_format(surface) == CAIRO_FORMAT_ARGB32);
	return buffer_wrap(surface, cairo_image_surface_get_width(surface),
		cairo_image_surface_get_height(surface), 1.0f);
}

struct lab_data_buffer *buffer_create_cairo(uint32_t logical_width,
	uint32_t logical_height, float scale)
{
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		logical_width * scale, logical_height * scale);
	return buffer_wrap(surface, logical_width, logical_height, scale);
}

struct lab_data_buffer *buffer_create_from_data(void *pixel_data,
	uint32_t width, uint32_t height, uint32_t stride)
{
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		pixel_data, CAIRO_FORMAT_ARGB32, width, height, stride);
	return buffer_wrap(surface, width, height, 1.0f);
}

struct lab_data_buffer *buffer_resize(struct lab_data_buffer *src_buffer,
	int width, int height, double scale)
{
	struct lab_data_buffer *dst = buffer_create_cairo(width, height, scale);
	cairo_t *cairo = cairo_create(dst->surface);

	/* Render the source centered, at the requested scale. */
	int src_width = cairo_image_surface_get_width(src_buffer->surface);
	int src_height = cairo_image_surface_get_height(src_buffer->surface);
	double draw_width = src_buffer->logical_width * scale;
	double draw_height = src_buffer->logical_height * scale;
	cairo_translate(cairo, (width - draw_width) / 2.0,
		(height - draw_height) / 2.0);
	cairo_scale(cairo, draw_width / src_width, draw_height / src_height);
	cairo_set_source_surface(cairo, src_buffer->surface, 0, 0);
	cairo_paint(cairo);
	cairo_destroy(cairo);

	buffer_destroy(src_buffer);
	return dst;
}

void buffer_destroy(struct lab_data_buffer *buffer)
{
	if (!buffer)
	{
		return;
	}

	if (buffer->surface_owns_data)
	{
		cairo_surface_finish(buffer->surface);
		free(buffer->data);
	}

	cairo_surface_destroy(buffer->surface);
	free(buffer);
}
