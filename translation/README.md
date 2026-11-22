# Translations

stadion's text tables, following the translation architecture in
[hippodrome-utils' TRANSLATION.md](https://github.com/team-hippodrome/hippodrome-utils/blob/main/docs/specs/TRANSLATION.md).
Nothing here contains the original game text: only ids, md5s, ROM offsets and translations.

## One file per source ROM

Stadium 1 was localized into six official languages, each its own ROM. Each official ROM is a **source**, and each
source is translated into every other language from its own text only. No file ever takes a cell from another
source's file or from another official ROM: an English translation of the German ROM is made from the German text,
not copied from the US ROM. That is how the official localizations stay side by side instead of blending.

| File | Source ROM | md5 of the .z64 | Target columns |
| --- | --- | --- | --- |
| `en.csv` | USA 1.0 (the port's base ROM) | `ed1378bc12115f71209a77844965ba50` | es, jp, pt, de, ko, fr, it |
| `es.csv` | Spain (NPOS) | `d14a499bc4e324974eae3e42dec58625` | en, jp, pt, de, ko, fr, it |
| `fr.csv` | France (NPOF) | `0e85a098d0f0e8a7eb572a69612a6873` | en, es, jp, pt, de, ko, it |
| `de.csv` | Germany (NPOD) | `24be2ccb0dea0755908c02bf67e22fe5` | en, es, jp, pt, ko, fr, it |
| `it.csv` | Italy (NPOI) | `a81321759af38beb30a40fdaca2ea22a` | en, es, jp, pt, de, ko, fr |
| `jp.csv` | Japan, ポケモンスタジアム２ (CP2J) | `2449bb712a64e3363a6cbd56f5adeda5` | en, es, pt, de, ko, fr, it |
| `kanji.csv` | Japan: its kana text written with kanji | | kanji |
| `<src>_localized.csv` | the same source's COM Pokémon nicknames, localized | | as `<src>.csv` |

The European English ROMs and the US revisions are not separate sources: they share most strings with USA 1.0 and
are matched by md5.

## Format

UTF-8, RFC 4180 quoting, one header row:

```
id,md5,address,<targets>,note
```

The targets are `en,es,jp,pt,de,ko,fr,it` in that order with the source's own code left out (the source text is the
ROM). `kanji.csv` is `id,md5,address,kanji,note` with the same ids as `jp.csv`.

- `id` names the string.
  - The western ROMs keep almost all text in the `textdata` archive (`Text_GetStringTable`/`Text_GetString` in
    pokestadium's `src/text_system.c`): 46 string tables in the English ROM, 47 in the FIGS ROMs (one extra table and
    a few extra strings). The tables line up across all five, so a string is `T<table, 2 hex>_<index, 3 decimal>`,
    such as `T11_004`, and the same id means the same string in every western file.
  - Text outside textdata, and all of the Japanese ROM's text (it has no textdata archive; its strings are EUC-JP in
    code and data), is `ROM_<8 hex ROM offset of its first copy>`.
  - COM Pokémon nicknames are `<TRAINER>_<SPECIES>`, such as `BURGLAR_ARCANINE`, in both files.
- `md5` is the lowercase hex md5 of the string's raw bytes in the source ROM (latin-1 in the western ROMs, EUC-JP in
  the Japanese one), up to but not including the NUL. It is the fallback key.
- `address` lists the ROM file offsets of the string's bytes in the source ROM, 8 uppercase hex digits, space
  separated (every copy, for the Japanese scan). A textdata string's offset is that of its first byte inside the
  archive.
- The language cells are the translations. A line break is a real newline in a quoted cell; a non-printing byte is
  `\xNN`. Keep the `#NN` text tokens (`Text_SubstituteTokens`: `#01`–`#09` numbers, `#20`–`#41` names) and printf
  conversions (`%s`, `%d` …) in the same order; if a language needs another order, use positional forms
  (`%2$s`) and say so in `note`. An empty cell falls through to the source text.
- The western ROMs' font draws byte 0xBE as ♀ and 0xA9 as ♂ (latin-1 ¾ and ©), in all five ROMs; the dumps show
  them as ♀ and ♂, and cells use ♀ and ♂ too. 0xA5 (¥) is the Pokédollar sign.
- The `jp` column (in every source file) is Japanese written with kanji, not the kana-only style of the original ROM.
- Leave overflowing lines to the human pass, and leave Pokédex heights and weights as they are: the units (PAL
  English is probably metric) will be handled in the ROM data separately.
- `note` is context for translators. It never holds the source text.
- Rows are sorted: `T` ids in table and index order, then `ROM_` ids by offset, then named rows.

### Nicknames

The standard files hold COM nicknames transliterated. `<src>_localized.csv` holds them localized: each trainer's
gimmick (a prefix, a suffix, a transformation) applied to the species' native name in the target language, as in
the spec's `BURGLAR_ARCANINE` example. In-universe terms follow TPCI's official localizations (PokeAPI), with the
spec's dialect fallbacks.

## Tools

```
python3 tools/text/extract_text.py <rom.z64>               # translation/rom-text/<src>.csv: your ROM's text
python3 tools/text/extract_text.py <rom.z64> --skeleton    # add missing id/md5/address rows to translation/<src>.csv
python3 tools/text/check_tables.py                         # format
python3 tools/text/check_tables.py --rom <rom.z64> ...     # also md5, address and tokens against those ROMs
```

```
python3 tools/text/check_names.py --download                # PokeAPI's name tables -> translation/pokeapi/
python3 tools/text/check_names.py --rom <rom.z64> ...       # official names
```

`check_names.py` checks every species, move, type and item name (the western ROMs' tables T24, T25, T26, T27; the
Japanese ROM's rows whose text is such a name) against TPCI's official name in each target language from PokeAPI,
ignoring case, accents, spaces and punctuation. Spanish takes Spain's or Latin America's names; Portuguese species
names are the English ones; targets PokeAPI has no names for are not checked.

`translation/rom-text/` is gitignored: it holds your ROM's text, which must never be committed. Give
`check_tables.py` several official ROMs and it also reports a cell that is identical to another official ROM's own
text for the same id (longer than a few words): a sign that a source borrowed an official translation.

The Japanese scan is a heuristic (aligned, NUL-terminated EUC-JP runs that are mostly two-byte characters); add the
strings it misses by hand with `ROM_` ids.

## The port's own strings

`port.csv` (`id,en,es,jp,pt,de,ko,fr,it,note`) holds text the port draws itself, such as the first-boot language prompt
(`src/port/language_prompt.cpp`), in every language. It is not ROM text. `{rom}` and `{locale}` stand for the
`LANGUAGE_NAME_*` of the ROM's and the system's language in the cell's language; English `{a_rom}` adds "a" or "an".

## Not done yet

- The port does not read these tables at run time yet, and only boots the USA 1.0 ROM.
- Strings outside textdata in the western ROMs (code data, compressed COM trainer tables) are not listed by the
  tool; they are added by hand with `ROM_` ids.
