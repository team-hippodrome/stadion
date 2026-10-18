#!/usr/bin/env python3 -I
"""Makes the Linux and Windows icons from the macOS one (tools/macos/AppIcon.png, drawn by tools/macos_icon.swift):
the rounded tile without the macOS margin, as tools/launcher/assets/pokestadium.png (256 px, AppImage) and
tools/launcher/assets/pokestadium.ico (16-256 px, PNG-compressed entries; the Windows executable). Needs macOS (sips).

Usage: tools/make_icons.py
"""
import os, struct, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "macos", "AppIcon.png")
ASSETS = os.path.join(ROOT, "tools", "launcher", "assets")
TILE = 824  # the tile inside the 1024 px macOS canvas (100 px margin)


def sips(*args):
    subprocess.run(["sips", *args], check=True, capture_output=True)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tile = os.path.join(tmp, "tile.png")
        sips("--cropToHeightWidth", str(TILE), str(TILE), SRC, "--out", tile)  # centred crop
        pngs = {}
        for s in (16, 24, 32, 48, 64, 128, 256):
            p = os.path.join(tmp, f"{s}.png")
            sips("-z", str(s), str(s), tile, "--out", p)
            pngs[s] = open(p, "rb").read()
        with open(os.path.join(ASSETS, "pokestadium.png"), "wb") as f:
            f.write(pngs[256])
        # ICO: header, one directory entry per size (0 means 256), then the PNG data
        sizes = sorted(pngs)
        offset = 6 + 16 * len(sizes)
        head, data = struct.pack("<HHH", 0, 1, len(sizes)), b""
        for s in sizes:
            d = pngs[s]
            head += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(d), offset + len(data))
            data += d
        with open(os.path.join(ASSETS, "pokestadium.ico"), "wb") as f:
            f.write(head + data)
    print("wrote pokestadium.png and pokestadium.ico in", ASSETS)


if __name__ == "__main__":
    main()
