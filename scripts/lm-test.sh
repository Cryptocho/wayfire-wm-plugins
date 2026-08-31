#!/bin/bash
# layout-manager automated tests (headless). Started with 3 autostart kitties;
# assertions run over IPC: layout-manager/* methods + ipc-rules observation.
# Process safety: wayfire PID recorded in WPID, cleaned up via trap only.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/lm-test.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-lm-test.sock"
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK

echo "=== starting headless wayfire (layout test config) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/layout-manager" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-layout.ini" -d >"$LOG" 2>&1 &
WPID=$!
echo "wayfire PID=$WPID"

for _ in $(seq 1 80); do
    [ -S "$SOCK" ] && break
    kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
    sleep 0.1
done
[ -S "$SOCK" ] || { echo "FAIL: socket never appeared"; tail -30 "$LOG"; exit 1; }

python3 "$ROOT/scripts/lm-test.py"
STATUS=$?
if [ $STATUS -ne 0 ]; then
    echo "--- wayfire log tail ---"
    grep -E "EE|Fatal|SEGV" "$LOG" | tail -10
fi
exit $STATUS
