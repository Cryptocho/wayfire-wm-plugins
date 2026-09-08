#!/usr/bin/env python3
"""Drag-to-reorder + fraction-resize tests for layout-manager (headless).

Drives the virtual pointer (vinput, one invocation per gesture — each
invocation is a separate client and disconnecting destroys the device,
which would end any drag) and asserts over the layout-manager IPC:

  D-1  drag a tiled view over the lower third of another tiled view
       -> INSERT_BELOW (new split wrapping both)
  D-2  drag a tiled view to the center of another -> INSERT_SWAP,
       geometries exchange
  D-3  resize button on the right half of a view widens its column
  D-4  resize button on the left half of a view widens the left column

Process safety: the wayfire PID belongs to the shell wrapper (lm-drag-test.sh);
this script only spawns temporary kitty windows and kills exactly those PIDs.
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time

RUN = os.getenv("RUN", os.path.join(os.getenv("REPO", "."), "build", "run"))
WF_DISPLAY = os.environ["WF_DISPLAY"]
REPO = os.environ["REPO"]
SOCK = os.environ["SOCK"]

FAIL = [0]


def fail(msg):
    print(f"[FAIL] {msg}")
    FAIL[0] = 1


def ok(msg):
    print(f"[ok] {msg}")


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


def layout():
    return call("layout-manager/get-layout")


def view_info(view_id):
    return call("ipc-rules/view-info", {"id": view_id})


def leaves(node, out):
    if "view-id" in node:
        out.append(node)
        return
    for key in ("vertical-split", "horizontal-split"):
        if key in node:
            for c in node[key]:
                leaves(c, out)


def vinput(*args):
    subprocess.run([os.path.join(REPO, "build", "vinput"), WF_DISPLAY, *args],
                   check=True)


def mapped_view_ids():
    r = call("window-rules/list-views")
    if isinstance(r, list):
        views = r
    else:
        views = r.get("views", [])
    return [v["id"] for v in views
            if v.get("app-id") == "kitty" and v.get("role") == "toplevel"]


def wait_view_count(n, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if len(mapped_view_ids()) >= n:
            return True
        time.sleep(0.2)
    return False


def main():
    r = call("layout-manager/set-mode", {"mode": "tiling"})
    ok(f"set-mode tiling {r}")

    if not wait_view_count(3):
        fail("3 kitty views never mapped")
        return
    time.sleep(0.5)

    lay = layout()
    ids = []
    leaves(lay.get("layout", {}), ids)
    if len(ids) != 3:
        fail(f"expected 3 leaves, got {len(ids)}: {json.dumps(lay)[:200]}")
        return
    ok(f"3 leaves harvested {len(ids)}")

    geo = {e["view-id"]: e["geometry"] for e in ids}
    left = ids[0]["view-id"]
    right_top = ids[1]["view-id"]
    right_bottom = ids[2]["view-id"]
    # default_orientation auto on 1280x720 -> SPLIT_VERTICAL root: 3 columns
    ok(f"geometry {geo}")

    def drag(from_x, from_y, to_x, to_y, btn="0x110"):
        # vinput move is RELATIVE; the pointer is reset to (0,0) by the
        # initial -5000,-5000 move (headless cursor is clamped to the
        # output), so a relative move by from_x/from_y lands on that point.
        vinput("move", "-5000", "-5000",
               "move", str(from_x), str(from_y),
               "button", btn, "1",
               "move", str(to_x - from_x), str(to_y - from_y),
               "button", btn, "0")

    # ---- D-1: drag left view into the lower third of the middle column ----
    src = (geo[left]["x"] + geo[left]["width"] // 2,
           geo[left]["y"] + geo[left]["height"] // 2)
    dst = (geo[right_top]["x"] + geo[right_top]["width"] // 2,
           geo[right_top]["y"] + int(geo[right_top]["height"] * 0.8))
    drag(src[0], src[1], dst[0], dst[1])
    time.sleep(0.5)

    lay = layout()
    out = []
    leaves(lay.get("layout", {}), out)
    if len(out) != 3:
        fail(f"D-1 tree lost views: {json.dumps(lay)[:300]}")
        return

    # the dragged view must now sit in the right column, below right_top
    geoa = {e["view-id"]: e["geometry"] for e in out}
    if geoa[left]["x"] < 640:
        fail(f"D-1 dragged view still on the left: {geoa[left]}")
    else:
        ok(f"D-1 dragged view moved into the right column {geoa[left]}")
    if geoa[right_top]["y"] < geoa[left]["y"]:
        ok(f"D-1 dragged view inserted below right_top "
           f"(top.y={geoa[right_top]['y']} dragged.y={geoa[left]['y']})")
    else:
        fail(f"D-1 insert-below ordering wrong: {geoa}")
    ok("D-1 all 3 views preserved in the tree")

    # ---- D-2: drag right_top to the CENTER of left (swap) ------------------
    src2 = (geoa[right_top]["x"] + geoa[right_top]["width"] // 2,
            geoa[right_top]["y"] + geoa[right_top]["height"] // 2)
    dst2 = (geoa[left]["x"] + geoa[left]["width"] // 2,
            geoa[left]["y"] + geoa[left]["height"] // 2)
    drag(src2[0], src2[1], dst2[0], dst2[1])
    time.sleep(0.5)

    out = []
    leaves(layout().get("layout", {}), out)
    geob = {e["view-id"]: e["geometry"] for e in out}
    if len(out) != 3:
        fail(f"D-2 tree lost views: {json.dumps(layout())[:300]}")
        return
    # swap: the two views exchanged their boxes (left column <-> right_top box)
    if geob[left] != geoa[right_top]:
        fail(f"D-2 left view did not take right_top's box: {geob[left]} vs {geoa[right_top]}")
    else:
        ok(f"D-2 swap: left view now at {geob[left]}")
    if geob[right_top] != geoa[left]:
        fail(f"D-2 right_top did not take left's box: {geob[right_top]} vs {geoa[left]}")
    else:
        ok(f"D-2 swap: right_top now at {geob[right_top]}")

    # ---- D-3: resize button on a column: exactly one column changes by ~20px ----
    out = []
    leaves(layout().get("layout", {}), out)
    geob = {e["view-id"]: e["geometry"] for e in out}
    target = right_bottom
    trg = geob[target]
    vinput("move", "-5000", "-5000",
           "move", str(trg["x"] + int(trg["width"] * 0.75)),
                    str(trg["y"] + trg["height"] // 2),
           "button", "0x111", "1",
           "move", "20", "0",
           "button", "0x111", "0")
    time.sleep(0.5)

    out = []
    leaves(layout().get("layout", {}), out)
    geoc = {e["view-id"]: e["geometry"] for e in out}
    deltas = {v: geoc[v]["width"] - geob[v]["width"] for v in geob}
    # the dragged column grows by ~20px; every view on the other side of
    # the boundary gives up the same share (fraction conservation)
    grew = deltas.get(target, 0)
    others = [d for v, d in deltas.items() if v != target]
    if abs(grew - 20) <= 3 and len(others) >= 1 and \
            all(abs(d + 20) <= 3 for d in others):
        ok(f"D-3 resize moved the boundary +20px (column {target}: "
           f"{geob[target]['width']} -> {geoc[target]['width']}, "
           f"others {others})")
    else:
        fail(f"D-3 expected boundary +20px, got {deltas}")

    # ---- D-4: abusive drag clamps at MIN_SANE ~= 100px ---------------------
    trg4 = geoc[target]
    vinput("move", "-5000", "-5000",
           "move", str(trg4["x"] + int(trg4["width"] * 0.25)),
                    str(trg4["y"] + trg4["height"] // 2),
           "button", "0x111", "1",
           "move", "-5000", "0",
           "button", "0x111", "0")
    time.sleep(0.5)

    out = []
    leaves(layout().get("layout", {}), out)
    geod = {e["view-id"]: e["geometry"] for e in out}
    wd = geod[target]["width"]
    if 60 <= wd <= 150:
        ok(f"D-4 abusive resize clamped at MIN_SANE ({geoc[target]['width']} -> {wd})")
    else:
        fail(f"D-4 expected clamp ~100px, got {wd}")

    print("PASS" if not FAIL[0] else "FAIL")
    sys.exit(1 if FAIL[0] else 0)


if __name__ == "__main__":
    main()