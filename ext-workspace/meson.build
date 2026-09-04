# ext-workspace — server-side ext_workspace_v1 protocol (staging v1).
# Protocol XML is vendored in protocols/ (frozen v1, no wayland-protocols
# dependency at build time).

gen_server_header = generator(wl_scanner,
  output: '@BASENAME@-server-protocol.h',
  arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)
gen_client_header = generator(wl_scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)
gen_protocol = generator(wl_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

ew_proto_xml = ['../protocols/ext-workspace-v1.xml']
ew_server_h  = gen_server_header.process(ew_proto_xml)
ew_client_h  = gen_client_header.process(ew_proto_xml)
ew_proto_c   = gen_protocol.process(ew_proto_xml)

shared_module('ext-workspace',
  'ext-workspace.cpp', ew_proto_c, ew_server_h,
  dependencies: [dep_wf],
  cpp_args: ['-fno-gnu-unique', '-DWLR_USE_UNSTABLE'],
  install: false,
)

# Minimal test client (event-stream assertions, see scripts/ext-workspace-test.sh).
executable('ext-workspace-client',
  '../tools/ext-workspace-client.c', ew_proto_c, ew_client_h,
  dependencies: [dep_wayland_client],
  install: false,
)