#!/usr/bin/env python3
"""layout-manager auto-float assertions (headless, over IPC).

Runs against test/wayfire-layout-float.ini (float_by_default = app-id kitty):
- harvest in tiling mode leaves all matching views floating (tree empty)
- windows mapped DURING tiling mode are auto-floated on arrival
- an explicit toggle-float overrides the rule for that view instance
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time

SOCK = os.environ["SOCK"]
WF_DISPLAY = os.environ.get("WF_DISPLAY", "")
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


def leaves(node, out):
    if "view-id" in node:
        out.append(node["view-id"])
        return
    for key in ("horizontal-split", "vertical-split"):
        for ch in node.get(key, []):
            leaves(ch, out)


def tree_ids():
    lay = call("layout-manager/get-layout")
    out = []
    leaves(lay.get("layout", {}), out)
    return out


def wait_kitties(n, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        ks = find_kitty()
        if len(ks) >= n:
            return ks
        time.sleep(0.1)
    return find_kitty()


ks = wait_kitties(3)
check("3 autostart kitties mapped", len(ks) >= 3, f"n={len(ks)}")

# auto-float views mapped in stacking mode are still excluded from harvest
call("layout-manager/set-mode", {"mode": "tiling"})
time.sleep(0.3)
check("harvest skips auto-float views (tree empty)", tree_ids() == [],
      str(tree_ids()))

# a window mapped DURING tiling mode is auto-floated on arrival
extra = subprocess.Popen(["kitty"], env={**os.environ,
        "WAYLAND_DISPLAY": WF_DISPLAY},
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    ks = wait_kitties(4)
    check("4th kitty mapped", len(ks) >= 4, f"n={len(ks)}")
    time.sleep(0.3)
    check("tree still empty (new view auto-floated)", tree_ids() == [],
          str(tree_ids()))
    known = {v["id"] for v in ks} - {v["id"] for v in ks[:0]}
    new_id = None
    for v in ks:
        st = call("layout-manager/get-view-state", {"id": v["id"]})
        if st.get("floating"):
            new_id = v["id"]
            break
    check("new view marked floating", new_id is not None, str(new_id))

    # explicit toggle overrides the rule for this view instance
    if new_id is not None:
        r = call("layout-manager/toggle-float", {"id": new_id})
        check("explicit toggle tiles an auto-float view",
              r.get("floating") is False, str(r))
        check("tree now holds the explicitly tiled view", len(tree_ids()) == 1,
              str(tree_ids()))
        r = call("layout-manager/toggle-float", {"id": new_id})
        check("toggle back to floating", r.get("floating") is True, str(r))
        check("tree empty again", tree_ids() == [], str(tree_ids()))
finally:
    if extra.poll() is None:
        extra.terminate()
        try:
            extra.wait(timeout=5)
        except subprocess.TimeoutExpired:
            extra.kill()

call("layout-manager/set-mode", {"mode": "stacking"})

print()
if failures:
    print(f"LM FLOAT TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("LM FLOAT TEST OK (M-C auto-float)")
