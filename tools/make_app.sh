#!/bin/bash
# Builds "Pokemon Stadium.app" (a double-clickable macOS launcher for the port) in the project folder.
# Usage: tools/make_app.sh [title screenshot .png for the icon]
# The app opens a launcher (styled after PokemonStadiumRecomp's): the Stadium ROM + save and a Game Boy cartridge + save
# per player's Transfer Pak, remembered in the shared JSON config; PLAY starts
# build/pokestadium. Auto-play skips the launcher next time (hold Option while opening the app to see it again).
# Config and saves live beside the .app (or in SHIP_HOME); the last run's log is saves/last_run.log.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
APP="$HERE/Pokemon Stadium.app"
ICON_SRC="${1:-$HERE/build/tour/r3_menus/00_title.png}"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cat > "$APP/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Pokemon Stadium</string>
    <key>CFBundleDisplayName</key><string>Pokémon Stadium</string>
    <key>CFBundleIdentifier</key><string>local.pokestadium-port.launcher</string>
    <key>CFBundleVersion</key><string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleExecutable</key><string>launcher</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

# launcher window (tools/launcher/Launcher.swift): pick ROM, save, GB cartridge and GB save, then Play
printf '%s\n' "$HERE" > "$APP/Contents/Resources/project-path.txt"
cp "$HERE"/tools/launcher/assets/*.png "$APP/Contents/Resources/"  # cartridge pictures (from PokemonStadiumRecomp)
xcrun swiftc -parse-as-library -O -target arm64-apple-macos13 -o "$APP/Contents/MacOS/launcher" \
    "$HERE/tools/launcher/PortableLauncherStore.swift" "$HERE/tools/launcher/Launcher.swift"

# icon: the title screen, cropped square
if [ -f "$ICON_SRC" ]; then
    TMP="$(mktemp -d /tmp/pokestadium_icon.XXXXXX)"
    python3 -I - "$ICON_SRC" "$TMP/square.png" <<'PY'
import struct, sys, zlib
def read_png(p):
    d = open(p, "rb").read(); pos, idat, w, h = 8, b"", 0, 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8]); c = d[pos + 8:pos + 8 + n]
        if t == b"IHDR": w, h = struct.unpack(">II", c[:8])
        elif t == b"IDAT": idat += c
        pos += 12 + n
    raw = zlib.decompress(idat); stride = w * 3; out = bytearray(); prev = bytearray(stride); i = 0
    for y in range(h):
        f = raw[i]; i += 1; line = bytearray(raw[i:i + stride]); i += stride
        for x in range(stride):
            a = line[x - 3] if x >= 3 else 0; b = prev[x]; c = prev[x - 3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line; prev = line
    return w, h, out
full_w, h, px = read_png(sys.argv[1])
w = min(full_w, h * 4 // 3)       # tour pictures are port|ares side by side: keep the port half
side = min(w, h); x0 = (w - side) // 2
rows = []
for y in range(side):
    start = (y * full_w + x0) * 3
    rows.append(b"\x00" + bytes(px[start:start + side * 3]))
def chunk(t, c): return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", side, side, 8, 2, 0, 0, 0)) +
                               chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
PY
    mkdir -p "$TMP/AppIcon.iconset"
    for s in 16 32 64 128 256 512; do
        sips -z $s $s "$TMP/square.png" --out "$TMP/AppIcon.iconset/icon_${s}x${s}.png" >/dev/null
        d=$((s * 2)); sips -z $d $d "$TMP/square.png" --out "$TMP/AppIcon.iconset/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$TMP/AppIcon.iconset" -o "$APP/Contents/Resources/AppIcon.icns"
    rm -r "$TMP/AppIcon.iconset"; rm "$TMP/square.png"; rmdir "$TMP"
fi
touch "$APP"
echo "built: $APP"
