#!/bin/bash
# ext-workspace protocol tests (headless).
# Drives the minimal C client against a headless wayfire with the
# ext-workspace plugin loaded, asserting the ext-workspace-v1 event flow.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${XDG_RUNTIME_DIR:?"run from a user session (XDG_RUNTIME_DIR required)"}"

LOG=/tmp/ext-workspace-test.log
WPID=""
SOCK="$XDG_RUNTIME_DIR/wayfire-ew-test.sock"
WF_DISPLAY=""
cleanup() {
    [ -n "$WPID" ] && kill "$WPID" 2>/dev/null && wait "$WPID" 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT
rm -f "$SOCK"
export SOCK

echo "=== starting headless wayfire (ext-workspace test config) ==="
_WAYFIRE_SOCKET="$SOCK" \
WAYFIRE_PLUGIN_PATH="$ROOT/build-ew/ext-workspace" \
WAYFIRE_PLUGIN_XML_PATH="$ROOT/metadata" \
WLR_BACKENDS=headless \
wayfire -c "$ROOT/test/wayfire-ext-workspace.ini" -d >"$LOG" 2>&1 &
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
[ -n "$WF_DISPLAY" ] || { echo "FAIL: wayland socket name not found in log"; tail -30 "$LOG"; exit 1; }
echo "wayland display: $WF_DISPLAY"

CLIENT="$ROOT/build-ew/ext-workspace/ext-workspace-client"
[ -x "$CLIENT" ] || { echo "FAIL: client binary missing ($CLIENT)"; exit 1; }

grep -q "Loaded plugin.*ext-workspace" "$LOG" && echo "OK: ext-workspace plugin loaded"

STATUS=0
run() {
    NAME="$1"
    shift
    echo "--- $NAME ---"
    if "$CLIENT" "$@" "$WF_DISPLAY"; then
        echo "OK: $NAME"
    else
        echo "FAILED: $NAME"
        STATUS=1
    fi
}

# A: initial state - 1 headless output, 2x2 grid, active at (0,0)
run "A: initial state (1 group, 4 workspaces, active (0,0), outputs announced)" \
    --expect-groups 1 --expect-workspaces 4 --expect-outputs 1 --expect-active 0 0

# B: activate (1,0) via the protocol, expect the active state to move
run "B: activate (1,0) switches the active workspace" \
    --activate 1 0 --then-expect-active 1 0

# C: second client gets its own complete initial state (independent broadcast)
run "C: second client independent initial state" \
    --expect-groups 1 --expect-workspaces 4 --expect-active 1 0

# D: stop -> finished
run "D: stop request ends with finished event" --stop

# E: IPC get-state mirrors the protocol view (1 group, 4 workspaces, active (1,0))
echo "--- E: IPC ext-workspace/get-state ---"
if python3 - "$SOCK" <<'EOF'
import json, socket, struct, sys
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(5.0)
s.connect(sock)
payload = json.dumps({"method": "ext-workspace/get-state", "data": {}}).encode()
s.sendall(struct.pack("<I", len(payload)) + payload)
header = b""
while len(header) < 4:
    header += s.recv(4 - len(header))
(length,) = struct.unpack("<I", header)
body = b""
while len(body) < length:
    body += s.recv(length - len(body))
s.close()
st = json.loads(body)
groups = st["groups"]
assert len(groups) == 1, f"expected 1 group, got {len(groups)}"
g = groups[0]
wss = g["workspaces"]
assert len(wss) == 4, f"expected 4 workspaces, got {len(wss)}"
act = [w for w in wss if w["active"]]
assert len(act) == 1, f"expected exactly one active, got {len(act)}"
a = act[0]
assert (a["x"], a["y"]) == (1, 0), f"active is ({a['x']},{a['y']}), expected (1,0)"
assert a["name"] == "2", f"active name is {a['name']}, expected 2"
print("OK: IPC get-state groups=1 workspaces=4 active=(1,0) name=2")
EOF
then
    echo "OK: E: IPC get-state"
else
    echo "FAILED: E: IPC get-state"
    STATUS=1
fi

if [ $STATUS -ne 0 ]; then
    echo "--- wayfire log tail ---"
    grep -E "EE|Fatal|SEGV" "$LOG" | tail -10
fi

exit $STATUS