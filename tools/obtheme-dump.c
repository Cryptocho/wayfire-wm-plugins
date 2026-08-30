/* SPDX-License-Identifier: GPL-2.0-only */
/* obtheme-dump — parse a theme and print/assert its values.
 * Used by scripts/obtheme-test.sh to lock parsing fidelity (T2). */
#include "obtheme.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static struct theme *theme;

static void hex_from_color(const float *c, char out[8])
{
	unsigned r = 0, g = 0, b = 0;
	if (c[3] > 0.0f)
	{
		r = (unsigned)(c[0] / c[3] * 255.0f + 0.5f);
		g = (unsigned)(c[1] / c[3] * 255.0f + 0.5f);
		b = (unsigned)(c[2] / c[3] * 255.0f + 0.5f);
	}

	if (r > 255) { r = 255; }

	if (g > 255) { g = 255; }

	if (b > 255) { b = 255; }

	snprintf(out, 8, "#%02x%02x%02x", r, g, b);
}

static const char *justify_str(enum lab_justification j)
{
	switch (j)
	{
		case LAB_JUSTIFY_LEFT:
			return "left";
		case LAB_JUSTIFY_CENTER:
			return "center";
		case LAB_JUSTIFY_RIGHT:
			return "right";
	}

	return "?";
}

static const char *gradient_str(enum lab_gradient g)
{
	switch (g)
	{
		case LAB_GRADIENT_NONE:
			return "solid";
		case LAB_GRADIENT_VERTICAL:
			return "vertical";
		case LAB_GRADIENT_SPLITVERTICAL:
			return "splitvertical";
	}

	return "?";
}

static const struct {
	const char *name;
	enum lab_node_type type;
} buttons[] = {
	{ "close", LAB_NODE_BUTTON_CLOSE },
	{ "iconify", LAB_NODE_BUTTON_ICONIFY },
	{ "max", LAB_NODE_BUTTON_MAXIMIZE },
	{ "shade", LAB_NODE_BUTTON_SHADE },
	{ "desk", LAB_NODE_BUTTON_OMNIPRESENT },
	{ "menu", LAB_NODE_BUTTON_WINDOW_MENU },
};

/* Values table: computed once, looked up for assertions. */
#define MAX_VALUES 64
static char value_keys[MAX_VALUES][64];
static char value_vals[MAX_VALUES][64];
static int value_count;

static void set_value(const char *key, const char *fmt, ...)
{
	va_list ap;
	snprintf(value_keys[value_count], 64, "%s", key);
	va_start(ap, fmt);
	vsnprintf(value_vals[value_count], 64, fmt, ap);
	va_end(ap);
	printf("%-42s = %s\n", value_keys[value_count], value_vals[value_count]);
	value_count++;
}

static void hex_value(const char *key, const float *c)
{
	char hex[8];
	hex_from_color(c, hex);
	set_value(key, "%s", hex);
}

int main(int argc, char **argv)
{
	struct ob_theme_config cfg = {};
	const char *assert_file = NULL;

	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--theme") && i + 1 < argc)
		{
			cfg.theme_name = argv[++i];
		}
		else if (!strcmp(argv[i], "--font") && i + 1 < argc)
		{
			cfg.font = argv[++i];
		}
		else if (!strcmp(argv[i], "--layout") && i + 1 < argc)
		{
			cfg.titlebar_layout = argv[++i];
		}
		else if (!strcmp(argv[i], "--radius") && i + 1 < argc)
		{
			cfg.corner_radius = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--assert-file") && i + 1 < argc)
		{
			assert_file = argv[++i];
		}
		else
		{
			fprintf(stderr, "unknown arg: %s\n", argv[i]);
			return 2;
		}
	}

	if (ob_theme_init(&theme, cfg) != 0)
	{
		fprintf(stderr, "ob_theme_init failed\n");
		return 2;
	}

	set_value("border_width", "%d", ob_theme_border_width(theme));
	set_value("titlebar_height", "%d", ob_theme_titlebar_height(theme));
	set_value("padding_width", "%d",
		ob_theme_titlebar_padding_width(theme));
	set_value("padding_height", "%d",
		ob_theme_titlebar_padding_height(theme));

	int bw, bh, bs;
	ob_theme_button_size(theme, &bw, &bh, &bs);
	set_value("button_width", "%d", bw);
	set_value("button_height", "%d", bh);
	set_value("button_spacing", "%d", bs);
	set_value("justify", "%s", justify_str(ob_theme_justify(theme)));
	set_value("title_bg_active", "%s",
		gradient_str(theme->window[OB_THEME_ACTIVE].title_bg.gradient));
	set_value("title_bg_inactive", "%s",
		gradient_str(theme->window[OB_THEME_INACTIVE].title_bg.gradient));

	hex_value("border_color_active",
		ob_theme_border_color(theme, OB_THEME_ACTIVE));
	hex_value("border_color_inactive",
		ob_theme_border_color(theme, OB_THEME_INACTIVE));
	/* menu.* theme section (labwc keys, parsed by vendored theme.c) */
	set_value("menu_border_width", "%d", ob_theme_menu_border_width(theme));
	hex_value("menu_border_color", ob_theme_menu_border_color(theme));
	set_value("menu_items_padding_x", "%d",
		ob_theme_menu_items_padding_x(theme));
	set_value("menu_items_padding_y", "%d",
		ob_theme_menu_items_padding_y(theme));
	hex_value("menu_items_bg", ob_theme_menu_items_bg(theme));
	hex_value("menu_items_text", ob_theme_menu_items_text(theme));
	hex_value("menu_items_active_bg", ob_theme_menu_items_active_bg(theme));
	hex_value("menu_items_active_text",
		ob_theme_menu_items_active_text(theme));
	set_value("menu_separator_thickness", "%d",
		ob_theme_menu_separator_thickness(theme));
	hex_value("menu_separator_color", ob_theme_menu_separator_color(theme));
	hex_value("menu_title_bg", ob_theme_menu_title_bg(theme));
	hex_value("menu_title_text", ob_theme_menu_title_text(theme));
	set_value("menu_title_justify", "%s",
		justify_str(ob_theme_menu_title_justify(theme)));

	hex_value("label_color_active",
		ob_theme_label_text_color(theme, OB_THEME_ACTIVE));
	hex_value("label_color_inactive",
		ob_theme_label_text_color(theme, OB_THEME_INACTIVE));

	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
	{
		char key[64];
		hex_value((snprintf(key, 64, "button_color_%s_unpressed",
			buttons[i].name), key),
			ob_theme_button_color(theme, OB_THEME_ACTIVE, buttons[i].type));

		/* Check the image actually loads (xbm discovery + baking). */
		cairo_surface_t *surf = ob_theme_render_button(theme,
			buttons[i].type, OB_BTN_DEFAULT, OB_THEME_ACTIVE, bw, bh, 1.0);
		set_value((snprintf(key, 64, "button_render_%s", buttons[i].name),
			key), "%s", surf ? "ok" : "missing");
		if (surf)
		{
			cairo_surface_destroy(surf);
		}
	}

	ob_theme_finish(theme);

	if (!assert_file)
	{
		return 0;
	}

	/* Assertion mode: every line of the file must match a computed value. */
	FILE *f = fopen(assert_file, "r");
	if (!f)
	{
		fprintf(stderr, "cannot open assert file %s\n", assert_file);
		return 2;
	}

	int failures = 0;
	char line[256];
	int line_nr = 0;
	while (fgets(line, sizeof(line), f))
	{
		line_nr++;
		/* whole-line comments only ('#' would collide with hex values) */
		const char *scan = line;
		while (*scan == ' ' || *scan == '\t')
		{
			scan++;
		}

		if (*scan == '#' || *scan == '\n' || *scan == '\0')
		{
			continue;
		}

		char *eq = strchr(line, '=');
		if (!eq)
		{
			continue;
		}

		*eq = '\0';
		char *key = line;
		char *want = eq + 1;

		char *end = key + strlen(key);
		while (end > key && (end[-1] == ' ' || end[-1] == '\t'))
		{
			*--end = '\0';
		}

		while (*want == ' ')
		{
			want++;
		}

		end = want + strlen(want);
		while (end > want && (end[-1] == ' ' || end[-1] == '\t' ||
				end[-1] == '\n'))
		{
			*--end = '\0';
		}

		int found = 0;
		for (int i = 0; i < value_count; i++)
		{
			if (!strcasecmp(value_keys[i], key))
			{
				found = 1;
				if (strcasecmp(value_vals[i], want))
				{
					printf("ASSERT FAIL line %d: %s: got %s, want %s\n",
						line_nr, key, value_vals[i], want);
					failures++;
				}
				else
				{
					printf("assert ok line %d: %s == %s\n",
						line_nr, key, want);
				}

				break;
			}
		}

		if (!found)
		{
			printf("ASSERT FAIL line %d: no such key %s\n", line_nr, key);
			failures++;
		}
	}

	fclose(f);
	if (failures)
	{
		fprintf(stderr, "%d assertion(s) failed\n", failures);
		return 1;
	}

	printf("all assertions passed\n");
	return 0;
}
