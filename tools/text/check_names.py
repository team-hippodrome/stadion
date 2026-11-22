#!/usr/bin/env python3
"""Checks the official names in translation/*.csv against PokeAPI (TRANSLATION.md: in-universe terminology follows
TPCI's localizations, queried through PokeAPI).

    python3 tools/text/check_names.py --download                  # PokeAPI's name CSVs -> translation/pokeapi/
    python3 tools/text/check_names.py --rom <source ROM> ...       # check those sources' files

Everything is translated literally from the source ROM's wording (Julius, 2026-11-22: German HYPERBALL is "Hyper
Ball", not "Ultra Ball") except species, move, trainer/character and city names, which take the official names (trainer
classes are literal). This checks species, moves and Kanto's city names (translation/names/cities.csv; PokeAPI has no trainer names): a row whose source text is the official name of one in the source's language
must have, in each target column, its official name in the target language. --kinds item type checks those too (for
reference only: they are meant to be literal). Names are
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
         "item": ("item_names.csv", "item_id"), "type": ("type_names.csv", "type_id"),
         "city": ("location_names.csv", "location_id")}
# Kanto's city names: translation/names/cities.csv, the hub's one table for every source (en, de and fr are
# PokeAPI's location_names.csv and checked against it; PokeAPI has no es, it, ja or ko Kanto names, so those are
# the games' names written down by hand, and pt is English). A target cell naming a city must contain its name there,
# without its City/Town/Island word, so GIMNASIO DE CIUDAD PLATEADA, PLUMBEOPOLI and ニビジム all pass.
CITY_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "translation", "names", "cities.csv")
# The gyms' rows (T19_000-007, the same in the five western ROMs): Spanish GIM. PLATEADO does not spell its city.
GYM_ROWS = {"T19_000": "PEWTER", "T19_001": "CERULEAN", "T19_002": "VERMILION", "T19_003": "CELADON",
            "T19_004": "FUCHSIA", "T19_005": "SAFFRON", "T19_006": "CINNABAR", "T19_007": "VIRIDIAN"}
CITY_SUFFIXES = ("city", "town", "island", "insel", "ile", "シティ", "タウン", "じま", "島", "시티", "타운", "마을", "섬")
CITY_PREFIXES = ("ciudad", "pueblo", "isla", "isola", "bourg")
# PokeAPI local_language_id per table language: 1 ja-hrkt (kana), 11 ja (kanji), 3 ko, 5 fr, 6 de, 7 es, 14 es-419,
# 8 it, 9 en, 13 pt-br. Spanish accepts Spain's and Latin America's names (the dialect rules).
LANG_IDS = {"en": [9], "es": [7, 14], "jp": [11, 1], "kanji": [11, 1], "pt": [13], "de": [6], "ko": [3], "fr": [5],
            "it": [8]}
SOURCE_IDS = {"en": [9], "es": [7, 14], "jp": [1, 11], "de": [6], "fr": [5], "it": [8]}
DIR = os.path.join(T.TABLE_DIR, "pokeapi")
# The western ROMs' name tables (textdata; the same in all five): only their rows are checked, so a Pokédex category
# or a menu word that happens to spell a name is not. The Japanese ROM has none: its rows are matched by text.
NAME_TABLES = {"T24": "species", "T25": "move", "T26": "type", "T27": "item"}
# Stadium 1 has the first 151 species and 165 moves: a later one's name is a false match (ブルー is Blue the trainer,
# not Snubbull).
LAST_ID = {"species": 151, "move": 165}
# The Japanese ROM's move-name list (ROM offsets): its species table also holds the Pokédex categories, some of which
# spell a move (どくばり, ねんりき, かいりき ...), so only rows here count as moves.
JP_MOVES = (0x80AC0, 0x81370)


LIGATURES = str.maketrans({"Œ": "OE", "œ": "oe", "Æ": "AE", "æ": "ae", "ß": "ss"})


def norm(s):
    s = unicodedata.normalize("NFKD", s.replace("\\n", " ").replace("\n", " ").translate(LIGATURES))
    s = "".join(c for c in s if not unicodedata.combining(c))
    return "".join(c for c in s.casefold() if c.isalnum() or c in "♀♂")


def city_core(name):
    n = norm(name)
    for w in map(norm, CITY_SUFFIXES):
        if n.endswith(w) and len(n) >= len(w) + 2:
            n = n[: -len(w)]
            break
    for w in map(norm, CITY_PREFIXES):
        if n.startswith(w) and len(n) >= len(w) + 4:
            return n[len(w):]
    return n


def load_cities(names):
    """cities.csv as {city: {lang: name}}; exits when its en/de/fr names are not PokeAPI's."""
    with open(CITY_TABLE, encoding="utf-8", newline="") as fh:
        rows = list(csv.DictReader(fh))
    bad = []
    for r in rows:
        api = names.get(("city", int(r["pokeapi_id"])), {})
        for col in ("en", "de", "fr"):
            if norm(r[col]) not in {norm(n) for n in api.get(LANG_IDS[col][0], [])}:
                bad.append(f"{r['id']} [{col}] {r[col]!r} is not PokeAPI's {api.get(LANG_IDS[col][0])}")
    if bad:
        sys.exit("translation/names/cities.csv: " + "; ".join(bad))
    return {r["id"]: r for r in rows}


def words(s):
    """`s` normalized like norm() but with its words kept apart: " pewter gym " (Latin and Hangul text only)."""
    s = unicodedata.normalize("NFKD", s.replace("\\n", " ").translate(LIGATURES))
    s = "".join(c for c in s if not unicodedata.combining(c)).casefold()
    return " " + " ".join("".join(c if c.isalnum() else " " for c in s).split()) + " "


def names_city(text, name, lang):
    """Whether a source text names the city `name` (in the source's language): Japanese by the core anywhere (no
    spaces), Spanish by the whole name (the core of Ciudad Verde is a common word), the others by the core as whole
    words (French ARGENT A is not Argenta)."""
    if lang == "jp":
        return city_core(name) in norm(text)
    w = words(name)
    if lang != "es":
        for x in CITY_SUFFIXES:
            if w.endswith(" " + x + " ") and len(w) > len(x) + 4:
                w = w[: -len(x) - 1]
        for x in CITY_PREFIXES:
            if w.startswith(" " + x + " ") and len(w) > len(x) + 4:
                w = w[len(x) + 1:]
    return w in words(text)


def load():
    names = defaultdict(lambda: defaultdict(list))  # (kind, id) -> language id -> names
    with open(CITY_TABLE, encoding="utf-8", newline="") as fh:
        cities = {int(r["pokeapi_id"]) for r in csv.DictReader(fh)}
    for kind, (f, key) in KINDS.items():
        path = os.path.join(DIR, f)
        if not os.path.exists(path):
            sys.exit(f"{path} missing: run with --download first")
        with open(path, encoding="utf-8", newline="") as fh:
            for row in csv.DictReader(fh):
                if kind == "city" and int(row[key]) not in cities:
                    continue
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
    ap.add_argument("--kinds", nargs="*", default=["species", "move", "city"], choices=sorted(KINDS))
    a = ap.parse_args()
    if a.download:
        os.makedirs(DIR, exist_ok=True)
        for f, _ in KINDS.values():
            urllib.request.urlretrieve(BASE + f, os.path.join(DIR, f))
            print("downloaded", f)
        if not a.rom:
            return
    names = load()
    city_names = load_cities(names) if "city" in a.kinds else {}
    problems, checked = [], 0
    for rom_path in a.rom:
        src, enc, rows = T.rom_rows(open(rom_path, "rb").read())
        index = defaultdict(set)  # normalized source-language name -> things
        for thing, langs in names.items():
            if thing[0] not in a.kinds or thing[0] == "city" or thing[1] > LAST_ID.get(thing[0], 1 << 30):
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
                if "city" in a.kinds:
                    named = {GYM_ROWS[d["id"]]} if src != "jp" and d["id"] in GYM_ROWS else set()
                    if text:
                        named |= {c for c, r in city_names.items() if names_city(text, r[src], src)}
                    for col in head[3:-1]:
                        for c in sorted(named):
                            if not d[col]:
                                continue
                            checked += 1
                            want = city_names[c]["jp" if col == "kanji" else col]
                            if city_core(want) not in norm(d[col]):
                                problems.append(f"{fname}:{n} {d['id']} [{col}] city: {d[col]!r} should name "
                                                + want)
                text = source.get(d["id"])
                things = index.get(norm(text)) if text else None
                if things and src != "jp":
                    kind = NAME_TABLES.get(d["id"][:3])
                    things = {t for t in things if t[0] == kind}
                elif things:
                    first = int(d["address"].split()[0], 16) if d["address"] else -1
                    if not JP_MOVES[0] <= first < JP_MOVES[1]:
                        things = {t for t in things if t[0] != "move"}
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
