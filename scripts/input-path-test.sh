#!/bin/bash
# Real-input-path menu tests: wlr virtual keyboard + pointer injection
# (tools/vinput) — exercises the grab routing that IPC-driven tests skip.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obmenu-inputpath.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-wm-plugins-test.sock"
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK LOG

[ -x "$ROOT/build/vinput" ] || { echo "vinput missing (run ninja)"; exit 1; }

echo "=== starting headless wayfire (input-path test) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/ob-menu" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-menu.ini" -d >"$LOG" 2>&1 &
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
echo "screenshot display: $WF_DISPLAY"

python3 "$ROOT/scripts/input-path-test.py"
STATUS=$?
if [ $STATUS -ne 0 ]; then
    echo "--- wayfire log tail ---"
    grep -E "EE|Fatal|SEGV" "$LOG" | tail -10
fi
exit $STATUS
