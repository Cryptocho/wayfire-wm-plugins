# ob-decor — openbox-themed server-side decorations (GPL-2.0-only).

ob_menu_events_inc = include_directories('../ob-menu')

shared_module('ob-decor',
  'decoration.cpp',
  'deco-theme.cpp',
  'deco-layout.cpp',
  'deco-button.cpp',
  'deco-subsurface.cpp',
  dependencies: [dep_wf, libobtheme_dep, dep_cairo, dep_pangocairo],
  include_directories: [ob_menu_events_inc],
  cpp_args: ['-fno-gnu-unique', '-DWLR_USE_UNSTABLE'],
  install: true,
  install_dir: plugindir,
  # installed plugin dir is where libobtheme.so also lives
  install_rpath: '$ORIGIN',
)
