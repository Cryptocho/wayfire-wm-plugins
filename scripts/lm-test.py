#!/usr/bin/env python3
"""layout-manager automated assertions (headless, over IPC).

M-A: mode state machine (get/set/toggle-mode).
M-B: harvest into the tiling tree, sway insertion, unmap shrink, restore
on switch back to stacking.

Observation endpoints: layout-manager/get-layout + ipc-rules
(window-rules/list-views, view-info).
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


def view_info(vid):
    r = call("window-rules/view-info", {"id": vid})
    if isinstance(r, dict) and "app-id" in r:
        return r
    return r.get("info", {})


def layout(ws=None):
    data = {"workspace": ws} if ws else {}
    return call("layout-manager/get-layout", data)


def leaves(node, out):
    """collect the leaf view entries of a layout JSON tree"""
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


# --- wait for the 3 autostarted kitties to map ---------------------------
ks = wait_kitties(3)
check("3 autostart kitties mapped", len(ks) >= 3, f"n={len(ks)}")

# --- M-A: mode state machine ---------------------------------------------
r = call("layout-manager/get-mode")
check("default mode is stacking", r.get("mode") == "stacking", str(r))

r = call("layout-manager/set-mode", {"mode": "floaty"})
check("invalid mode rejected", "error" in r, str(r))
r = call("layout-manager/set-mode", {})
check("missing mode rejected", "error" in r, str(r))

# --- M-B: harvest into the tree -------------------------------------------
pre_geo = {v["id"]: view_info(v["id"]).get("geometry") for v in ks}

r = call("layout-manager/set-mode", {"mode": "tiling"})
check("set-mode tiling", r.get("mode") == "tiling", str(r))

lay = layout()
root = lay.get("layout", {})
kids = root.get("vertical-split", root.get("horizontal-split"))
check("root split exists with 3 children", isinstance(kids, list) and
      len(kids) == 3, json.dumps(lay)[:200])

if isinstance(kids, list) and len(kids) == 3:
    ids = []
    leaves(root, ids)
    leaf_ids = [e["view-id"] for e in ids]
    kitty_ids = sorted(v["id"] for v in ks)
    check("all 3 kitties are leaves of the tree",
          sorted(leaf_ids) == kitty_ids, f"{sorted(leaf_ids)} vs {kitty_ids}")

    # geometry: per-side gap model — outer 8 on workarea edges, inner 4 on
    # both sides of each internal boundary (2 boundaries between 3 columns)
    geos = [e["geometry"] for e in ids]
    xs = sorted(g["x"] for g in geos)
    ws_sum = sum(g["width"] for g in geos)
    check("views start at outer gap (x=8)", xs[0] == 8, str(xs))
    check("views end at width-outer gap", xs[-1] + geos[-1]["width"] == 1272,
          str(xs[-1] + geos[-1]["width"]))
    check("total tiled width accounts inner+outer gaps", ws_sum == 1248,
          str(ws_sum))
    ys = {g["y"] for g in geos}
    hs = {g["height"] for g in geos}
    check("full-height columns with outer gap", ys == {8} and hs == {704},
          f"y={ys} h={hs}")
    widths = sorted(g["width"] for g in geos)
    check("equal thirds (rounding + per-side inner gaps)",
          widths[-1] - widths[0] <= 2 * 4 + 1, str(widths))

    # view-info agrees with the tree leaf geometry + tiled edges set
    info = view_info(ids[0]["view-id"])
    g0 = next(e["geometry"] for e in ids if e["view-id"] == ids[0]["view-id"])
    check("tiled-edges applied (15)",
          info.get("tiled-edges") == 15, str(info.get("tiled-edges")))
    check("view geometry matches tree leaf",
          info.get("geometry") == g0,
          f"{info.get('geometry')} vs {g0}")

# --- M-B: new window inserts after the last focused tiled view ------------
extra = subprocess.Popen(["kitty"], env={**os.environ,
        "WAYLAND_DISPLAY": WF_DISPLAY},
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    ks4 = wait_kitties(4)
    check("4th kitty mapped", len(ks4) >= 4, f"n={len(ks4)}")
    lay = layout()
    ids = []
    leaves(lay.get("layout", {}), ids)
    check("tree now has 4 leaves", len(ids) == 4, str(len(ids)))
    check("new view appended after last focused (last leaf)",
          ids[-1]["view-id"] == ks4[-1]["id"],
          f"last={ids[-1]['view-id']} new={ks4[-1]['id']}")

    # --- M-B: unmapping shrinks the tree -----------------------------------
    os.kill(extra.pid, 15)
    deadline = time.time() + 10
    while time.time() < deadline and len(find_kitty()) >= 4:
        time.sleep(0.1)
    lay = layout()
    ids = []
    leaves(lay.get("layout", {}), ids)
    check("tree shrinks back to 3 leaves after close", len(ids) == 3,
          str(len(ids)))
finally:
    if extra.poll() is None:
        extra.kill()
        extra.wait()

# --- M-B: restore on switch back to stacking ------------------------------
r = call("layout-manager/set-mode", {"mode": "stacking"})
check("set-mode stacking", r.get("mode") == "stacking", str(r))

lay = layout()
ids = []
leaves(lay.get("layout", {}), ids)
check("tree is empty in stacking mode", len(ids) == 0, str(len(ids)))

restored = True
for v in find_kitty():
    info = view_info(v["id"])
    if info.get("tiled-edges") != 0:
        restored = False
    g = info.get("geometry", {})
    pg = pre_geo.get(v["id"], {})
    if not g or not pg or (g != pg):
        restored = False
check("pre-tiling geometry restored, tiled-edges cleared", restored,
      json.dumps({v["id"]: view_info(v["id"]).get("geometry")
                  for v in find_kitty()})[:300])

# --- round trip: harvest again --------------------------------------------
r = call("layout-manager/set-mode", {"mode": "tiling"})
lay = layout()
ids = []
leaves(lay.get("layout", {}), ids)
check("re-harvest puts all 3 kitties back in the tree", len(ids) == 3,
      str(len(ids)))
call("layout-manager/set-mode", {"mode": "stacking"})

r = call("layout-manager/toggle-mode")
check("toggle-mode works", r.get("mode") == "tiling", str(r))
call("layout-manager/set-mode", {"mode": "stacking"})

check("views still alive", len(find_kitty()) >= 3, f"n={len(find_kitty())}")

print()
if failures:
    print(f"LM TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("LM TEST OK (M-A+M-B)")
