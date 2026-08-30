#!/bin/bash
# Reproduce the M4 blocker: IPC open-root hangs inside open_root/add_front.
# Observation only — no fix attempts here.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG="$(mktemp /tmp/obmenu-repro.XXXXXX.log)"
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-wm-plugins-test.sock"
cleanup() {
    if [ -n "$WPID" ]; then
        # process-safety rule: kill the exact recorded PID only
        kill "$WPID" 2>/dev/null
        wait "$WPID" 2>/dev/null
    fi
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"

echo "=== starting nested headless wayfire ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build/ob-menu:$ROOT/build/ob-decor" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-menu.ini" -d >"$LOG" 2>&1 &
WPID=$!
echo "wayfire PID=$WPID  log=$LOG"

for _ in $(seq 1 80); do
    [ -S "$SOCK" ] && break
    kill -0 "$WPID" 2>/dev/null || { echo "wayfire DIED during startup"; tail -20 "$LOG"; exit 1; }
    sleep 0.1
done
[ -S "$SOCK" ] || { echo "FAIL: socket never appeared"; tail -30 "$LOG"; exit 1; }
sleep 0.5

echo "=== test 1: get-state (control: is IPC alive) ==="
WF_SOCK="$SOCK" python3 - <<'EOF'
import os, socket, struct, json
def call(method, data=None, timeout=5.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(os.environ["WF_SOCK"])
    payload = json.dumps({"method": method, "data": data or {}}).encode()
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
    s.close()
    return json.loads(body)

r = call("ob-menu/debug", {"op": "get-state"}, 5.0)
print("get-state reply:", r)
EOF
RC1=$?
echo "get-state exit code: $RC1 (0 = IPC alive)"
[ $RC1 -ne 0 ] && { echo "--- log tail ---"; tail -20 "$LOG"; }

echo
echo "=== test 2: open-root (the suspect) ==="
WF_SOCK="$SOCK" python3 - <<'EOF'
import os, socket, struct, json, time
def call(method, data=None, timeout=5.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(os.environ["WF_SOCK"])
    payload = json.dumps({"method": method, "data": data or {}}).encode()
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
    s.close()
    return json.loads(body)

t0 = time.time()
try:
    r = call("ob-menu/debug", {"op": "open-root", "x": 100, "y": 100}, 5.0)
    print(f"open-root reply after {time.time()-t0:.2f}s:", r)
except Exception as e:
    print(f"open-root FAILED after {time.time()-t0:.2f}s: {type(e).__name__}: {e}")
EOF
RC2=$?
echo "open-root client exit code: $RC2 (124/timeout-ish = hang reproduced)"

sleep 1
echo
echo "=== wayfire process state after open-root ==="
if kill -0 "$WPID" 2>/dev/null; then
    echo "PID $WPID alive"
    echo "-- threads (comm/wchan) --"
    for T in /proc/$WPID/task/*; do
        printf "%s  %-16s  %s\n" "$(basename "$T")" "$(cat "$T/comm" 2>/dev/null)" "$(cat "$T/wchan" 2>/dev/null)"
    done
    echo "-- main thread stack (may need ptrace perms) --"
    cat /proc/$WPID/task/$WPID/stack 2>/dev/null || echo "(no permission for /proc stack)"
else
    echo "PID $WPID DEAD (silent death reproduced)"
    wait "$WPID" 2>/dev/null; echo "exit status: $?"
fi

echo
echo "=== test 3: control — is IPC still responsive? ==="
WF_SOCK="$SOCK" python3 - <<'EOF'
import os, socket, struct, json
def call(method, data=None, timeout=3.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(os.environ["WF_SOCK"])
    payload = json.dumps({"method": method, "data": data or {}}).encode()
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
    s.close()
    return json.loads(body)
try:
    r = call("ob-menu/debug", {"op": "get-state"}, 3.0)
    print("post-hang get-state reply:", r)
except Exception as e:
    print(f"post-hang get-state FAILED: {type(e).__name__}: {e}")
EOF

echo
echo "=== full wayfire log ==="
cat "$LOG"
