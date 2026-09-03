#!/usr/bin/env python3
"""ob-switcher assertions: real vinput key sequences + IPC observations.

Flow (every key sequence is ONE vinput call — a device disconnect is an
implicit release, so grab-lifetimes must not span calls):
  1. two kitties mapped (switcher-a / switcher-b)
  2. super+tab  -> focus moves to the other kitty, switcher closed
  3. super+shift+tab -> focus moves back
  4. super+tab then Esc -> focus unchanged (cancel), switcher closed
  5. ob-switcher/debug get-state sanity while active (via IPC activate)
"""
import json
import os
import socket
import struct
import subprocess
import time

SOCK = os.environ["SOCK"]
WF_DISPLAY = os.environ["WF_DISPLAY"]
VINPUT = os.environ["VINPUT"]
failures = []

KEY_LEFTALT = 56
KEY_LEFTSHIFT = 42
KEY_TAB = 15
KEY_ESC = 1


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


def run_vinput(key_cmds):
    """One vinput process per key sequence: a device disconnect is an
    implicit release, so a grab lifetime must not span calls.
    key_cmds: list of "<evdev> <0|1>" strings, applied in order."""
    args = [VINPUT, WF_DISPLAY]
    for c in key_cmds:
        evdev, state = c.split()
        args += ["key", evdev, state]
    subprocess.run(args, check=True)


def focused_title():
    r = call("window-rules/get-focused-view")
    info = r.get("info", r) if isinstance(r, dict) else {}
    return (info or {}).get("title", "")


def wait_title(expected, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        t = focused_title()
        if t == expected:
            return True
        time.sleep(0.1)
    return False


# 1. two kitties mapped
deadline = time.time() + 10
titles = []
while time.time() < deadline:
    r = call("window-rules/list-views")
    titles = sorted(v.get("title", "") for v in r or [])
    if "switcher-a" in titles and "switcher-b" in titles:
        break
    time.sleep(0.2)
check("two kitties mapped", "switcher-a" in titles and "switcher-b" in titles,
      str(titles))

# 2. forward: super+tab single press-release; focus lands on the other kitty
start = focused_title()
other = "switcher-b" if start == "switcher-a" else "switcher-a"
subprocess.run([VINPUT, WF_DISPLAY, "key", str(KEY_LEFTALT), "1", "key",
    "15", "1", "key", "15", "0", "key", str(KEY_LEFTALT), "0"], check=True)
check(f"forward commit: focus {start} -> {other}", wait_title(other),
      f"got {focused_title()!r}")

st = call("ob-switcher/debug", {"op": "get-state"})
st = st.get("state") or {}
check("switcher inactive after commit", st.get("active") is False, str(st))
check("osd destroyed after commit", st.get("osd_visible") is False, str(st))

# 3. backward: super+shift+tab moves focus back
subprocess.run([VINPUT, WF_DISPLAY, "key", str(KEY_LEFTALT), "1", "key",
    str(KEY_LEFTSHIFT), "1", "key", "15", "1", "key", "15", "0", "key",
    str(KEY_LEFTSHIFT), "0", "key", str(KEY_LEFTALT), "0"], check=True)
check("backward commit moves focus back", wait_title(start),
      f"got {focused_title()!r}")

# 4. Esc cancel: focus must not change
subprocess.run([VINPUT, WF_DISPLAY, "key", str(KEY_LEFTALT), "1", "key",
    "15", "1", "key", "15", "0", "key", "1", "1", "key", "1", "0", "key",
    str(KEY_LEFTALT), "0"], check=True)
check("Esc cancel keeps focus", focused_title() == start,
      f"got {focused_title()!r}")
st = (call("ob-switcher/debug", {"op": "get-state"}).get("state") or {})
check("switcher inactive after cancel", st.get("active") is False, str(st))

# 5. IPC activate opens (no modifiers held -> stays active), cancel closes
r = call("ob-switcher/debug", {"op": "activate"})
check("IPC activate opens switcher",
      (r.get("activated") is True) and (r.get("state") or {}).get("active"),
      str(r))
st = r.get("state") or {}
check("state reports 2 views + OSD", st.get("nr_views") == 2 and
      st.get("osd_visible") is True, str(st))

call("ob-switcher/debug", {"op": "cancel"})
time.sleep(0.3)
st = (call("ob-switcher/debug").get("state") or {})
check("IPC cancel closes switcher", st.get("active") is False, str(st))
check("IPC cancel keeps focus", focused_title() == start,
      f"got {focused_title()!r}")

if failures:
    print(f"ob-switcher-test: {len(failures)} FAIL")
    raise SystemExit(1)
print("ob-switcher-test: all assertions passed")
