project('wayfire-wm-plugins', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.61.0')

# Wayfire is loaded via its installed headers + pkg-config.
# There is no libwayfire.so: plugin symbols resolve from the wayfire binary
# itself (linked with -rdynamic), so plugins must NOT link any wayfire lib.
dep_wf = dependency('wayfire')

subdir('proto-pan')
