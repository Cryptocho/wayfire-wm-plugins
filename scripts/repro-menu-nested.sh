#!/bin/bash
# Reproduce the M4 crash in NESTED mode (real rendering, like the original
# session that produced the coredumps: `wayfire -c test/wayfire-menu.ini -d`).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/obmenu-nested-repro.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-wm-plugins-test.sock"
cleanup() {
    if [ -n "$WPID" ]; then
        kill "$WPID" 2>/dev/null   # exact-PID rule
        wait "$WPID" 2>/dev/null
    fi
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"

echo "=== starting NESTED wayfire (real wayland backend, on top of the desktop) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/ob-menu:$ROOT/build/ob-decor" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
wayfire -c "$ROOT/test/wayfire-menu.ini" -d >"$LOG" 2>&1 &
WPID=$!
echo "wayfire PID=$WPID"

for _ in $(seq 1 100); do
    [ -S "$SOCK" ] && break
    kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
    sleep 0.1
done
[ -S "$SOCK" ] || { echo "FAIL: socket never appeared"; tail -30 "$LOG"; exit 1; }

# find the nested session's wayland socket for grim
WF_DISPLAY=""
for _ in $(seq 1 50); do
    WF_DISPLAY=$(grep -oP "Using socket name \Kwayland-\d+" "$LOG" | tail -1)
    [ -n "$WF_DISPLAY" ] && break
    sleep 0.1
done
echo "nested WAYLAND_DISPLAY=$WF_DISPLAY"
sleep 1

shot() {
    WAYLAND_DISPLAY="$WF_DISPLAY" grim -c /tmp/menu-shot-$1.png 2>&1 && echo "screenshot: /tmp/menu-shot-$1.png"
}

echo "=== step 1: get-state (control) ==="
call_ipc() {
    python3 - "$@" <<'EOF'
import os, socket, struct, json, sys
method, data, timeout = sys.argv[1], json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}, 5.0
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(timeout)
s.connect(os.environ["SOCK"])
payload = json.dumps({"method": method, "data": data}).encode()
s.sendall(struct.pack("<I", len(payload)) + payload)
header = b""
while len(header) < 4:
    chunk = s.recv(4 - len(header))
    if not chunk:
        raise RuntimeError("conn closed (header)")
    header += chunk
(length,) = struct.unpack("<I", header)
body = b""
while len(body) < length:
    chunk = s.recv(length - len(body))
    if not chunk:
        raise RuntimeError("conn closed (body)")
    body += chunk
print(json.loads(body))
EOF
}
export SOCK
call_ipc "ob-menu/debug" '{"op": "get-state"}' || true

echo "=== step 2: open-root via IPC ==="
call_ipc "ob-menu/debug" '{"op": "open-root", "x": 200, "y": 150}' && echo "IPC open-root returned OK" || echo "IPC open-root FAILED"
shot "1-opened"

sleep 1
echo "=== process state after open-root ==="
if kill -0 "$WPID" 2>/dev/null; then
    echo "PID $WPID alive"
else
    echo "PID $WPID DEAD — crash reproduced!"
fi

echo "=== step 3: get-state again ==="
call_ipc "ob-menu/debug" '{"op": "get-state"}' || true

echo "=== step 4: close ==="
call_ipc "ob-menu/debug" '{"op": "close"}' || true
shot "2-closed"

echo "=== full DBG log ==="
grep -E "DBG|EE|Fatal|SEGV" "$LOG" | tail -20
