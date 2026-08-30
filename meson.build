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

subdir('libobtheme')
libobtheme_dep = declare_dependency(
  link_with: libobtheme,
  include_directories: include_directories('libobtheme', 'libobtheme/include'),
  dependencies: [dep_cairo],
)

subdir('proto-pan')

# T1 linkage check: obtheme.h consumable from C++.
executable('include-check', 'tools/include-check.cpp',
  dependencies: [libobtheme_dep],
)
