"""Shared schema of translation/*.csv (see translation/README.md).

Each official ROM is a *source*. translation/<src>.csv holds that ROM's strings, keyed by id, md5 and address, and
their translations into every other language. Sources never share cells: each file is filled from its own ROM only.
"""
import csv
import hashlib
import io
import os
import re
import struct

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
TABLE_DIR = os.path.join(ROOT, "translation")
DUMP_DIR = os.path.join(TABLE_DIR, "rom-text")

# Every language a cell can be in, in column order. pt and ko have no official ROM (non-official languages).
LANGS = ["en", "es", "jp", "pt", "de", "ko", "fr", "it"]

# The official source ROMs: md5 of the .z64 -> (source code, textdata ROM offset or None, text encoding).
# Offsets are textdata_ROM_START in team-hippodrome/pokestadium's linker_scripts/<version>/symbol_addrs.txt.
# The Japanese ROM has no textdata archive: its text is EUC-JP strings in code and data.
SOURCES = {
    "ed1378bc12115f71209a77844965ba50": ("en", 0x783760, "latin-1"),  # USA 1.0, the port's base ROM
    "d14a499bc4e324974eae3e42dec58625": ("es", 0x7635C0, "latin-1"),  # Spain (NPOS)
    "0e85a098d0f0e8a7eb572a69612a6873": ("fr", 0x75FB10, "latin-1"),  # France (NPOF)
    "24be2ccb0dea0755908c02bf67e22fe5": ("de", 0x764DB0, "latin-1"),  # Germany (NPOD)
    "a81321759af38beb30a40fdaca2ea22a": ("it", 0x760610, "latin-1"),  # Italy (NPOI)
    "2449bb712a64e3363a6cbd56f5adeda5": ("jp", None, "euc-jp"),       # Japan (CP2J), ポケモンスタジアム２
}
SOURCE_CODES = [s for s, _, _ in SOURCES.values()]
ENCODING = {s: e for s, _, e in SOURCES.values()}

ID_RE = re.compile(r"^(T[0-9A-F]{2}_[0-9]{3}|ROM_[0-9A-F]{8}|[A-Z][A-Z0-9_]*)$")
MD5_RE = re.compile(r"^[0-9a-f]{32}$")
ADDR_RE = re.compile(r"^([0-9A-F]{8}( [0-9A-F]{8})*)?$")
# What a translation must keep: text tokens (#NN, see Text_SubstituteTokens) and printf conversions. No space
# flag: "100% sure" is a literal percent sign followed by text, not "% s".
TOKEN_RE = re.compile(r"#[0-9]{2}|%(?:[0-9]+\$)?[-+0#]*[0-9]*(?:\.[0-9]+)?[hl]*[diuxXocsfp%]")


def targets(src):
    return [l for l in LANGS if l != src]


def header(name):
    """The header a file must have, from its name: <src>.csv, <src>_localized.csv or kanji.csv."""
    base = os.path.basename(name)[:-4]
    if base == "kanji":
        return ["id", "md5", "address", "kanji", "note"]
    src = base[: -len("_localized")] if base.endswith("_localized") else base
    if src not in SOURCE_CODES:
        return None
    return ["id", "md5", "address"] + targets(src) + ["note"]


def source_of(name):
    base = os.path.basename(name)[:-4]
    return "jp" if base == "kanji" else base.split("_")[0]


def sort_key(row_id):
    if row_id.startswith("T") and re.match(r"^T[0-9A-F]{2}_[0-9]{3}$", row_id):
        return (0, row_id)
    if row_id.startswith("ROM_"):
        return (1, row_id)
    return (2, row_id)


def identify(rom):
    md5 = hashlib.md5(rom).hexdigest()
    if md5 not in SOURCES:
        raise SystemExit(f"not an official source ROM (md5 {md5}); see SOURCES in tools/text/tables.py. "
                         "Byte-swapped .n64/.v64 dumps must be converted to .z64 first.")
    return SOURCES[md5]


# Bytes the western ROMs' font draws as something other than their latin-1 character (same in all five ROMs):
# NIDORAN + 0xBE is Nidoran female, NIDORAN + 0xA9 Nidoran male. 0xA5 (¥) is the Pokédollar sign.
LATIN1_GLYPHS = {0xBE: "\u2640", 0xA9: "\u2642"}


def escape(raw, encoding):
    """Raw ROM bytes as cell text: decoded, '\\n' kept, other control bytes and undecodable bytes as \\xNN."""
    out, i = [], 0
    while i < len(raw):
        b = raw[i]
        if b == 0x0A or 0x20 <= b < 0x7F:
            out.append(chr(b)); i += 1; continue
        if encoding == "euc-jp" and b >= 0xA1 and i + 1 < len(raw):
            try:
                out.append(raw[i:i + 2].decode("euc-jp")); i += 2; continue
            except UnicodeDecodeError:
                pass
        if encoding == "latin-1" and b >= 0xA0:
            out.append(LATIN1_GLYPHS.get(b, chr(b))); i += 1; continue
        out.append(f"\\x{b:02X}"); i += 1
    return "".join(out)


def textdata_strings(rom, start):
    """(table, index, rom offset, raw bytes) for every string of the textdata BinArchive at `start`.

    The archive: 16-byte header [0][0][total size][file count], one 16-byte entry (offset, size, 0, 0) per file.
    Each file is a string table: u32 count, count u32 offsets relative to the file, NUL-terminated strings.
    """
    total, count = struct.unpack_from(">II", rom, start + 8)
    for t in range(count):
        off, size = struct.unpack_from(">II", rom, start + 16 + 16 * t)
        base = start + off
        if size < 4:
            continue
        n = struct.unpack_from(">I", rom, base)[0]
        for i, so in enumerate(struct.unpack_from(f">{n}I", rom, base + 4)):
            a = base + so
            yield t, i, a, rom[a:rom.index(b"\0", a)]


def eucjp_strings(rom, min_chars=2):
    """(rom offset, raw bytes) for each NUL-terminated, 4-byte aligned run that is valid EUC-JP and holds at least
    two two-byte characters (kana, symbols or JIS level 1-2 kanji) making up at least half of it. A heuristic: the Japanese thread refines it (and adds rows by hand) where it misses."""
    i, n = 0, len(rom)
    while i < n:
        if i % 4 or (i and rom[i - 1] != 0) or rom[i] == 0:
            i += 1; continue
        j, chars, wide, ok = i, 0, 0, True
        while j < n and rom[j] != 0:
            b = rom[j]
            if b == 0x0A or 0x20 <= b < 0x7F:
                j += 1
            elif (0xA1 <= b <= 0xA5 or 0xB0 <= b <= 0xF4) and j + 1 < n and 0xA1 <= rom[j + 1] <= 0xFE:
                j += 2; wide += 1
            elif b == 0x8E and j + 1 < n and 0xA1 <= rom[j + 1] <= 0xDF:
                j += 2; wide += 1
            else:
                ok = False; break
            chars += 1
            if chars > 400:
                ok = False; break
        # Mostly two-byte characters: rules out data that happens to look like a short string.
        if ok and j < n and wide >= min_chars and 2 * wide >= chars:
            try:
                rom[i:j].decode("euc-jp")
                yield i, rom[i:j]
            except UnicodeDecodeError:
                pass
            i = (j + 4) & ~3
        else:
            i += 4


def rom_rows(rom):
    """The source code and its rows: dicts with id, md5, address, raw."""
    src, start, enc = identify(rom)
    rows = []
    if start is not None:
        for t, i, a, raw in textdata_strings(rom, start):
            rows.append({"id": f"T{t:02X}_{i:03d}", "md5": hashlib.md5(raw).hexdigest(), "address": f"{a:08X}",
                         "raw": raw})
    else:
        by_bytes = {}
        for a, raw in eucjp_strings(rom):
            by_bytes.setdefault(raw, []).append(a)
        for raw, addrs in by_bytes.items():
            rows.append({"id": f"ROM_{addrs[0]:08X}", "md5": hashlib.md5(raw).hexdigest(),
                         "address": " ".join(f"{a:08X}" for a in addrs), "raw": raw})
    rows.sort(key=lambda r: sort_key(r["id"]))
    return src, enc, rows


def read_table(path):
    with open(path, encoding="utf-8", newline="") as f:
        r = list(csv.reader(f))
    return r[0], [dict(zip(r[0], row)) for row in r[1:]], r[1:]


def write_table(path, head, rows):
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(head)
    for row in rows:
        w.writerow([row.get(c, "") for c in head])
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(buf.getvalue())
