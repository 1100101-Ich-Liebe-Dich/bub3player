#!/bin/bash
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
xvfb-run -a -s "-screen 0 1380x880x24" ./squashfs-root/AppRun --no-scan --screenshot "$OLDPWD/start.png" --screenshot-delay 5000 > start.txt 2>&1
tail -20 start.txt
[ -s "$OLDPWD/start.png" ] || { echo "FEHLER: kein Bild vom Start"; fail=1; }
exit $fail
