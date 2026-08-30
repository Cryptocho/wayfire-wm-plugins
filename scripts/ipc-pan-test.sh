#!/bin/bash
# Automated pan-state test over the wayfire IPC socket (headless, no input
# devices needed). Verifies get-state, step +/-1, end-of-rail clamping and
# rejection of bad arguments.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG="$(mktemp)"
WPID=""
# _WAYFIRE_SOCKET forces the ipc plugin to listen on a known path
# (setenv'd WAYFIRE_SOCKET is invisible to /proc/PID/environ).
SOCK="$XDG_RUNTIME_DIR/wayfire-wm-plugins-test.sock"
cleanup() { [ -n "$WPID" ] && kill "$WPID" 2>/dev/null; rm -f "$LOG" "$SOCK"; }
trap cleanup EXIT
rm -f "$SOCK"

_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/proto-pan" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire.ini" -d >"$LOG" 2>&1 &
WPID=$!

for _ in $(seq 1 100); do
    grep -q "proto-pan init" "$LOG" && break
    sleep 0.1
done

WF_SOCK=""
for _ in $(seq 1 50); do
    [ -S "$SOCK" ] && WF_SOCK="$SOCK" && break
    sleep 0.1
done
[ -n "$WF_SOCK" ] || { echo "IPC TEST FAIL: socket $SOCK never appeared"; exit 1; }

export WF_SOCK
python3 "$ROOT/scripts/ipc-pan-test.py"
STATUS=$?

echo "--- proto-pan log lines ---"
grep -E "proto-pan (step|adopted|rail)" "$LOG"
exit $STATUS
