#!/bin/sh
# record.sh [output.gif] — interactive gif recorder for the UI demo.
#
# Opens the SDL emulator with frame recording enabled: drive the demo
# yourself with the normal keys (see README), then press ESC or close
# the window to stop. The script trims the leading blank frames
# (display still off during boot), inverts the colors back to what the
# real panel shows, upscales 2x pixel-perfect and assembles the gif.
#
#   ./record.sh                          -> ui_demo.gif in this directory
#   ./record.sh ../../docs/assets/ui_demo.gif
#   REC_MS=50 ./record.sh                -> 20 fps instead of 10
#
# Needs ffmpeg. Frames are kept in frames/ until the next recording.

cd "$(dirname "$0")" || exit 1
OUT=${1:-ui_demo.gif}
REC_MS=${REC_MS:-100}
FPS=$((1000 / REC_MS))

command -v ffmpeg >/dev/null || { echo "record.sh: ffmpeg not found" >&2; exit 1; }
[ -x emulator ] || ./build.sh || exit 1

rm -rf frames && mkdir frames || exit 1

echo "record.sh: recording every ${REC_MS} ms — ESC or close window to stop"
./emulator --rec "$REC_MS"

# find the first frame with actual pixel data (skip display-off boot)
START=""
for f in frames/f*.ppm; do
    [ -e "$f" ] || break
    if [ -n "$(tail -n +4 "$f" | tr -d '\0')" ]; then
        START=${f#frames/f}; START=${START%.ppm}
        break
    fi
done
[ -n "$START" ] || { echo "record.sh: no non-blank frames captured" >&2; exit 1; }
START=$(printf '%s' "$START" | sed 's/^0*//'); : "${START:=0}"

ffmpeg -y -v error -framerate "$FPS" -start_number "$START" -i frames/f%04d.ppm \
    -vf "negate,scale=iw*2:ih*2:flags=neighbor,split[s0][s1];[s0]palettegen=stats_mode=diff[p];[s1][p]paletteuse=dither=none" \
    -loop 0 "$OUT" || exit 1

echo "record.sh: wrote $OUT ($(ls frames | wc -l) frames captured, first used: $START)"
