# ob-theme — openbox theme provider plugin (GPL-2.0-only).

shared_module('ob-theme',
  'ob-theme.cpp',
  dependencies: [dep_wf, libobtheme_dep],
  cpp_args: ['-fno-gnu-unique', '-DWLR_USE_UNSTABLE'],
  include_directories: include_directories('.'),
  install: false,
)
