#!/usr/bin/env python3
"""Reproduce the M4 submenu-open crash over IPC.

Sequence mirrors 'attempting to open a submenu': open-root -> select the
'Apps' entry (3x down) -> nav:right (opens the submenu) -> deeper navigation.
Process liveness is checked by the wrapping shell after every call.
"""
import json
import os
import socket
import struct
import sys
import subprocess

SOCK = os.environ["SOCK"]


def alive():
    pid = os.environ["WPID"]
    try:
        subprocess.run(["kill", "-0", pid], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except subprocess.CalledProcessError:
        return False


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


steps = [
    ("open-root", {"op": "open-root", "x": 200, "y": 150}),
    ("state", {"op": "get-state"}),
    ("down x1", {"op": "nav:down"}),
    ("down x2", {"op": "nav:down"}),
    ("down x3 (Apps selected)", {"op": "nav:down"}),
    ("state (expect selected=3)", {"op": "get-state"}),
    ("nav:right -> OPEN SUBMENU", {"op": "nav:right"}),
    ("state (expect levels=2)", {"op": "get-state"}),
    ("down x1", {"op": "nav:down"}),
    ("state", {"op": "get-state"}),
    ("nav:right (apps-more level3)", {"op": "nav:right"}),
    ("state (expect levels=3)", {"op": "get-state"}),
    ("nav:left", {"op": "nav:left"}),
    ("nav:escape", {"op": "nav:escape"}),
    ("nav:escape (close all)", {"op": "nav:escape"}),
    ("state (expect closed)", {"op": "get-state"}),
]

for name, data in steps:
    try:
        r = call("ob-menu/debug", data)
        print(f"[ok]    {name:35s} -> {r}")
    except Exception as e:
        print(f"[ERR]   {name:35s} -> {type(e).__name__}: {e}")
        if not alive():
            print(f"*** wayfire (PID {os.environ['WPID']}) is DEAD — crash reproduced at: {name} ***")
            sys.exit(2)
        # IPC responded not; but process alive — keep going
    if not alive():
        print(f"*** wayfire (PID {os.environ['WPID']}) is DEAD — crash reproduced at: {name} ***")
        sys.exit(2)

print("ALL STEPS PASSED, wayfire alive")
