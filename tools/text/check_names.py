#!/usr/bin/env python3
"""Checks the official names in translation/*.csv against PokeAPI (TRANSLATION.md: in-universe terminology follows
TPCI's localizations, queried through PokeAPI).

    python3 tools/text/check_names.py --download                  # PokeAPI's name CSVs -> translation/pokeapi/
    python3 tools/text/check_names.py --rom <source ROM> ...       # check those sources' files

Everything is translated literally from the source ROM's wording (Julius, 2026-11-22: even HYPERBALL from German is
"Hyper Ball", not "Ultra Ball"), so only Pokémon species names are held to the official names: a row whose source
text is the official name of a species in the source's language must have, in each target column, that species'
official name in the target language. --kinds also checks moves, items or types (for reference only). Names are
compared ignoring case, accents, spaces and punctuation, so "THUNDERSHOCK" matches "Thunder Shock". Targets with no
PokeAPI data are not checked (Portuguese moves and items; Portuguese species names are the English ones).
translation/pokeapi/ is gitignored. PokeAPI's data is BSD-3-Clause (https://github.com/PokeAPI/pokeapi).
"""
import argparse
import csv
import os
import sys
import unicodedata
import urllib.request
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tables as T

BASE = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/"
KINDS = {"species": ("pokemon_species_names.csv", "pokemon_species_id"), "move": ("move_names.csv", "move_id"),
         "item": ("item_names.csv", "item_id"), "type": ("type_names.csv", "type_id")}
# PokeAPI local_language_id per table language: 1 ja-hrkt (kana), 11 ja (kanji), 3 ko, 5 fr, 6 de, 7 es, 14 es-419,
# 8 it, 9 en, 13 pt-br. Spanish accepts Spain's and Latin America's names (the dialect rules).
LANG_IDS = {"en": [9], "es": [7, 14], "jp": [11, 1], "kanji": [11, 1], "pt": [13], "de": [6], "ko": [3], "fr": [5],
            "it": [8]}
SOURCE_IDS = {"en": [9], "es": [7, 14], "jp": [1, 11], "de": [6], "fr": [5], "it": [8]}
DIR = os.path.join(T.TABLE_DIR, "pokeapi")
# The western ROMs' name tables (textdata; the same in all five): only their rows are checked, so a Pokédex category
# or a menu word that happens to spell a name is not. The Japanese ROM has none: its rows are matched by text.
NAME_TABLES = {"T24": "species", "T25": "move", "T26": "type", "T27": "item"}


def norm(s):
    s = unicodedata.normalize("NFKD", s.replace("\\n", " ").replace("\n", " "))
    s = "".join(c for c in s if not unicodedata.combining(c))
    return "".join(c for c in s.casefold() if c.isalnum() or c in "♀♂")


def load():
    names = defaultdict(lambda: defaultdict(list))  # (kind, id) -> language id -> names
    for kind, (f, key) in KINDS.items():
        path = os.path.join(DIR, f)
        if not os.path.exists(path):
            sys.exit(f"{path} missing: run with --download first")
        with open(path, encoding="utf-8", newline="") as fh:
            for row in csv.DictReader(fh):
                names[(kind, int(row[key]))][int(row["local_language_id"])].append(row["name"])
    return names


def expected(names, thing, col):
    langs = names[thing]
    want = [n for i in LANG_IDS[col] for n in langs.get(i, [])]
    if not want and col == "pt" and thing[0] == "species":
        want = langs.get(9, [])  # Portuguese uses the English species names
    return want


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--download", action="store_true")
    ap.add_argument("--rom", nargs="*", default=[])
    ap.add_argument("--kinds", nargs="*", default=["species"], choices=sorted(KINDS))
    a = ap.parse_args()
    if a.download:
        os.makedirs(DIR, exist_ok=True)
        for f, _ in KINDS.values():
            urllib.request.urlretrieve(BASE + f, os.path.join(DIR, f))
            print("downloaded", f)
        if not a.rom:
            return
    names = load()
    problems, checked = [], 0
    for rom_path in a.rom:
        src, enc, rows = T.rom_rows(open(rom_path, "rb").read())
        index = defaultdict(set)  # normalized source-language name -> things
        for thing, langs in names.items():
            if thing[0] not in a.kinds:
                continue
            for i in SOURCE_IDS[src]:
                for n in langs.get(i, []):
                    index[norm(n)].add(thing)
        source = {r["id"]: T.escape(r["raw"], enc) for r in rows}
        for fname in (f"{src}.csv", "kanji.csv") if src == "jp" else (f"{src}.csv",):
            path = os.path.join(T.TABLE_DIR, fname)
            if not os.path.exists(path):
                continue
            head, dicts, _ = T.read_table(path)
            for n, d in enumerate(dicts, start=2):
                text = source.get(d["id"])
                things = index.get(norm(text)) if text else None
                if things and src != "jp":
                    kind = NAME_TABLES.get(d["id"][:3])
                    things = {t for t in things if t[0] == kind}
                if not things:
                    continue
                for col in head[3:-1]:
                    cell = d[col]
                    if not cell:
                        continue
                    want = [w for t in sorted(things) for w in expected(names, t, col)]
                    if not want:
                        continue
                    checked += 1
                    if norm(cell) not in {norm(w) for w in want}:
                        kinds = "/".join(sorted({k for k, _ in things}))
                        problems.append(f"{fname}:{n} {d['id']} [{col}] {kinds}: {cell!r}, official "
                                        + " or ".join(sorted(set(want))))
    for p in problems:
        print("name:", p)
    print(f"{checked} official-name cells checked, {len(problems)} differ from PokeAPI")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
