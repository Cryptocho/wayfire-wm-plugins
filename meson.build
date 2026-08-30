project('wayfire-wm-plugins', 'cpp', 'c',
  version: '0.1.0',
  license: 'GPL-2.0-only',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.61.0')

# Wayfire is loaded via its installed headers + pkg-config.
# There is no libwayfire.so: plugin symbols resolve from the wayfire binary
# itself (linked with -rdynamic), so plugins must NOT link any wayfire lib.
dep_wf = dependency('wayfire')

# Dependencies for libobtheme (vendored labwc theme engine).
dep_cairo = dependency('cairo')
dep_pangocairo = dependency('pangocairo')
dep_png = dependency('libpng')
dep_glib = dependency('glib-2.0')
dep_wayland_server = dependency('wayland-server')  # wayland-util.h (wl_array)
dep_libdrm = dependency('libdrm')                  # drm_fourcc.h
dep_libxml = dependency('libxml-2.0')              # ob-menu menu.xml parsing

subdir('libobtheme')
libobtheme_dep = declare_dependency(
  link_with: libobtheme,
  include_directories: include_directories('libobtheme', 'libobtheme/include'),
  dependencies: [dep_cairo],
)

subdir('proto-pan')
subdir('ob-decor')

# T1 linkage check: obtheme.h consumable from C++.
executable('include-check', 'tools/include-check.cpp',
  dependencies: [libobtheme_dep],
)

# T2: theme parsing dump/assert CLI.
executable('obtheme-dump', 'tools/obtheme-dump.c',
  dependencies: [libobtheme_dep],
)

# M1: menu.xml parse dump/assert CLI.
executable('obmenu-dump', ['tools/obmenu-dump.cpp', 'ob-menu/menu-parse.cpp'],
  include_directories: include_directories('ob-menu', 'libobtheme', 'libobtheme/include'),
  link_with: libobtheme,
  dependencies: [dep_glib, dep_libxml],
)

# M3: ob-menu plugin.
subdir('ob-menu')
