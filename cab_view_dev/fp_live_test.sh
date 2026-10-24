#!/bin/bash
#
# Live test of the cab view and the first-person view without a display.
#
# Runs the game under Xvfb with the test AI (CabTestAI), which builds a railway,
# a road, a canal and airports with vehicles. When the train runs, the console
# commands given are run in turn, each followed by a screenshot <n>.png, and
# the output of the console goes to console.log.
#
# Usage: fp_live_test.sh <openttd binary> <output dir> [<console command>|<step>...]
#
#   In commands, {TRAIN}, {TRAIN2}, {BUS}, {SHIP} and {PLANE} stand for the
#   numbers of the vehicles the AI logs. Besides console commands, a step can be
#   'wait <seconds>', 'hold <key> <seconds>' (xdotool key name, like Up or w),
#   'key <key>' (press it once), 'click <x> <y>' (left mouse button),
#   'catch <vehicle>' (wait until the vehicle loads at a station, then pause the game),
#   'look <dx> <dy>' (drag with the right mouse button over the middle of the
#   screen) or 'shot' (a screenshot only).
#
#   Example:
#     fp_live_test.sh build/openttd /tmp/fp 'fp cab {TRAIN}' 'wait 5' shot \
#         'fp walk' 'hold w 2' 'look 200 0' shot
#
# Needs: Xvfb, xdotool, ImageMagick (import) and OpenGFX
# (Debian/Ubuntu: apt install xvfb xdotool imagemagick openttd-opengfx).
# OPENGFX_DIR overrides where OpenGFX is looked for, DISPLAY_NUMBER the display,
# OTTD_DEBUG adds a debug level, like misc=3, OTTD_LANGUAGE sets the language, like russian.

set -u

if [ $# -lt 2 ]; then
	echo "Usage: $0 <openttd binary> <output dir> [<console command>|<step>...]" >&2
	exit 2
fi

BINARY=$(realpath "$1")
OUT=$(realpath -m "$2")
shift 2
DEV_DIR=$(dirname "$(realpath "$0")")
OPENGFX_DIR=${OPENGFX_DIR:-/usr/share/games/openttd/baseset/opengfx}
DISPLAY_NUMBER=${DISPLAY_NUMBER:-98}

for tool in Xvfb xdotool import; do
	command -v $tool >/dev/null || { echo "Missing tool: $tool" >&2; exit 2; }
done
[ -x "$BINARY" ] || { echo "Not an executable: $BINARY" >&2; exit 2; }
[ -d "$OPENGFX_DIR" ] || { echo "OpenGFX not found in $OPENGFX_DIR" >&2; exit 2; }

mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/console.log "$OUT"/openttd.log

# A private home keeps the test independent of the user's configuration.
TEST_HOME=$(mktemp -d)
DATA="$TEST_HOME/.local/share/openttd"
mkdir -p "$DATA/baseset" "$DATA/ai" "$DATA/scripts"
ln -s "$OPENGFX_DIR" "$DATA/baseset/opengfx"
cp -r "$DEV_DIR/CabTestAI" "$DATA/ai/"
cp "$DEV_DIR/game_start.scr" "$DATA/scripts/"
sed -e "s/^language = .*/language = ${OTTD_LANGUAGE:-english}.lng/" -e 's/^resolution = .*/resolution = 1280,800/' "$DEV_DIR/test.cfg" > "$TEST_HOME/test.cfg"

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

# OpenTTD finds its language files next to the binary.
(cd "$(dirname "$BINARY")" && HOME="$TEST_HOME" exec "$BINARY" -c "$TEST_HOME/test.cfg" -g -v sdl -r 1280x800 -m null -s null -d script=4 ${OTTD_DEBUG:+-d $OTTD_DEBUG}) > "$OUT/openttd.log" 2>&1 &
OTTD_PID=$!

# Wait for the AI to start its train.
for _ in $(seq 1 180); do
	grep -q "TRAIN [0-9]" "$OUT/openttd.log" && break
	kill -0 "$OTTD_PID" 2>/dev/null || { echo "OpenTTD exited early; see $OUT/openttd.log" >&2; exit 1; }
	sleep 1
done
grep -q "TRAIN [0-9]" "$OUT/openttd.log" || { echo "The test AI did not start a train; see $OUT/openttd.log" >&2; exit 1; }
# The rest of what the AI builds.
for _ in $(seq 1 60); do
	grep -q "PLANE [0-9]\|NO PLANE\|NO AIRPORT" "$OUT/openttd.log" && break
	sleep 1
done
sleep 3

vehicle() {
	grep -o "$1 [0-9]*" "$OUT/openttd.log" | head -1 | cut -d' ' -f2
}
TRAIN=$(vehicle TRAIN)
TRAIN2=$(vehicle TRAIN2)
BUS=$(vehicle BUS)
SHIP=$(vehicle SHIP)
PLANE=$(vehicle PLANE)
echo "Vehicles: train $TRAIN, second train ${TRAIN2:-none}, bus ${BUS:-none}, ship ${SHIP:-none}, plane ${PLANE:-none}"

# Run a console command: open the console with the backquote key, type, close it.
console() {
	xdotool key grave; sleep 0.5
	xdotool type --delay 20 "$1"; xdotool key Return; sleep 0.5
	xdotool key grave; sleep 0.5
}

console "script $OUT/console.log"

n=0
shot() {
	n=$((n + 1))
	import -window root "$OUT/$(printf '%02d' $n).png"
}

for step in "$@"; do
	step=${step//\{TRAIN\}/$TRAIN}
	step=${step//\{TRAIN2\}/$TRAIN2}
	step=${step//\{BUS\}/$BUS}
	step=${step//\{SHIP\}/$SHIP}
	step=${step//\{PLANE\}/$PLANE}
	read -r -a words <<< "$step"
	case "${words[0]}" in
		wait) sleep "${words[1]}" ;;
		shot) shot ;;
		hold) xdotool keydown "${words[1]}"; sleep "${words[2]}"; xdotool keyup "${words[1]}"; sleep 0.5 ;;
		key) xdotool key "${words[1]}"; sleep 1 ;;
		click) xdotool mousemove "${words[1]}" "${words[2]}" click 1; sleep 1 ;;
		catch)
			# The game is paused while the log is read, as loading at a station does not take long.
			for _ in $(seq 1 120); do
				console "pause"
				console "fp status ${words[1]}"
				# The log is written out when it is closed; it is opened again to append.
				console "script"
				console "script $OUT/console.log"
				grep "^Vehicle" "$OUT/console.log" | tail -1 | grep -q "loading at a station" && break
				console "unpause"
			done
			;;
		look)
			xdotool mousemove 640 400 mousedown 3; sleep 0.2
			for _ in $(seq 1 20); do xdotool mousemove_relative -- $((words[1] / 20)) $((words[2] / 20)); sleep 0.03; done
			xdotool mouseup 3; sleep 0.5
			;;
		*) console "$step"; sleep 2; shot ;;
	esac
done

console "fp status"
console "script"
sleep 1

if ! kill -0 "$OTTD_PID" 2>/dev/null; then
	echo "OpenTTD exited early; see $OUT/openttd.log" >&2
	exit 1
fi
echo "Screenshots and logs in $OUT"
