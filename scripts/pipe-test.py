#!/usr/bin/env python3
"""M6 pipemenu assertions over the wayfire IPC (headless).

Covers: lazy spawn on open, items from script output, re-spawn after the
tree closes, empty-output '<' validation, 4 s timeout, 1 MiB cap.
Fixture: tests/root-menu.xml defines pipe-demo (cat $PIPE_FILE),
pipe-slow (sleep 30) and pipe-huge (2 MB of 'x').
"""
import json
import os
import socket
import struct
import sys
import time

SOCK = os.environ["SOCK"]
LOG = os.environ["LOG"]
PIPE_FILE = os.environ["PIPE_FILE"]
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


def menu(op, **extra):
    d = {"op": op}
    d.update(extra)
    return call("ob-menu/debug", d)


def state():
    return menu("get-state").get("state", {})


def poll(fn, timeout=5.0, step=0.05):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if fn():
            return True
        time.sleep(step)
    return False


def wayfire_alive():
    try:
        call("ob-menu/debug", {"op": "get-state"})
        return True
    except Exception:
        return False


def log_has(pat):
    try:
        with open(LOG) as f:
            return pat in f.read()
    except OSError:
        return False


def write_pipe(items):
    body = "".join(
        f'  <item label="{t}"><action name="Execute" command="true" /></item>\n'
        for t in items)
    with open(PIPE_FILE, "w") as f:
        f.write(f'<menu id="pipe-demo" label="Pipe">\n{body}</menu>\n')


def open_and_select(index, downs):
    """open root-menu and walk the selection to the given item index."""
    menu("open-root", x=100, y=100)
    for _ in range(downs):
        menu("nav:down")
    return state()


# --- A: normal pipemenu output -------------------------------------------
write_pipe(["Pipe_item_a", "Pipe_item_b"])
st = open_and_select(5, 5)
check("root-menu opened (10 items incl. pipemenus)",
      st.get("items") == 10, str(st.get("items")))
check("selected Pipe (item 5)", st.get("selected") == 5,
      str(st.get("selected")))

menu("nav:right")  # lazy-spawn pipe-demo
ok = poll(lambda: state().get("levels") == 2, 3.0)
lv = state().get("stack", [])
check("pipemenu opened as level 2", ok and len(lv) == 2, str(lv))
check("pipemenu items from script output",
      bool(lv) and lv[1].get("items") == 2, str(lv[1] if lv else None))

# --- re-spawn after close (content not cached across trees) ----------------
write_pipe(["Pipe_item_a", "Pipe_item_b", "Pipe_item_c"])
menu("nav:escape")
menu("nav:escape")
st = state()
check("tree closed", not st.get("open"), str(st))
open_and_select(5, 5)
menu("nav:right")
ok = poll(lambda: state().get("levels") == 2, 3.0)
lv = state().get("stack", [])
check("pipemenu re-spawned with new content (3 items)",
      ok and bool(lv) and lv[1].get("items") == 3,
      str(lv[1] if lv else None))
menu("nav:escape")
menu("nav:escape")

# --- B: empty output → '<' validation abort --------------------------------
os.remove(PIPE_FILE)
open_and_select(5, 5)
menu("nav:right")
ok = poll(lambda: state().get("waiting") is False, 3.0)
st = state()
check("empty pipemenu aborted, no level added",
      ok and st.get("levels") == 1 and st.get("open"), str(st))
check("log: '<' validation message", log_has("does not start with '<'"), "")
check("wayfire survived empty pipemenu", wayfire_alive(), "")
menu("nav:escape")

# --- C: hanging script → 4 s timeout ----------------------------------------
open_and_select(8, 7)   # ...→ 8 = Pipe_slow
menu("nav:right")
time.sleep(0.4)
st = state()
check("waiting while hanging script runs", st.get("waiting") is True, str(st))
sel_before = st.get("selected")
menu("nav:down")  # must be ignored while waiting
st2 = state()
check("selection blocked while waiting",
      st2.get("selected") == sel_before and st2.get("levels") == 1, str(st2))
t0 = time.time()
ok = poll(lambda: state().get("waiting") is False, 6.0)
elapsed = time.time() - t0
st = state()
check("timeout released waiting (~4s)",
      ok and (3.0 < elapsed < 5.5) and st.get("levels") == 1,
      f"elapsed={elapsed:.1f}s {st}")
check("log: timeout message", log_has("timeout reached"), "")
check("wayfire survived pipemenu timeout", wayfire_alive(), "")
menu("nav:escape")
check("tree closed after timeout", not state().get("open"), str(state()))

# --- D: 1 MiB cap ------------------------------------------------------------
open_and_select(9, 8)   # ...→ 9 = Pipe_huge
menu("nav:right")
ok = poll(lambda: state().get("waiting") is False, 6.0)
st = state()
check("oversized output aborted, no level added",
      ok and st.get("levels") == 1 and st.get("open"), str(st))
check("log: size-cap message", log_has("too big"), "")
check("wayfire survived oversized pipemenu", wayfire_alive(), "")
menu("nav:escape")
check("tree closed after size cap", not state().get("open"), str(state()))

print()
if failures:
    print(f"M6 TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("M6 TEST OK")
