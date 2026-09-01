#!/usr/bin/env python3
"""M5 action-whitelist + dynamic-menu assertions over the wayfire IPC.

Observation endpoint: ipc-rules (window-rules/list-views, view-info,
get-focused-view). Requires a kitty autostarted inside the test session.
"""
import json
import os
import socket
import struct
import sys
import time

SOCK = os.environ["SOCK"]
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


def views():
    r = call("window-rules/list-views")
    if isinstance(r, list):
        return r
    return r.get("views", [])


def find_kitty(vids=None):
    out = []
    for v in views():
        if v.get("app-id") == "kitty" and v.get("role") == "toplevel":
            out.append(v)
    return out


def kitty_info():
    """view-info of the first kitty toplevel."""
    ks = find_kitty()
    if not ks:
        return None
    r = call("window-rules/view-info", {"id": ks[0]["id"]})
    if isinstance(r, dict) and "app-id" in r:
        return r
    return r.get("info", {})


# --- wait for the autostarted kitty to map -----------------------------
kitty = None
for _ in range(100):
    kitty = kitty_info()
    if kitty and kitty.get("mapped"):
        break
    time.sleep(0.1)
check("kitty autostarted & mapped", bool(kitty and kitty.get("mapped")),
      str(bool(kitty)))

# --- root menu is clamped into the workarea (no off-screen menus) --------
r = menu("open-root", x=1279, y=719)
st = r.get("state", {})
box = st.get("root_box")
check("menu anchored at bottom-right is clamped into view",
      box and box[0] + box[2] <= 1280 and box[1] + box[3] <= 720, str(box))
menu("close")

# --- dynamic menus ------------------------------------------------------
r = menu("open-root", menu="client-list-combined-menu", x=100, y=100)
st = r.get("state", {})
check("client-list shows 1 view", st.get("items") == 1, str(st))
check("client-list opened", st.get("open") is True, str(st))
menu("close")

r = menu("open-root", menu="client-send-to-menu", x=100, y=100)
st = r.get("state", {})
check("client-send-to has 2 workspaces (vwidth=2)",
      st.get("items") == 2, str(st))
menu("close")

# --- Iconify via client-menu -------------------------------------------
r = menu("open-client")
st = r.get("state", {})
check("client-menu opened", st.get("open") is True and
      st.get("menu_id") == "client-menu", str(st))
menu("nav:down")   # select first selectable = Minimize
r = menu("nav:enter")
info = kitty_info()
check("Iconify minimized the view", info and info.get("minimized") is True,
      str(info and info.get("minimized")))

# restore for next tests: Iconify again (toggle behavior of minimize_request
# is not guaranteed for client-menu; open the view via client-list Activate)
r = menu("open-root", menu="client-list-combined-menu", x=100, y=100)
st = r.get("state", {})
check("client-list still lists minimized view", st.get("items") == 1, str(st))
menu("nav:down")   # Activate it
menu("nav:enter")
info = kitty_info()
check("Activate restored the view", info and info.get("minimized") is False,
      str(info and info.get("minimized")))

# --- ToggleMaximize ------------------------------------------------------
menu("open-client")
menu("nav:down")   # Minimize
menu("nav:down")   # Maximize
menu("nav:enter")
info = kitty_info()
check("ToggleMaximize tiled the view",
      info and info.get("tiled-edges") == 15, str(info and info.get("tiled-edges")))

menu("open-client")
menu("nav:down")
menu("nav:down")   # Maximize again -> untile
menu("nav:enter")
info = kitty_info()
check("ToggleMaximize untiled the view",
      info and info.get("tiled-edges") == 0, str(info and info.get("tiled-edges")))

# --- SendToDesktop -------------------------------------------------------
# wayfire's workspace coords are viewport-local (set_workspace translates
# all views), so geometry can't prove the move. Instead: after SendToDesktop
# (follow=yes) the view must be VISIBLE on the new current workspace (ws2).
# We detect visibility via a grim screenshot (empty desktop = 1-2 colors).
def screenshot_colors(display):
    import subprocess
    out = subprocess.run(["grim", "-t", "ppm", "-"], env={
        **os.environ, "WAYLAND_DISPLAY": display},
        capture_output=True, timeout=10)
    data = out.stdout
    # ppm header: P6\n<w> <h>\n255\n
    body = data.split(b"255\n", 1)[1]
    return len(set(body[i:i + 3] for i in range(0, len(body) - 2, 3)))


WF_DISPLAY = os.environ.get("WF_DISPLAY", "")
geo0 = kitty_info().get("geometry", {})
menu("open-client")
menu("nav:down")  # Minimize
menu("nav:down")  # Maximize
menu("nav:down")  # Close — hovered but NOT activated
menu("close")
r = menu("open-root", menu="client-send-to-menu", x=100, y=100)
menu("nav:down")   # WS 1 (index 0)
menu("nav:down")   # WS 2 (index 1)
menu("nav:enter")  # SendToDesktop to=2 (follow → switches to ws2)
time.sleep(0.5)
geo1 = kitty_info().get("geometry", {})
n_colors = screenshot_colors(WF_DISPLAY) if WF_DISPLAY else -1
check("SendToDesktop: view visible on ws2 after follow",
      n_colors > 8, f"colors={n_colors} geo {geo0} -> {geo1}")

# --- Close via client-menu ----------------------------------------------
menu("open-client")
menu("nav:down")  # Minimize
menu("nav:down")  # Maximize
menu("nav:down")  # Close
menu("nav:enter")
time.sleep(0.5)
check("Close destroyed the view", len(find_kitty()) == 0, "")

# --- Execute (root-menu Terminal) spawns + focus ------------------------
r = menu("open-root", x=100, y=100)
menu("nav:down")  # first selectable = Terminal
menu("nav:enter")
new_kitty = None
for _ in range(60):
    ks = find_kitty()
    if len(ks) >= 1:
        new_kitty = ks
        break
    time.sleep(0.1)
check("Execute spawned a kitty", new_kitty is not None, "")
_fv = call("window-rules/get-focused-view")
focused = _fv.get("info", _fv) if isinstance(_fv, dict) else {}
check("Execute result has keyboard focus (menu closed first)",
      focused.get("app-id") == "kitty", str(focused.get("app-id")))

menu("close")

print()
if failures:
    print(f"M5 TEST FAIL: {len(failures)} failures: {failures}")
    sys.exit(1)
print("M5 TEST OK")
