#!/bin/bash
# Run the player headless (Xvfb) with its own profile, for screenshots, click
# tests and memory measurements. Every profile has its own config, database
# and cache below $HALCYON_TEST_DIR (default /tmp/halcyon-test/<profil>).
#
#   tools/ui-test.sh shot NAME SEKUNDEN [halcyon-Optionen …]
#       start, wait, save NAME.png, quit. The log is NAME.log.
#       Example: tools/ui-test.sh shot synth 6 --skin synthwave --ui-test "playall now-playing"
#
#   tools/ui-test.sh start [halcyon-Optionen …]   start and keep running (display :77)
#   tools/ui-test.sh click X Y [rechts]           click into the running window (needs xdotool)
#   tools/ui-test.sh key TASTE                    e.g. ctrl+a, Escape, Return
#   tools/ui-test.sh type TEXT
#   tools/ui-test.sh drag X1 Y1 X2 Y2             drag with the mouse (playlists, queue)
#   tools/ui-test.sh snap NAME                    screenshot of the running window
#   tools/ui-test.sh stop
#
#   tools/ui-test.sh mem NAME SEKUNDEN [halcyon-Optionen …]
#       like shot, but samples memory twice a second: peak and end, all (RSS)
#       and the player's own data (anonymous memory).
#
# Environment: PROFIL (default "test"), HALCYON (binary, default build/halcyon),
# WAIT (seconds after "start", default 4), RULES (Qt logging rules, e.g.
# "halcyon.mix.debug=true" for the automix log).
#
# The --ui-test actions are listed in qml/Main.qml (uiTestTimer): playall, play,
# row=N, radio=ID, seek=MS, pauseat=/playat=/nextat=MS, queue, now-playing,
# album, theme-tab, mix-tab, klang-tab, jellyfin-tab, jf-login=URL, jf-quick,
# cycle, cycleloop, pair, swap-<theme>, scroll, skip, blendloop, autoplay,
# search[=wort], mkpl, plopen=N, pledit, plnew, plsel, eqonat=/eqoffat=/bassat=MS,
# fullscreen (needs a window manager to take effect, e.g. openbox on the Xvfb).
#
# Audio tests: HALCYON_TEST_SINK="filesink location=/tmp/x.raw" replaces the
# output device (any GStreamer sink description), GST_DEBUG_DUMP_DOT_DIR=DIR
# writes the pipeline as a graph when playback starts.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROFIL=${PROFIL:-test}
DIR=${HALCYON_TEST_DIR:-/tmp/halcyon-test}/$PROFIL
BIN=${HALCYON:-$ROOT/build/halcyon}
mkdir -p "$DIR/shots"
export XDG_CONFIG_HOME=$DIR/cfg XDG_DATA_HOME=$DIR/data XDG_CACHE_HOME=$DIR/cache
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export QT_LOGGING_RULES=${RULES:-}
SIZE=1380x880
DISP=:77

quiet() { grep -v -E "XDG_RUNTIME|^$|ALSA|pw.conf|ALSOFT|GStreamer: Failed" "$1" | head -${LINES_:-40}; }

cmd=${1:-}; shift || true
case "$cmd" in
shot)
    name=$1 secs=$2; shift 2
    timeout $((secs + 30)) xvfb-run -a -s "-screen 0 ${SIZE}x24" "$BIN" \
        --screenshot "$DIR/shots/$name.png" --screenshot-delay $((secs * 1000)) "$@" > "$DIR/$name.log" 2>&1
    quiet "$DIR/$name.log"
    echo "Bild: $DIR/shots/$name.png"
    ;;
start)
    pgrep -f "Xvfb $DISP" > /dev/null || { (Xvfb $DISP -screen 0 ${SIZE}x24 > /dev/null 2>&1 &); sleep 1; }
    (DISPLAY=$DISP setsid "$BIN" "$@" > "$DIR/run.log" 2>&1 &)
    sleep "${WAIT:-4}"
    ;;
click)
    DISPLAY=$DISP xdotool mousemove "$1" "$2" click $([ "${3:-}" = rechts ] && echo 3 || echo 1)
    ;;
key)
    DISPLAY=$DISP xdotool key "$1"
    ;;
type)
    DISPLAY=$DISP xdotool type "$1"
    ;;
drag)
    x1=$1 y1=$2 x2=$3 y2=$4
    DISPLAY=$DISP xdotool mousemove "$x1" "$y1" mousedown 1
    for i in 1 2 3 4 5 6 7 8 9 10; do
        DISPLAY=$DISP xdotool mousemove $((x1 + (x2 - x1) * i / 10)) $((y1 + (y2 - y1) * i / 10))
        sleep 0.05
    done
    sleep 0.2
    DISPLAY=$DISP xdotool mouseup 1
    ;;
snap)
    DISPLAY=$DISP import -window root "$DIR/shots/$1.png" && echo "Bild: $DIR/shots/$1.png"
    ;;
stop)
    pkill -f "[b]uild/halcyon" ; pkill -f "[X]vfb $DISP"; sleep 0.5
    quiet "$DIR/run.log" 2>/dev/null
    ;;
mem)
    name=$1 secs=$2; shift 2
    (Xvfb :81 -screen 0 ${SIZE}x24 > /dev/null 2>&1 &)
    sleep 0.8
    DISPLAY=:81 "$BIN" "$@" > "$DIR/$name.log" 2>&1 &
    app=$!
    : > "$DIR/$name.mem"
    for i in $(seq 1 $((secs * 2))); do
        sleep 0.5
        rss=$(ps -o rss= -p $app 2>/dev/null) || break
        anon=$(awk '/^Anonymous:/ {print int($2/1024)}' /proc/$app/smaps_rollup 2>/dev/null)
        echo "$((i / 2)) $((rss / 1024)) $anon" >> "$DIR/$name.mem"
    done
    kill $app 2>/dev/null; sleep 0.5; kill -9 $app 2>/dev/null; pkill -f "[X]vfb :81"
    awk -v n="$name" '{ if ($2>m) m=$2; l=$2; if ($3>ma) ma=$3; la=$3 }
        END { printf "%-20s gesamt Spitze %4d Ende %4d MB | eigene Daten Spitze %4d Ende %4d MB\n", n, m, l, ma, la }' "$DIR/$name.mem"
    quiet "$DIR/$name.log" | head -5
    ;;
*)
    sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
