#!/usr/bin/env python3
"""layout-manager integration assertions (phase 3): tiling with ob-decor
decorations + ob-menu co-loaded.

- leaf geometry vs the decorated view box (frame margins match ob-decor)
- divider drag works while decorations are active (presses in the band are
  swallowed before ob-decor's border-resize can start)
- right-click in the outer gap opens the ob-menu root menu
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
REPO = os.environ.get("REPO", ".")
failures = []

# OB-Arc-Dark + ring_width=2 (see wayfire-layout-interactive.ini):
# left/right/bottom margin = border(2)+ring(2) = 4; top = title(26)+4 = 30
THICK = 4
TOP = 30


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


def view_info(vid):
    r = call("window-rules/view-info", {"id": vid})
    if isinstance(r, dict) and "app-id" in r:
        return r
    return r.get("info", {})


def leaves(node, out):
    if "view-id" in node:
        out.append(node)
        return
    for key in ("horizontal-split", "vertical-split"):
        for ch in node.get(key, []):
            leaves(ch, out)


def wait_kitties(n, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        ks = find_kitty()
        if len(ks) >= n:
            return ks
        time.sleep(0.1)
    return find_kitty()


ks = wait_kitties(3)
check("3 decorated kitties mapped", len(ks) >= 3, f"n={len(ks)}")

call("layout-manager/set-mode", {"mode": "tiling"})
time.sleep(0.3)

ids = []
leaves(call("layout-manager/get-layout").get("layout", {}), ids)
check("3 leaves harvested with decorations active", len(ids) == 3,
      str(len(ids)))

if len(ids) == 3:
    # The tree's leaf geometry is the window-management box: the ob-decor
    # frame (titlebar + borders + ring) lives INSIDE this box, so the
    # decorated view reports exactly the leaf geometry.
    leaf0 = ids[0]["geometry"]
    wbox = view_info(ids[0]["view-id"]).get("geometry", {})
    check("decorated view box = tree leaf geometry (frame inside wm box)",
          wbox == leaf0, f"{wbox} vs {leaf0}")

    # divider drag with decorations active (the drag press lands inside the
    # band, which overlaps the SSD borders — layout-manager must swallow it
    # before ob-decor's border-resize reacts)
    vinput = os.path.join(REPO, "build", "vinput")

    def drag(seq):
        subprocess.run([vinput, WF_DISPLAY, *seq], check=True)

    w0b = ids[0]["geometry"]["width"]
    w1b = ids[1]["geometry"]["width"]
    drag(["move", "-5000", "-5000", "move", "427", "360",
          "button", "0x110", "1",
          "move", "20", "0", "move", "20", "0",
          "button", "0x110", "0"])
    time.sleep(0.4)
    ids = []
    leaves(call("layout-manager/get-layout").get("layout", {}), ids)
    w0a = ids[0]["geometry"]["width"]
    w1a = ids[1]["geometry"]["width"]
    check("divider drag works with decorations (left +~40)",
          abs((w0a - w0b) - 40) <= 2, f"{w0b}->{w0a}")
    check("divider drag works with decorations (right -~40)",
          abs((w1b - w1a) - 40) <= 2, f"{w1b}->{w1a}")

# --- outer gap right-click opens the ob-menu root menu --------------------
# left outer gap: x in [0,4) is bare desktop (frame starts at x=4); y=360
# is mid-screen (the top strip is titlebar, which would be a client menu)
subprocess.run([vinput, WF_DISPLAY,
                "move", "-5000", "-5000", "move", "2", "360",
                "button", "0x111", "1", "button", "0x111", "0"], check=True)
time.sleep(0.5)
st = call("ob-menu/debug", {"op": "get-state"}).get("state", {})
check("outer gap right-click opens root menu",
      st.get("open") is True and st.get("menu_id") == "root-menu", str(st))
call("ob-menu/debug", {"op": "close"})

call("layout-manager/set-mode", {"mode": "stacking"})

print()
if failures:
    print(f"LM INTEGRATION TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("LM INTEGRATION TEST OK (M-E)")
