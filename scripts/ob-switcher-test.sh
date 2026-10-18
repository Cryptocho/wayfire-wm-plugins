#!/bin/bash
# ob-switcher headless test: real vinput key sequences (super+tab forward,
# super+shift+tab backward, Esc cancel) + IPC assertions via ipc-rules.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obswitcher-test.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-ob-switcher-test.sock"
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK

echo "=== starting headless wayfire (ob-theme + ob-switcher, 2 kitties) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/ob-theme:$ROOT/build/ob-switcher:$ROOT/build/ob-decor:$ROOT/build/ob-menu" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-switcher.ini" -d >"$LOG" 2>&1 &
WPID=$!
echo "wayfire PID=$WPID"

for _ in $(seq 1 80); do
    [ -S "$SOCK" ] && break
    kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
    sleep 0.1
done
[ -S "$SOCK" ] || { echo "FAIL: socket never appeared"; tail -30 "$LOG"; exit 1; }

# nested/headless wayland display name for vinput
WF_DISPLAY=""
for _ in $(seq 1 50); do
    WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
    [ -n "$WF_DISPLAY" ] && break
    sleep 0.1
done
[ -n "$WF_DISPLAY" ] || { echo "FAIL: no wayland display"; tail -30 "$LOG"; exit 1; }
export WF_DISPLAY
echo "input display: $WF_DISPLAY"

SOCK="$SOCK" WF_DISPLAY="$WF_DISPLAY" VINPUT="$ROOT/build/vinput" \
    python3 "$ROOT/scripts/ob-switcher-test.py"
STATUS=$?
[ "$STATUS" = 0 ] && echo "ob-switcher test OK"
exit $STATUS
