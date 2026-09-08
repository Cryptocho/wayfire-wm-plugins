#!/bin/bash
# ob-theme provider smoke test (headless):
#   1. ob-theme loads FIRST (order_hint), initializes the OB-Arc-Dark theme
#   2. ob-theme/debug get-state reports has_theme=true, theme_name, epoch=1
#   3. op=reload bumps epoch and keeps the theme name
#   4. no second-init warning in the log
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obtheme-provider.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-ob-theme-test.sock"
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK

echo "=== starting headless wayfire (ob-theme provider only) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/ob-theme" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-ob-theme.ini" -d >"$LOG" 2>&1 &
WPID=$!
echo "wayfire PID=$WPID"

for _ in $(seq 1 80); do
    [ -S "$SOCK" ] && break
    kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
    sleep 0.1
done
[ -S "$SOCK" ] || { echo "FAIL: socket never appeared"; tail -30 "$LOG"; exit 1; }

SOCK="$SOCK" python3 "$ROOT/scripts/ob-theme-test.py"
STATUS=$?
grep -c "theme already initialized" "$LOG" >/dev/null && {
    echo "FAIL: unexpected second-init (first-wins violated)"; STATUS=1
}
[ "$STATUS" = 0 ] && echo "ob-theme provider test OK"
exit $STATUS
