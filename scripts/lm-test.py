#!/usr/bin/env python3
"""layout-manager automated assertions (headless, over IPC).

M-A: mode state machine (get/set/toggle-mode), with 3 autostart kitties
present to prove load/unload safety around mapped views.
Observation endpoint: ipc-rules (window-rules/list-views, view-info).
"""
import json
import os
import socket
import struct
import sys
import time

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


def views():
    r = call("window-rules/list-views")
    if isinstance(r, list):
        return r
    return r.get("views", [])


def find_kitty():
    return [v for v in views()
            if v.get("app-id") == "kitty" and v.get("role") == "toplevel"]


# --- wait for the 3 autostarted kitties to map ---------------------------
kitties = []
for _ in range(150):
    kitties = find_kitty()
    if len(kitties) >= 3:
        break
    time.sleep(0.1)
check("3 autostart kitties mapped", len(kitties) >= 3, f"n={len(kitties)}")

# --- M-A: mode state machine ---------------------------------------------
r = call("layout-manager/get-mode")
check("default mode is stacking", r.get("mode") == "stacking", str(r))

r = call("layout-manager/set-mode", {"mode": "tiling"})
check("set-mode tiling accepted", r.get("mode") == "tiling", str(r))

r = call("layout-manager/get-mode")
check("get-mode reflects tiling", r.get("mode") == "tiling", str(r))

r = call("layout-manager/toggle-mode")
check("toggle-mode -> stacking", r.get("mode") == "stacking", str(r))

r = call("layout-manager/set-mode", {"mode": "floaty"})
check("invalid mode rejected", "error" in r, str(r))
r = call("layout-manager/get-mode")
check("mode unchanged after invalid set", r.get("mode") == "stacking", str(r))

r = call("layout-manager/set-mode", {})
check("missing mode rejected", "error" in r, str(r))

# --- round-trips with mapped views (stub migration; must not crash) ------
for i in range(3):
    call("layout-manager/set-mode", {"mode": "tiling"})
    call("layout-manager/set-mode", {"mode": "stacking"})
r = call("layout-manager/get-mode")
check("3x mode round-trip with views present", r.get("mode") == "stacking",
      str(r))

# views still alive after round-trips
check("views still alive after mode round-trips", len(find_kitty()) >= 3,
      f"n={len(find_kitty())}")

print()
if failures:
    print(f"LM TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("LM TEST OK (M-A)")
