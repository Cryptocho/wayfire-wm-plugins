#!/bin/bash
# M1: menu.xml parse fidelity assertions.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DUMP="$ROOT/build/obmenu-dump"
[ -x "$DUMP" ] || { echo "obmenu-test FAIL: $DUMP missing (run ninja)"; exit 1; }

STATUS=0
echo "=== fixture menu assertions ==="
"$DUMP" --menu-file "$ROOT/tests/root-menu.xml" \
    --assert-file "$ROOT/tests/menu-assert.txt" \
    >/tmp/obmenu-dump.log 2>&1 || STATUS=1
grep -E "ASSERT FAIL" /tmp/obmenu-dump.log | head -6
tail -1 /tmp/obmenu-dump.log

echo "=== defaults (no menu file) ==="
if ! "$DUMP" >/tmp/obmenu-defaults.log 2>&1; then
    echo "obmenu-test FAIL: defaults run crashed"
    cat /tmp/obmenu-defaults.log
    exit 1
fi
grep -E "menu.root-menu.item.[0-9]+.(text|actions)" /tmp/obmenu-defaults.log | head -4

[ $STATUS -eq 0 ] && echo "obmenu-test OK" || echo "obmenu-test FAIL"
exit $STATUS
