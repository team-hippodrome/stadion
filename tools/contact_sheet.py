#!/usr/bin/env python3 -I
"""Tiles PPM frames (downscaled to 160x120) into one PNG. Usage: contact_sheet.py out.png cols a.ppm b.ppm ..."""
import sys, zlib, struct
def load(p):
    d = open(p, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]
def main():
    out, cols, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
    if not files: return
    tw, th = 160, 120
    rows = (len(files) + cols - 1) // cols
    W, H = cols * tw, rows * th
    img = bytearray(W * H * 3)
    for i, f in enumerate(files):
        w, h, pix = load(f)
        ox, oy = (i % cols) * tw, (i // cols) * th
        fx, fy = max(1, w // tw), max(1, h // th)   # works for any frame size (Retina frames are 2x)
        for y in range(th):
            sy = y * fy
            if sy >= h: break
            row = pix[sy * w * 3:(sy + 1) * w * 3]
            dst = ((oy + y) * W + ox) * 3
            for x in range(min(tw, w // fx)):
                img[dst + x * 3:dst + x * 3 + 3] = row[x * fx * 3:x * fx * 3 + 3]
        # 1px separator
        for y in range(th):
            j = ((oy + y) * W + ox + tw - 1) * 3
            img[j:j + 3] = b"\x40\x40\x40"
    raw = b"".join(b"\x00" + bytes(img[y * W * 3:(y + 1) * W * 3]) for y in range(H))
    def chunk(t, c): return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    open(out, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
main()
