#!/usr/bin/env python3 -I
"""Pairs each port picture with the most similar ares picture (for scenes whose timing differs, e.g. the intro).

Usage: tools/match_frames.py <port dir> <ares dir> <out dir>
Reads stop_*.ppm (port) and f_*.ppm (ares), compares 32x24 thumbnails, writes <out>/<port frame>.png side by side
(port | best ares match) and prints the pairing with its difference score.
"""
import os, struct, sys, zlib


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def thumb(w, h, rgb, tw=32, th=24):
    out = []
    for y in range(th):
        for x in range(tw):
            sx, sy = x * w // tw + w // tw // 2, y * h // th + h // th // 2
            i = (sy * w + sx) * 3
            out.extend(rgb[i:i + 3])
    return out


def scale_to(w, h, rgb, tw, th):
    out = bytearray(tw * th * 3)
    for y in range(th):
        sy = y * h // th
        row = sy * w
        for x in range(tw):
            i = (row + x * w // tw) * 3
            o = (y * tw + x) * 3
            out[o:o + 3] = rgb[i:i + 3]
    return bytes(out)


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    pdir, adir, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    ares = []
    for f in sorted(os.listdir(adir)):
        if f.startswith("f_") and f.endswith(".ppm"):
            w, h, rgb = read_ppm(os.path.join(adir, f))
            ares.append((f, thumb(w, h, rgb)))
    for f in sorted(os.listdir(pdir)):
        if not (f.startswith("stop_") and f.endswith(".ppm")):
            continue
        w, h, rgb = read_ppm(os.path.join(pdir, f))
        t = thumb(w, h, rgb)
        best = min(ares, key=lambda a: sum(abs(x - y) for x, y in zip(t, a[0 + 1])))
        score = sum(abs(x - y) for x, y in zip(t, best[1])) / len(t)
        aw, ah, argb = read_ppm(os.path.join(adir, best[0]))
        left = scale_to(w, h, rgb, 640, 480)
        right = scale_to(aw, ah, argb, 640, 480)
        canvas = bytearray(1280 * 480 * 3)
        for y in range(480):
            canvas[y * 3840:y * 3840 + 1920] = left[y * 1920:(y + 1) * 1920]
            canvas[y * 3840 + 1920:(y + 1) * 3840] = right[y * 1920:(y + 1) * 1920]
        write_png(os.path.join(out, f[5:10] + ".png"), 1280, 480, bytes(canvas))
        print("%s -> %s  diff %.1f" % (f, best[0], score))


if __name__ == "__main__":
    main()
