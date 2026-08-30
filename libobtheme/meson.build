# libobtheme — vendored labwc theme engine + obtheme shims (GPL-2.0-only).
# See VENDORED-DIFFS.md for the exact local changes vs upstream labwc.

libobtheme_sources = files(
  'obtheme.c',
  'src/buffer.c',
  'src/theme.c',
  'src/common/buf.c',
  'src/common/dir.c',
  'src/common/font.c',
  'src/common/graphic-helpers.c',
  'src/common/match.c',
  'src/common/mem.c',
  'src/common/parse-bool.c',
  'src/common/string-helpers.c',
  'src/common/xml.c',
  'src/img/img.c',
  'src/img/img-png.c',
  'src/img/img-xbm.c',
  'src/img/img-xpm.c',
)

libobtheme_includes = include_directories(
  '.',            # buffer.h, obtheme.h
  'include',      # theme.h, common/*, img/*
  'src',          # xcolor-table.h is included as "xcolor-table.h" from theme.c
  'src/common',
  'shims',        # wlr/util/{log,box}.h, config.h, config/rcxml.h
)

libobtheme = static_library('obtheme',
  libobtheme_sources,
  include_directories: libobtheme_includes,
  dependencies: [dep_cairo, dep_pangocairo, dep_png, dep_glib, dep_libxml,
    dep_wayland_server, dep_libdrm],
  # vendored img-xbm.c uses GNU case ranges
  override_options: ['c_std=gnu11'],
  pic: true,
)
