#!/usr/bin/env python3 -I
"""Converts PPM (P6) files to PNG so they can be viewed. Usage: ppm2png.py a.ppm [b.ppm ...]"""
import sys, zlib, struct
def convert(src, dst):
    d = open(src, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pix = parts[3]
    raw = b"".join(b"\x00" + pix[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, c): return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    open(dst, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
for a in sys.argv[1:]:
    if a.endswith(".ppm"): convert(a, a[:-4] + ".png")
