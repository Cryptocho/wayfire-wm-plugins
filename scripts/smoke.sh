#!/bin/bash
# Headless smoke test: build product must load and initialize in wayfire.
# Usage: scripts/smoke.sh [plugin-name]   (default: proto-pan)
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUG="${1:-proto-pan}"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"
[ -f "$ROOT/build/$PLUG/lib$PLUG.so" ] || { echo "SMOKE FAIL: $ROOT/build/$PLUG/lib$PLUG.so missing (run ninja first)"; exit 1; }

LOG="$(mktemp)"
WPID=""
cleanup() { [ -n "$WPID" ] && kill "$WPID" 2>/dev/null; rm -f "$LOG"; }
trap cleanup EXIT

WAYFIRE_PLUGIN_PATH="$ROOT/build/$PLUG" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire.ini" -d >"$LOG" 2>&1 &
WPID=$!

for _ in $(seq 1 100); do
    if grep -q "$PLUG init" "$LOG"; then
        echo "SMOKE OK: '$PLUG' loaded and initialized"
        exit 0
    fi
    kill -0 "$WPID" 2>/dev/null || break
    sleep 0.1
done

echo "SMOKE FAIL: '$PLUG' did not initialize; log follows:"
cat "$LOG"
exit 1
