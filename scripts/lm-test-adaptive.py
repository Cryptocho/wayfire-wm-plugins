#!/usr/bin/env python3
"""layout-manager adaptive tiling style + focus-follows-mouse assertions.

Config: tiling_layout=adaptive, focus_follows_mouse=true, 3 autostart
kitties at (100,100) 890x560.

Expected spiral (workarea 1280x720, outer gap 8, inner 4 per side):
  k1 full:         (8, 8, 1264, 704) -> halved on k2's arrival (w>=h):
  k1 keeps left:   (8, 8, 628, 704)      k2 right: (644, 8, 628, 704)
  k3 splits k2 (h 704 >= w 628 -> stacked): k2 top (644, 8, 628, 348),
  k3 bottom (644, 364, 628, 348)
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


def layout():
    return call("layout-manager/get-layout").get("layout", {})


def near(g, x, y, w, h, tol=2):
    return (abs(g["x"] - x) <= tol and abs(g["y"] - y) <= tol and
            abs(g["width"] - w) <= tol and abs(g["height"] - h) <= tol)


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

call("layout-manager/set-mode", {"mode": "tiling"})
time.sleep(0.3)

ids = []
leaves(layout(), ids)
check("3 leaves harvested (adaptive)", len(ids) == 3, str(len(ids)))

if len(ids) == 3:
    geos = {e["view-id"]: e["geometry"] for e in ids}
    left = [g for g in geos.values() if near(g, 8, 8, 628, 704)]
    top = [g for g in geos.values() if near(g, 644, 8, 628, 348)]
    bot = [g for g in geos.values() if near(g, 644, 364, 628, 348)]
    check("spiral: left column full-height (halved once, w>=h)",
          len(left) == 1, json.dumps(geos))
    check("spiral: top-right half (halved h>=w)", len(top) == 1, "")
    check("spiral: bottom-right half", len(bot) == 1, "")

    # structure: root [leaf, nested split [leaf, leaf]] — the halved view
    # must be wrapped so its sibling keeps its share
    root = layout()
    topk = root.get("vertical-split", [])
    check("root split has 2 children (sibling untouched)",
          len(topk) == 2, json.dumps(root)[:160])
    nested = topk[1].get("horizontal-split", []) if len(topk) == 2 else []
    check("halved view wrapped in its own split (2 leaves)",
          len(nested) == 2, json.dumps(topk[1])[:140] if len(topk) == 2 else "")

    # --- 4th window: splits the focused (bottom-right) view ---------------
    if bot:
        g = bot[0]
        subprocess.run([os.path.join(REPO, "build", "vinput"), WF_DISPLAY,
                        "move", "-5000", "-5000",
                        "move", str(g["x"] + g["width"] // 2),
                        str(g["y"] + g["height"] // 2)], check=True)
        time.sleep(0.3)
    extra = subprocess.Popen(["kitty"], env={**os.environ,
            "WAYLAND_DISPLAY": WF_DISPLAY},
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ks4 = wait_kitties(4)
        check("4th kitty mapped", len(ks4) >= 4, f"n={len(ks4)}")
        time.sleep(0.4)
        ids = []
        leaves(layout(), ids)
        check("4 leaves after spawn", len(ids) == 4, str(len(ids)))
        q4 = [e for e in ids if near(e["geometry"], 962, 364, 310, 348)]
        check("new window took the halved half (bottom-right quarter)",
              len(q4) == 1, json.dumps([e["geometry"] for e in ids]))
        if q4:
            fid = (call("window-rules/get-focused-view").get("info", {}) or {}).get("id")
            check("focus given to the new window", fid == q4[0]["view-id"],
                  f"focused={fid}")
    finally:
        if extra.poll() is None:
            extra.terminate()
            try:
                extra.wait(timeout=5)
            except subprocess.TimeoutExpired:
                extra.kill()
        time.sleep(0.3)
else:
    check("spiral geometry skipped", False, "harvest incomplete")

# --- focus follows mouse ---------------------------------------------------
ids = []
leaves(layout(), ids)
if len(ids) >= 2:
    by_id = {e["view-id"]: e["geometry"] for e in ids}
    left = next((e for e in ids if e["geometry"]["x"] < 400), None)
    right = next((e for e in ids if e["geometry"]["x"] > 400 and
                  e["geometry"]["y"] < 300), None)
    vinput = os.path.join(REPO, "build", "vinput")

    def hover(leaf):
        g = leaf["geometry"]
        subprocess.run([vinput, WF_DISPLAY, "move", "-5000", "-5000",
                        "move", str(g["x"] + g["width"] // 2),
                        str(g["y"] + g["height"] // 2)], check=True)
        time.sleep(0.4)

    if left and right:
        hover(left)
        fid = (call("window-rules/get-focused-view").get("info", {}) or {}).get("id")
        check("hover left view focuses it", fid == left["view-id"],
              f"focused={fid}")
        hover(right)
        fid = (call("window-rules/get-focused-view").get("info", {}) or {}).get("id")
        check("hover right view focuses it", fid == right["view-id"],
              f"focused={fid}")
    else:
        check("follow-mouse leafs found", False, json.dumps(ids))
else:
    check("follow-mouse skipped", False, "no leaves")

call("layout-manager/set-mode", {"mode": "stacking"})

print()
if failures:
    print(f"LM ADAPTIVE TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("LM ADAPTIVE TEST OK")
