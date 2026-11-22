#!/usr/bin/env python3
"""Checks translation/*.csv (see translation/README.md).

    python3 tools/text/check_tables.py                  # format only
    python3 tools/text/check_tables.py --rom A.z64 ...  # also md5/address against each given source ROM,
                                                        # tokens kept, and cross-source contamination

With several ROMs, a cell in <src>.csv that is identical to another official ROM's own text for the same id (and
longer than a name) is reported: each source is translated from its own ROM only. Reports are leads to review
(set phrases can match honestly); --strict makes them fail the check.
"""
import argparse
import glob
import hashlib
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tables as T


def tokens(s):
    # A positional %2$s counts as %s, so a translation may reorder the arguments.
    return Counter(re.sub(r"^%[0-9]+\$", "%", t) for t in T.TOKEN_RE.findall(s.replace("%%", "")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", nargs="*", default=[])
    ap.add_argument("--strict", action="store_true", help="contamination warnings fail the check too")
    a = ap.parse_args()
    errors, warnings = [], []
    dumps = {}  # src -> id -> (md5, address, text)
    for path in a.rom:
        src, enc, rows = T.rom_rows(open(path, "rb").read())
        dumps[src] = {r["id"]: (r["md5"], r["address"], T.escape(r["raw"], enc)) for r in rows}

    files = sorted(glob.glob(os.path.join(T.TABLE_DIR, "*.csv")))
    for path in files:
        name = os.path.relpath(path, T.ROOT)
        head = T.header(path)
        if head is None:
            errors.append(f"{name}: not a known table name (<src>.csv, <src>_localized.csv, kanji.csv)")
            continue
        try:
            h, dicts, raw_rows = T.read_table(path)
        except UnicodeDecodeError:
            errors.append(f"{name}: not UTF-8"); continue
        if h != head:
            errors.append(f"{name}: header {','.join(h)}\n  expected {','.join(head)}"); continue
        src = T.source_of(path)
        seen, prev = set(), None
        for n, (d, rr) in enumerate(zip(dicts, raw_rows), start=2):
            where = f"{name}:{n} {d.get('id', '')}"
            if len(rr) != len(head):
                errors.append(f"{where}: {len(rr)} cells, expected {len(head)}"); continue
            rid = d["id"]
            if not T.ID_RE.match(rid):
                errors.append(f"{where}: bad id")
            if rid in seen:
                errors.append(f"{where}: duplicate id")
            seen.add(rid)
            key = T.sort_key(rid)
            if prev is not None and key < prev:
                errors.append(f"{where}: out of order (T ids, then ROM_ ids, then named rows)")
            prev = key
            if not T.MD5_RE.match(d["md5"]):
                errors.append(f"{where}: md5 must be 32 lowercase hex digits")
            if not T.ADDR_RE.match(d["address"]):
                errors.append(f"{where}: address must be 8 uppercase hex digits, space separated")
            ref = dumps.get(src, {}).get(rid)
            if ref:
                if d["md5"] != ref[0]:
                    errors.append(f"{where}: md5 does not match the {src} ROM")
                if d["address"] != ref[1]:
                    errors.append(f"{where}: address does not match the {src} ROM ({ref[1]})")
                want = tokens(ref[2])
                for col in head[3:-1]:
                    cell = d[col]
                    if cell and tokens(cell) != want:
                        errors.append(f"{where} [{col}]: tokens {sorted(tokens(cell).elements())} "
                                      f"!= source {sorted(want.elements())}")
            for col in head[3:-1]:
                cell = d[col]
                other = dumps.get(col, {}).get(rid) if rid.startswith("T") else None
                if cell and other and len(cell.split()) > 3 and cell == other[2]:
                    warnings.append(f"{where} [{col}]: identical to the official {col} ROM's text")
    for e in errors:
        print("error:", e)
    for w in warnings:
        print("contamination:", w)
    print(f"{len(files)} tables, {len(errors)} errors, {len(warnings)} contamination warnings")
    # Contamination is a lead to review, not proof: set phrases (TM cards, official names, short battle lines) can
    # match an official translation honestly.
    sys.exit(1 if errors or (a.strict and warnings) else 0)


if __name__ == "__main__":
    main()
