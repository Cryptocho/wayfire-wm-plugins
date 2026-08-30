#!/bin/bash
# M6: pipemenus — normal script output (lazy spawn on open), re-spawn after
# the tree closes, empty output ('<' validation), 4s timeout kill, 1 MiB cap.
# Driven over IPC (headless); assertions in scripts/pipe-test.py.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obmenu-m6.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-wm-plugins-test.sock"
PIPE_FILE=/tmp/obmenu-pipe.xml
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK LOG PIPE_FILE

# initial pipemenu output (2 items)
cat > "$PIPE_FILE" <<'EOF'
<menu id="pipe-demo" label="Pipe">
  <item label="Pipe_item_a"><action name="Execute" command="true" /></item>
  <item label="Pipe_item_b"><action name="Execute" command="true" /></item>
</menu>
EOF

echo "=== starting headless wayfire (M6 test config) ==="
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

python3 "$ROOT/scripts/pipe-test.py"
STATUS=$?
if [ $STATUS -ne 0 ]; then
    echo "--- wayfire log tail ---"
    grep -E "EE|Fatal|SEGV|pipemenu" "$LOG" | tail -12
fi
exit $STATUS
