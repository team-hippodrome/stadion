#!/usr/bin/env python3 -I
"""Offline helpers to look at ROM assets: BinArchive tables, PERS-SZP (Yay0) decompression, FRAGMENT headers."""
import struct, sys
ROM = open("/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64", "rb").read()
be = lambda b, o: struct.unpack(">I", b[o:o+4])[0]
def yay0(d, o=0):
    size, lo, co = be(d, o+4), be(d, o+8), be(d, o+12)
    out = bytearray(); mp = o + 16; bits = 0; nb = 0
    while len(out) < size:
        if nb == 0:
            bits = be(d, mp); mp += 4; nb = 32
        if bits & 0x80000000:
            out.append(d[o + co]); co += 1
        else:
            v = struct.unpack(">H", d[o+lo:o+lo+2])[0]; lo += 2
            dist = (v & 0xFFF) + 1; n = v >> 12
            if n == 0: n = d[o + co] + 18; co += 1
            else: n += 2
            for _ in range(n): out.append(out[-dist])
        bits = (bits << 1) & 0xFFFFFFFF; nb -= 1
    return bytes(out[:size])
def archive(base):
    n = be(ROM, base + 12)
    return [(be(ROM, base+16+i*16), be(ROM, base+20+i*16)) for i in range(n)]
def load(base, i):
    off, size = archive(base)[i]
    d = ROM[base+off: base+off+size]
    if d[:8] == b"PERS-SZP":
        hs, d1, d2, npatch = be(d, 8), be(d, 12), be(d, 16), be(d, 20)
        out = bytearray(yay0(d, hs)); out += bytes(max(0, d2 - len(out)))
        for k in range(npatch):
            val, off2 = be(d, 24+k*8), be(d, 28+k*8)
            out[off2:off2+4] = struct.pack(">I", val)
        return bytes(out)
    return d
if __name__ == "__main__":
    base = int(sys.argv[1], 0); i = int(sys.argv[2])
    print("files:", len(archive(base)))
    b = load(base, i)
    print("len", len(b)); print(b[:0x80].hex(" ", 4))
    open(sys.argv[3] if len(sys.argv) > 3 else "/tmp/blob.bin", "wb").write(b)
