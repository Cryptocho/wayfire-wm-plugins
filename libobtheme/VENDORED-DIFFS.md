# VENDORED-DIFFS.md — libobtheme vs upstream labwc

Source: labwc @ `5aaa5da0571b` (https://github.com/labwc/labwc), GPL-2.0-only.
Every vendored file carries a provenance banner as its first line.

## Vendored files

- `include/` — theme.h, buffer.h(*), common/{node-type,buf,dir,font,graphic-helpers,
  list,box,macros,match,mem,parse-bool,string-helpers}.h, img/{img,img-png,img-xbm,img-xpm}.h
- `src/` — theme.c, buffer.c(*)
- `src/common/` — {dir,buf,font,graphic-helpers,match,mem,parse-bool,string-helpers}.c, xcolor-table.h
- `src/img/` — {img,img-png,img-xbm,img-xpm}.c

(*) = rewritten locally (cairo-surface shim); all others verbatim except the
diffs below.

## New files (written for this project)

- `obtheme.h`, `obtheme.c` — public API: rcxml-shim population, theme_init/
  theme_finish wrapper, button rendering helper
- `shims/wlr/util/{log,box}.h` — fprintf-based wlr_log, plain struct wlr_box
- `shims/config.h` — `#define HAVE_RSVG 0` (SVG support intentionally off)
- `shims/config/rcxml.h` — `struct rcxml` subset + `extern rc` +
  `enum ssd_active_state` + `FOR_EACH_ACTIVE_STATE` + `ssd_get_corner_width()`
  (mirrors labwc's ssd.h)

## Local diffs in vendored files

### src/theme.c
1. Includes: removed `labwc.h`, `ssd.h`, `<wlr/render/pixman.h>` (supplied by
   `shims/config/rcxml.h` or dropped).
2. `zdrop()`: `wlr_buffer_drop(&(*buffer)->base)` → `buffer_destroy(*buffer)`.
3. `theme_builtin()` pixman check:
   `wlr_renderer_is_pixman(server.renderer)` branch removed; the non-pixman
   (GL) branch is unconditional — wayfire always uses the GL renderer.

### include/common/xml.h
1. Added `extern "C"` guards (header is consumed from C++ by ob-menu).

### src/common/xml.c
1. `lab_xml_expand_dotted_attributes()`: added
   `__attribute__((visibility("default")))` — the library builds as a
   shared lib with `gnu_symbol_visibility: hidden`, and ob-menu's parser
   consumes this helper, so it must stay exported.

### src/img/img.c
1. Includes: removed `labwc.h`.
2. `img_destroy()`: `wlr_buffer_drop(&img->data->buffer->base)` →
   `buffer_destroy(img->data->buffer)`.

### include/buffer.h
1. `struct lab_data_buffer`: removed `struct wlr_buffer base`; added
   `float scale`. Removed `buffer_create_from_wlr_buffer()` (unused by
   vendored code).
2. `buffer_destroy()` declared (replaces direct wlr_buffer_drop at call sites).

## Shim semantics (see shims/ + src/buffer.c)

- `struct lab_data_buffer` wraps a cairo ARGB32 surface; `buffer_create_cairo`
  allocates at `logical_size * scale`; `buffer_resize` repaints centered at
  scale, destroys the source, returns the new buffer.
- `rc.*` fields provided: merge_config, theme_name, config_dir(NULL),
  corner_radius, resize_corner_range, title_buttons_left/right(+nr),
  font_{activewindow,inactivewindow,menuheader,menuitem,osd},
  `struct theme *theme` back-pointer (set before theme_init, used by
  load_button's hover-overlay painter).
