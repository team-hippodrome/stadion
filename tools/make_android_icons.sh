#!/bin/bash
# Makes the Android app's adaptive launcher icon (Android 8 and newer: the launcher masks it to its own shape, so a
# plain picture shows shrunk inside that shape): the two layers drawn by tools/macos_icon.swift, at 108 dp in every
# density, as android/app/src/main/res/mipmap-*/ic_launcher_background.png and ic_launcher_foreground.png
# (mipmap-anydpi-v26/ic_launcher.xml puts them together; the lettering is also the themed icon's monochrome layer).
# The plain ic_launcher.png files stay for older Android versions. Needs macOS (swift, sips).
# Usage: tools/make_android_icons.sh
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
swift tools/macos_icon.swift --android-background "$TMP/background.png"
swift tools/macos_icon.swift --android-foreground "$TMP/foreground.png"
RES=android/app/src/main/res
for d in mdpi:108 hdpi:162 xhdpi:216 xxhdpi:324 xxxhdpi:432; do
    dir=$RES/mipmap-${d%%:*}
    size=${d##*:}
    for layer in background foreground; do
        sips -z "$size" "$size" "$TMP/$layer.png" --out "$dir/ic_launcher_$layer.png" >/dev/null
    done
done
echo "wrote ic_launcher_background.png and ic_launcher_foreground.png in $RES/mipmap-*"
