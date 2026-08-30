# ob-decor — openbox-themed server-side decorations (GPL-2.0-only).

shared_module('ob-decor',
  'decoration.cpp',
  'deco-theme.cpp',
  'deco-layout.cpp',
  'deco-button.cpp',
  'deco-subsurface.cpp',
  dependencies: [dep_wf, libobtheme_dep, dep_cairo, dep_pangocairo],
  cpp_args: ['-fno-gnu-unique', '-DWLR_USE_UNSTABLE'],
  install: false,
)
