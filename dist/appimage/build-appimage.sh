#!/bin/bash
# Builds BuB3Player-x86_64.AppImage: the player with Qt, GStreamer (only the
# plugins it needs) and all libraries, runnable on any recent x86-64 Linux.
#
# Used by the GitHub workflow (.github/workflows/build.yml). Works locally too
# on Debian/Ubuntu with the build dependencies from the README plus
# patchelf, file and glib-networking:
#   dist/appimage/build-appimage.sh            -> build-appimage/BuB3Player-x86_64.AppImage
# QMAKE=/path/to/qmake picks a different Qt (default: qmake6 / qmake from PATH).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-$ROOT/build-appimage}
ARCH=$(uname -m)
OUTPUT=${OUTPUT:-BuB3Player-$ARCH.AppImage}
VERSION=${VERSION:-$(sed -n 's/^project(Halcyon VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")}
MULTIARCH=$(gcc -print-multiarch 2>/dev/null || echo "$ARCH-linux-gnu")
GST_PLUGINS=${GSTREAMER_PLUGINS_DIR:-/usr/lib/$MULTIARCH/gstreamer-1.0}
GST_HELPERS=${GSTREAMER_HELPERS_DIR:-/usr/lib/$MULTIARCH/gstreamer1.0/gstreamer-1.0}
GIO_MODULES=${GIO_MODULES_DIR:-/usr/lib/$MULTIARCH/gio/modules}

mkdir -p "$WORK"
cd "$WORK"

echo "== Bauen (Version $VERSION)"
cmake -S "$ROOT" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
rm -rf AppDir
DESTDIR="$WORK/AppDir" cmake --install build

echo "== Werkzeuge"
fetch() { [ -s "$2" ] || curl -fsSL --retry 3 -o "$2" "$1"; chmod +x "$2"; }
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage linuxdeploy
fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage linuxdeploy-plugin-qt
export PATH="$WORK:$PATH"        # linuxdeploy finds its Qt plugin next to it
export APPIMAGE_EXTRACT_AND_RUN=1 # no FUSE needed (containers, CI)

echo "== GStreamer: nur was der Player braucht"
# playback, decoding, the effects of the filter chain, outputs, HTTP for Jellyfin
plugins="coreelements playback typefindfunctions audioconvert audioresample volume app pbtypes
         ogg vorbis opus alsa gio
         level spectrum replaygain equalizer autodetect pulseaudio flac wavparse audioparsers id3demux
         apetag isomp4 mpg123 wavpack soup icydemux matroska
         aiff libav"
dest=AppDir/usr/lib/gstreamer-1.0
mkdir -p "$dest"
for p in $plugins; do
    if [ -f "$GST_PLUGINS/libgst$p.so" ]; then
        cp "$GST_PLUGINS/libgst$p.so" "$dest/"
    else
        echo "Hinweis: GStreamer-Plugin $p fehlt, wird ausgelassen"
    fi
done
helpers=AppDir/usr/lib/gstreamer1.0/gstreamer-1.0
mkdir -p "$helpers"
cp "$GST_HELPERS/gst-plugin-scanner" "$helpers/"
# TLS for https streams (GIO); certificates come from the system
gio=AppDir/usr/lib/gio/modules
mkdir -p "$gio"
[ -f "$GIO_MODULES/libgiognutls.so" ] && cp "$GIO_MODULES/libgiognutls.so" "$gio/" || echo "Hinweis: glib-networking fehlt (https-Streams)"

mkdir -p AppDir/apprun-hooks
cat > AppDir/apprun-hooks/bub3player-env.sh <<'HOOK'
# GStreamer and GIO from inside the AppImage
export GST_REGISTRY_REUSE_PLUGIN_SCANNER="no"
export GST_PLUGIN_SYSTEM_PATH_1_0="${APPDIR}/usr/lib/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0="${APPDIR}/usr/lib/gstreamer-1.0"
export GST_PLUGIN_SCANNER_1_0="${APPDIR}/usr/lib/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner"
export GST_REGISTRY_1_0="${XDG_CACHE_HOME:-$HOME/.cache}/halcyon/gstreamer-registry-$(uname -m).bin"
export GIO_MODULE_DIR="${APPDIR}/usr/lib/gio/modules"
HOOK

echo "== Abhängigkeiten einsammeln"
export QML_SOURCES_PATHS="$ROOT/qml"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export EXTRA_QT_MODULES="svg"
[ -n "${QMAKE:-}" ] || export QMAKE=$(command -v qmake6 || command -v qmake)
desktop=AppDir/usr/share/applications/de.bauenundbooten.BuB3Player.desktop
icon=AppDir/usr/share/icons/hicolor/scalable/apps/de.bauenundbooten.BuB3Player.svg
linuxdeploy --appdir AppDir --desktop-file "$desktop" --icon-file "$icon" \
    --executable AppDir/usr/bin/halcyon \
    --deploy-deps-only "$dest" --deploy-deps-only "$helpers" --deploy-deps-only "$gio" \
    --plugin qt
# plugins look for their libraries one level up
for f in "$dest"/*.so "$gio"/*.so; do [ -f "$f" ] && patchelf --set-rpath '$ORIGIN/..:$ORIGIN/../..:$ORIGIN' "$f"; done
patchelf --set-rpath '$ORIGIN/../..' "$helpers/gst-plugin-scanner"

echo "== AppImage packen"
rm -f "$OUTPUT"
LDAI_OUTPUT="$OUTPUT" OUTPUT="$OUTPUT" LINUXDEPLOY_OUTPUT_VERSION="$VERSION" \
    linuxdeploy --appdir AppDir --output appimage
ls -l "$OUTPUT"
