#!/bin/bash
# Safety verification for the ob-decor theme-singleton crash fix (P2 closeout):
# a view's decoration used to run ob_theme_finish on destroy, NULLing the
# global rc.theme; hovering a remaining view's titlebar button then crashed in
# draw_hover_overlay_on_button.
#
# Procedure (process-safe: every spawned PID is recorded, cleanup only
# touches recorded PIDs):
#   1. headless wayfire with ob-decor + ob-menu co-loaded   -> WPID
#   2. spawn two kitty windows                              -> KPID1 KPID2
#   3. SIGTERM KPID1 (kitty exits gracefully -> decoration destroyed)
#   4. hover + click the close button of kitty #2 (second destroy path)
#   5. the compositor must survive with no Fatal error in the log
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obmenu-decor-destroy.log
WPID=""; KPID1=""; KPID2=""
SOCK="$XDG_RUNTIME_DIR/wayfire-decor-destroy-test.sock"
cleanup() {
    # only recorded PIDs, never by name
    [ -n "$KPID2" ] && kill "$KPID2" 2>/dev/null
    [ -n "$KPID1" ] && kill "$KPID1" 2>/dev/null
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
echo "test display: $WF_DISPLAY"

# --- two test windows, PIDs recorded -------------------------------------
WAYLAND_DISPLAY="$WF_DISPLAY" kitty >/dev/null 2>&1 &
KPID1=$!
WAYLAND_DISPLAY="$WF_DISPLAY" kitty >/dev/null 2>&1 &
KPID2=$!
echo "kitty PIDs: $KPID1 $KPID2"
export KPID1 KPID2 ROOT

STATUS=0
python3 - <<'EOF'
import json, os, socket, struct, subprocess, time, sys

SOCK = os.environ["SOCK"]
VINPUT = os.path.join(os.environ["ROOT"], "build", "vinput")

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

def vinput(*args):
    subprocess.run([VINPUT, os.environ["WF_DISPLAY"], *args], check=True)

def kitties():
    r = call("window-rules/list-views")
    vs = r if isinstance(r, list) else r.get("views", [])
    return [v for v in vs
            if v.get("app-id") == "kitty" and v.get("role") == "toplevel"]

# wait for both windows
for _ in range(200):
    ks = kitties()
    if len(ks) >= 2:
        break
    time.sleep(0.1)
if len(ks) < 2:
    print(f"[FAIL] expected 2 kitties, got {len(ks)}"); sys.exit(1)
print("[ok] two kitty windows mapped")

# SIGTERM kitty #1 -> graceful exit -> its decoration is destroyed.
# (This is the fixed crash path: decor theme destructor must NOT finish the
# shared theme instance.)
os.kill(int(os.environ["KPID1"]), 15)
for _ in range(100):
    if len(kitties()) == 1:
        break
    time.sleep(0.1)
ks = kitties()
if len(ks) != 1:
    print(f"[FAIL] kitty #1 did not close cleanly, n={len(ks)}"); sys.exit(1)
print("[ok] kitty #1 closed -> first decoration destroyed")

# hover the remaining view's close button (hover overlay redraw path).
# view-info geometry is the frame-inclusive wbox; OB-Arc-Dark: border 2 +
# ring 2, titlebar 26, buttons 26x26 spacing 0, close is rightmost ->
# button occupies x [w-30, w-4], y [4, 30] in wbox-local coords.
g = ks[0]["geometry"]
tx, ty = g["x"] + g["width"] - 17, g["y"] + 17
vinput("move", "-5000", "-5000")
vinput("move", str(tx), str(ty))
time.sleep(0.4)
r = call("ob-menu/debug", {"op": "get-state"})
print("[ok] compositor alive after hover on remaining view's close button",
      bool(r))

# click the close button -> second decoration destroyed
vinput("click", "0x110")
for _ in range(100):
    if len(kitties()) == 0:
        break
    time.sleep(0.1)
if kitties():
    print("[FAIL] close button click did not close kitty #2"); sys.exit(1)
print("[ok] kitty #2 closed via titlebar -> second decoration destroyed")
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
    echo "decor-destroy safety test OK"
fi
exit $STATUS
