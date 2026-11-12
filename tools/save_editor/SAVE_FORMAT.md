# Pokémon Stadium (N64, US) save format

This describes the cartridge save of *Pokémon Stadium* (US, `NPOE`, ROM MD5 `ed1378bc12115f71209a77844965ba50`).
Source references are to the decomp at `pokestadium/src` (functions named as in that tree); the same files are in
`pokestadium-port/game/src`. The editor in this directory (`stadium_save/`) implements exactly this layout.

Evidence levels used below:
- **code**: read from the decomp source.
- **ROM**: decoded from ROM data (text tables, species/move tables).
- **ares**: observed in the real game running in ares (`verify/`, screenshots in `verify/shots/`).
- **inferred**: a reasoned guess; the operation is named so it can be checked.

## 1. Hardware

- **FlashRAM, 1 Mbit (128 KiB = 0x20000 bytes)**, 128-byte pages, 16 KiB (0x4000-byte, 128-page) sectors.
  (code: `src/flash.c` `Flash_*`, `src/jpeg_stream.c` `Flash_ProgramPages` writes 0x80 bytes per page,
  `Flash_EraseSectorByIndex(n)` erases page `n << 7`.) No SRAM or EEPROM is used for game data.
- The image is described in N64 (big-endian) byte order, the order ares stores in `save.flash`. All multi-byte
  fields are big-endian. Emulators that store FlashRAM byte- or word-swapped need a conversion first.
- An erased chip reads 0xFF. On a blank chip nothing is written until the first save (e.g. leaving Options);
  then every record is written (ares: `fixtures/first_save.flash`).

## 2. Banks and the two copies

The game keeps four RAM "banks" (`SaveBank D_800AE4E8[4]` in `src/save_data.c`). Each bank has a primary and a
backup copy in flash, one sector each:

| bank | size (`D_80073650`) | primary | backup | contents |
|---|---|---|---|---|
| 0 | 0x3700 | 0x00000 | 0x10000 | registered teams, categories 0–3 |
| 1 | 0x3700 | 0x04000 | 0x14000 | registered teams, categories 4–7 |
| 2 | 0x3F80 | 0x08000 | 0x18000 | registered teams cat. 8, Victory Palace, saved battle, options/progress, gallery |
| 3 | 0x3880 | 0x0C000 | 0x1C000 | Pokémon Lab boxes and item boxes |

- **Load** (`Save_EnsureBankLoaded` → `Save_InitializeBankN` → `Save_AllocateBank`): both copies are read into
  two RAM buffers, then each record is checked with `Save_LoadWithRepair`:
  - primary valid → primary is copied over the backup buffer;
  - only backup valid → backup is copied over the primary buffer;
  - neither valid → the record is reset (`Save_ResetAndCommitTypedRecord`, see §4).
  Repairs mark the bank dirty but are written only by the next flush.
- **Write** (`Save_FlushBank`, only if the bank is loaded and dirty): primary RAM is copied to backup RAM, sector
  `n` is erased and programmed with the primary, then sector `n + 4` is erased and programmed with the backup. A
  completed flush therefore leaves both copies identical; they differ only after an interrupted write.
- Bank 2 is loaded at boot (N64 logo / title); bank 3 when the Lab is used; banks 0/1 when a cup's registered teams
  are used (`Save_EnsureBankLoaded(ruleCategory / 4)`); Options loads all banks and flushes all dirty banks on exit
  (`Options_Main` → `Save_EnsureAllBanksLoaded`, `Save_FlushDirtyBanks`).
- Bytes of a bank after its last record (bank 2: 0x3E04–0x3F7F; bank 3: 0x3860–0x387F) are written back
  unchanged (they hold whatever was read; 0xFF on a first save). Bytes from the bank size to the end of the
  sector are never programmed and stay 0xFF. The editor keeps all of these bytes as they are (`_banks` in JSON).

## 3. Record footer and checksum

Every record ends with a 6-byte footer (code: `Save_WriteFooter`, `Save_RecordIsValid`, `Save_Checksum`):

| footer | meaning |
|---|---|
| `'P' 'O' 'K' 'E'` + u16 *sum* | committed record (`Save_CommitRecord`) — **valid** |
| `'X' 'X' 'X' 'X'` + u16 *sum* ^ 0x00FF | record left mid-transaction (`Save_InvalidateRecord`) — **not valid**, the game discards it |

*sum* = 16-bit sum (mod 0x10000) of every record byte except the last two, i.e. the data **and** the 4 magic bytes.
A record is valid only with `POKE` and a matching sum.

Byte 0 of every record is a flag byte:
- bit 1: modified since last commit. Set by `Save_MarkRecordDirty(…, 2)`/`Save_ClearRecord`;
  `Save_CommitRecord` writes the footer only if this bit is set and clears it. Committed records normally have it clear.
- bit 0: record-specific (in use / initialized), see each record.

**Two-phase commit in the Lab** (code: `LabPC_CommitChanges`, `LabItem_CommitChanges` in `fragments/lab_pc`):
box and item-box records are first closed with `Deck_CloseAndFlushBox` → `Save_InvalidateTypedRecord` (`XXXX`
footer) and bank 3 is flushed; then the Game Boy cartridge is written; then `Save_CommitBankRecords(3)` turns them
into `POKE` records and bank 3 is flushed again. If power is lost in between, both copies hold `XXXX` records, which
are reset on the next load, so moved Pokémon exist only on the Game Boy cartridge and are never duplicated. The
editor's `fix` command leaves `XXXX` records alone unless `--include-pending` is given.

**Reset contents** (`Save_ClearTypedRecord` then commit): all zero, then
- registered team / box / item box: byte 1 = 0x50 (empty name), flag byte 0;
- Victory Palace, options, album, photo set: flag byte = 1;
- saved battle: flag byte 0.
`SaveFile()` / `stadium_save new` produce exactly the game's blank save (test `test_new_save_matches_game`).

## 4. Record map

Record types are the `arg0` of the `Save_*TypedRecord` / `Deck_*` functions. Offsets are within the bank.

| type | record | bank | offset | size | count | entry size × capacity |
|---|---|---|---|---|---|---|
| 0x10 | registered team (`RegisteredTeamSlot`) | c/4 | (c%4)·0xDC0 + s·0x160 | 0x160 | 9 categories × 10 slots | 0x37 × 6 |
| 0x13 | Victory Palace | 2 | 0x0DC0 | 0xF60 | 1 | 0x1A × 151 |
| 0x15 | saved battle (`ContinueRecordBlock`) | 2 | 0x1D20 | 0x180 | 1 | 0x37 × 6 |
| 0x14 | options & progress (`SaveOptionsRecord`) | 2 | 0x1EA0 | 0x28 | 1 | — |
| 0x16 | gallery album | 2 | 0x1EC8 | 0x13B8 | 1 | 0x8C × 36 |
| 0x17 | gallery photo set | 2 | 0x3280 | 0xB84 | 1 | 0x8C × 21 |
| 0x11 | Lab Pokémon box | 3 | i·0x468 | 0x468 | 12 | 0x37 × 20 |
| 0x12 | Lab item box | 3 | 0x34E0 + i·0xE0 | 0xE0 | 4 | 2 × 100 |

111 records in total; they tile banks 0/1 completely (0x3700), bank 2 up to 0x3E04 and bank 3 up to 0x3860.
Capacities are from `Deck_GetSaveRecordCapacity`.

Absolute primary offsets (add 0x10000 for the backup):

| category c (`ruleCategory`, `src/game_state.h`) | name in the game | first slot |
|---|---|---|
| 0 `RULE_CATEGORY_ANYTHING_GOES` | Free Battle ("ANYTHING GOES") | 0x00000 |
| 1 `RULE_CATEGORY_NINTENDO_CUP_97` | hidden "POKé CUP" entry in the Stadium menu (not selectable in the US game, inferred) | 0x00DC0 |
| 2 `RULE_CATEGORY_NINTENDO_CUP_98` | same | 0x01B80 |
| 3 | Poké Cup | 0x02940 |
| 4 | Petit Cup | 0x04000 |
| 5 | Pika Cup | 0x04DC0 |
| 6 | Prime Cup | 0x05B80 |
| 7 | Gym Leader Castle | 0x06940 |
| 8 `RULE_CATEGORY_VS_MEWTWO` | Vs. Mewtwo | 0x08000 |

Other records: Victory Palace 0x08DC0, saved battle 0x09D20, options 0x09EA0, album 0x09EC8, photo set 0x0B280,
box *i* (1..12) 0x0C000 + (i−1)·0x468, item box *i* (1..4) 0x0F4E0 + (i−1)·0xE0.

Category/mode names: ROM text table 0x13 (`stadium_select`: entries {3,1,2,6,4,5} → "POKé CUP" ×3, PRIME, PETIT,
PIKA) and table 0x0E (mode names, divisions, Castle stages) (ROM).

Note (code): `Options_EraseSaveData` ("DELETE SAVED DATA") resets team slots 0..11 of each category. Slots 10 and
11 of category *c* address the next category's slots 0/1 (or, for categories 3 and 7, the start of the bank's
backup buffer, and for category 8 the start of the Victory Palace record); all of these are reset by the same
function, so the overrun has no visible effect.

## 5. Common pieces

### 5.1 List header (`DeckRecordHeader`, registered teams, boxes, item boxes)

| off | type | field | notes |
|---|---|---|---|
| 0x00 | u8 | flags | bit0: slot in use (`Deck_IsSaveEntryPresent`); teams: registered; item boxes: owned by a trainer |
| 0x01 | gb[11] | name | trainer name (`Deck_SetSaveEntryTrainerIdAndName`); 0x50 = empty |
| 0x0C | u16 | trainer ID | |
| 0x0E | s16 | count | entries in use (`Deck_GetSaveEntryCount`) |
| 0x10 | … | entries | |

### 5.2 Pokémon (`GbPokemonNamedRecord`, 0x37 bytes; `src/gb_data.h`)

The Game Boy (Red/Blue/Yellow) box structure plus names, converted by `Pokemon_ToGbRecord` /
`Pokemon_FromGbRecord` (`src/gb_data.c`) and `GbSave_SaveBattleMonRecord` / `GbSave_LoadBattleMonRecord`.

| off | type | field |
|---|---|---|
| 0x00 | u8 | species, Gen 1 **internal index** (`gDexToInternalIndex` / `gInternalIndexToDex`, `src/pokemon_stats.c`) |
| 0x01 | u16 | current HP |
| 0x03 | u8 | level (`BattleMon.unk_04`, set from the battle level `unk_24` before saving) |
| 0x04 | u8 | status: bits 0–2 sleep, 3 poison, 4 burn, 5 freeze, 6 paralysis |
| 0x05 | u8 | type 1 (Gen 1 type id: 0 Normal, 1 Fighting, 2 Flying, 3 Poison, 4 Ground, 5 Rock, 7 Bug, 8 Ghost, 0x14 Fire, 0x15 Water, 0x16 Grass, 0x17 Electric, 0x18 Psychic, 0x19 Ice, 0x1A Dragon) |
| 0x06 | u8 | type 2 |
| 0x07 | u8 | catch rate (Gold/Silver read it as held item; the Lab's bonus Pokémon get 0xA7 / 0xA8 for Round 2) |
| 0x08 | u8[4] | move ids (1..165, `gMoveData`) |
| 0x0C | u16 | original trainer ID |
| 0x0E | u24 | experience |
| 0x11 | u16 ×5 | stat experience: HP, Attack, Defense, Speed, Special |
| 0x1B | u16 | DVs: Attack << 12 \| Defense << 8 \| Speed << 4 \| Special; HP DV = low bits of Atk,Def,Spd,Spc |
| 0x1D | u8[4] | PP: bits 0–5 current, bits 6–7 PP Ups |
| 0x21 | gb[11] | nickname |
| 0x2C | gb[11] | OT name |

Stats are not stored: `GbSave_LoadBattleMonRecord` calls `Pokemon_RecalcStats`
(`stat = ((ceil√statexp)/4 + (base + DV)·2) · level / 100 + 5`, HP: `+ level + 10`, capped at 999; `CalculateStatValue`).
Experience per level follows `Pokemon_ExpForLevel` with the growth byte of the species table (`D_80070FA0` +0x0E).

### 5.3 Text

- **gb**: Game Boy character set (`D_80073030` maps it to the N64 font; `D_80073130` the reverse), terminated by
  0x50; bytes after the terminator are not text (the game leaves stack garbage there, the editor keeps them).
  0x5D is the player-name token (`Text_UntranscodeName` substitutes the player label). Names are at most 10
  characters. Stadium's table follows the European Game Boy charset (0xBA-0xCC are à è é ù ß ç and umlauts, where the
  English games have é 'd 'l ...), PK/MN are 0xE1/0xE2, ♂ 0xEF, ♀ 0xF5, × 0xF1, ' 0xE0; bytes it has no glyph for
  (0xCD-0xDF, 0xE5, 0xE8-0xEE) decode as `<xx>` and survive a round trip. Encoding accepts `<M>` `<F>` `<PK>` `<MN>`
  (any case), `<xx>`, typographic ' and -, and both cases of letters.
- **n64**: the game's own font bytes (latin-1 compatible; 0xA9 ♂, 0xBE ♀, 0xBC/0xBD PK/MN), NUL terminated.

## 6. Records in detail

### 6.1 Registered teams (type 0x10, 0x160 bytes)

Header (§5.1), then 6 Pokémon at 0x10 (to 0x14A), 0x14A–0x159 unused, footer at 0x15A.
Written by `func_8420DBA0` (`fragments/rental_team_selection/rental_team_selection_2E5430.c`) via
`Deck_OpenAndSetName(0x10, …)` into the first free slot (name and ID of the Game Boy trainer), read by `RegisteredTeam_LoadSelected`; `Deck_CompactRegisteredTeams` moves
used slots to the front when a category is opened. Category selection: `Save_SetActiveRecordBank(ruleCategory)`.
(ares: a team written by the editor appears under "Registered Pokémon" in the Poké Cup with name, ID, species,
nickname and level — `verify/shots/registered_team_*.png`.)

### 6.2 Victory Palace (type 0x13, 0xF60 bytes)

| off | type | field |
|---|---|---|
| 0x000 | u8 | flags (1 after reset) |
| 0x001 | u8 | unused |
| 0x002 | entry[151] | one per species, index = dex − 1 |
| 0xF5A | footer | |

Entry (0x1A, `VictoryPalace_SaveSpeciesRecord` / `VictoryPalace_LoadSpeciesRecord`, `src/pokemon_stats.c`):

| off | type | field |
|---|---|---|
| 0x00 | u8 | session mode of the win (3 Poké Cup, 4 Petit, 5 Pika, 6 Prime, 7 Castle), +0x80 if won in Round 2; **0 = no statue** |
| 0x01 | u8 | level |
| 0x02 | u16 | OT ID |
| 0x04 | gb[11] | nickname |
| 0x0F | gb[11] | OT name |

Written for every Pokémon of the winning team after a cup is won (`BattlePrep_SaveVictoryPalaceRecords`,
`fragments/battle_prep`): Poké/Prime Cup only for the Master Ball division, the Castle only for the Rival stage.
(ares: statue and its NAME/LEVEL/OT/ID/HALL OF FAME panel — `verify/shots/victory_palace_*.png`.)

### 6.3 Saved battle / CONTINUE (type 0x15, 0x180 bytes)

| off | type | field |
|---|---|---|
| 0x000 | u8 | flags: bit0 a run is saved (`Save_ContinueRecordExists`), bit1 set when written |
| 0x002 | 0x20 | session (`SessionContinueData`, below) |
| 0x022 | s16 | number of Pokémon |
| 0x024 | pokémon[6] | the player's team (§5.2) |
| 0x16E | u8[6] | `BattleMon.unk_52` per Pokémon (cleared on normal loads; role unknown) |
| 0x174 | u8[6] | `BattleMon.unk_53` per Pokémon (role unknown) |
| 0x17A | footer | |

Session (`Session_SaveContinueData`, `src/session.c`):

| off | type | field |
|---|---|---|
| 0x00 | u8 | mode (3 Poké Cup, 4 Petit, 5 Pika, 6 Prime, 7 Castle) |
| 0x01 | u8 | mode selection: division 0..3 (Poké/Great/Ultra/Master Ball) or Castle stage 0..9 (8 gyms, Elite Four, Rival) |
| 0x02 | u8 | battle number (1-based; 7 = semi-final, 8 = final in cups) |
| 0x03 | u8 | Round 2 flag (`SessionContext.unk_11F2`) |
| 0x04 | u8 | continues used |
| 0x05 | u8[8] | opponent history (`SessionContext.opponentHistory`) |
| 0x0D | u8 | `gbStarterChoice` |
| 0x0E | u16 | trainer ID |
| 0x10 | gb[16] | player name |

Cleared (reset) when the resumed battle starts (`BattlePrep_ShowSingleBattleIntro` / `BattlePrep_ShowRoundIntro`). (ares: an editor-written run
resumes at battle 3 of the Poké Cup with the saved team — `verify/shots/continue_*.png`.)

### 6.4 Options and progress (type 0x14, 0x28 bytes)

| off | type | field | source |
|---|---|---|---|
| 0x00 | u8 | flags (1 after reset) | |
| 0x01 | u8 | unused | |
| 0x02 | u16 | sound: bit0 mono, bit1 voice off | `Options_ApplySoundSettings` (ares: Options shows MONO/OFF) |
| 0x04 | s16 | GB Tower speed: 0 normal, 1 Doduo, 2 Dodrio; incremented (max 2) by the first Master Ball win in the Poké Cup or Prime Cup | `BattlePrep_MarkFirstClearBonus`; 2 also switches the area-select background to night (`AreaSelect_LoadBackgroundArt`, ares) |
| 0x06 | 2 | unused | |
| 0x08 | u32 | word handed to GB Tower (`UnkInputStruct8000D738.unk_00`) and stored back after GB Tower exits | `State_GbTower`, `GbTower_BuildPartyResult`; role unknown |
| 0x0C | progress[2] | Round 1, Round 2 (8 bytes each, below) | `Save_Get/SetModeSettings(…, unk_11F2)` |
| 0x1C | u16 | bonus Pokémon waiting at the Lab: dex, \| 0x100 if earned in Round 2; 0 = none | `Completion_FadeOutAndSave`, `LabPC_*` (cleared when collected) |
| 0x1E | u8 | Kids Club: bit0 Hyper difficulty unlocked | `Widget_PauseMenuCheckHyperUnlock`, `KidsClub_LoadDifficultySetting` |
| 0x1F | u8 | set to 1 when the bonus Pokémon was awarded in session mode 0x1A | `Completion_FadeOutAndSave`; read by the Victory Palace |
| 0x20 | 2 | unused | |
| 0x22 | footer | | |

Progress (`ModeSettings`, written by `BattlePrep_MarkCupCleared` / `BattlePrep_MarkNewRoundRecord`):

| off | type | field |
|---|---|---|
| 0x00 | u16 | cleared modes: bit (1 << mode); 3 Poké Cup, 4 Petit, 5 Pika, 6 Prime, 7 Castle, 8 Mewtwo. Set only for the last division (cups) / Rival (Castle). 0xF8 = everything before Mewtwo |
| 0x02 | 2 | unused |
| 0x04 | u8 | Gym Leader Castle stages cleared (0..8) |
| 0x05 | u8 | Poké Cup divisions cleared (0..4) |
| 0x06 | u8 | Prime Cup divisions cleared (0..4) |
| 0x07 | u8 | Mewtwo unlocked: set when cleared modes reaches 0xF8 |

The Gallery checks Round 2 cleared modes == 0x1F8 (`Gallery_LoadUnlockedBackgrounds`). (ares: with all bits set,
Mewtwo appears over the Castle on the area-select screen — `verify/shots/unlock_area.png`.)

### 6.5 Gallery (types 0x16 album, 0x17 photo set)

Album (0x13B8): u8 flags, u8 unused, 36 photos at 0x002, footer at 0x13B2. Photo set (0xB84): u8 flags, u8 unused,
21 photos at 0x002, footer at 0xB7E; photos 0–3 and 4–19 are the two sticker layouts ("4×4 or 16×1",
`D_83407ABC` / `D_83407AC0`), photo 20 is a control record whose byte 0x6C holds bit0 = layout toggle and bit1 =
a flag cleared on load (`Gallery_LoadUnlockedBackgrounds`). Written by `Gallery_SaveUnlockedBackgrounds`
(when leaving the Gallery and confirming).

Photo (0x8C; `unk_D_83403C60` / `unk_D_83402EE0`, `src/gallery.h`; field roles from `Gallery_InitializePhotoModel`):

| off | type | field |
|---|---|---|
| 0x00 | f32 | model Y position |
| 0x04 | u8 | subject: 1..151 species, 0x99 drawn with Pikachu's model; anything else = empty slot (`Gallery_IsPhotoSpeciesValid`) |
| 0x05 | u8 | animation (`ModelAnim_SetAnimation`) |
| 0x06 | s16 | animation frame |
| 0x08 | u8 | event track |
| 0x09 | u8 | event frame |
| 0x0A | 0x46 | slot data (`Particle31_UnpackSlotData`) |
| 0x50 | f32[3] | camera vector A |
| 0x5C | f32[3] | camera vector B |
| 0x68 | u32 | unknown |
| 0x6C | u8 | flags (see photo 20 above) |
| 0x70 | u16 | OT ID (`Gallery_CopyPhotoMonFromBattleMon`) |
| 0x72 | u8 | species (dex) |
| 0x73 | n64[11] | nickname |
| 0x7E | n64[11] | OT name |

Not verified in ares (taking and keeping a photo needs a longer input sequence than the verification scripts have).

### 6.6 Pokémon Lab boxes (type 0x11, 0x468 bytes ×12)

Header (§5.1; name/ID unused by the Lab), 20 Pokémon at 0x10 (to 0x45C), 0x45C–0x461 unused, footer 0x462.
`LabPC_WriteDeckBack(0x11, …)`; capacity check in `pokemon_lab.c` (`Deck_GetSaveEntryCount(0x11, i) < 20`).
The Lab can only be entered with a Game Boy Game Pak in the Transfer Pak, so edits here were checked only by the
editor's own tests, not in ares.

### 6.7 Pokémon Lab item boxes (type 0x12, 0xE0 bytes ×4)

One per Game Boy trainer: header name/ID = owner (`LabItem_FindOrAllocateGamePakBoxSlot` matches both), flags bit0 =
in use, then 100 × (u8 item id, u8 quantity) at 0x10 (to 0xD8), footer at 0xDA. Item ids are Gen 1 ids (ROM text
table 0x27 index id − 1; 0xC4–0xC8 HM01–05, 0xC9–0xFA TM01–50).

## 7. What the editor adds on top

- JSON dump of every record (`stadium_save dump`). Every byte is either a decoded field or kept as `_raw_XXXX`,
  `_tail`, `_unused_entries`, `_backup` (a backup copy that differs from the primary) or `_banks` (bytes after the
  records), so `build` turns any dump back into the identical image — also for random data (tests).
- Edits commit records like the game: footer `POKE` + checksum, flag bit 1 cleared, written to both copies.
- `repair` applies `Save_LoadWithRepair` offline; `fix` recomputes footers after hex editing.
