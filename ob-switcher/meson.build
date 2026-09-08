# ob-switcher — fast-switcher + openbox-themed window-switcher OSD.

shared_module('ob-switcher',
  'ob-switcher.cpp',
  dependencies: [dep_wf, libobtheme_dep, dep_cairo, dep_pangocairo],
  cpp_args: ['-fno-gnu-unique', '-DWLR_USE_UNSTABLE'],
  install: true,
  install_dir: plugindir,
  # installed plugin dir is where libobtheme.so also lives
  install_rpath: '$ORIGIN',
)
