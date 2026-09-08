#!/usr/bin/env python3
"""ob-theme provider assertions over the wayfire IPC (see ob-theme-test.sh)."""
import json
import os
import socket
import struct

SOCK = os.environ["SOCK"]
failures = []


def call(method, data=None, timeout=5.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(SOCK)
    payload = json.dumps({"method": method, "data": data or {}}).encode()
    s.sendall(struct.pack("<I", len(payload)) + payload)
    header = b""
    while len(header) < 4:
        chunk = s.recv(4 - len(header))
        if not chunk:
            raise RuntimeError("conn closed (header)")
        header += chunk
    (length,) = struct.unpack("<I", header)
    body = b""
    while len(body) < length:
        chunk = s.recv(length - len(body))
        if not chunk:
            raise RuntimeError("conn closed (body)")
        body += chunk
    s.close()
    return json.loads(body)


def check(name, cond, detail=""):
    print(f"[{'ok' if cond else 'FAIL'}] {name} {detail}")
    if not cond:
        failures.append(name)


st = call("ob-theme/debug", {"op": "get-state"})
check("provider has live theme", st.get("has_theme") is True, str(st))
check("theme name parsed", st.get("theme_name") == "OB-Arc-Dark",
      f"got {st.get('theme_name')!r}")
check("epoch starts at 1", st.get("epoch") == 1, str(st.get("epoch")))

st2 = call("ob-theme/debug", {"op": "reload"})
check("reload bumps epoch", st2.get("epoch") == 2, str(st2.get("epoch")))
check("theme survives reload", st2.get("has_theme") is True and
      st2.get("theme_name") == "OB-Arc-Dark", str(st2))

if failures:
    print(f"ob-theme-test: {len(failures)} FAIL")
    raise SystemExit(1)
print("ob-theme-test: all assertions passed")
