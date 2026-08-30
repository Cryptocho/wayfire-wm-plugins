#!/usr/bin/env python3
"""Drive proto-pan via the wayfire IPC socket and assert pan-state transitions.

Framing (wayfire plugins/ipc/ipc.cpp): 4-byte LE uint32 length + JSON body,
both directions. Requires env WF_SOCK = path to the IPC unix socket.
"""
import json
import os
import socket
import struct
import sys


def call(method, data=None):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(os.environ["WF_SOCK"])
    payload = json.dumps({"method": method, "data": data or {}}).encode()
    s.sendall(struct.pack("<I", len(payload)) + payload)

    header = b""
    while len(header) < 4:
        chunk = s.recv(4 - len(header))
        if not chunk:
            raise RuntimeError("connection closed while reading header")
        header += chunk
    (length,) = struct.unpack("<I", header)

    body = b""
    while len(body) < length:
        chunk = s.recv(length - len(body))
        if not chunk:
            raise RuntimeError("connection closed while reading body")
        body += chunk
    s.close()
    return json.loads(body)


failures = []


def check(name, cond, detail=""):
    print(f"[{'ok' if cond else 'FAIL'}] {name} {detail}")
    if not cond:
        failures.append(name)


r = call("proto-pan/get-state")
check("initial state tile=0 offset=0", r.get("tile") == 0 and r.get("offset") == 0, r)

r = call("proto-pan/step", {"dir": 1})
W = -r.get("offset", 0)
check("step right -> tile 1", r.get("tile") == 1 and W > 0, f"{r} (W={W})")

r = call("proto-pan/step", {"dir": 1})
check("step right -> tile 2", r.get("tile") == 2 and r.get("offset") == -2 * W, r)

r = call("proto-pan/step", {"dir": 1})
check("clamped at last tile", r.get("tile") == 2 and r.get("offset") == -2 * W, r)

r = call("proto-pan/step", {"dir": -1})
check("step left -> tile 1", r.get("tile") == 1 and r.get("offset") == -W, r)

call("proto-pan/step", {"dir": -1})
r = call("proto-pan/step", {"dir": -1})
check("clamped at first tile", r.get("tile") == 0 and r.get("offset") == 0, r)

r = call("proto-pan/step", {"dir": 5})
check("bad dir rejected", "error" in r, r)

r = call("proto-pan/step", {})
check("missing dir rejected", "error" in r, r)

# ---- drag simulation (same code path as real swipe gestures) ----
# Nearest-tile snap on end; natural feel: dx>0 = content follows fingers right.

r = call("proto-pan/drag", {"op": "dx", "dx": 100})
check("dx before begin rejected", "error" in r, r)

r = call("proto-pan/drag", {"op": "begin"})
check("drag begin", r.get("swiping") is True and r.get("drag_px") == 0, r)

r = call("proto-pan/drag", {"op": "begin"})
check("double begin rejected", "error" in r, r)

r = call("proto-pan/drag", {"op": "dx", "dx": -600})
check("drag -600 -> offset -600", r.get("drag_px") == -600 and r.get("offset") == -600, r)

r = call("proto-pan/drag", {"op": "end"})
check("snap back to tile 0 (<half)", r.get("tile") == 0 and r.get("offset") == 0 and
    r.get("swiping") is False, r)

call("proto-pan/drag", {"op": "begin"})
r = call("proto-pan/drag", {"op": "dx", "dx": -700})
r = call("proto-pan/drag", {"op": "end"})
check("snap to tile 1 (>half)", r.get("tile") == 1 and r.get("offset") == -W, r)

call("proto-pan/drag", {"op": "begin"})
r = call("proto-pan/drag", {"op": "dx", "dx": 2000})
# content_x = clamp(2000 - 1*W, -(N-1)*W, 0) = 0 (clamped at rail start)
check("overdrag clamped at content_x<=0", r.get("offset") == 0, r)
r = call("proto-pan/drag", {"op": "end"})
check("big swipe back -> tile 0", r.get("tile") == 0 and r.get("offset") == 0, r)

call("proto-pan/drag", {"op": "begin"})
r = call("proto-pan/drag", {"op": "dx", "dx": -3000})
check("far drag clamped at rail end", r.get("offset") == -2 * W, r)
r = call("proto-pan/drag", {"op": "end"})
check("far drag -> last tile", r.get("tile") == 2 and r.get("offset") == -2 * W, r)

if failures:
    print(f"IPC TEST FAIL: {len(failures)} failure(s)")
    sys.exit(1)
print("IPC TEST OK: all pan-state assertions passed")
