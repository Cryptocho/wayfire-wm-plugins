#!/bin/bash
# Regression: ob-decor + ob-menu coexistence. MenuState used to init a
# SECOND libobtheme instance which repointed the global rc.theme; the
# first hover over a titlebar button then crashed in
# draw_hover_overlay_on_button (rc.theme->window_button_hover_bg_color).
# This loads both plugins, initializes the menu system, then hovers a
# titlebar button — the compositor must survive.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obmenu-cologoad.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-wm-plugins-test.sock"
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK

echo "=== starting headless wayfire (ob-decor + ob-menu co-load) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/ob-decor:$ROOT/build/ob-menu" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-menu-interactive.ini" -d >"$LOG" 2>&1 &
WPID=$!
echo "wayfire PID=$WPID"

for _ in $(seq 1 80); do
    [ -S "$SOCK" ] && break
    kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
    sleep 0.1
done
[ -S "$SOCK" ] || { echo "FAIL: socket never appeared"; tail -30 "$LOG"; exit 1; }

WF_DISPLAY=""
for _ in $(seq 1 50); do
    WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
    [ -n "$WF_DISPLAY" ] && break
    sleep 0.1
done
export WF_DISPLAY

STATUS=0
# the interactive ini has no autostart section; spawn the test view directly
WAYLAND_DISPLAY="$WF_DISPLAY" kitty >/dev/null 2>&1 &
KPID=$!
echo "kitty PID=$KPID"
kill_kitty() { [ -n "$KPID" ] && kill "$KPID" 2>/dev/null; }
trap 'kill_kitty; cleanup' EXIT

python3 - <<'EOF'
import json, os, socket, struct, subprocess, time, sys

SOCK = os.environ["SOCK"]
VINPUT = "build/vinput"
WF_DISPLAY = os.environ["WF_DISPLAY"]

def call(method, data=None):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(5)
    s.connect(SOCK)
    p = json.dumps({"method": method, "data": data or {}}).encode()
    s.sendall(struct.pack("<I", len(p)) + p)
    h = s.recv(4); (l,) = struct.unpack("<I", h); b = b""
    while len(b) < l:
        b += s.recv(l - len(b))
    s.close()
    return json.loads(b)

def kitty():
    r = call("window-rules/list-views")
    vs = r if isinstance(r, list) else r.get("views", [])
    ks = [v for v in vs if v.get("app-id") == "kitty"]
    return ks[0] if ks else None

ok = True
# wait for autostart kitty
info = None
for _ in range(200):
    info = kitty()
    if info and info.get("mapped"):
        break
    time.sleep(0.1)
if not (info and info.get("mapped")):
    print("[FAIL] kitty did not map"); sys.exit(1)
print("[ok] kitty mapped")

g = info["geometry"]
# hover the close button (right end of the titlebar)
tx = g["x"] + g["width"] - 24
ty = g["y"] + 2
subprocess.run([VINPUT, WF_DISPLAY, "move", "-5000", "-5000"])
subprocess.run([VINPUT, WF_DISPLAY, "move", str(tx), str(ty)])
time.sleep(0.3)
# initialize the menu system (second consumer of the theme) and repeat
call("ob-menu/debug", {"op": "open-root", "menu": "client-menu"})
time.sleep(0.2)
call("ob-menu/debug", {"op": "close"})
# hover buttons again — crash happened on the first hover AFTER menu init
subprocess.run([VINPUT, WF_DISPLAY, "move", "-40", "0"])
time.sleep(0.3)
subprocess.run([VINPUT, WF_DISPLAY, "move", "0", "2"])
time.sleep(0.3)
r = call("ob-menu/debug", {"op": "get-state"})
print("[ok] compositor alive after button hover with menu initialized:", bool(r))

# menu theme follows ob-decor's themerc (regression: menu used to show
# built-in defaults when its init raced ahead of the decor theme init)
call("ob-menu/debug", {"op": "open-root", "menu": "root-menu", "x": 300, "y": 300})
time.sleep(0.5)
st = call("ob-menu/debug", {"op": "get-state"}).get("state")
box = st.get("root_box")
out = subprocess.run(["grim", "-t", "ppm", "-"],
    env={**os.environ, "WAYLAND_DISPLAY": WF_DISPLAY}, capture_output=True).stdout
hdr = out.index(b"255\n") + 4
W = 1280
px = out[hdr:]
x0, y0, bw, bh = box
tot = n = 0
for yy in range(y0 + 2, min(y0 + bh - 2, 720)):
    row = px[(yy * W + x0 + 2) * 3:(yy * W + min(x0 + bw - 2, W)) * 3]
    tot += sum(row); n += len(row)
avg = tot / n
print(f"[{'ok' if avg < 120 else 'FAIL'}] menu uses decor theme (dark), avg brightness {avg:.0f}")
if avg >= 120:
    sys.exit(1)
call("ob-menu/debug", {"op": "close"})
EOF
STATUS=$?

sleep 0.5
if ! kill -0 "$WPID" 2>/dev/null; then
    echo "FAIL: wayfire crashed"
    grep -E "Fatal|SEGV" "$LOG" | head -3
    STATUS=1
elif grep -q "Fatal error" "$LOG"; then
    echo "FAIL: fatal in log"
    grep -A6 "Fatal error" "$LOG" | head -8
    STATUS=1
else
    echo "co-load hover test OK"
fi
kill "$WPID" 2>/dev/null; wait "$WPID" 2>/dev/null
exit $STATUS
