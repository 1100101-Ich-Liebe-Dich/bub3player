#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tries out a built AppImage: help, audio analysis through the bundled
# GStreamer, start with a screenshot. Usage: smoke-test.sh BuB3Player-x86_64.AppImage
set -u
img=$(readlink -f "$1")
dir=$(mktemp -d)
cd "$dir"
"$img" --appimage-extract > /dev/null || { echo "Entpacken fehlgeschlagen"; exit 1; }
export XDG_CONFIG_HOME=$dir/cfg XDG_DATA_HOME=$dir/data XDG_CACHE_HOME=$dir/cache
fail=0
echo "== Hilfe"
QT_QPA_PLATFORM=offscreen ./squashfs-root/AppRun --help | head -5 || fail=1
echo "== Audioanalyse"
python3 -c "import wave,math,struct; w=wave.open('ton.wav','wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(b''.join(struct.pack('<hh',*(2*[int(9000*math.sin(2*math.pi*440*i/44100))])) for i in range(44100*3))); w.close()"
GST_DEBUG=2 ./squashfs-root/AppRun --analyze ton.wav > analyse.txt 2>&1
cat analyse.txt | tail -20
grep -q "LUFS" analyse.txt || { echo "FEHLER: Analyse ohne Ergebnis"; fail=1; }
echo "== Start"
out=$OLDPWD
xvfb-run -a -s "-screen 0 1380x880x24" ./squashfs-root/AppRun --no-scan --screenshot "$out/start.png" --screenshot-delay 5000 > start.txt 2>&1
tail -20 start.txt
[ -s "$out/start.png" ] || { echo "FEHLER: kein Bild vom Start"; fail=1; }

echo "== Alle Themes, Playlists, Dialoge"
for skin in klassik soft glas aurora vintage synthwave tower terminal; do
    xvfb-run -a -s "-screen 0 1380x880x24" ./squashfs-root/AppRun --no-scan --skin $skin \
        --ui-test "mkpl plopen=1 pledit" --screenshot "$out/$skin.png" --screenshot-delay 4500 >> start.txt 2>&1
    [ -s "$out/$skin.png" ] || { echo "FEHLER: Theme $skin startet nicht"; fail=1; }
done
echo "== Klang: Wiedergabe mit Equalizer"
mkdir -p musik && cp ton.wav musik/
xvfb-run -a -s "-screen 0 1380x880x24" ./squashfs-root/AppRun --add-folder "$dir/musik" \
    --ui-test "playall klang-tab eqonat=1500" --screenshot "$out/klang.png" --screenshot-delay 4500 >> start.txt 2>&1
[ -s "$out/klang.png" ] || { echo "FEHLER: Klang-Seite startet nicht"; fail=1; }
if grep -q "GStreamer-Element fehlt" start.txt; then
    grep "GStreamer-Element fehlt" start.txt | sort -u
    echo "::warning title=GStreamer::$(grep "GStreamer-Element fehlt" start.txt | sort -u | tr '\n' ' ')"
    fail=1
fi
# QML problems (e.g. with another Qt version than the one it was written with)
qml=$(grep -E "qrc:/|QML|ReferenceError|TypeError" start.txt | sort | uniq -c | sort -rn | head -40)
if [ -n "$qml" ]; then
    echo "$qml"
    msg=$(echo "$qml" | sed -e 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
    echo "::warning title=QML-Meldungen::$msg"
    grep -qE "is not a type|is not installed|unavailable|failed to load|ReferenceError|TypeError" start.txt && fail=1
fi
exit $fail
