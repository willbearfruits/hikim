#!/usr/bin/env bash
# Build a HIKIM flatpak bundle from the locally-built binary.
# Uses flatpak's low-level build commands (no flatpak-builder needed).
#   packaging/build-flatpak.sh [path-to-HIKIM-binary] [out.flatpak]
set -euo pipefail

APP=io.github.willbearfruits.hikim
BRANCH=24.08
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$HERE")"
BIN="${1:-$REPO_ROOT/build/dawglitch_artefacts/Release/HIKIM}"
OUT="${2:-$REPO_ROOT/dist/HIKIM.flatpak}"

[ -f "$BIN" ] || { echo "binary not found: $BIN (build first)"; exit 1; }

WORK="$(mktemp -d /tmp/hikim-flatpak.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

flatpak build-init "$WORK/build" "$APP" org.freedesktop.Sdk org.freedesktop.Platform "$BRANCH"

mkdir -p "$WORK/build/files/bin" \
         "$WORK/build/files/share/applications" \
         "$WORK/build/files/share/icons/hicolor/256x256/apps"
cp "$BIN" "$WORK/build/files/bin/HIKIM"
strip "$WORK/build/files/bin/HIKIM" || true
cp "$HERE/$APP.desktop" "$WORK/build/files/share/applications/"
cp "$HERE/$APP.png" "$WORK/build/files/share/icons/hicolor/256x256/apps/"

# bundle a STATIC ffmpeg so decode-anything works inside the sandbox
# (the host's ffmpeg is dynamically linked and won't run in the runtime)
CACHE="$HERE/.cache"
mkdir -p "$CACHE"
if [ ! -x "$CACHE/ffmpeg-static" ]; then
    echo "fetching static ffmpeg..."
    curl -sL "https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-amd64-static.tar.xz" \
        -o "$CACHE/ffmpeg-static.tar.xz"
    tar -xJf "$CACHE/ffmpeg-static.tar.xz" -C "$CACHE" --wildcards "*/ffmpeg" --strip-components=1
    mv "$CACHE/ffmpeg" "$CACHE/ffmpeg-static"
    chmod +x "$CACHE/ffmpeg-static"
fi
cp "$CACHE/ffmpeg-static" "$WORK/build/files/bin/ffmpeg"

# x11 (no wayland: JUCE runs through X), pulse for audio, device=all for ALSA
# cards + MIDI controllers, network for OSC in/out, home for projects/samples
flatpak build-finish "$WORK/build" \
    --command=HIKIM \
    --socket=x11 --share=ipc \
    --socket=pulseaudio \
    --device=all \
    --share=network \
    --filesystem=home

flatpak build-export "$WORK/repo" "$WORK/build"
mkdir -p "$(dirname "$OUT")"
flatpak build-bundle "$WORK/repo" "$OUT" "$APP"
echo "bundle: $OUT"
echo "install: flatpak install --user -y --reinstall $OUT"
