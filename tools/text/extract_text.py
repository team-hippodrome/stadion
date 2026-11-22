#!/usr/bin/env python3
"""Lists an official ROM's text for translating it (see translation/README.md).

    python3 tools/text/extract_text.py <rom.z64>              # translation/rom-text/<src>.csv (gitignored)
    python3 tools/text/extract_text.py <rom.z64> --skeleton   # also adds missing rows to translation/<src>.csv

The dump has id, md5, address and the ROM's own text: it stays on your machine (ROM text rule). --skeleton writes
only ids, md5s and addresses, keeps every existing row and cell, and sorts the rows.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tables as T


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--skeleton", action="store_true", help="add missing rows to translation/<src>.csv")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    src, enc, rows = T.rom_rows(rom)

    os.makedirs(T.DUMP_DIR, exist_ok=True)
    dump = os.path.join(T.DUMP_DIR, f"{src}.csv")
    for r in rows:
        r["text"] = T.escape(r["raw"], enc)
    T.write_table(dump, ["id", "md5", "address", "text"], rows)
    print(f"{src}: {len(rows)} strings -> {os.path.relpath(dump, T.ROOT)}")

    if a.skeleton:
        path = os.path.join(T.TABLE_DIR, f"{src}.csv")
        head = T.header(path)
        old = {}
        if os.path.exists(path):
            h, dicts, _ = T.read_table(path)
            if h != head:
                sys.exit(f"{path}: header is {h}, expected {head}")
            old = {d["id"]: d for d in dicts}
        merged = dict(old)
        for r in rows:
            cur = merged.setdefault(r["id"], {"id": r["id"]})
            cur["md5"], cur["address"] = r["md5"], r["address"]
        out = sorted(merged.values(), key=lambda d: T.sort_key(d["id"]))
        T.write_table(path, head, out)
        print(f"{src}: {len(out) - len(old)} rows added -> {os.path.relpath(path, T.ROOT)}")


if __name__ == "__main__":
    main()
