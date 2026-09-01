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

# wayland socket of the test session (needed for spawning test windows)
WF_DISPLAY=""
for _ in $(seq 1 50); do
    WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
    [ -n "$WF_DISPLAY" ] && break
    sleep 0.1
done
export WF_DISPLAY REPO="$ROOT"

python3 "$ROOT/scripts/lm-test.py"
STATUS=$?

# --- phase 2: auto-float rules (separate config, second compositor) ------
if [ $STATUS -eq 0 ]; then
    kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
    echo "=== phase 2: auto-float rules ==="
    _WAYFIRE_SOCKET="$SOCK" \
    WAYFIRE_PLUGIN_PATH="$ROOT/build/layout-manager" \
    WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
    WLR_BACKENDS=headless \
    wayfire -c "$ROOT/test/wayfire-layout-float.ini" -d >"$LOG" 2>&1 &
    WPID=$!
    echo "wayfire PID=$WPID"
    for _ in $(seq 1 80); do
        [ -S "$SOCK" ] && break
        kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
        sleep 0.1
    done
    WF_DISPLAY=""
    for _ in $(seq 1 50); do
        WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
        [ -n "$WF_DISPLAY" ] && break
        sleep 0.1
    done
    export WF_DISPLAY REPO="$ROOT"
    python3 "$ROOT/scripts/lm-test-float.py"
    STATUS=$?
fi

# --- phase 3: integration with ob-decor + ob-menu -------------------------
if [ $STATUS -eq 0 ]; then
    kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
    echo "=== phase 3: integration (ob-decor + ob-menu co-load) ==="
    _WAYFIRE_SOCKET="$SOCK"     WAYFIRE_PLUGIN_PATH="$ROOT/build/layout-manager:$ROOT/build/ob-decor:$ROOT/build/ob-menu"     WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata"     WLR_BACKENDS=headless     wayfire -c "$ROOT/test/wayfire-layout-interactive.ini" -d >"$LOG" 2>&1 &
    WPID=$!
    echo "wayfire PID=$WPID"
    for _ in $(seq 1 80); do
        [ -S "$SOCK" ] && break
        kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
        sleep 0.1
    done
    WF_DISPLAY=""
    for _ in $(seq 1 50); do
        WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
        [ -n "$WF_DISPLAY" ] && break
        sleep 0.1
    done
    export WF_DISPLAY
    python3 "$ROOT/scripts/lm-integration-test.py"
    STATUS=$?
fi

# --- phase 4: adaptive tiling style + focus-follows-mouse ------------------
if [ $STATUS -eq 0 ]; then
    kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
    echo "=== phase 4: adaptive tiling + follow-mouse ==="
    _WAYFIRE_SOCKET="$SOCK" \
    WAYFIRE_PLUGIN_PATH="$ROOT/build/layout-manager:$ROOT/build/ob-decor:$ROOT/build/ob-menu" \
    WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
    WLR_BACKENDS=headless \
    wayfire -c "$ROOT/test/wayfire-layout-adaptive.ini" -d >"$LOG" 2>&1 &
    WPID=$!
    echo "wayfire PID=$WPID"
    for _ in $(seq 1 80); do
        [ -S "$SOCK" ] && break
        kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
        sleep 0.1
    done
    WF_DISPLAY=""
    for _ in $(seq 1 50); do
        WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
        [ -n "$WF_DISPLAY" ] && break
        sleep 0.1
    done
    export WF_DISPLAY
    python3 "$ROOT/scripts/lm-test-adaptive.py"
    STATUS=$?
fi

if [ $STATUS -ne 0 ]; then
    echo "--- wayfire log tail ---"
    grep -E "EE|Fatal|SEGV" "$LOG" | tail -10
fi
exit $STATUS
