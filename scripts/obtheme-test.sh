#!/bin/bash
# T2: theme-parsing fidelity assertions.
# 1. OB-Arc-Dark (real openbox theme at ~/.themes) must parse to exact values.
# 2. No-theme run must produce labwc built-in defaults without crashing.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DUMP="$ROOT/build/obtheme-dump"
[ -x "$DUMP" ] || { echo "obtheme-test FAIL: $DUMP missing (run ninja)"; exit 1; }

THEME="${OB_THEME:-OB-Arc-Dark}"
STATUS=0

# LeakSanitizer is off: pango/fontconfig keep internal one-time caches that
# LSA flags in short-lived processes. ASAN memory-error detection stays on.
export ASAN_OPTIONS=detect_leaks=0

echo "=== $THEME assertions ==="
"$DUMP" --theme "$THEME" --assert-file "$ROOT/tests/arc-dark-assert.txt" \
	>/tmp/obtheme-dump.log 2>&1 || STATUS=1
tail -3 /tmp/obtheme-dump.log

echo "=== built-in defaults (no theme) ==="
if ! "$DUMP" >/tmp/obtheme-defaults.log 2>&1; then
	echo "obtheme-test FAIL: default-theme run crashed"
	cat /tmp/obtheme-defaults.log
	exit 1
fi
grep -E "border_width|button_width|justify" /tmp/obtheme-defaults.log

echo "=== gradient fixture (XDG_DATA_HOME override) ==="
XDG_DATA_HOME="$ROOT/test" "$DUMP" --theme gradtest \
    --assert-file "$ROOT/tests/grad-assert.txt" \
    >/tmp/obtheme-grad.log 2>&1 || STATUS=1
grep -E "title_bg_active|assert ok line" /tmp/obtheme-grad.log | tail -3

[ $STATUS -eq 0 ] && echo "obtheme-test OK" || echo "obtheme-test FAIL"
exit $STATUS
