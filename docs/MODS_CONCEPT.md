# Mod support: concept

Status: proposal for the team to discuss. Nothing in it is implemented yet.

This document describes how the Pokémon Stadium port could support mods. The model is
[Lighthouse](https://github.com/HarbourMasters/Lighthouse), the Banjo-Kazooie port, which is also built on
libultraship. It was read on its `develop` branch (October 2026). Section 1 describes what Lighthouse and
libultraship already do. Section 2 explains why this port cannot copy that approach as is. Sections 3–9 describe the
proposal.

---

## 1. What exists

### 1.1 Lighthouse

**Base assets.** At first start, Lighthouse runs Torch on the user's ROM to produce `bk.o2r`, a zip archive of
converted resources such as models, sprites, dialog, maps, music and animations. Each converted type has a resource
factory (`src/port/Engine.cpp` → `RegisterResourceFactories`). Torch also writes a table of asset IDs to resource
paths as a Blob at `assets/aBKAssetTable`. The decomp's asset loader calls `ResourceMgr_LoadByAssetId(id)`, which
looks the ID up in that table and loads the resource through the ResourceManager
(`src/port/ResourceHelpers.cpp`). In other words, the game no longer reads the ROM at runtime.

**Mods are more `.o2r` archives in the virtual file system.** libultraship's `ArchiveManager` merges all mounted
archives into one VFS, and the archive added last wins for each path. A mod is an `.o2r` that contains some of the
same paths as `bk.o2r`. Lighthouse mounts, in this order (`GameEngine::FinishInit`):

1. the base ROM archives (`bk.o2r`, plus `bkpal.o2r`/`bkjp.o2r` for other regions);
2. the enabled mod `.o2r` files under `mods/`, in the user's order (`UpdateModFiles(true)`);
3. any *loose folder* directly under `mods/`, mounted as a `FolderArchive` (`LoadLooseModDirectories`), which is
   convenient while authoring a mod;
4. language packs from `mods/~lang/` and `mods/~lang/<hack>/` (`LoadLanguagePacks`).

**Folder conventions** under `mods/`:

| Folder | Meaning |
|---|---|
| `mods/*.o2r` | ordinary ("base") mods |
| `mods/~romhacks/*.o2r` | romhack overlays: an `.o2r` that contains `assets/aGameConfig`. Only one can be active |
| `mods/<hackname>/` | mods that only load while that romhack is active ("scoped") |
| `mods/~shared/` | mods that load with or without a romhack |
| `mods/~lang/` | language packs. They are not listed in the mod menu |

**Load order and conflicts.** The enabled and disabled lists are stored as two CVars, `EnabledMods` and
`DisabledMods`, each a `|`-separated list (`|` is not allowed in NTFS file names). The ImGui *Mods* window
(`src/port/UI/LighthouseModMenuWindow.cpp`) has two columns, Enabled and Disabled. The user can drag to reorder or use
up/down arrows, then click **Apply & Restart**, because mods are not reloaded at runtime. The help text says
*"Mod priority is top to bottom. They override mods listed below them"*. However, the load loop calls `AddArchive` in
list order, and in libultraship the last archive added wins. If we copy this design, we should define the direction
explicitly and test it. Conflicts are resolved per file: the higher-priority archive simply wins. The only conflict
Lighthouse detects is between romhacks: if more than one enabled archive contains `aGameConfig`, all of them are
"quarantined" (disabled), and a popup explains why (`DetectAndQuarantineGameConfigConflicts`).

**Romhacks.** Lighthouse can turn a patched ROM into a mod ("Generate Mod from ROM" runs Torch on it). The result is
an overlay `.o2r` that contains only the assets that differ from the base game, plus `aGameConfig`. That blob holds
values the decomp normally hard-codes, such as scene remaps, music assignments, collectible caps and warp targets. They
are exposed to the game as `port_getRomhack*()` accessors (`src/port/Romhack/RomhackConfig.*`). Asset IDs present in
the overlay are redirected to it (`ResourceHelpers_BuildOverlayRepoints`). Romhacks also change MIPS code, and
Lighthouse cannot run that code. Instead, Torch hashes the injected code, and `RomhackTable.h` maps known hashes to
hand-written C++ ports of the hack's code changes (`Romhack/Specific/*.cpp`, registered in
`RomhackRegistration.cpp`). These ports hook into the game through Lighthouse's event/"VB" hook system
(`Enhancements/Events/Hooks`). **So code mods are compiled into the port and enabled by data. The mods themselves
contain no code.** Lighthouse does not enable libultraship's TCC scripting.

**HD/alternate assets.** libultraship tries `alt/<path>` before `<path>` when alt assets are on
(`ResourceManager::SetAltAssetsEnabled`). Lighthouse switches this with a CVar and the Tab key, which toggles HD
graphics at runtime. Sprites that the game rasterises on the CPU needed a dedicated path (`Resource/Alt/AltSprites.cpp`).

**Text and localisation.** Language packs are built with `torch modding export` and `torch modding import`, which
convert to and from editable YAML. The resulting packs are slim `.o2r` files under `lang/<region>/`, and a `langinfo`
manifest names the language. HD versions of the same assets live under `alt/assets/lang/<region>/` and are built with
HarbourMasters' Retro tool (`docs/modding/LANGUAGE PACKS.md`).

### 1.2 libultraship in this repo (`libultraship/`, at `759b134`)

- `ArchiveManager` (`src/ship/resource/archive/ArchiveManager.cpp`): mounts `.o2r` and `.zip` archives, plus `.otr`
  and `.mpq` with `INCLUDE_MPQ_SUPPORT`. A path without an extension is mounted as a `FolderArchive`. A directory
  passed as an archive path is scanned for archives. The last archive added wins. `AddArchive`, `RemoveArchive` and
  `SetArchives` exist, and removing an archive rebuilds the VFS. `ListFiles(glob)` and `GetArchiveFromFile` make it
  possible to report conflicts.
- Per-archive `manifest.json` (`Archive::Load`). Fields: `name`, `icon`, `author`, `version`, `website`,
  `description`, `license`, `code_version`, `game_version`, `main`, `binaries`, `dependencies`, `checksum`,
  `signature` (ed25519) and `public_key`. Unknown keys are ignored (`json.value(...)`), so the port can add its own.
- Game-version gate: `ResourceManager::Init` takes `validHashes`. An archive's `version` file (or its manifest's
  `game_version`) is checked against them. **Caveat:** if the list is not empty, archives *without* a version are
  rejected too, because the default is `0xFFFFFFFF`. That includes our own `pokestadium.o2r` today.
- Alt assets: the `alt/` prefix, plus `SetAltAssetsEnabled`.
- Scripting (`src/ship/scripting`, CMake `ENABLE_SCRIPTING`, **off by default**): `ScriptLoader` compiles a mod's C
  sources (`main`) with TCC in memory, or loads a prebuilt per-platform shared library (`binaries`). It calls
  `ModInit` and `ModExit` in dependency order and has a `SafeLevel` (disabled / trusted only / warn / all) that is
  tied to signed archives and the `Keystore`.
- An event bus (`ship/events`, `eventsbridge.h`). Lighthouse builds its hooks on it.

### 1.3 This port today

- **The game reads the ROM at runtime.** There is no Torch step. `Port::LoadRom` holds the whole `.z64` in `gRom`
  (`src/port/os_devices.cpp`). Every PI DMA is a `memcpy` from `gRom` (same file, the cartridge branch of the PI
  handler). Loaders work with ROM offsets: `Asset_LoadCompressed(romStart, romEnd, …)` reads 0x18 bytes, detects
  `PERS-SZP` (Yay0), `PRESJPEG` or raw data, then loads the whole range (`game/src/jpeg_stream.c`).
  `BinArchive_Open(romStart, …)` reads an archive header and table, and `BinArchive_GetFile` loads member *n* from
  `romStart + entry.offset` (same file). Top-level ranges are compile-time constants, for example
  `TRAINER_DATA_ROM_START`, `textdata_ROM_START`, `backgrounds_ROM_START`, `pokemon_models` (0x920000) and
  `pokemon_icons` (0x820000). They come from the decomp's `pokestadium-us.yaml`.
- Some game data is **compiled into the binary** rather than read from the ROM. The base stats table
  (`gBaseStats`, an alias into `D_8006FF00` in `game/src/status_icons.c` and `gb_data.h`), the move and type name
  tables, and other `.data`/`.rodata` live in the C sources.
- `pokestadium.o2r` (`tools/make_o2r.py`) contains only the Fast3D shaders, ImGui fonts and a `portVersion` file.
  `src/port/Engine.cpp` mounts it as the only archive, with `validHashes` empty.
- Textures: the port's renderer patch (`patches/libultraship.patch`, `Interpreter::ImportTexture`) already computes a
  **content hash** (FNV-1a over texels and the palette in use) for every texture it imports. It currently feeds only
  the texture cache key.
- Display lists, vertices and matrices are raw F3DEX2 from the ROM, translated on first use
  (`src/port/gfx_translate.cpp`). Pokémon models are blobs with a MIPS entry stub (`lui/addiu`) and geo layouts
  (`display_object_textures.c`, `geo_layout.c`).
- N64 audio is not ported yet (`PORT_NO_AUDIO`). Only the Game Boy audio of the GB Tower plays.
- Saves: the Stadium save is one 128 KiB FlashRAM image (`PORT_FLASH_FILE`) with 111 checksummed records, in the
  cartridge's byte order (`tools/save_editor/SAVE_FORMAT.md`). Game Boy saves and ROMs come from the user
  (`PORT_GB_ROM`/`PORT_GB_SAVE`, and `…2..4` for the other players).
- The launcher (`tools/launcher/Launcher.swift`, AppKit, **macOS only**) starts `build/pokestadium` with the working
  directory `build/` and passes everything through environment variables. The port has no in-game ImGui menu of its
  own yet.
- Regression testing relies on deterministic replays (`replays/*.rec`) and on tours compared against ares.

## 2. What this means for the design

Lighthouse can override assets per file because the game asks the ResourceManager for a *named* resource. Our game
asks for a *ROM byte range*. There are two possible routes:

- **A. Do it like Lighthouse:** convert the ROM to an `.o2r` (with Torch or a port-side extractor), then change every
  loader to load resources by name. That is the long-term direction for decomp ports, but it needs schemas and
  factories for every Stadium format (PERS-SZP blobs, BinArchives, model blobs with MIPS stubs, geo layouts, F3DEX2,
  text tables, JPEG). That is months of work, and it touches the loaders that just passed the ares comparisons.
- **B. Give names to the ROM's own units and override those** (proposed). Mod files use *virtual paths* that name a
  ROM unit, for example `rom/trainer_data/012.bin` or `rom/textdata/003.bin`. A small resolver in `src/port` answers
  "what are the bytes of this ROM unit?" and checks the mounted mod archives before the ROM. The game code keeps
  loading by offset; only the PI DMA and the size computations need to know about it (§5.2). Mods contain data in
  the **native format** (PERS-SZP, raw or JPEG, as the loader already accepts), so no new decoders are needed.

Route B still uses libultraship's `ArchiveManager` and VFS for discovery, priority, manifests and folder archives, as
Lighthouse does. Only the last step, from a path to the bytes the game sees, is specific to this port. Route A can
come later, one asset type at a time, without changing the mod format, because a virtual path is just a name.

## 3. Mod types, in order of priority

| # | Type | What it changes | Mechanism | Risk |
|---|---|---|---|---|
| 1 | **Texture packs** (SD and HD) | anything drawn from a texture: menus, fonts, Pokémon skins, stadiums | replacement keyed by the content hash in `ImportTexture`, loaded from `textures/<hash>.png` (HD under `alt/`) | low; renderer only, the game doesn't notice |
| 2 | **Asset replacement** (same format) | any ROM unit: model blobs, backgrounds, icons, JPEG pictures, archive members | VFS override of `rom/<unit>/<member>.bin`, served to the game as ROM bytes | medium; the bytes must keep the original format |
| 3 | **Text** | menu and battle strings, Pokémon, move and trainer names, translations | `text/<table>.json` merged into `Text_GetStringTable` (`text_system.c`) | low to medium; the font and charset limit what can be shown |
| 4 | **Data edits** | trainer teams (Gym Leader Castle, cups, Mewtwo), rental Pokémon, base stats, move data, cup rules | JSON files the port applies: to archive members (trainers) or to the C tables at boot (`gBaseStats`) | medium to high; affects saves, the Transfer Pak and replays |
| 5 | **Gameplay tweaks** | for example faster text, skipping intros, AI or level rules | **compiled into the port** behind CVars and hooks, like Lighthouse's Enhancements. Mods can only switch them on | low for mods; the work is in the port |
| 6 | Code mods | arbitrary C | libultraship `ScriptLoader` (TCC). **Not planned** before a stable hook API exists | high |
| — | Audio | music and sound effects | blocked until N64 audio is ported; afterwards the same as type 2 for the `audio_*` archives | — |
| — | Game Boy side | GB ROMs and saves | out of scope: they belong to the user, and the GB Tower runs them unchanged | — |

Texture packs come first: they are the most requested kind of mod for these ports, they need no knowledge of the game
formats, and they cannot corrupt saves.

## 4. Package format and folder layout

### 4.1 Where mods live

```
<app dir>/mods/                 # Ship::Context::GetPathRelativeToAppDirectory("mods"); today that is build/mods
  hd-menus.o2r                  # a packaged mod (zip)
  kanto-league-teams.o2r
  my-wip-mod/                   # loose folder: mounted as a FolderArchive, for authoring
  ~packs/<name>/…               # reserved, like Lighthouse's ~lang/~romhacks: total conversions and their scoped mods (phase 6)
```

`.o2r` is the only packaged form. A plain `.zip` is accepted as well, because libultraship treats it the same. `.otr`
(MPQ) is not supported: `INCLUDE_MPQ_SUPPORT` is not needed.

### 4.2 Inside an archive

```
manifest.json                       # libultraship manifest, plus a "pokestadium" object (below)
textures/<hash16>.png               # SD replacement: same size as the original
alt/textures/<hash16>.png           # HD replacement: any multiple of the original size; used while alt assets are on
rom/<unit>/<member>.bin             # native-format ROM unit, e.g. rom/trainer_data/012.bin, rom/textdata/003.bin
rom/<unit>.bin                      # a whole top-level unit that is not a BinArchive
text/<table>.json                   # { "file": 3, "strings": { "17": "…" } }: sparse overrides of one string table
data/trainers/<id>.json             # structured edits (phase 4)
data/base_stats.json
```

- **Unit names** come from `pokestadium-us.yaml`: `textdata`, `trainer_data`, `backgrounds`, `battle_headers`,
  `stadium_models`, `pokemon_models`, `pokemon_icons` and so on. Units without a name use their hex ROM offset,
  for example `rom/5C7A70/…`. The table lives in one generated file (`src/port/generated/rom_units.inc`) so that the
  tools and the port agree on it.
- `<hash16>` is the 64-bit content hash in hex. To make it stable for packs, the format and size are part of the
  name: `textures/<fmt>-<siz>-<w>x<h>-<hash16>.png`. The resolver matches on the hash and checks the rest.
- PNG for textures, so artists can edit them directly. The port converts them to RGBA once on load.

### 4.3 `manifest.json`

```json
{
  "name": "Kanto League Teams",
  "author": "…",
  "version": "1.2.0",
  "description": "Gym Leader Castle teams based on the Red/Blue leaders.",
  "license": "CC-BY-4.0",
  "website": "https://…",
  "game_version": 3982212223,
  "pokestadium": {
    "format": 1,
    "min_port_version": "0.3.0",
    "kind": ["data", "text"],
    "affects": ["gameplay", "save"],
    "exclusive": false,
    "requires": [],
    "conflicts": []
  }
}
```

- `game_version` is a fixed `u32` that identifies the US v1.0 ROM, which is the only ROM the port supports (SHA-1
  `ed7bef5a…`). The port would pass it as `validHashes`. Because of the libultraship caveat in §1.2, this needs a
  `version` file in `pokestadium.o2r` as well. Until then the port checks the field itself and does not pass
  `validHashes`.
- `affects` drives the save handling (§6.2) and the launcher warnings: `cosmetic` (textures and images only), `text`,
  `gameplay`, `save` and `transfer`. Tools derive it from the file list, and authors can only widen it.
- `requires` and `conflicts` are lists of mod names. They are checked at boot, and the result is shown in the UI
  (libultraship's `dependencies` is only used by the ScriptLoader).

## 5. How the port loads mods

### 5.1 Boot sequence (`src/port/Engine.cpp`)

1. Mount `pokestadium.o2r` as today.
2. Read the ordered list of enabled mods. The CVar `gSettings.Mods.Enabled` is `|`-separated like Lighthouse's, and
   the **first entry has the highest priority**. The environment variable `PORT_MODS` can override it for one run:
   `PORT_MODS=off` disables all mods (replays and regression tours use this), and `PORT_MODS=a.o2r|b` uses that list.
3. Mount the mods **in reverse list order** with `ArchiveManager::AddArchive`, so that libultraship's "last one wins"
   rule matches the order shown in the UI. Then mount loose folders under `mods/`, which have the highest priority.
4. Validate: version, `min_port_version`, `requires`, `conflicts` and `exclusive`. Mods that fail are unmounted and
   logged. A popup shows the failures (once there is in-game UI) or the launcher does.
5. Build the override indexes from `ListFiles`:
   - `rom/**` → a `RomOverride` table (§5.2);
   - `textures/**` and `alt/textures/**` → a hash map for the renderer (§5.3);
   - `text/**` and `data/**` → merged in priority order (§5.4).
6. Write a summary to the log, for example `mods: 3 active; 41 rom units, 812 textures, 2 text tables`. Mods active at
   a crash are also written into crash reports.

Mods take effect at **boot only**, as in Lighthouse. The game caches archives and assets in its pools, so changing
`rom/` or `data/` at runtime is not safe. The one exception is texture packs: the HD toggle can work at runtime
(§5.3).

### 5.2 ROM unit overrides (`rom/…`)

The game addresses data in two ways:

1. **BinArchive members** (`BinArchive_Open` + `BinArchive_GetFile`, `BinArchive_LoadFileFromRom*`). The table entry
   holds `offset` and `size` relative to the archive start.
2. **Fixed ranges** (`Asset_LoadCompressed(X_ROM_START, X_ROM_END)`, `Asset_LoadToSegment`, fragments).

Proposal: at boot, build a **virtual ROM**. It starts as a copy of `gRom` (32 MiB). Each overridden member is
appended after the end of the ROM, aligned to 16 bytes, and its table entry in the virtual ROM is rewritten to point
there. The archive tables are big-endian ROM data, so this is a plain byte patch. Offsets stay below the
`devAddr & 0x0FFFFFFF` limit as long as the virtual ROM stays under 256 MiB. Nothing in the game code has to change
for archive members, because the game reads the patched table and DMAs from the new offset.

Fixed ranges need a small hook, because the game computes the size from `romEnd - romStart`:

- `Port_RomRangeEnd(romStart, romEnd)` is called in `Asset_LoadCompressed` and `Asset_LoadToSegment`, and in
  `BinArchive_Open` mode 0, which loads the whole archive. It returns the end of the replacement if that range is
  overridden, and `romEnd` otherwise. Replacements are relocated to the appended area in the same way, and the PI DMA
  handler maps the original start to the new one through a small redirect table.
- Archives that are compressed as a whole (mode 0) are rebuilt uncompressed in the virtual ROM. The raw branch of
  `Asset_LoadCompressed` already handles that, and the port's 96 MiB pool (`PORT_HEAP_SIZE`) has room for it.

**Units that are excluded from overrides** (the builder refuses them and names the mod):

- fragments (`FRAGMENT` magic), meaning code overlays. Their code is the statically linked decomp, so only their data
  image comes from the ROM, and replacing it would break relocations (`Memmap_RelocateFragment`,
  `port_fragment_entries.c`);
- the boot, code and audio ucode segments.

**Model blobs** (`pokemon_models`, `stadium_models`) are allowed, but the replacement must keep the MIPS entry stub
and table layout that `Fragment_ProcessGeoLayout` decodes. In practice this means "edited copies of an original
blob", made by a tool. Authoring new models (glTF → Stadium blob) is out of scope here (§9).

`tools/mods/check.py` validates each `rom/` file against the base ROM: the unit exists, the format magic matches the
original, a PERS-SZP header is consistent, and model blobs keep their stub. Problems are found when the mod is built,
not inside the game.

### 5.3 Texture replacement

This is a hook in the port's `ImportTexture` patch, right after `key.content_hash` is computed:

- Look up the hash in the mod index. If there is a match, decode the PNG once (and cache it). Upload it in place of
  the decoded N64 texels, with the scale between the original and replacement sizes. This is the same mechanism as
  libultraship's `importReplacement` and `raw_tex_metadata` for alt textures, so UV and tile maths stay as they are.
- `alt/textures/` is preferred while `gSettings.Mods.AltAssets` is on. Toggling it flushes the texture cache, which
  makes the runtime toggle cheap, like Lighthouse's Tab key.
- **Dump mode** for pack authors: `PORT_TEXTURE_DUMP=<dir>` writes every texture the first time it is imported, as
  `<fmt>-<siz>-<w>x<h>-<hash16>.png`. Together with the replays, that gives authors a way to collect almost every
  texture (play `to_battle.rec`, the menu tours and so on).
- Textures that are not stable are not eligible: render-to-texture frame buffers, Gallery photos, the GB Tower screen
  and JPEG pictures decoded at runtime. They are left out of dumps and lookups by their source (frame-buffer and RTT
  addresses below 4 GB, `PRESJPEG` decodes). JPEG pictures can still be replaced as ROM units through `rom/`.

### 5.4 Text and data

- **Text.** `Text_GetStringTable(file)` (`game/src/text_system.c`) returns the swapped string table of file *n* from
  the `textdata` archive. The override applies the merged `text/<n>.json` entries on top: new strings are allocated
  in a port-owned buffer, and the original offsets table is patched. Strings use the game's own encoding and control
  codes (tokens handled by `Text_SubstituteTokens`). The text tool exports them with escapes, in the same way as
  Lighthouse's YAML (`'\xNN'`). Whether accented or non-Latin characters can be shown depends on the font assets,
  which are then a texture or `rom/` override as well (open question 3).
- **Trainer teams.** The trainer archive (`TRAINER_DATA_ROM_START`) is read by `Team_LoadPresetTrainer`
  (`game/src/session.c`) and `gym_leader_castle.c`. `data/trainers/<id>.json` describes a team in readable fields
  (species, level, moves, DVs, stat exp, item, nickname). The tool turns it into the native member, so at runtime it
  is a §5.2 member override. The Pokémon record layout is already decoded in the save editor
  (`tools/save_editor/stadium_save`) and can be reused, with the same names in the UI and the JSON.
- **Rental Pokémon, cup rules**: the same approach once their ROM location and layout are confirmed (they are read in
  `rental_hub`, `rental_rules` and `battle_prep`).
- **Base stats, move data**: these are compiled `.data` (`D_8006FF00`/`gBaseStats`). `data/base_stats.json` is applied
  once at boot by the port, before the game reads the tables. This only writes to fields that exist, by species and
  field name. Species, moves and types **cannot be added**: their IDs are shared with the Game Boy games (§6.3).

### 5.5 Gameplay tweaks and hooks

As in Lighthouse, the code lives in the port: `src/port/enhancements/*.cpp`, each one behind a CVar. At the decomp
call site there is a small hook (`if (PORT_SHOULD(SKIP_INTRO)) …`) built on libultraship's event bus. A mod can only
*switch tweaks on and set their values* through its manifest
(`"pokestadium": { "settings": { "Enhancements.TextSpeed": 2 } }`). It cannot ship code. This is how Lighthouse's
romhack ports work, and it keeps the code reviewable and covered by the replays.

Code mods with libultraship's `ScriptLoader` (TCC, `ModInit`/`ModExit`) would only make sense once the hooks above
form a stable C API. Building with `ENABLE_SCRIPTING` also needs TCC and its runtime on three platforms, and a signing
policy (`SafeLevel`, `Keystore`). Recommendation: no scripting for 1.0. Reconsider once there are about 20 hooks.

## 6. Port-specific concerns

### 6.1 The ROM stays the source

Mods are applied **on top of the user's ROM in memory**: `gRom` is never written to disk. Distributing a mod does not
mean distributing game data, as long as the mod contains only its own changed files. `tools/mods/pack.py` warns if a
`rom/` file is byte-identical to the original, because such a file is useless and is copyrighted data. The SHA-1 of
the base ROM (the launcher already checks `stadiumSha1`) is required before any mod is mounted.

### 6.2 Saves

The FlashRAM save keeps registered teams, the Victory Palace, the saved cup run, the Lab boxes and so on. Data mods
can create states that the unmodded game rejects or "repairs", for example a team from a changed rental list, or
species stats that no longer match. Proposal, modelled on Lighthouse's per-romhack scoping:

- Mods marked `cosmetic` or `text` share the normal save.
- If any active mod has `gameplay` or `save` in `affects`, the port uses a **save profile**:
  `saves/profiles/<profile>/stadium.flash`. The profile name defaults to a hash of the sorted mod names. The launcher
  shows which save will be used, and the save editor opens that file.
- The first time a profile starts, it can begin empty or as a copy of the normal save. The user chooses.

### 6.3 Transfer Pak, Lab and GB Tower

- **Species, move and item IDs are shared with Red, Blue, Yellow and Green.** The Lab PC writes Pokémon back into the
  Game Boy save. A mod must never make the Stadium side produce IDs or records that the Game Boy games cannot handle.
  For this reason, adding species or moves is excluded (§5.4), and a data mod that touches stats or moves of
  Pokémon that can be transferred is marked `transfer`.
- With a `transfer` mod active, the launcher shows a warning on the Player cards, and the Lab PC's "send to Game Boy"
  is disabled. A Lighthouse-style VB hook in the Lab PC fragment can do that. Reading Game Boy Pokémon into Stadium
  stays allowed.
- Stadium computes the stats of transferred Pokémon from `gBaseStats` (`gb_data.c`). A base-stats mod therefore
  changes them too, which is intended and should be documented.
- The GB Tower runs the user's Game Boy ROM with the port's GB core. Mods do not touch it. The Tower's frame
  (borders, UI) is a normal texture and can be replaced.

### 6.4 Replays, regression tours, ares comparisons

Mods break determinism by design. The rules:

- `PORT_MODS=off` is the default in `tools/replay.sh`, `tools/tour.py` and the regression runner.
- `PORT_RECORD` writes the active mod list and its hash into the `.rec` header. `PORT_REPLAY` warns if the list differs.
- A small mod fixture (`tools/mods/fixtures/`) covers each override path: one texture, one archive member, one fixed
  range, one text table and one trainer. It is checked by a dedicated replay with screenshots.

### 6.5 Memory and formats

The port's main pool is 96 MiB, far more than the N64's. Larger replacement assets fit, but game code that allocates
fixed-size buffers, or that assumes texture sizes, is still limited to the original sizes. This is why HD goes through
the renderer (§5.3) and not through `rom/` overrides. `rom/` replacements should stay the same size as the original or
close to it. `check.py` warns above twice the original size.

## 7. UI

### 7.1 Launcher (`tools/launcher`, macOS, now)

Add a **Mods** card to the launcher window (it currently has the ROM, the save, four Player cards, Auto-play and
Debug):

- A list of everything under `mods/`: icon (manifest `icon`), name, version, author, and badges from `affects`
  (Cosmetic, Text, Gameplay, Affects save, Affects Transfer Pak). Each row has a checkbox, and rows can be dragged to
  change priority (top wins, the same rule as the game).
- Conflicts are shown on each row (*"overrides 12 files from HD Menus"*), computed by opening the archives' file lists
  (a zip index in Swift, or `python3 -m tools.mods list --json`). Missing `requires` and declared `conflicts` disable
  PLAY until resolved.
- Buttons: **Install…** (copies an `.o2r` or `.zip` into `mods/`), **Open mods folder**, **Remove** (moves the file
  to the Trash and does not delete it).
- The save line shows the profile that will be used (§6.2).
- **The game owns the setting**: the launcher writes `gSettings.Mods.Enabled` into `pokestadium.cfg.json` while the
  game is not running (it already refuses to edit while the game runs). Test hooks follow the existing pattern:
  `LAUNCHER_MODS=<list>` overrides the stored setting for one run.
- With **Debug** on: "Dump textures" (starts the game with `PORT_TEXTURE_DUMP`) and "Reload mods" (rebuilds the
  indexes; this needs a restart of the game, like Lighthouse's Apply & Restart).

### 7.2 In-game (all platforms, later)

The port has no ImGui menu yet, and the Swift launcher only exists on macOS. Linux and Windows users need an in-game
**Mods** window. It works like Lighthouse's `LighthouseModMenuWindow`: Enabled and Disabled columns, drag order,
Apply & Restart, an alt-asset checkbox and hotkey, and the boot-time conflict popups. It edits the same CVars, so the
launcher and the game never disagree. This is worth building together with the general port menu (graphics,
controls), not just for mods.

## 8. Implementation plan

Estimates are for one developer who knows the port. "Verified" means checked with the replays and fixtures in §6.4.

| Phase | Content | Estimate |
|---|---|---|
| **0. Foundation** | `mods/` discovery; mounting in reverse priority; `PORT_MODS` and the CVar; manifest parsing and validation (`pokestadium` object, ROM SHA-1, port version); a `version` file in `pokestadium.o2r` (`make_o2r.py`); log summary; `PORT_MODS=off` in the replay and tour tools; mod list in `.rec` headers; `tools/mods/pack.py` (folder → `.o2r`, like `make_o2r.py`) | 3–4 days |
| **1. Texture packs** | hash lookup in the `ImportTexture` patch; PNG decode and cache; SD and `alt/` HD with scale; runtime toggle with cache flush; `PORT_TEXTURE_DUMP`; excluding frame buffers and JPEG; fixture and a sample pack (for example HD main-menu buttons) | 1–1.5 weeks |
| **2. ROM unit overrides** | `rom_units.inc` generated from `pokestadium-us.yaml` and the archive tables; virtual ROM builder (member append + table patch); `Port_RomRangeEnd` + PI redirect; fragment and code exclusions; `tools/mods/export.py` (dump units and members by name; builds on `PORT_DUMP_FRAGS`); `check.py` | 1.5–2 weeks |
| **3. Text** | `Text_GetStringTable` merge; text export and import (JSON, escapes); research on encoding and font coverage; a sample "typo fixes" or translation pack | ~1 week |
| **4. Data edits** | trainer JSON ↔ native member (reusing `stadium_save`'s Pokémon codec); base-stats applier at boot; save profiles; Transfer Pak guard (`transfer` badge, blocked Lab "send"); rental and cup rule data once their layout is confirmed | 2–3 weeks (+1 if the rental/cup layouts are still unknown) |
| **5. Launcher UI** | Mods card, install/remove, conflicts, badges, profile display, test hooks (`LAUNCHER_MODS`), UI test in `tools/save_editor/tests/test_ui.py` style | ~1 week |
| **6. In-game UI + tweaks** | ImGui Mods window (all platforms) next to a first port menu; first hook-based tweaks; manifest `settings`; `exclusive` packs and `~packs/<name>/` scoping (Lighthouse's romhack and scoped-mod model) | 2–3 weeks |
| Later | audio overrides (after the audio port); Route A converters per asset type; model authoring tools; scripting (`ENABLE_SCRIPTING`) if the hook API is stable | — |

Phases 0 → 1 together are the first deliverable that users can see (about 2 weeks). Phases 2–4 can then be done in
any order. Phase 5 can start after phase 0 and be developed in parallel.

## 9. Open questions for the team

1. **Route A vs B.** Do we accept that the game reads the ROM at runtime with overrides added on top (§2) for 1.0,
   or should asset conversion (Torch-style `.o2r`) come first so that mods match Lighthouse 1:1?
2. **Texture hash stability.** FNV-1a over texels and the palette is computed after the port's TMEM handling. Is it
   the same on Metal, OpenGL and DX11, and for textures that are loaded in strips (for example the "Game Pak Check"
   label drawn in 3 loadblock strips)? Should the key be computed from the DRAM source instead of TMEM, the way the
   GLideN64 and Rice packs do it? This decides whether existing N64 hi-res packs could be converted.
3. **Text encoding and fonts**: which charset does `textdata` use, and are there unused glyph slots for accented
   characters? Do we want translations at all (US ROM only), or only English text edits?
4. **Rental and cup rule data**: where exactly do they live, in the ROM or in compiled `.data`? This decides whether
   they are `rom/` members or boot-time patches like base stats.
5. **Save profiles**: should a profile be automatic per mod set (a hash), or named by the user? What happens to a
   profile when a mod is updated?
6. **Transfer safety**: is blocking the Lab's "send to Game Boy" with `transfer` mods acceptable, or should we
   validate each Pokémon instead (legal Gen 1 data only)?
7. **Priority direction**: we propose "top of the list wins" and mounting in reverse. Lighthouse's code and help text
   disagree on this, so we should agree on one rule and test it.
8. **Exclusive packs** (total conversions such as "Stadium 2 Kanto teams" or a randomizer output): do we need
   Lighthouse's romhack and scoped-mods model in 1.0, or is `exclusive: true` (only one active, quarantined
   otherwise) enough?
9. **Distribution and trust**: no code in mods means no signing is needed. If scripting comes later, do we use
   libultraship's ed25519 signatures and `SafeLevel`, and who holds the keys?
10. **Where mods live in the `.app` bundle and on Linux/Windows**: `build/mods` works for development. Should
    packaged builds use the libultraship app directory (`~/Library/Application Support/…`, `~/.local/share/…`,
    with `SHIP_HOME` as Lighthouse documents it)?
11. **Randomizer**: Lighthouse ships one. A Stadium "rental/trainer randomizer" would be a generator for phase-4 data
    mods (an `.o2r` written by a tool) and needs no engine support. Is that wanted, and should it live in the launcher?
