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
# ob-theme before its consumers (provider owns ob_theme_init; loading
# order in wayfire.ini decides at runtime, repo order is documentation).
subdir('ob-theme')
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

# Input-injection client (virtual keyboard + pointer) for real-input-path
# menu tests (see scripts/input-path-test.sh). Protocols are copied from
# the reference wlroots (scroll/subprojects/wlroots/protocol).
dep_wayland_client = dependency('wayland-client')
dep_xkbcommon = dependency('xkbcommon')
wl_scanner = find_program('wayland-scanner')
gen_protocol = generator(wl_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)
gen_client_header = generator(wl_scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)
vinput_proto_src = [
  'protocols/virtual-keyboard-unstable-v1.xml',
  'protocols/wlr-virtual-pointer-unstable-v1.xml',
]
vinput_proto_c = gen_protocol.process(vinput_proto_src)
vinput_proto_h = gen_client_header.process(vinput_proto_src)
executable('vinput', ['tools/vinput.c', vinput_proto_c, vinput_proto_h],
  include_directories: include_directories('.'),
  dependencies: [dep_wayland_client, dep_xkbcommon],
)

# M3: ob-menu plugin.
subdir('ob-menu')

# P3: layout-manager (global layout mode switching).
subdir('layout-manager')

# P5: ob-switcher (fast-switcher + themerc-themed window-switcher OSD).
subdir('ob-switcher')
