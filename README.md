# Pokémon Stadium (US) — native macOS port

A native port of Pokémon Stadium (N64, US) built from the decompilation on top of
[libultraship](https://github.com/Kenix3/libultraship) (Metal renderer), with the Transfer Pak and the GB Tower
(Game Boy emulation) working, including German Pokémon Yellow.

No game data is included. You need your own ROM of *Pokemon Stadium (USA)*
(SHA-1 `ed7bef5a306f88c0a6e96b15e71fee2ef32058f3`) and, for the Transfer Pak / GB Tower, your own Game Boy ROMs
and saves.

## Build

```bash
tools/setup.sh                      # submodules + the port's libultraship patch (patches/libultraship.patch)
cmake -S . -B build
cmake --build build -j
tools/make_app.sh                   # optional: "Pokemon Stadium.app" with the launcher
```

Run directly: `cd build && ./pokestadium "/path/to/Pokemon Stadium (USA).z64"`.
Useful environment variables (all in PORTING_NOTES.md): `PORT_FLASH_FILE` (Stadium save), `PORT_GB_ROM` /
`PORT_GB_SAVE` (Transfer Pak cartridge of player 1; `PORT_GB_ROM2..4` for the others).

## Layout

- `game/` — the decompiled game (C), adapted for 64-bit hosts
- `src/port/` — host layer: OS/threads, VI, controllers, Transfer Pak, flash saves, GB core
- `libultraship/` — submodule; the port's renderer changes are in `patches/libultraship.patch`
- `ghostship-ref/` — submodule (fonts used by `tools/make_o2r.py`)
- `tools/` — launcher, save editor, ares comparison tooling (`tools/tour.py`), replay helpers
- `replays/` — input recordings used as regression tests
- `PORTING_NOTES.md` — what was done, how it was checked against ares, debug switches
