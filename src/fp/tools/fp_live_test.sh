#!/bin/bash
#
# Live test of the first-person mode without a GPU or a display.
#
# Runs OpenTTD under Xvfb with Mesa's software OpenGL on the title game,
# opens the first-person view through the console, moves the camera and
# stores screenshots plus the output of 'fp status'.
#
# Usage: fp_live_test.sh <openttd binary> <output dir> [console command...]
#
#   The binary must be built with -DOPTION_FP_SOFTWARE_GL=ON, otherwise the
#   OpenGL driver refuses Mesa's llvmpipe. Extra console commands are run
#   after 'fp view', each followed by a screenshot (extra_<n>.png).
#
# Needs: Xvfb, xdotool, ImageMagick (import) and OpenGFX
# (Debian/Ubuntu: apt install xvfb xdotool imagemagick openttd-opengfx).
# OPENGFX_DIR overrides where OpenGFX is looked for.
#
# FP_NEWGRF=<file.grf> runs a new game (seed FP_SEED, default 42) with that
# NewGRF instead of the title game; src/fp/tools/newgrf/ builds a test NewGRF.

set -u

if [ $# -lt 2 ]; then
	echo "Usage: $0 <openttd binary> <output dir> [console command...]" >&2
	exit 2
fi

BINARY=$(realpath "$1")
OUT=$(realpath -m "$2")
shift 2
OPENGFX_DIR=${OPENGFX_DIR:-/usr/share/games/openttd/baseset/opengfx}
DISPLAY_NUMBER=${DISPLAY_NUMBER:-97}

for tool in Xvfb xdotool import; do
	command -v $tool >/dev/null || { echo "Missing tool: $tool" >&2; exit 2; }
done
[ -x "$BINARY" ] || { echo "Not an executable: $BINARY" >&2; exit 2; }
[ -d "$OPENGFX_DIR" ] || { echo "OpenGFX not found in $OPENGFX_DIR" >&2; exit 2; }

mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/console.log "$OUT"/openttd.log

# A private home keeps the test independent of the user's configuration.
TEST_HOME=$(mktemp -d)
mkdir -p "$TEST_HOME/.local/share/openttd/baseset"
ln -s "$OPENGFX_DIR" "$TEST_HOME/.local/share/openttd/baseset/opengfx"

if [ -n "${FP_NEWGRF:-}" ]; then
	[ -f "$FP_NEWGRF" ] || { echo "Not a file: $FP_NEWGRF" >&2; exit 2; }
	mkdir -p "$TEST_HOME/.local/share/openttd/newgrf" "$TEST_HOME/.config/openttd"
	cp "$FP_NEWGRF" "$TEST_HOME/.local/share/openttd/newgrf/"
	# NewGRFs of the [newgrf] section are used for new games.
	printf '[newgrf]\n%s = \n' "$(basename "$FP_NEWGRF")" > "$TEST_HOME/.config/openttd/openttd.cfg"
fi

XVFB_PID=""
OTTD_PID=""
cleanup() {
	[ -n "$OTTD_PID" ] && kill "$OTTD_PID" 2>/dev/null && sleep 1 && kill -9 "$OTTD_PID" 2>/dev/null
	[ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
	rm -rf "$TEST_HOME"
}
trap cleanup EXIT

Xvfb ":$DISPLAY_NUMBER" -screen 0 1280x800x24 >/dev/null 2>&1 &
XVFB_PID=$!
export DISPLAY=":$DISPLAY_NUMBER"
sleep 2

# OpenTTD finds its language files and base sprites next to the binary.
(cd "$(dirname "$BINARY")" && HOME="$TEST_HOME" exec "$BINARY" -v sdl-opengl -r 1280x800 -m null -s null -d fp=5) > "$OUT/openttd.log" 2>&1 &
OTTD_PID=$!

# Wait for the window, then a bit for the title game.
for _ in $(seq 1 60); do
	xdotool search --name OpenTTD >/dev/null 2>&1 && break
	sleep 1
done
sleep 10

# Run a console command: open the console with the backquote key, type, close it.
console() {
	xdotool key grave; sleep 0.7
	xdotool type --delay 30 "$1"; xdotool key Return; sleep 0.7
	xdotool key grave; sleep 0.7
}

console "script $OUT/console.log"
if [ -n "${FP_NEWGRF:-}" ]; then
	# Starting a game closes the console itself.
	xdotool key grave; sleep 0.7
	xdotool type --delay 30 "newgame ${FP_SEED:-42}"; xdotool key Return
	sleep 15
fi
console "fp view"
sleep 6
import -window root "$OUT/1_open.png"

# Fly forward.
xdotool keydown Up; sleep 2; xdotool keyup Up; sleep 1
import -window root "$OUT/2_forward.png"

# Look around with the right mouse button held.
xdotool mousemove 640 420 mousedown 3; sleep 0.3
for _ in $(seq 1 25); do xdotool mousemove_relative -- 14 4; sleep 0.04; done
xdotool mouseup 3; sleep 1

# Climb with the mouse wheel.
for _ in $(seq 1 6); do xdotool click 4; sleep 0.1; done
sleep 2
import -window root "$OUT/3_look_climb.png"

n=0
for command in "$@"; do
	n=$((n + 1))
	console "$command"
	sleep 3
	import -window root "$OUT/extra_$n.png"
done

console "fp status"
console "script"
sleep 1

# Images written by console commands, e.g. 'fp capture <x> <y> <name>', go to the screenshot directory.
cp "$TEST_HOME"/.local/share/openttd/screenshot/*.png "$OUT"/ 2>/dev/null

if ! kill -0 "$OTTD_PID" 2>/dev/null; then
	echo "OpenTTD exited early; see $OUT/openttd.log" >&2
	exit 1
fi
if ! grep -q "Renderer: llvmpipe\|Renderer: .*OpenGL" "$OUT/console.log" 2>/dev/null; then
	echo "The first-person renderer did not run; see $OUT/console.log and $OUT/openttd.log" >&2
	exit 1
fi
echo "Screenshots and logs in $OUT"
