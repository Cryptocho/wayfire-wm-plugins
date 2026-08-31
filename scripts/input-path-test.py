#!/usr/bin/env python3
"""Real-input-path menu assertions: inject events through the wlr virtual
keyboard/pointer protocols (tools/vinput) so the grab node → interaction
routing is exercised, unlike the IPC-driven tests.

Pointer assertions (hover / click / outside-click) are hard requirements.
Keyboard assertions are best-effort: vinput's synthetic key events reach
wf::keyboard_t::handle_keyboard_key but do not trigger the keybindings in
headless runs (virtual-keyboard modifier plumbing), so they are reported
as SKIP — the authoritative keyboard check is TEST.md on a real keyboard.
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VINPUT = os.path.join(ROOT, "build", "vinput")
WF_DISPLAY = os.environ["WF_DISPLAY"]
SOCK = os.environ["SOCK"]
failures = []
skipped = []

KEY_ESC = 1
BTN_LEFT = 0x110


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


def skip(name, why):
    print(f"[SKIP] {name} ({why})")
    skipped.append(name)


def menu(op, **extra):
    d = {"op": op}
    d.update(extra)
    return call("ob-menu/debug", d)


def state():
    return menu("get-state").get("state") or {}


def root_box():
    st = state()
    return st.get("root_box", [])


def poll(fn, timeout=3.0, step=0.05):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if fn():
            return True
        time.sleep(step)
    return False


def vinput(*cmds):
    r = subprocess.run([VINPUT, WF_DISPLAY] + list(cmds),
        capture_output=True, text=True, timeout=10)
    if r.returncode != 0:
        print(f"vinput failed: {r.stderr.strip()}")
    return r.returncode == 0


def kitty_views():
    r = call("window-rules/list-views")
    views = r if isinstance(r, list) else r.get("views", [])
    return [v for v in views
        if v.get("app-id") == "kitty" and v.get("role") == "toplevel"]


def hover_until(select, max_steps=25):
    """walk the cursor down 4px at a time until `select` (state, idx) holds."""
    trail = []
    for step in range(max_steps):
        st = state()
        trail.append(f"{step}:sel={st.get('selected')}lv={st.get('levels')}")
        if st.get("selected", -1) == select:
            return True
        vinput("move", "0", "4")
    final = state()
    print("hover trail:", trail, "final:", final)
    return final.get("selected", -1) == select


def open_root_at_cursor():
    """top-left the cursor (clamped), open the menu anchored on it.
    Since anchor == cursor, all subsequent offsets are known relative
    to the menu origin regardless of where the clamp lands."""
    vinput("move", "-3000", "-3000")
    menu("open-root")   # no x/y → anchor = current cursor position
    return root_box()


def enter_first_row():
    """cursor is at the menu anchor; step into the first row (ih=24,b=1),
    on the right half of the row so later down-walks stay clear of the
    submenu windows, which open on the LEFT of their parent item."""
    vinput("move", "80", "12")


poll(lambda: len(kitty_views()) > 0, 15.0)
check("kitty autostarted", len(kitty_views()) > 0, str(len(kitty_views())))

# --- best-effort keyboard checks (see module docstring) ---------------------
# cursor lands top-left first so the anchor is deterministic if it works
vinput("move", "-3000", "-3000")
vinput("key", "125", "1", "key", "59", "1", "key", "59", "0",
    "key", "125", "0")
st = state()
if st.get("open"):
    check("real <super>F1 opened root-menu", True)
    vinput("key", "108", "1", "key", "108", "0")
    st = state()
    check("real keyboard DOWN selects Terminal", st.get("selected") == 0,
        str(st.get("selected")))
    vinput("key", "108", "1", "key", "108", "0")
    st = state()
    check("real keyboard DOWN skips separator", st.get("selected") == 2,
        str(st.get("selected")))
    vinput("key", "106", "1", "key", "106", "0")  # RIGHT
    ok = poll(lambda: state().get("levels") == 2)
    check("real keyboard RIGHT opens submenu", ok, str(state()))
    vinput("key", "1", "1", "key", "1", "0")  # Esc: close the submenu level
    ok = poll(lambda: state().get("levels") == 1)
    vinput("key", "1", "1", "key", "1", "0")  # Esc again: close the tree
    check("real Esc closes level then tree",
          ok and not state().get("open"), str(state()))
else:
    skip("keyboard open/nav/escape via vinput",
        "synthetic key events do not trigger keybindings headless")

# --- real pointer hover selects (menu anchored on the cursor) ----------------
box = open_root_at_cursor()
check("menu anchored at cursor", bool(box), str(box))
enter_first_row()
hover_ok = hover_until(0)
check("real pointer hover selects first item", hover_ok, str(state()))

# --- real pointer click activates (Terminal → new kitty) ----------------------
time.sleep(0.5)  # past the release-guard (250ms)
before = len(kitty_views())
vinput("click", hex(BTN_LEFT))
poll(lambda: len(kitty_views()) > before, 5.0)
st = state()
check("real pointer click ran Execute (menu closed first)",
      (len(kitty_views()) > before) and not st.get("open"),
      f"kitty {before}->{len(kitty_views())} {st}")

# --- real pointer hover opens a submenu ----------------------------------------
# Walk down to the Windows row: the client-list submenu must open by itself.
# (Deeper rows overlap the open submenu window, where deepest-level hover
# semantics apply — labwc behaves the same.)
box = open_root_at_cursor()
enter_first_row()
win_ok = hover_until(2)
st = state()
check("real pointer hover opens submenu (Windows → client-list)",
      win_ok and st.get("levels") == 2, str(st))
menu("close")

# --- outside click closes the tree ---------------------------------------------
time.sleep(0.5)
vinput("move", "600", "400")  # far from the menu
vinput("click", hex(BTN_LEFT))
check("outside click closed the tree", not state().get("open"), str(state()))

print()
if failures:
    print(f"INPUT-PATH TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print(f"INPUT-PATH TEST OK ({len(skipped)} skipped)")
