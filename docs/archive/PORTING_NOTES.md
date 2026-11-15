# Pokemon Stadium (US) -> libultraship port: status and findings

## Layout
- `libultraship/`  [Team Hippodrome's fork](https://github.com/team-hippodrome/libultraship), pinned as a submodule and built as a CMake subproject.
- `src/port/`      port harness (libultraship context, window, per-frame display-list submission).
- The decomp lives untouched in `../pokestadium`.

## Build / run
    cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target pokestadium
    cd build && ./pokestadium

The port's resources (shaders, the shader warm-up list, launcher pictures; `tools/make_o2r.py`) are packed into
`pokestadium.o2r` and built into the executable as a C array (`generated/pokestadium_o2r.c`), mounted through
`ResourceManager::SetBuiltInArchive` and an in-memory `O2rArchive` (in the libultraship fork). It used to be a
separate file next to the executable; a damaged, unreadable or missing copy (a user's Windows install in a Steam folder)
left libultraship without an archive and the renderer crashed a moment later on the shaders it reads from it (an
uncaught C++ exception, 0x20474343). A `pokestadium.o2r` left from an older version is not read.

## Milestone 0 (done): window + Fast3D
`main.cpp` builds a fill-rectangle display list and submits it every frame. Verified: process stays alive
for 6s, no exception. Not verified visually (screenshot capture was unavailable).

## Findings that shape the real port (measured on pokestadium @ 697f9ba7)
- 347 C files, ~250k lines. 306/347 already pass `clang -fsyntax-only` with the repo's compile_flags; 41 fail
  (mostly 1-2 errors each, battle_prep_* worst at up to 14).
- Pointer width is the main obstacle. Counts in src/*.c: 553 `(u32)` casts, 48 `(u32)&...`, 235 hard-coded
  0x80xxxxxx addresses, ~1964 0x0Exxxxxx-style segment constants; `include/global.h` does
  `typedef unsigned int uintptr_t`; many structs hold pointers in `s32` fields.
- A "keep everything below 4 GB" shortcut does NOT work natively: on arm64 macOS, any `-pagezero_size`
  below 4 GB gets the process SIGKILLed, so `mmap(0x10000000, MAP_FIXED)` fails. It only works as an
  x86_64 binary under Rosetta (verified with a test program), which would need x86_64 builds of every dep.
  The practical route is a pointer-clean 64-bit port.
- Not portable as-is: hasm segments (boot, entry, exception, Yay0 decoder, abs, libgsm, audio Yay0 decoder,
  and the GB Tower emulator core `gb_tower_emulator_code_asm`), libleo bootstrap, libultra internals (replaced
  by libultraship), custom RSP audio ucode (N_MICRO naudio) and JPEG ucode (libultraship has no audio HLE),
  fragment/overlay loader (Memmap_RelocateFragment) which must become static linking + a symbol table.
- Game data: compressed (Yay0/PERS-SZP) assets with segment-relative pointers; first runnable target should
  read the ROM at runtime and emulate Storage_QueueRomRead, deferring O2R/Torch conversion.

## Suggested order
1. Copy src/ + include/ into `game/`, switch to libultraship headers, get all 347 files compiling (stubs for RSP/audio/asm).
2. Replace thread/message-queue/scheduler glue (main.c, rsp.c, dp_intro.c) with libultraship equivalents.
3. Pointer cleanup pass (u32/s32 address fields), starting with gfx/segment handling.
4. ROM-backed DMA + Yay0 decode in C, boot to N64 logo / title screen.
5. Audio: HLE of the naudio ucode or a C reimplementation; GB Tower emulator in C.


## Stage 1 (done): all 347 game sources compile for the host
`game/` started as a copy of the decomp's `src/` and `include/` (plus the SDK headers libultraship does not provide
under `game/lib/ultralib/include`). It was then a submodule with the port's changes kept as patches; it is now
vendored again, synced with the decomp at the commit in `game/UPSTREAM.md` (see docs/BUILDING.md "The vendored game"). The SDK stand-in headers described below live in
`src/port/sdk/`. The decomp checkout in `../pokestadium` is untouched.

    tools/syntax_check.sh              # clang -fsyntax-only over all of game/src: 0 errors / 347 files
    cmake --build build --target stadium_game   # compiles all 347 files into build/libstadium_game.a

What was changed to get there:
- `src/port/sdk/ultra64.h` replaces the SDK `<ultra64.h>`: includes libultraship's libultra headers one by one
  (not `libultra.h`, whose `abi.h` is a different HLE audio command set and clashes with the SDK `abi.h`
  that libnaudio needs), hides libultraship's `OSMesg` union so the game keeps the SDK's `void*` form, and
  maps `osSendMesg`/`osRecvMesg`/... to `port_*` names that the port must implement (blocking queues for
  host threads). It also maps `gSPDisplayList/gSPVertex/gSPSegment` to libultraship's `__`-prefixed forms.
- `src/port/sdk/PR/*.h` forward the game's `<PR/...>` includes to that header.
- `src/port/sdk/port_compat.h` (force-included) declares libultra pieces libultraship lacks: threads, EPi/PI,
  Transfer Pak (`osGbpak*`), flash constants, CPU register helpers, `_Printf`, `osTvType`, etc.
- Local patch to the vendored libultraship: `include/libultraship/libultra/mbi.h` skips `abi.h` when
  `PORT_NO_LUS_ABI` is defined. Reapply if libultraship is re-cloned.
- game/include string.h, stdlib.h, stdarg.h now forward to the system headers; math.h also includes them;
  `typedef unsigned int uintptr_t` removed from global.h; duplicate `MtxF` removed.
- 64-bit widening, each tagged `PORT64`: ~120 `u32/s32` initializer tables that hold addresses became
  `uintptr_t` (script-like data in fragments), `ModelSegment` segment fields, `unk_D_8006FF00.unk_08/unk_18`,
  `unk_D_8140E728_01C_00C.arg`. Readers of these tables still assume 4-byte words; each needs an audit
  (find them with `grep -rn PORT64 game`). `tools/widen_addr_tables.py` produced most of them.
- The flags in `tools/compile_flags.txt` still keep `-Wno-int-conversion -Wno-incompatible-pointer-types` etc.
  from the original build; "compiles" does not mean pointer-correct. Truncating `(u32)ptr`/`(s32)ptr`
  casts (553 `(u32)` casts in the sources) still compile and will misbehave at runtime. See "Game diagnostics"
  below for what is reported now and the plan to turn these into errors.

## Game diagnostics
The game (`stadium_game`) and the libultra runtime layer (`port_os`) build with `-Wall -Wextra` (clang-cl: `/W4`),
without `-Werror`. `STADION_GAME_WARNINGS` in CMakeLists.txt, mirrored in `tools/compile_flags.txt`, follows
2Ship2Harkinian's list: it turns off the warnings decompiled code produces by design (unused parameters and
variables, stack pads, flat initialisers, parentheses, sign compares). The vendored libultra sources
(`cmake/ultralib_sources.txt`) stay at `-w`, like 2Ship's libultra copy.

Ratchets: a group is still off while it has hits, and becomes `-Werror=<group>` once its count reaches zero, so
it can't come back. Counts are unique sites in `game/src` from a full `stadium_game` build with each group
turned on (clang 21, Linux x86-64):

| Group | Status | Sites |
|---|---|---|
| `implicit-function-declaration` | `-Werror` | 0 |
| `return-type` | off | 20 |
| `shorten-64-to-32` | off | 136 |
| `pointer-to-int-cast` | on (warning) | 343 |
| `int-conversion` | off | 1,698 (same as `tools/intconv.sh`) |
| `incompatible-function-pointer-types` | off | 592 |
| `incompatible-pointer-types` | off | 2,525 |

Per-file ratchet: until a group is clean everywhere, the files listed in `cmake/strict_sources.txt` compile with
all of the type groups above (plus `int-to-pointer-cast`) as errors (`STADION_STRICT_GROUPS` in CMakeLists.txt).
Subsystems join the list as they are fixed:

| Subsystem | Files | Check |
|---|---|---|
| Audio (sequencer, sound effects, streams, libnaudio, cries) | 41 | 20/20 runnable replays (2 more need GB saves), normal and ASan builds |
| Core runtime (main/memory pools, scheduler, storage queue, memmap, save banks, session, profiler) | 15 | 20/20 runnable replays, normal and ASan builds |

Conventions used for these fixes:
- ROM offsets stay 32-bit integers (`u32`/`uintptr_t`); a pointer-typed field that holds a ROM offset (libaudio's
  `ALSeqFile.seqArray[].offset`) is converted with an explicit `(uintptr_t)` cast where it is read.
- RAM destinations are pointers (`void*`), as in `Rom_DmaRead(uintptr_t romOffset, void* dest, u32 size)`.
- Globals and parameters get the type of the data they hold (`SoundBank*`, `fx_t*`) instead of `u8*` plus casts.
- Intrusive lists whose node is the first member (`ALLink node`) store `&x->node` and cast back explicitly.
- Narrowing that was free on the 32-bit N64 (pointer differences into `s32`, `u64` frame counters into `u32`) gets an
  explicit cast, so the truncation is visible.
- Integer messages go through `OS_MESG_32(x)` and come back with `OS_MESG_GET_32(msg)`; pointers use `OS_MESG_PTR(p)`
  (2Ship's names; `src/port/sdk/ultra64.h` defines them for the game's `void*` `OSMesg`).
- A header shared by several structs is a named struct embedded as the first member (`SchedClient client`), and the
  API takes the header type: `Sched_AddClient(&D_800A6D20.client)`.
- `ALIGN16(x)` and friends keep the type of `x`, as in 2Ship's `alignment.h`; cast a pointer to `uintptr_t` first.
- A main-pool arena (`MainPool_AllocState`, `MainPool_AllocAligned`) is a `MainPoolState*` everywhere. Pool free
  callbacks (`AllocateFunc`) get the block's data pointer and its tag: `void (*)(void* data, u32 arg)`.
- Raw record buffers whose stride is decided at run time take `void*` and walk a local `u8*` (`Save_ClearRecord`,
  `Deck_ReadEntries`).

`tools/ptr_warnings.sh` lists the pointer-truncation subset (490 sites). When fixing these, pair a replay run
(`tools/run_replays.py`) with an AddressSanitizer build (`cmake -DPORT_ASAN=ON`): a truncated pointer or a
32-bit size can survive a normal replay by luck of heap layout and fail later on Windows or Android.

Byte order: N64 data (ROM, cartridge saves, fragment blobs) is big-endian. `game/include/port_endian.h` (included by
`macros.h`, so every game file has it) is the one place for the helpers: `Port_ReadBE32`/`Port_ReadBE16` read a value
from memory, `Port_SwapWords`/`Port_SwapHalves` swap an array in place (any alignment), `Port_Swap32`/`Port_Swap16`
swap a value already loaded. They use libultraship's `BE32SWAP`/`BE16SWAP`, as the HarbourMasters ports do.
`tools/check_port_decls.py` fails when a `Port*_` helper is defined in more than one file.

Printing fixed-width integers: `uint64_t` is `unsigned long` on 64-bit Linux but `unsigned long long` on Windows
and macOS, so `%llu` with a bare `uint64_t` warns on one of them. Cast at the call site
(`(unsigned long long)x` with `%llu`) or use the `<inttypes.h>` macros (`PRIu64`, `PRIxPTR`).

Before changing a header or a struct type in `game/`, ask: is this also wrong on the N64? If yes, fix it in the
pokestadium decomp too, so the two copies don't drift apart.

## Link surface of libstadium_game.a (1305 undefined symbols, measured with nm)
- 856 `D_<hex>`: segment/virtual-address asset symbols (D_1000000, D_3000000, ...; defined in the original by the
  linker script). They should become address constants or ROM-segment lookups.
- ~300 linker-script ROM/fragment boundaries: `_fragmentN_ROM_START/_TEXT_START/_relocs_ROM_END`, asset blobs
  (`_fonts_ROM_START`, `_battle_ui_ROM_START`, ...). Needed by the overlay loader; replace with a ROM index table
  and static linking of fragment code.
- Missing implementations: the `port_os*` message queue/thread layer, `osEPi*`, flash/PI/Leo/Gbpak, `_Printf`,
  `bzero/bcmp/_bcopy`, `Yay0_Decompress` (hasm), `Gsm_*` (hasm libgsm), `GbCpu_*`/`GbEmu_*` (hasm GB core),
  `F3DEX2_data_bin`, `aspMain*` (RSP audio ucode), alHeap/alSeq/__init_lpfilter pieces, `__osPi*` internals.

## Stage 2 (done): runtime layer, symbols, linked executable
`pokestadium` now links `stadium_game` + libultraship and the game's `Main()` runs on host threads.

    cmake --build build --target pokestadium
    tools/try_run.sh 12            # rebuild, run 12 s against the US ROM, symbolize a crash (PORT_TRACE=1 logs thread/queue events)
    ./build/pokestadium "/path/to/Pokemon Stadium (USA).z64"

New pieces (all under src/port/):
- `os_thread.cpp`: libultra threads and blocking message queues on host threads. One token decides which
  OSThread runs (highest priority, FIFO among equals); switches happen at libultra calls and when an
  interrupt-like post wakes a higher-priority thread while nothing holds the token. The original idle thread
  (id 1) parks forever when it drops to priority 0. `port_osSendMesg/RecvMesg/...` replace the libultraship
  non-blocking queues (names remapped in `src/port/sdk/ultra64.h`).
- `os_devices.cpp`: cartridge ROM loaded from the path on the command line and served by `osEPiStartDma`; VI
  retrace thread (60 Hz) feeding `osViSetEvent`; `osSpTaskStartGo` queues RSP tasks that the window thread
  runs (`Port::PumpRspTasks`): gfx tasks go to Fast3D then post DP/SP events, other tasks complete at once;
  timers; Controller/Transfer Pak report "not present".
- `stubs.c`: C `Yay0_Decompress`, `_Printf`, `_bcopy`, a 96 MB host arena for the game's main pool
  (`gPortHeap`, used by `Util_InitMainPools`), hardware-register scratch page (`port_hw_reg`, behind
  `HW_REG/IO_READ/IO_WRITE`), and TODO stubs for the GB Tower emulator core, libgsm, libultra internals and
  audio synth pieces.
- `generated/abs_syms.S` (from `tools/gen_abs_syms.py` + the decomp linker map): 1146 absolute symbols
  (`D_<hex>` segment/virtual addresses, `*_ROM_START/_END`, `*_TEXT_START`, `gAudioData`). Regenerate after
  rebuilding `libstadium_game.a` if new unresolved data symbols appear. Verified that Mach-O absolute symbols
  link and resolve to the intended constants.
- SDK C sources copied into `game/lib/ultralib/src` (gu matrix/projection helpers, audio heap/bank file) and
  built into `stadium_game`; libultraship does not provide the `gu*`/`al*` functions the game calls.

Fixes made while booting: `ALIGN*` macros now mask at pointer width; `GfxImage_Initialize` takes a
`uintptr_t`; ~370 explicit `(u32)/(s32)pointer` casts were widened by `tools/widen_ptr_casts.py`.

### Where boot stops now
Threads 1 (idle), 2 (crash screen), 20 (RSP), 3 (scheduler), 4 (audio), 5 (display worker) start, then the
RSP thread crashes in `port_osEPiStartDma`: `Storage_ReadSram`-style code builds `OSIoMesg.dramAddr` from
`u32` fields (e.g. `struct UnkStruct800006C4_2.vaddr`, `struct UnkStruct80000E80`), so the pointer was truncated
before it reached the DMA call. This is the general failure mode: structs that keep addresses in `u32/s32`
fields. Also `Util_Malloc(0x2C)`-style literal sizes no longer match the larger 64-bit structs.

`tools/ptr_warnings.sh` lists 894 compiler-visible truncation sites (710 explicit casts, ~110 implicit
narrowings, ~70 integer-to-pointer casts; 367 casts already widened). It cannot see struct fields that merely
hold addresses; run an AddressSanitizer build once the obvious ones are fixed.

## Stage 3 (in progress): boot to the N64 logo
`tools/try_run.sh 25` now gets through the whole N64-logo state: save banks load from an emulated flash chip,
`fragment35` (N64 logo) is "loaded", the scheduler/display-worker/RSP-thread pipeline runs, and about 20 graphics
tasks reach Fast3D (Metal) without errors (`[sp] task` lines with PORT_TRACE=1). The logo texture is addressed
through the segment table. NOT verified visually: no screen capture is possible in this environment, so whether
pixels are correct is unconfirmed. After the logo the intro scene crashes in `Font_Init`/`Font_LoadSizes`
(`main_pool_set_func`, memory_main.c:382) - see "Where it stops now".

What was added/changed in this stage:
- Fragment loader: `tools/gen_fragment_entries.py` -> `src/port/generated/port_fragment_entries.c` (ROM offset -> statically
  linked entry function, 72 of 77 fragments; 30, 31, 51, 74, 77 have no named entry symbol yet).
  `Fragment_Load` (PORT_STATIC_FRAGMENTS) still DMA-loads the blob (for its data image) but returns the table entry;
  `Memmap_RelocateFragment` no longer patches MIPS code.
- Address translation for the renderer: `Fast::gPortAddressResolver` (local patch in libultraship
  `src/fast/interpreter.cpp`, `SegAddr`) maps any address below 4 GB - segment (0x0Nxxxxxx) or fragment link
  (0x8Nxxxxxx) - through `Memmap_GetSegmentVaddr/GetFragmentVaddr`. Host pointers are always >= 4 GB on arm64 macOS
  so the two cannot be confused. The `& 0x1FFFFFFF` physical-address masks (139 sites) were removed and
  `osVirtualToPhysical` is the identity.
- Metal: `tools/make_o2r.py` packs Fast3D's shader sources into `pokestadium.o2r` and strips `thread` from helper
  parameters, which the macOS 27 Metal compiler rejects. The archive is rebuilt by CMake after linking.
- Flash/SRAM: command-level flash chip emulation (`os_devices.cpp`, file `pokestadium.flash`), SRAM, ROM reads.
- Pointer fixes found by running: Storage request queues take `OSMesgQueue*`; message-queue storage is owned by the
  port (the game's 4-byte-slot buffers overflow with 8-byte slots); `osRecvMesg` writes only as many bytes as the
  caller's variable (`_Generic` on the pointer type); scheduler client structs, `UnkArray4`/`unk_D_800A7450`,
  the display-list double buffers (Gfx is 16 bytes on the host), `mem_pool_init` (was aliasing MainPool/MemoryPool by
  32-bit offsets), `ALIGN*` masks, `GfxImage_Initialize`, direct reads from cartridge address space
  (`port_rom_read_u32`), hardware registers (`port_hw_reg`).
- Audio is compiled out of the boot path (PORT_NO_AUDIO): no bank init, no frame tasks; audio commands go to a dummy
  queue.

### ROM asset layer (stage 3b)
Done:
- BinArchive tables (header and entries) are byte-swapped on open; the per-file cache pointer lives in a 32-bit
  slot as an index into a handle table (`Port_PtrToHandle/Port_HandleToPtr`, jpeg_stream.c).
- Text files are `u32 count, u32 offsets[count]` big-endian; swapped once per table (`Text_GetStringTable`).
- The scene-graph arena (`MainPoolState`) was rewritten for 64-bit (its header is 0x20 bytes now, not 0x10, which had
  made every graph node overlap the state struct; `MainPool_AllocAligned/FinalizeAllocation/InitState`).
- Geo layout scripts (`game/src/geo_layout.c`): arrays hold one `uintptr_t` slot per original 32-bit word, each slot
  holding the word's numeric value, so fields are decoded with shifts (`GL_U8/GL_S16/GL_U32/GL_PTR` in
  geo_layout.h; `tools/geo_words.py` did the conversion). Verified: the boot diorama's layout executes all its
  commands and builds nodes.
- Controllers: pads are polled on the window thread and published as a snapshot; `osContStartReadData` posts the SI
  completion that libultraship never sends; Diorama input polling no longer blocks or touches Cocoa from a game thread.
- Util_ConvertAddrToVirtAddr passes host pointers through.

State now: the boot diorama (after the N64 logo) builds its scene graph and ~31 graphics tasks reach Fast3D, then it
dereferences NULL because the Pokemon models cannot be loaded.

### Stage 3c: blockers worked this round
How other projects handle these problems (research):
- The existing Pokemon Stadium (US v1.0) *recompilation* (mstan/PokemonStadiumRecomp, built on N64Recomp) avoids all of
  them by keeping the original 32-bit big-endian memory image and running the original MIPS translated to C; it never
  needs per-asset converters. Our decomp-based route needs the converters below.
- libultraship-based decomp ports (Ship of Harkinian, Starship, Ghostship/SM64) convert assets at extraction time with
  per-type tools (ZAPD / Torch) into host-format display lists, vertices, matrices and resources.
  We do the same conversion lazily at first use instead (no schemas for Stadium's formats exist yet).

Done:
- **Compressed-asset headers/magic.** `((u32*)magic)[0] == 'PERS'` compared a little-endian load with a big-endian
  constant, so no asset was ever recognised as compressed (every texture/model was loaded still compressed). Tag compares
  use `PORT_BE32`; PERS-SZP / PRESJPEG header words are swapped once after load; the Yay0 patch table is applied
  big-endian; fragment headers (`sizeInRam`...) are normalised in `Memmap_NormalizeFragmentHeader` (marker in `inst[1]`).
  BinArchive's first header word is two big-endian u16 (flags, fragment slot id), not one u32.
- **Model blobs** (`display_object_textures.c: Fragment_ProcessGeoLayout`): the blob's MIPS entry is decoded
  (`lui/addiu` -> table link address), the table and layout list are read big-endian, a host result struct holds 64-bit
  node pointers. Verified on the boot diorama: node trees build.
- **Packed layouts**: `geo_layout.c` decodes either the slot form (C arrays) or the packed big-endian form (ROM blobs);
  the form follows from `Port_IsRawData(command pointer)`.
- **Code pointers in data** (`tools/gen_func_table.py` -> `src/port/generated/port_func_table.c`, `Port_ResolveCode`): maps a 32-bit
  link address back to the statically linked function, keyed by the fragment loaded in that slot.
- **Raw display lists** (`src/port/gfx_translate.cpp`): F3DEX2 lists, Vtx arrays, matrices, viewports translated on first
  use and cached; hooked through `gSPDisplayList/gSPBranchList/gSPVertex/gSPMatrix` in `src/port/sdk/ultra64.h`. Tracks list-local
  segment bases (`G_MOVEWORD segment`). NULL branch placeholders become `G_ENDDL`. Sign-extended `s32` addresses are normalised.
- Asset-layer fixes found with AddressSanitizer (`cmake -DPORT_ASAN=ON`, `tools/asan_run.sh`): undersized `ModeSettings`
  destination, sine/cosine tables that the decomp split but the code indexes as one array (now one `gSineTable[0x1400]`),
  the 8-entry joint matrix table that was really 128 pointers.
- Audio API is stubbed (`tools/stub_audio.py`, PORT_NO_AUDIO), animation API is stubbed (`tools/stub_animation.py`,
  PORT_NO_ANIMATION: banks are big-endian blob data reached through the blob's MIPS entry).
- Pointer cleanup helpers: `tools/widen_idents.py`, `tools/widen_returns.py`, `tools/sync_protos.py`, `tools/fix_param_types.py`.

State: the boot diorama builds model scene graphs and renders ~88+ frames; it now stops in the renderer
(`ImportTextureRgba16` crash) because texture/palette descriptors referenced by materials are not decoded correctly:
`Port_ConvertDesc` assumes {fmt, size, count, pad, u32 data@+8}; the second descriptor (`unk_10`, the TLUT) dumped as raw palette
data, so the material's field mapping (`GeoRender_SetMaterialTexture` argument order / node field) needs checking against the
original struct. Use `PORT_VALIDATE_DL=1` (list validation, `[xlat]`, `[desc]`, `@bad-dl`) to see it.

### Open items (in order)
1. Texture/LUT descriptors and the rest of the per-model structs read by `geo_render.c` (materials, texture descriptors).
2. Animation banks (big-endian, reached through the blob entry stub with a1 selecting the bank).
3. Audio HLE, GB Tower core, Transfer Pak, and the pointer lists in "Known systemic problems".


## Status: boots into the main menu

Verified with the framebuffer capture aid (`tools/shot_run.sh`, `PORT_AUTOSTART` + `PORT_AUTOA`):

N64 logo -> intro diorama -> title screen ("PRESS START", JPEG background) -> Game Pak Check ("Game Pak None",
"OK?") -> main menu ("Please Select": Battle Now!, Pokemon Stadium, Event Battle, Options, description text).

Fixes that got here (each was a 64-bit / endianness / hardware-emulation gap):
- Matrices: `MtxF_ToFixed` and ROM matrices must be packed 32-bit words, not s16 halves.
- RSP JPEG microcode emulated on the host (`src/port/jpeg_task.cpp`); `Jpeg_ReadBits` needs explicit modulo-32 shifts.
- Fast3D: native size follows the color image width (320x240 vs 640x480 hi-res); render-to-texture (offscreen
  framebuffers keyed by the `G_SETCIMG` address, bound when a texture load falls inside the buffer, with the strip's
  row offset added to V); in-list segment pointers take priority over the port resolver in `SegAddr`.
- Font glyph map table lookup relied on the 8-byte `Gfx` layout (`Font_MapGlyphCode`).
- Task submission waits until the window thread has rendered the task (scenes free display-list data right after
  submitting the last frame; async rendering then read freed memory).
- Guards: SIGTERM exits immediately, `InRaw`/`ResolveRaw` reject non-host pointers, bad `SETTIMG` addresses ignored.

Known gaps: selecting a menu entry crashes on a direct cartridge address read (`0xb0000504`, an `IO_READ` that bypasses
`port_hw_reg`); menu cards are drawn duplicated/clipped (texture repeat of render-to-texture cards); model animation is
stubbed (intro models look "exploded"); audio, Transfer Pak and GB Tower emulation are stubbed.

Debug aids: `PORT_SCREENSHOT_DIR` (+ `PORT_SCREENSHOT_STUCK=secs`, `PORT_SCREENSHOT_NOSTOP`, `PORT_SCREENSHOT_ALL`, `PORT_SCREENSHOT_MAX_MB` (default 2048: no more changed frames past that size)), `PORT_ASPECT=4:3|16:9` (the
picture's shape, else the launcher's Screen option), `PORT_NO_BACKDROP_STRETCH=1` (2D backdrops stay 4:3 in widescreen), `PORT_NO_2D_LOCK=1` (2D screens are not kept
inside the 4:3 area), `PORT_NO_3D_SCENE_MARK=1` (the Elite Four room is taken for a 2D screen again, as before its fix), `PORT_NO_WIDE_PATTERN=1` (the menus' patterned backdrops stay 4:3 in widescreen), `PORT_NO_2D_CLIP=1` (2D shapes in 3D scenes are not cut at the 4:3 sides),
`tools/shot_run.sh <rom> [max secs]` (`STEP`, `KEEP` env; builds `build/shots/sheet.png`, deletes the PPMs, kills the
game), `tools/run_for.sh`, `PORT_AUTOSTART=<ms>` (taps Start; `PORT_AUTOA` taps Start+A), `PORT_SPEED=<n>`,
`PORT_TRACE_STATE`, `PORT_TEXLOG`, `PORT_TRILOG`, `PORT_MTXLOG`, `PORT_OPLOG`, `PORT_TRACE_JPEG`, `PORT_ALLOCLOG`,
`PORT_TEXDUMP`, `PORT_NO_RTT`. The window title shows the FPS.

## Deterministic runs: record, replay, jump straight to a bug

Time is virtual by default (lockstep mode, `src/port/os_devices.cpp`): the VI retrace and OS timers fire only when every
game thread is blocked, and `osGetTime`/`osGetCount` report that virtual time (the game seeds its RNG from them). The same
input therefore always produces the same run, and the speed is only a throttle: `PORT_SPEED=max` runs as fast as the host
allows (~20x; the intro-to-team-select path takes ~10 s), `PORT_SPEED=10` caps at 10x, default 1x.
`PORT_REALTIME=1` restores the old wall-clock threads.
`PORT_TRACE_PACING=1` logs hitches: retraces that came late in wall-clock time (with how long the game was busy),
presented frames more than 40 ms apart or slow to draw, and how long each new Metal shader took to build (also alone:
`PORT_TRACE_SHADERS=1`; see Shader warm-up). A Metal shader
now builds its pipeline only for the window's MSAA level (others on first use), not for every supported level. It also
sums RSP tasks per retrace (`[task]`) and lists the game threads when a retrace waits more than 150 ms. That found the
pause at boot and at the title-to-menu change (~0.35 s): each scene decodes its backgrounds in 300 JPEG tasks, and each
task waited for the next window-loop pass, which slept 1 ms; the loop now wakes when a task is queued
(`Port::WaitForRspTasks`), and the pause is ~20 ms.
Later measurement at maximum resolution (16:9, Resolution 200%) still showed the game held up 15-30 ms at each screen
change that decodes its JPEG backgrounds: each of the 300 tasks went to the window thread and back (~25 us each) and
cost a window-loop pass with its Cocoa event pump. JPEG tasks now run on the submitting game thread when no other task
is queued or in flight (`osSpTaskStartGo`; order unchanged, `PORT_JPEG_ON_WINDOW_THREAD=1` restores the old way), and
tasks queued back to back are run for up to 4 ms before the events are pumped again (main loop). Late retraces on
`to_battle` 18 -> 12, e.g. frame 740 held up 24 -> 13 ms; `tour_galmenu2` replays in 28 s instead of ~38. The other
pauses seen there are the game's own: the N64 loading waits of the intro and title (`Port_LoadStall`) and screen
changes where the game draws no new frame for 3-5 retraces (no late retrace: game time keeps its pace).

### Controllers 2-4

libultraship's `ControlDeck::Init` reports only controller 1 in the controller bits `osContInit` returns, so the game
greyed out every multiplayer choice (Free Battle with 2-4 players, team battles such as 1P & 2P vs COM). The game now
takes its bits from `Port_ControllerBits` (`os_devices.cpp`, called in `Cont_SetupControllers`): controller 1 always,
2-4 when the launcher's Controls panel switches "Plugged in" on (`gPort.Controller<N>.PluggedIn`) or
`PORT_CONTROLLERS=1,2` lists them. The game reads the bits once at boot, so a change applies the next time it starts.
Recordings made with more than one controller carry a `# controllers 1 2` line; a replay uses that line and nothing
else (no line: controller 1 alone), so old recordings and the settings never change a replay. Checked on the Free
Battle rule screen (`free_battle_gamepak.rec`, frame 3000): unchanged by default, "1P & 2P" offered with the env
variable or the recording line; the evidence pilot's two-player team battle (`free_battle_team_1p2p.rec`) plays
through with pad 2 driven from the recording.

### Frame pacing: retraces follow the display

At normal speed the VI retraces are timed by the display the window is on (`RetraceSchedule` in `os_devices.cpp`,
display clock in `src/port/pacing_mac.mm`): when it refreshes at a whole multiple of 60 Hz (within 2%: 59.94, 60, 120,
240 Hz), a retrace comes 0.5 ms after every n-th vblank (n = refresh / 60; `PORT_PACING_LEAD_US` changes the 0.5 ms).
The game then draws its frame early in a refresh and every frame stays on screen for the same number of refreshes; a
30 fps scene shows each frame for exactly 2 refreshes at 60 Hz (4 at 120 Hz). Other refresh rates (75, 144 Hz),
`PORT_SPEED` other than 1 and `PORT_DISPLAY_PACING=0` keep the free-running 1/60 s clock. Only wall-clock timing
changes: virtual time, and with it every replay, stays the same. Only macOS reports the display's vblanks so far
(CVDisplayLink); Linux and Windows keep the free-running clock.

What it fixed (measured with `PORT_TRACE_PACING`):
- The retrace thread's sleeps were coalesced by macOS and ended up to 7.5 ms late; the clock then caught up with the
  next retrace (the 7 ms / 27 ms gaps). It now runs with a time-constraint (real-time) thread policy and wakes within
  0.05 ms (`Port_PreciseTimingThread`).
- The free-running clock drifted through the display's refresh. On this Mac (and the window server's compositing) a
  frame handed to the GPU anywhere in refresh N is shown at the start of N+2, so a frame finishing right at a vblank
  is shown a refresh early or late, and a 30 fps scene alternates 1 and 3 refreshes per frame. With retraces 0.5 ms
  after the vblank, frames finish 2-6 ms into the refresh, far from it. At 120 Hz the refresh is half as long and the
  free clock hit that edge a third of the time (estimated from the 60 Hz measurements; no 120 Hz display was at hand).
- libultraship's frame limiter (sleeps after a present until 1/60 s after the previous one) caps at 64 fps while
  retraces follow the display, so it can never push presents later on a display slightly faster than 60 Hz.
- `PORT_SPEED` > 1 turns vsync off for that run; the config was saved with it (first run, settings menu), and later
  normal-speed runs from that folder (`build/` after `tools/run_replays.py`) presented without vsync: frames went out
  whenever they were done (about a third of the shown intervals off the 1/60 s grid). The player's value is now kept in
  `gPort.VsyncSaved` and restored by the next normal-speed run. A config that already holds `"gVsyncEnabled": 0` from
  before this is not changed: set it back to 1.

`PORT_TRACE_PACING=1` prints every 600 retraces, for the last 10 s and in total, per event (`retrace`: delivered to the
game; `drawn`: frame handed to the GPU; `shown`: on the display, from Metal's presented handler, macOS): interval
percentiles, the share off the 1/60 s grid (more than 2 ms from a whole number of 1/60 s), rhythm breaks (an interval of
a different number of 1/60 s than the one before) and how many intervals were 1, 2, 3 ... 1/60 s. `PORT_TRACE_PACING=2`
also lists every interval with its phase in the display's refresh. The window must be visible to measure `shown`:
macOS does not present a covered window; `PORT_WINDOW_ON_TOP=1` keeps it above other windows without taking the focus.
Other game instances running at the same time (parallel test runs) disturb the numbers.

### Slow GPUs: game time keeps up, sound does not stutter

Reported on an Intel HD 520 (Windows): low frame rate (a battle at 43.8 FPS with Smooth motion, 60 normally) and
stuttering sound. In lockstep mode a retrace waits until the window thread has drawn the frame the game submitted, and
the game makes its sound per game frame (audio_out.cpp), so drawing that took longer than real time slowed game time
itself: slow motion, and sound produced at ~70 % of the rate the device plays, which the playback-rate control (±3 %)
cannot absorb, so the queue ran dry and refilled over and over. With Smooth motion each 30 fps battle frame got an
in-between frame on the same thread; at ~22 ms per draw the two no longer fit in 33 ms.
- Retraces now make up a lag of up to 100 ms (`gClockLagNs`, os_devices.cpp; free-running and display-paced clocks)
  instead of restarting after one late retrace; a longer stall (loading) is still not made up.
- While the clock lags by more than a retrace, game frames are only tracked, not drawn (`WantSkipWhileBehind`, at most
  2 in a row), with the same safety as fast runs: a frame that renders to a texture or draws over the previous picture
  with a changed amount of drawing is drawn after all (battle frames usually are). `PORT_NO_FRAME_SKIP=1` turns it off.
- In-between frames (interp.cpp `PumpInterpolation`) are dropped when the game already waits for its next frame or the
  clock lags by more than 4 ms: Smooth motion never slows the game.
- The sound queue's target grows by half (up to 120 ms) after the queue ran dry without a long stall, past the first
  600 retraces (boot underruns do not count).
- `PORT_DEBUG_DRAW_MS=<ms>` adds that much to every drawn frame, to try this on a fast machine.
- The launcher's Graphics panel sets these and the drawing cost: Internal resolution and Scaling (see "Internal
  resolution and scaling" below), Anti-aliasing (`gMSAAValue`, `SetMsaaLevel`), VSync (`gVsyncEnabled`, read by the
  backends every frame), Smooth motion, Keep full speed (`gPort.FrameSkip`), and the Low-power preset (2x, MSAA and
  Smooth motion off). `PORT_LAUNCHER_OPEN_GRAPHICS=1` opens it (with `PORT_LAUNCHER_SHOT` for a picture). The
  handheld (Android) launcher does not have it yet.
Measured on an M-series Mac (debug battle to frame 1500, free-running clock as on Windows, `PORT_TRACE_AUDIO_OUT=1`):
Smooth motion with 22 ms draws: game speed 70 % → 96 %, sound queue refills 653 → 1 (at boot); 30 ms draws: 94 → 97 %,
7 refills. Without Smooth motion and 40 ms draws (a battle frame alone takes longer than 33 ms; battle frames cannot be
skipped safely) game time stays at 74 %, refills 735 → 243. No change without the added delay; pictures at fixed game
frames are identical with and without it (menus, battle with Smooth motion).

- Every run records the pad state per frame to `build/last_input.rec` (`PORT_RECORD=<file>` to choose another name). A
  crash ends the recording at the crash frame, and the crash report prints `game frame N`. Packaged builds (AppImage,
  macOS app: `SHIP_HOME` set) write it to their settings folder instead (`~/.local/share/pokestadium`,
  `~/Library/Application Support/com.team-hippodrome.pokestadium`). On Windows, `pokestadium.exe` has no console
  window; stderr reaches the console it was started from, a redirection (`2> log.txt`), or with `PORT_CONSOLE=1` a
  console window of its own.
- `tools/run_replays.py` is the quick sweep (~1 minute on an M-series Mac): it plays the stored recordings with the game
  window hidden (`PORT_HIDDEN_WINDOW=1`), as many at a time as there are cores (minus two), each from a fresh copy of its
  save (`replays/<name>.flash`, else `replays/default.flash`), up to the stop frame in `replays/replays.txt` (default:
  the recording's end), and prints OK / DIFF / BADADDR / FREEZE / CRASH / TIMEOUT per recording. BADADDR means the log
  has `[timg] bad` or `[dma-bad]` lines (a wild texture or DMA address, typically big-endian ROM data read unswapped;
  the cup-win crash showed up that way). The stop frame is compared with a local reference picture in
  `build/replays/ref/` (written on the first run, or with `--update`; game pictures are not committed). The
  `replays/tour_*` recordings are the smallest set of the ares comparison tours that together reach every game module
  (fragment) any tour reaches (17 of 241, chosen from a run of all tours with `PORT_TRACE_FRAGMENTS=1`). `--all` adds
  the long recordings; names run only those. A Game Boy cartridge (Transfer Pak tours) is your own dump in `gb/`; a
  recording that needs one is skipped without it.
- `tools/replay.sh <recording> [frame]` replays it at max speed and stops at `frame` (default: where the recording ended,
  i.e. the crash), saving that frame as `build/shots/stop_<frame>.png`. `STOP=live` as frame keeps running with live
  input afterwards. Verified: three replays of a crash give identical state changes and crash at the same frame.
- Recordings survive rebuilds (they hold input, not memory), so the loop is: reproduce once, copy the recording into
  `replays/`, fix, replay. A raw memory snapshot would not survive a rebuild: code addresses (function pointers, return
  addresses in thread stacks) move.
- `PORT_STOP_AT=<frame>` stops any run at that frame; `PORT_TRACE_STATE=1` prints game-state changes with their frame;
  `PORT_TRACE_TIMERS=1` logs OS timers; a run that stops advancing prints `[clock] ... not idle` plus the thread table.
- Saved recordings: `replays/cup_select_vtx_crash.rec` (crashed at frame 3892 before the vertex-array fix below),
  `replays/team_select_all_ready.rec` (reaches the rental team "All ready!" screen).
  `replays/cup_giveup.rec` (Poké Cup, first battle, Start → Quit → Yes: the LOSE/WIN animation at frame ~3600 and the
  cup grid with the opponent's portrait at 4200; was Bruno's face and garbled smoke, both big-endian ROM reads).

### Fixed in this round
- Text tables: whether a table is already byte-swapped is decided from its contents (a reloaded file can land at an old
  address; the address cache left it unswapped and the Transfer Pak screen crashed in `Font_Printf`).
- `DisplayNodeHeader.callbackArg` is `intptr_t` (it carries display-list pointers; truncated -> crash in the Stadium
  carousel callbacks).
- Adjacent vertex arrays: the game indexes across consecutive `Vtx` symbols (geo layouts with a vertex count spanning
  two arrays, `gsSPVertex(D_84B12498, 32, 0)` on a 31-entry array). `tools/merge_vtx_arrays.py` merged 28 such runs
  back into single arrays (later names become `#define D_x (D_first + n)`); before that, recolouring the cup-select
  banner overwrote the display list behind it.
- Fast3D: G_VTX with a garbage address is skipped with a `[badvtx]` dump instead of crashing.

## Status: reaches the battle (replays/to_battle.rec)
`tools/replay.sh replays/to_battle.rec 5500` shows the battle menu with both Pokémon's status boxes (L50, HP). The
recording picks Poké Cup rentals with A, C-up, C-left and confirms with A.

Fixed on the way (see DECOMP_FINDINGS.md for the decomp-side facts):
- Rental and trainer rosters are big-endian ROM data: swapped on load (`Port_BattleMonFromBE`, session.c and
  fragment61_2D78B0.c). Before, EXP read little-endian made every Pokémon L100 and the Poké Cup level cap blocked the
  third pick (it looked like C-buttons did not work).
- `stadium_models` entries are code: `Port_CallBlobEntry` (memmap.c) interprets their small MIPS lookup function.
- Pointer-in-int fields and two-view layouts: `BattleSessionTeams.unk_20`, `unk_func_8001C248`, tick-slot argument,
  model table views, resource tables (`Port_ConvertResourceTable`).
- Model animation is still compiled out (`PORT_NO_ANIMATION`); its "done" predicates now report finished, so battle
  steps that wait for an animation proceed.
- Fortified libc traps on original small-buffer overflows (sprintf/bzero): buffers enlarged.

Freeze detection: with `PORT_FREEZE_CHECK=1` (default for replays and screenshot runs) the presented picture is hashed
every rendered frame; if it does not change for `PORT_FREEZE_FRAMES` game frames (default 300) the clock prints
`[freeze]` with every game thread, the queue it waits on, how often it has blocked (rising = still looping) and where
(`@stack` lines; `tools/symbolize.sh` / atos). `PORT_EXIT_ON_FREEZE=1` ends the run after three reports.
`PORT_TRACE_BATTLE=1` logs the battle scene director's phase/step changes.

Known open: an intermittent renderer crash around frame 488 (intro, garbage display list; ~1 in 4 runs), Pokémon
models render as cyan silhouettes in battle, names in the status boxes miss glyphs, trainer portrait boxes are black.

## Struct layout checker (tools/layout_check.py)
Finds code that only works with the N64 struct layout. ~8 s for all 349 game sources:
1. clang `-fdump-record-layouts` for the host and for a 32-bit big-endian MIPS target (`tools/mips_sysroot` supplies
   the two libc headers the sources pull in): exact host and N64 offsets of every field.
2. clang `-ast-dump`: every conversion between two different struct pointer types (casts and implicit conversions,
   e.g. passing a `BattleSessionTeams*` to a parameter of another struct type).
3. For each converted pair, fields that sit at the same N64 offset must sit at the same host offset in both types.

Output in build/layout: `report.txt` (mismatching pairs with the conversion sites), `summary.txt`,
`comment_errors.txt` (decomp offset comments that disagree with the real N64 layout; 84, for the decomp).
`tools/ptr_warnings.sh` complements it for pointers stored in integers (compiler warnings, build/ptr/warnings.txt);
the harmless kinds (ROM offsets, segment symbols, size arithmetic) can be filtered as in build/ptr/suspicious.txt.

Fixed from the first run: `BattleSessionTeams.unk_04` (holds a `BattleMon*`; model_renderer read it as `TeamRoster`),
`Model_LoadWithScale` result, particle callback argument (`unk_D_8140E728_01C_004_1_04/08`: s32 -> intptr_t),
`FontContext.unk_4C` (glyph source cache key), minigame select (`unk_D_82508B30_03C` and sushi `unk_D_86808808_018`
were partial `DisplayObject` views padded to the N64 size: now real `DisplayObject`s; slot pointers typed),
sushi conveyor objects (pointer-sized slot at 0x008).

Lab PC / Pokédex widget system (26 pairs, all fixed; N64 sizes/offsets of every touched struct re-checked against
the MIPS layout dump):
- Controller-style widgets (header + input callback @0x10 + set-state callback @0x14: `WidgetGridMenu`,
  `unk_func_8850CD44`, `unk_func_8830867C_02C_04C`, `unk_func_8821421C_02C_06C_02C`, `unk_func_8830867C_048_04C`,
  `unk_func_8820B12C_02C`) now start with a real `WidgetLinkHeader` (or pointer-typed equivalents) instead of
  `char[0x10]`/`char[0xC]`+pointer, so the menu code's `child->unk_14(...)` hits the callback on the host.
- The append/init typedefs (`func8850BF80`, `func8850CC74`, `func88500668`) carry the real signatures
  (`WidgetLinkHeader*` child / node) instead of `WidgetNode*` / `unk_func_882025E0*`.
- Scrollable-grid helpers in widget_tree (`WidgetTree_BindScrollableGridData`, `..._SetScrollableGridIndex*`,
  `..._Clamp*`, scrollbar target, ...) take the 0x70-byte core `unk_func_8821421C_038_034` they actually use; the
  pokedex / lab_pc_list re-declarations of that core (`unk_func_888044BC_038_02C_000`, `unk_func_8830867C_04C_030`,
  `unk_func_8830867C_02C_0B4_030`) wrap the paged-grid part in an anonymous struct (same host padding as the nested
  `unk_func_88200FA0_030_030`) and use pointer types where the core has pointers.
- Real host bugs this removed: `LabPC_ReleaseSwapWidgets` read the swap screen's tab widgets through the wrong type
  (now `unk_func_8820BE14_02C*`, `unk_44[2/4/5/3/7]`); PC list sort comparators read `unk_func_88316128` 8 bytes
  early (its first 8 N64 bytes are a pointer + s32); `WidgetTree_InitChildWidgetGroup` allocated its pointer array
  with `* 4`.
- Pointer-only uses are typed `void*` (`TableView_Initialize` table base, `LabPCList_CompareByPointer`, the reused
  `temp_s0` in `func_88209B54`, lab_pc_1AE680.c).
Not run yet: the Lab needs a Game Pak (Transfer Pak is stubbed); `replays/to_lab_gamepak_gate.rec` reaches the
"A Pokémon Game Pak is not inserted" screen in the Lab (frame 1604) for when it is emulated.

Open (7 pairs left): sushi game types that conflate
different objects, GB Tower's hand-built `OSTask` (`unk_D_8122B1E8`; its custom microcode is not run anyway), and
harmless ones (`Geo_NodeReference`, `main_pool_set_func`, 64DD code in rsp.c).

## Renderer: state that persists across frames (this round)
- Skipped frames (render divisor at speed, and frames dropped by libultraship's frame pacing) used to skip their
  whole display list. The N64 keeps the RSP segment table and the RDP tile/TMEM state across tasks, so later frames
  used stale segments (the intermittent frame-488 intro crash: a stale segment pointed into a reused buffer) and
  missed texture loads. `Interpreter::TrackSegmentsOnly` now walks skipped frames and applies segment writes and
  texture loads (SETTIMG/SETTILE/SETTILESIZE/LOADBLOCK/LOADTILE/LOADTLUT); frames that render to a texture are drawn.
- Invalid F3DEX2 opcodes end the current display list (a freed buffer already holding asset data is no longer run).
- Full-screen fill rectangles are widened so that they also cover the widescreen margins. The new size used to be
  fixed at -1024..2048 (10.2 format: -256..512 pixels), which is right for 320x240 screens only. The Trade Machine
  draws at 640x480, so its clear at the start of every frame stopped at x 512 and left the right fifth of the screen
  untouched. Below the machine picture nothing else paints there, so the corners of earlier panels stayed: a
  Pokémon's detail panel ("/40" PP, "No") next to the box grid, in 4:3 as well as 16:9 (a Windows tester's
  screenshot). The margin now scales with the native width; 320x240 screens get the same numbers as before.
  `trade_box_pages.rec` (evidence/whole_game/routes/menus_trade) shows it from frame 9680.
- Texture cache keys include a hash of the texel/palette bytes (RAM textures change in place).
- Render targets carry a 16-byte marker in RDRAM; once the CPU overwrites it the target is dropped (fixed the
  missing letters: glyphs in a font file loaded where a render target used to be).
- `Color_RGBA8_u32.rgba` holds the bytes r,g,b,a in memory order; numeric 0xRRGGBBAA uses go through PORT_BE32
  (fixed the red channel: cyan/green Pokémon models).
- TMEM regions: RAM texture loads are copied at load time and tiles resolve their TMEM address against the loaded
  regions (fixed the far half of the arena floor). A load now only replaces the bytes it covers: the parts of older
  loads outside it stay readable, as on real TMEM.
- Render-target readback: when the game stops drawing into a render target, its pixels are copied back into the RDRAM
  buffer as big-endian RGBA5551 (`Interpreter::RttReadback`; the first 16 bytes keep the render-target marker, so the
  GPU target stays bound for normal texture use). `PORT_NO_RTT_READBACK=1` disables it. No measurable cost.
- Arena floor (fixed; checked against ares, see "Reference frames from ares"): the floor's combiners are
  `(TEXEL0 - TEXEL1) * ENV_A + SHADE` and `(COMBINED - ENV) * PRIM`, a detail effect where tile 1 is the same 32x32
  sand texture loaded again with shift 2/3. That second load is emitted each frame by the floor node's geo callback,
  `ParticleGfx_GraphNodeBuildDescriptorMaterialList`, which the stadium model names through a trampoline
  (0x810001D0 in fragment 31's jump table, see DECOMP_FINDINGS). `Port_ResolveCode` returned NULL for the trampoline,
  so the callback was dropped and tile 1 kept the HUD's setup from the previous frame: white and cyan overflow. Now
  trampolines are followed (only to callbacks checked to handle ROM callback data; the others are logged with
  PORT_TRACE_GEO and skipped as before), and the callback converts its big-endian material list (cached) and reads
  the node's real callbackArg. `arg1_func_84344CE8` (battle effect display-list nodes) was a flat node view whose
  display-list field overwrote the host GraphNode's callback; it is now `{ GraphNode node; Gfx* unk_18; }`.
  Debug: PORT_DRAWLOG=1 logs `[seq]` loads/tiles/TLUTs/combine/env in order and `[tex1]` for every distinct draw that
  samples tile 1 (tile parameters and the first TMEM bytes).
- Frame buffer keeps its picture between frames (like the N64's; the port always renders into the game framebuffer
  and clears only depth at frame start). Menus draw their static background only in the first frames of a screen and
  then only what changes (the order screen: Poké Ball background, computer's panel, labels and hint row were missing).
  Skipped frames (render divisor / dropped frames) are drawn anyway when they draw over the previous picture (no
  full-screen fill rectangle) and the amount of drawing changed from the frame before (`TrackSegmentsOnly`); steady
  animation is still skipped. PORT_SKIP_INCREMENTAL=1 skips them anyway; PORT_TRACE_FILL=1 logs the decision. Cost:
  menu route 7.4 s -> 9.7 s.
- Window resize on such a screen (user report: Free Battle's rule screen went black): a size change recreates every
  game framebuffer's colour texture, and its first pass cleared it to black (`sFreshColorFbs`), so the background
  drawn in the screen's first frames was gone until the next screen. Every backend now puts the old picture into the
  new texture, scaled uniformly so the 4:3 picture keeps its place (the rectangle: fast/backends/resize_keep.h);
  margins the old texture did not cover stay black. Metal and DX11 draw a quad sampling the old texture
  (`DrawOldPictureScaled`; DX11 saves and restores the context state it changes); OpenGL copies the old picture aside
  and blits it back scaled once the depth renderbuffer has its new size too (a framebuffer draws only where all its
  attachments reach: blitting earlier stopped at the old size), with the scissor test off meanwhile; Vulkan keeps the
  old image until `vkCmdBlitImage` has scaled it into the new one. With MSAA on, Metal and DX11 draw into the
  multisample buffer; OpenGL's multisample renderbuffers still start cleared and Vulkan only fills the single-sample
  image (MSAA not tested on any backend).
  Checked with `PORT_DEBUG_RESIZE` (now a list, `f:WxH,f:WxH,...`) on `tour_fb.rec` at 3000 -> 1100x700, 800x600,
  500x700 and a 20-step drag on Metal, OpenGL and Vulkan (MoltenVK, `-DLUS_ENABLE_VULKAN=ON`), and 1100x700 on DX11
  (llvm-mingw x64 build under Wine): the background stays (slightly softer after the drag, from the repeated bilinear
  scaling); with `PORT_NO_RESIZE_KEEP=1` (the old behaviour) every backend went black. `PORT_DEBUG_RESIZE` resizes
  through the window backend (`SetCurrentDimensions`): DX11's window is a Win32 one, not SDL's.
- Copy mode with alpha compare skips texels whose alpha bit is 0 (`use_alpha` forced for that case; the shader only
  discarded with alpha blending): fixed the white boxes behind the order screen's icons and pointer.
- Geo callbacks reached through fragment 31's jump table: all 60 targets are followed now. Their callback data from
  ROM models is converted where it is read: battle effect nodes (`Port_CbTexture`, `Port_CbTexPair`), the
  particle_data_library `DisplayList_*` callbacks (`Port_PdlWords` for address lists, frame records, frame ranges;
  their node views are real nodes now, `PDL_NODE_WITH_DATA`), MiniFx (`Port_MiniFxTexPair`, `Port_MiniFxColorRamp`)
  and the material lists. `DisplayList_SetFlag*` switch the previous sibling node's render flag.
  `DisplayList_BuildConditionalSegment` read the converted list through an `s32*` and called the low 4 bytes of the
  host pointer (`de 09900e30`), which the renderer skipped: Ditto, the model that uses it, lost its surface list
  (G_TEXTURE_GEN, TEXEL0 x PRIM x SHADE, its pink texture) and drew flat white with its node's PRIM x SHADE
  material, on every screen (rental details, VS grid, battle). In ares, overwriting the texture turns Ditto green.
  The converted lists and frame records are cached by host address, and each entry now keeps the raw bytes it was
  made from and converts again when they differ (user report: Rapidash's flames flat yellow with red and white
  patches, changing with the pose, after Ponyta's rental detail had been open; ares: correct). The rental screen loads
  every Pokémon's model into the same block, so Rapidash's animated flame record (eight texture frames,
  `Port_PdlFrameRecord`) sat where Ponyta's had been and the cache returned Ponyta's texture addresses, which inside
  Rapidash's blob point at its body and eye textures. A full table reuses its entries in turn (it converted into a
  new buffer on every call once full). The other converters with an address-only cache (`Port_CbTexPair`,
  `Port_MiniFxTexPair`, `Port_MiniFxColorRamp`, `Port_ConvertMaterialList`) are unchanged so far.
- Per-species slot effects (`StadiumModelSlot`, model_renderer_callbacks.c / 33FE0.c): `StadiumModel.modelSegment`
  holds a pointer and `unkSegment` is a whole `ModelSegment` (was u32 + padding: the copy overran `position`); the two
  slot-model templates (u32 words: s16 tables, Vtx, F3DEX2 lists) are converted to host layout on first use;
  `ModelRenderer_GetActiveMode/GetObjectType` return 0 when no model part is current (callbacks call them at
  installation).
- Model previews (`PokeIcon_RenderPreview`): the returned image was kept in s32 fields (rental screen, trade machine)
  and the preview struct kept pointers in s32 (`unk_0C`, `unk_14`): widened; the rental screen shows the 3D model.
- Battle input: after BATTLE (A) the moves are on the C buttons (no list on screen, same on hardware).
  `replays/boot_battle_move.rec` (with PORT_BOOT=battle): BATTLE, C-Up, then a full turn (both sides attack).
- (fixed later: the portraits are a second camera per side; see "Per-species battle records".)

## Fast test route and loop detection
- `replays/to_battle.rec` is now a scripted fast route (tools/rec_script.py): A+Start every 20 frames from boot, then the
  Pokémon picks (A, C-up, C-left) as soon as the order screen accepts input (frame ~1690; its first ~20 frames are a
  transition that ignores input), confirm with A. Battle menu at frame 2410 (was 5198); `tools/replay.sh
  replays/to_battle.rec 2700` takes ~7 s. The old recording is replays/to_battle_slow.rec.
- Freeze detection counts a picture as progress only if it was not presented in the last PORT_LOOP_WINDOW frames
  (default 600); channels are quantized to 3 bits so the per-frame dither noise does not count. Static screens and
  short animation loops are reported (`[freeze] ... looping over N pictures`). A continuously moving camera (the idle
  battle menu) always produces new pictures, so test runs should end at a stop frame: tools/replay.sh stops at the end
  of the recording by default. PORT_TRACE_PICTURES=1 logs every new picture.
- Trainer panel: `PresentationLayout` was a flat view of the camera node with its viewport fields after a pointer; it is
  now the camera type (36 field accesses renamed). The panels slide in correctly; the portraits render 3D content but
  are framed oddly (model animation is still off) and the player's portrait is still empty.
- Stop on a condition: `UNTIL='<regex>' tools/replay.sh <rec> live` watches the game log and sends SIGUSR1 when a line
  matches; the game then saves the next drawn frame (build/shots/stop_<frame>.png) and exits. Example: battle menu
  reached: `PORT_TRACE_BATTLE=1 UNTIL='step 11' tools/replay.sh replays/to_battle.rec live` → stops at frame ~2420,
  ~6.5 s wall. A stop frame no longer freezes the clock (the game draws only every second frame, so a frozen clock could
  wait forever for the frame to capture).

## Debug boot (PORT_BOOT)
- `PORT_BOOT=battle` starts the game in its own "Battle Now!" state (0x28: `Team_BuildQuickBattleTeams`, then the
  order screen and the battle fragment). With PORT_BOOT set, the player's order picks are made automatically (first
  slots, then confirm; battle_order_select.c), so no input is needed: battle intro from frame 508, battle menu at
  ~1150. `PORT_BOOT=battle tools/replay.sh - 1150` → ~1.8 s wall (the menu route needs ~6.5 s).
- `PORT_BOOT=battle2p`, `PORT_BOOT=menu` (main menu, skips intro/title), `PORT_BOOT=<number>` (any state from
  variables.h). Runs stay deterministic (teams come from the game's own quick-battle builder).
- `tools/replay.sh -` runs without a recording; combine with a stop frame or UNTIL='<regex>'.
- Seen in the Battle Now scene: Psyduck renders green (another red-channel loss, not yet tracked down).
- Nickname colours (cross-checked, game behaviour): `Model_ComputeNicknameColorVariant` (model_renderer.c) gives a Pokémon a
  colour variant only when its nickname differs from the species name. The value lies in the species' range
  (`D_8006FF00[species-1].unk_10..unk_12`) and is picked by a checksum of OT ID + nickname + OT name; it is applied to
  the model's textures (`func_80018C40` → Color_AdjustRGB5551/RGBA8). Un-nicknamed Pokémon get 0 (normal colours).
  The green Psyduck seen with PORT_BOOT=battle was a debug-boot bug: the species names are copied into their slots by
  `Text_LoadNameTable`, called from the N64-logo intro which the boot skipped, so every name compared against the
  placeholder "pmname_NNN" and every Pokémon counted as nicknamed. The boot now runs the intro's init
  (Save_EnsureBankLoaded(2), Text_InitStringTables, Text_LoadPlayerLabels, Text_LoadNameTable); PORT_TRACE_VARIANT=1
  logs the inputs and result of each variant computation.

## Model animation (enabled)
`PORT_NO_ANIMATION` is gone; models animate (battle Pokémon idle/intro poses, the intro's animated scenes).
- Animation banks: the model root node's `unk_28` (unk_D_86002F34) is the function returning the bank. For ROM models
  it is the blob's MIPS entry point, run by `Port_CallBlobEntry`; the bank is the blob's big-endian model table
  (u8 transform count +4, event-track count +5, u32 addresses of the transform / event-track address arrays at +0xC /
  +0x10). C-defined models get a native call (`Port_ModelAnimBank` / `Port_ModelAnimTrack` in model_renderer.c).
  `ModelAssetNode.getAnimationBank` sat at a different host offset than the root node's real field, so the root is
  read through its real type.
- Animation data (model_animation.c, model_animation_events.c) is read through accessors that byte-swap when the data
  lies in raw ROM data (model_animation.c: `Port_IsRawData`, blobs and segment/fragment-mapped data; events:
  `Port_VIsRaw`): TransformAnimData / EventTrackData headers (32-bit addresses), channel
  entries, s16 sample arrays, bit-packed streams (big-endian 16-bit words) and keyframe records.
- `ModelAnim_BindEventTrack` takes the track address as intptr_t (was s32).
- The battle intro's "wait for the send-out animation" steps now take the real animation time (step 5: 14 frames).
- Checked: PORT_BOOT=battle runs 20 000 frames without crash; the menu route (replays/to_battle.rec) still reaches the
  battle menu (frame 2422).
- The body-part gaps seen earlier were the unanimated bind pose; with animation on, intro and battle close-ups show
  closed models.

## Per-species battle records (portraits, camera framing)
`func_84302658` (battle_engine_2EC3C0.c) DMAs per-species records straight from the big-endian ROM; they are now swapped
after each copy:
- `SpeciesModelTransform` (`sSidePresentationTransform` portrait framing, `sSideCameraTransform` move-camera framing; table `D_6E910`, 0x20 per
  species): Vec3f offset + four s16 (yaw, pitch, alt-record index `unk_10` (0xFF = none), animation frame). Read raw,
  the portrait cameras (layers 1/2, `PresentationState.unk_168`) looked into empty space, so the portrait boxes were
  empty; it also drove the battle cameras' framing/zoom.
- `sPokemonModelDimensions` (4 floats: model height etc., used to place models) and `D_84390048` (`SwayLayoutConfig`, 6 floats).
- `BattleAI_LoadConfiguration` (battle_engine_361050.c): the three `AIDifficultyConfig` flag words (s32 at +0).
The 0xBC0-byte `MovePresentationCue` block and `TagTeamPartners` are bytes only.

## Reference frames from ares (tools/ares_ref)
A headless ares (N64 core + paraLLEl-RDP through MoltenVK) driven by a script gives real-hardware-accurate frames,
RDRAM dumps and save states, so port output can be compared with the original instead of guessed.
- Build: `tools/ares_ref/build.sh <dir>` clones ares, adds the driver target and builds it (needs cmake, ninja and
  /Applications/ares.app for libMoltenVK; `brew install --cask ares-emulator`). Boot logic follows
  michiiik/N64Recomp `ares-bridge`.
- Run: `ARES_REF_BUILD=<dir>/build ARES_REF_SRC=<dir>/ares tools/ares_ref/ref.sh <script> [state]`. Script lines:
  `hold`/`tap` buttons (A B Z START L R UP DOWN LEFT RIGHT CU CD CL CR, joined with +), `shot <frame> <file.ppm>`,
  `every <n> <dir>` (pictures plus a contact sheet `<dir>/sheet.png`), `save <frame> <state>`, `dump <frame> <file>`
  (8 MB RDRAM, big-endian), `fill` (poke RDRAM), `end <frame>`. Frames are VI frames; ~200 fps.
- Routes used (from the title-screen state, frame 1500 after boot): Start; A at the Game Pak check; on the main menu
  LEFT selects Battle Now (default is Pokémon Stadium, which mashing A+Start turns into a Poké Cup battle); mash
  A+Start; at the order screen pick (e.g. A, C-Up, C-Left) and confirm A, A. The computer's lead is random.
- Findings so far: the arena floor (above); in real Battle Now the floor display lists are byte-identical to the
  port's, so differences come from the renderer or from game code the port skips.

## Comparing with ares: tools/tour.py, tools/match_frames.py (and what they found)
- `tools/tour.py <name> "<script>"`: one input script (tokens as rec_script.py plus `shot:<label>`) runs on the port and
  on ares (from `build/ares_title.state`, the title screen 1500 frames after boot, when the script starts there);
  writes side-by-side pictures (port left) to build/tour/<name>/. `--port-only` reruns just the port side and reuses the
  ares pictures. build/ares/{build,src} link to the ares build (tools/ares_ref/build.sh).
- `tools/match_frames.py <port dir> <ares dir> <out>`: pairs each port picture with the most similar ares picture, for
  scenes whose timing differs (the intro runs ~200 frames earlier in the port: ares boots slower).
- PORT_SHOT_AT=f1,f2,...: the port saves those frames (stop_<frame>.ppm) without ending the run.
Fixes found this way:
- Render targets: a new GPU target starts with an empty viewport/scissor; Fast3D's cached viewport made it skip setting
  them when the target had the same size as the previous one (Game Pak check: cards 2-4 empty). Reset on every switch.
- Render-target tiles loaded from inside a row (LoadTextureTile with uls > 0) now get their column offset (main-menu
  card strips, the Game Pak warning bar).
- One persistent GPU framebuffer per N64 frame buffer address (the game alternates 2-3 buffers and updates each
  incrementally). Skipped frames are drawn when they do not repaint the whole screen and their buffer's amount of
  drawing changed (`TrackSegmentsOnly`). Menu route ~12 s.
- C arrays of u16/u32 words are little-endian on the host: textures/palettes read from them by the RDP now get their
  bytes swapped when copied into TMEM (`WordArrayAt` + generated list build/generated/word_arrays.c from
  tools/gen_word_arrays.py). Fixed the intro's sky (yellow/green streaks) and underwater backgrounds.
- `ParticleGfx_GraphNodeBuildDescriptorMaterialList` also takes C-defined material lists (native pointers to
  big-endian byte records: the intro sky's cross-faded cloud layers).
- `PokeIcon_AdjustImageColors` works on big-endian texels (nickname colour variants of icons: the order screen's
  computer team had wrong colours and a gold box behind KANS-E).
- Checked equal to hardware: title, Game Pak check, main menu, options, Battle Now menu, both order screens, a full
  battle turn (Thunderbolt / Fire Blast effects); the intro apart from timing.

## Game Boy games: Transfer Pak and GB Tower
- Transfer Pak (src/port/transfer_pak.cpp): `PORT_GB_ROM` (controllers 2-4: `PORT_GB_ROM2..4`) puts a cartridge in the
  pak; battery RAM comes from `PORT_GB_SAVE` (default: the ROM path with `.sav`) and is written back on every pak write
  and on power-off. osPfsIsPlug/osGbpakInit/Power/GetStatus/ReadWrite/ReadId/CheckConnector work on Game Boy bus
  addresses (MBC1/3/5). CheckConnector powers the cart on (libultra does too): the tower powers it off when idle and
  re-probes before each save write. Stadium only knows the US titles, so other-region Gen-1 carts are presented with
  the US header title (`PORT_GB_REGION_COMPAT=0` turns it off): German Yellow is accepted as POKEMON YELLOW.
- GB Tower: the hand-written MIPS Game Boy emulator and its RSP microcode are replaced by a native core
  (src/port/gb_core.c: SM83, MBC, scanline LCD, timer, joypad, Super Game Boy packets with palettes, attribute files,
  border and screen masks). `Port_GbPresent` draws the 256x224 SGB picture into the frame buffer where the RSP task
  did; the tower's own code (menus, status strip, save write-back, cartridge checks) still runs. Save RAM writes mark
  pages for the tower's write-back (page state / delay tables via `Port_GbEmuOffsets`); pages the tower marks 0xFE
  (scratch RAM below 0x500 on Pokémon carts, where it keeps its cartridge probe block) are left alone, or the probe
  check fails ("Unable to save").
- Other tower fixes: the menu's voice/sound streams table held N64 addresses (now host pointers); the menu waited on
  the RSP's SIG3 of the frame task (gone: the host composes synchronously).
- Game Boy Color carts (header 0x143 bit 7; Yellow is one) run as a Game Boy Color, as on the N64: A = 0x11 at boot,
  double speed, VRAM/WRAM banks, HDMA, colour palettes, tile attributes, CGB sprite priority. SGB packets still work
  (the tower is an SGB/CGB hybrid), so Yellow uses its GBC palettes (CGBBasePalettes) and sends its border; its border
  packets carry garbage on a GBC (its CGB palette update clobbers HL before the tile copy), so for cartridge variant 4
  the tower takes Yellow's (US, "YELLOW") border from the start of its UI asset (`sUiGraphics`, context +0x5C58) instead,
  like the original emulator. `PORT_GB_NO_CGB=1` runs everything as a Super Game Boy (the cart's own SGB border and
  palettes, e.g. "GELB"). Without a game border the tower's own frame image (`unk_58`: 256 RGBA5551 palette entries,
  then 256x224 indices; Game Boy device / Stadium borders) is drawn. SGB *_TRN transfers sample VRAM one frame later.
- The status strip sits at line 208 (ares), not 212. Checked against ares on the same save: border, colours and layout
  match (remaining per-pixel differences: a one-pixel fringe from horizontal resampling).
- Screen mask (MASK_EN) on a Game Boy Color: Yellow showed ~30 red-orange frames with stray tiles before the copyright
  screen (ares: white). Yellow masks the screen (GB frames 62-128) while it sends its border through VRAM, and in
  between converts its palettes through a still-null base pointer (WRAM 0xDEE6), i.e. from ROM 0x0000: colour 0x00FF.
  The N64 emulator hides all of that: with a Pokémon cartridge it ignores BG palette writes (0xFF69) while MASK_EN is
  set (context 0x5402), and while the mask is in effect (0x5401, updated at frame end, 4 frames late after a cancel)
  it does not hand the frame to its RSP task, so the last picture stays. Its CGB palettes start as white, white,
  black, black. The core now does the same (gb_core.c: maskShown, shown[]).
- Verified (German Yellow, gb/yellow_de.gb): Game Pak check shows the save's trainer; tower boots with the border
  and Game Freak intro; the core matches ares on the same save (pixel-identical garbage map from a broken save,
  which is how the save generator bug was found); new game through Oak's intro and naming, in-game SAVE written back
  to the .sav; continue, walking with Pikachu following, START menu, party screen.
- Test saves: `tools/gb_save.py out.sav [--base real.sav]`. From scratch it writes names, party, Pokédex and a
  Pokémon Center position; Stadium reads that fine, but the Game Boy game keeps tileset/sprite state in the save too,
  so for continuing on the GB use `--base` with a save the game wrote (gb/base/yellow_de_newgame.sav: new game saved
  in the bedroom) and only names/party/Pokédex are replaced.
- Sound: the native core forwards every write to 0xFF10-0xFF3F to the tower's own APU mixer (GbApu_QueueRegisterWrite, time =
  CPU cycles in the frame / 4) and reads NR52 back from it. The mixer's buffers go to src/port/gb_audio_out.cpp (an
  SDL3 stream at 48 kHz, opened on the first buffer; off in replay / `PORT_SPEED=max` unless `PORT_GB_SOUND=1`;
  `PORT_GB_WAV=<file>` dumps the raw 16-bit stereo PCM). Two big-endian layouts made it silent / wrong on the host:
  the GbApuRegs bitfield struct over the register bytes (now `GBAPU_<field>` byte/shift accessors) and the channel
  phase union, whose u16 is the high half of the s32 accumulator on the N64 (now `GBAPU_PHASE`). Checked on German
  Yellow: title and overworld music, all sampled windows on the 12-tone grid (Goertzel test: on-note energy 12.6x a
  quarter tone off); a 17 s sample is gb/yellow_tower_sample.wav.
- The Special Pikachu Edition file from Downloads is the same ROM as gb/yellow_de.gb (same SHA-1) and was run from
  its own path via `PORT_GB_ROM`.
- Screenshots (stop_*.ppm / tour shots) are taken from the Metal backend's 8-bit readback (`Fast::gPortScreenBgra`);
  the older RGBA5551 capture showed banding (e.g. the Victory Palace spotlight) that is not on screen.
- Tour scripts: Stadium's START at the tower's cartridge screen, then the GB intro (~1800 frames in the port, ~3300 in
  ares: ares loads the cartridge slower). `PORT_FREEZE_CHECK=0 PORT_SCREENSHOT_NOSTOP=1` keep runs going through
  static GB screens.

## Second ares round: Stadium, GLC, Kids Club, Lab, Victory Palace, Free Battle (and what was wrong)
Toured with tools/tour.py on both sides (hub moves: Stadium centre; RIGHT GB Tower; DOWN Free Battle; LEFT Kids Club;
LEFT Victory Palace; UP Pokémon Lab; Gym Leader Castle = LEFT, UP from the Stadium). Lab tests need a Game Pak with
the Pokédex flag and a Pokémon Center save (`tools/gb_save.py --got-pokedex`; map id 0x29).
- Registered/Rental choice (cups, GLC): Ditto, rental pak and hand were off screen. Entries 4-5 of the slot table were a
  separate big-endian byte array; merged (rental_team_selection_2DB900.c).
- Gym Leader Castle: trainer portraits were empty (pointer in an s32 field), then shifted by one (big-endian u16 from
  ROM data read as host u16).
- Mini-game instruction screens: red/white stripes instead of the background picture (picture address in s32).
- Victory Palace: crash (string table only declared, so it resolved to its N64 address; plus an f32 written into an
  s16 stack slot).
- Lab / Lab PC (crashes and garbage): message buffers sized for 4-byte messages (Lab UI queue, scheduler client and VI
  queues); `Ui_ReceiveMessage` writing 8 bytes into s32s; widget callbacks and `FragmentEntry` taking pointers as s32;
  pointer lists allocated with 4 bytes per entry and read as s32; record matrices laid out with N64 struct sizes
  (0x10/0x1C); pointer sort with s32 comparators; box-bank verify buffer too small (an original overrun); out-of-range
  text index for an unset gift Pokémon.
- GB Tower thread structs and two more scene structs were allocated with N64 sizes (now sizeof).
- Hub sparkle trail and the Stadium sparkle table: original out-of-range accesses, padded / merged.
- `osRecvMesg` now writes exactly `sizeof(*msg)` bytes for typed destinations (src/port/sdk/ultra64.h).
How they were found: `tools/ptr_warnings.sh`-style clang scans (`-Wint-conversion` errors are hidden by the build's
flags: grep for "error:" too) and the ASan build (`cmake --build build_asan`, run a tour's port.rec with
PORT_REPLAY/PORT_STOP_AT and ASAN_OPTIONS=halt_on_error=1). Both point at the exact site; the routes above now run
ASan-clean.
- Gallery: "Snap photo!" left the Gallery (fragment entry points declared void whose callers use the result: Gallery
  snap/album/film select and seven mini-games now return it); the camera's background sets are tiny MIPS code files in
  an archive (now run by `Port_CallFragmentLeaf`, a leaf-function interpreter in jpeg_stream.c); the scene layout held
  raw N64 node/callback addresses (also in the Pokédex area map and a sushi-game layout); the camera target floats are
  DMA'd from ROM big-endian. Snap mode now matches ares.
The Victory Palace spotlight bands seen earlier were not on screen: the old screenshot path saved the 5-bit RGBA5551
copy; with 8-bit captures (see the Game Boy section) it matches ares.

## Third ares round: Free Battle stages, Game Boy party in battle
Toured Free Battle into the battle on both sides, with rentals and with the German Yellow party from the Transfer Pak
(ares needs the same US-title header patch as the port, and must run from boot: `ARES_TITLE_STATE=`; a title state
saved without the pak leaves the cartridge undetected).
- Field stage (Free Battle "Anything Goes", stage file 0 of stadium_models) was not drawn: frames were never cleared,
  giving smeared copies of Pokémon and HUD. Its MIPS entry function dispatches through a jump table, whose words
  Port_RelocateDataBlob had already rewritten to pseudo addresses; Port_CallBlobEntry now maps a `jr` target in the
  blob's own pseudo slot back to the link address. The stage's fog record (entry 4) is a pseudo address too (now
  Port_VResolve'd) and holds big-endian s16 near/far (now byte-swapped; read raw the whole field was fogged white).
- Yellow's partner Pikachu (the GB Pikachu with `unk_52 & 0x80` uses model 0x99) had garbage textures (pieces of the
  stage). Its textures are CI with palettes: GeoRender_BindTexture calls the palette descriptor's load list, but read
  it through the wrong host type (`unk_D_800ABB28.unk_08`, +16 on the host) instead of the converted descriptor's
  pointer at +8, so it always got NULL, no LOADTLUT ran and TLUT stayed off. Every model with paletted textures went
  through this. Also implemented the RDP behaviour for CI textures drawn with TLUT off (texel = index as intensity).
- All 18 stage files (`PORT_BOOT=battle PORT_BOOT_STAGE=<0..17>`, a debug override of the stage index) now load with
  no blob-entry failure or crash and draw their arenas (field, the cup stadiums, Kids Club, Victory Palace, the GLC
  gyms, the final rooms). Compared against ares in battle: field (stage 0), Stadium arena, and all eight gyms (unlocked save): arena geometry, textures, lighting, overhead camera and HUD
  match. The opposing trainer's lead differs between the two runs in some gyms (AI/RNG timing, not rendering).
  Then Saffron, Cinnabar, Viridian (after the Moltres fix below) and the Elite Four room: match too. Not reached:
  the rival's room after the Elite Four (needs a won Elite Four run in both emulators).
- Intro balloon scene: the balloons were textured squares (ares: round balloons). Texture-swap animation tracks
  (ModelAnim_GetEventAtFrame) picked their descriptor as `&base[i]` with the host struct size, but ROM descriptors are
  12 bytes apart; now Port_DescAt. Every animated-texture material went through this; the "[timg] bad address
  0x20020/0x20040" warnings came from it and are gone.
- Area select (hub) came back black after leaving an area (seen after the Lab): the cursor glow still held the
  closing iris (alpha 0xFF) from the previous visit. On the N64 every fragment load restores the fragment's .data and
  clears its .bss from ROM; the port links fragment code statically, so its globals kept the old values. Now each
  fragment directory's writable data goes to its own Mach-O sections (tools/gen_frag_sections.py, applied by
  CMakeLists.txt), snapshotted at startup and restored by Fragment_Load (`PORT_NO_FRAG_RESET=1` turns it off).
- Debug for the fragment data reset: `PORT_TRACE_FRAG_RESET=1` logs each reset section (build/generated/
  frag_sections.txt maps section numbers to fragment directories), `PORT_FRAG_RESET_SKIP=__fdNN,...` skips some.
- Lab PC with the Yellow cartridge (a Pokémon Center save from `tools/gb_save.py --rom ... --got-pokedex`): the PC
  screens, party list and the status panel's Pikachu icon match ares. One early run showed the icon garbled/blank;
  not reproducible in 5 later runs (the icon is loaded into a double buffer while the screen is drawn).
- Transition "red frames" (fixed): between scenes the game has no frame to show, so it switches the VI to repeat-line
  mode (`osViRepeatLine(1)`) and shows a one-line 640-pixel buffer (dp_intro.c DisplayWorker_ProcessFrame; the line
  holds the stage's clear colour: black, or white after the title / for white fades). osViRepeatLine was a no-op stub,
  so the port presented 320x240 pixels from that one-line buffer: the rest came from unrelated memory, a white row
  and red/purple/orange blocks at the top of an otherwise black screen, at every scene change. It is now implemented
  (stubs.c → os_devices.cpp, PresentCpuFrame repeats the first line). Found with a full-pixel scan of every presented
  frame of a hub/Castle/Lab route (31 frames with red before, 0 after); the blank frames now match ares (black at the
  Castle entry, white at the fades that are white in ares). osViSetMode (redirected to port_osViSetMode) ends
  repeat-line mode, as the VI register rewrite does: without it the GB Tower, which sets its own mode after the scene
  change, showed only its first line (black). The line is written in N64 (big-endian) byte order now
  (Display_ClearFramebufferLine; Display_GetFramebufferClearColor reads it back): written natively, black 0x0001 was
  shown as 0x0100, a dark green (0,32,0) blank screen. Debug: `PORT_TRACE_VI=1` lists CPU-presented frames and
  the line colours set.
- Black frame after the title (fixed; user report: first confirm after boot only): the Game Pak check is a hi-res
  scene with two new 640-wide frame buffers. The renderer told frame buffers from screen-sized off-screen pictures
  (Gallery enlarge) by whether the VI had shown the address yet, so each new buffer's first frame was drawn as an
  off-screen picture, and its swap presented the buffer's RAM: all zero (black) for the first one at frame 229 of
  to_battle.rec, where ares shows white (title fade) and then the Game Pak check fading in. Later confirms reuse
  buffers already shown. The display worker now names the frame buffer of each display task it submits
  (`Port_NoteFrameBuffer`, dp_intro.c DisplayTask_Submit), so the first frame renders to the screen. Present since
  the first port commit (same black frame in a build from 22 October).
- Earlier attempt for the same report (kept, harmless): A frame buffer's colour texture is (re)created when
  the game first uses a frame-buffer address and whenever the window size changes; its contents are undefined
  (leftover GPU memory) until the game repaints it fully, and fades only add a translucent layer over the previous
  picture. Such textures are now cleared to black on their first render pass (gfx_metal.cpp `sFreshColorFbs`).
  Not confirmed as the cause: `PORT_DEBUG_RESIZE=<frame>:<w>x<h>` (resizes the window from inside the port) during
  the Castle fade-in gave identical frames with and without the clear (`PORT_NO_FRESH_CLEAR=1`), and no red pixels
  beyond the Castle's own picture. Needs the user's description of where the red frames appear.
- Transition "red frames" (user report), before that fix: not reproduced; no red pixel in ~1700 captured frames of the boot → title →
  menu → hub route at max speed and in real time. CPU-presented frames (RAM frame buffer shown directly) only occur at
  boot (frames 1-49).
- GB Tower: a crash once the game's sound used the noise channel. GbApu_Reset took the noise table pointer as s32
  (truncated on the host); now void*.
- ASan runs of all these routes (8 gyms, Elite Four, 4 cups, Mewtwo, Yellow-party battle, GB Tower, night hub) found
  and fixed N64-only-safe out-of-bounds reads/writes, each kept equal to what the N64 reads: the team-selection
  scale tables (index 12) and the intro camera presets (index 3..4 of a 3-entry table) are merged with the table that
  follows them in the ROM; the type-icon table gets the following ROM word as a sixth row; the AI's candidate ranking
  array (8 entries used, 6 declared) is 8; gMoveData has a zero element -1 (empty move slots read
  gMoveData[move - 1], which on the N64 is the zeroed tail of gMoveDisplayInfo); GeoRender_BindTexture no longer
  indexes its 4-entry size table with out-of-range size bytes (attract intro / a few models; visually unchanged).
- Mewtwo battle (night hub → VS MEWTWO): the player's HUD labels ("TRAINER", the Pokémon's name) were black boxes.
  The font set had been loaded into memory that earlier held a full-screen render target; the target's 16-byte
  marker at its start survived (the font starts further in), so glyph loads were served from the stale GPU
  framebuffer. main_pool_alloc_from_pool now tells the renderer (`Port_GfxForgetRenderTargets`) that a block handed
  out again is no longer a render target (queued, applied on the render thread).
- HUD digits, "HP:" and similar alpha-cut UI art were pink-tinged in every battle (ares: white). Above N64
  resolution the textures are filtered between texels, and the RGB of fully transparent texels (a magenta key colour)
  bled into the edges. RGBA16/32 and CI uploads now give transparent texels their opaque neighbours' colour
  (`BleedTransparentTexels`; `PORT_NO_ALPHA_BLEED=1` turns it off).
- Compared in battle against ares as well: the Pika, Prime and Petit Cup stadiums, and the Mewtwo arena; the night
  hub (everything cleared) with the VS MEWTWO spot. Cup/Castle progress came from an editor-made unlocked save.
- Moltres (Giovanni's gym trainer) drew its wings as two lines across the screen and some frames all yellow. The
  wings' scrolling fire texture is a per-frame list from DisplayList_InitAnimatedFrameSegment, allocated as 0x50
  bytes (10 N64 commands) but written as 9 host commands (0x90): the tail landed in the next allocation, the wing
  joint's matrix (Geo_NodeAnimatedPart), so the wing vertices went to ~10^7. Same N64-size pattern in 22 more
  particle/effect display-list allocations (particle_data_library, Drowzee game): now `GFX_N64_BYTES(n)`
  (gfx_buffer.h). Found with `PORT_DRAWLOG=1` [mtxdl]/[mtxwhere] (a G_MTX whose data are display-list commands) and
  `PORT_DLALLOC_AT=<hex offset>` (allocations covering a display-list buffer offset, with caller).
- Battle camera wipe (the "strip wipe" between shots, battle_engine_3597B0.c) drew blocky, shuffled wedges. Its I4
  wedge texture is a static u32 array; the renderer swaps such word arrays to N64 byte order by symbol name
  (gen_word_arrays.py + dladdr), but arm64 clang merges static globals into `_MergedGlobals` and drops their
  symbols, so ~40 registered static textures/palettes/tables were never swapped. The game library now builds with
  `-mno-global-merge` (CMakeLists.txt). Re-toured hub, cups, Kids Club, Gym Leader Castle, Lab, gallery, event,
  menus, options and Magikarp's Splash against ares afterwards: unchanged, no regressions.
- Saves (found while loading an editor-made save for the Castle): port_compat.h had FLASH_STATUS_ERASE_OK/ERROR as
  0x08/0x04 instead of libultra's 0/-1, so every flash page program "failed" after the first page, was retried four
  times and abandoned — nothing but the first page of each bank was ever written. And GameOptions (save_data.h) had a
  pointer field, 16 bytes on the host, which moved SaveOptionsRecord's progress array from 0x0C to 0x18: cup,
  Castle, Round 2 and Mewtwo progress were never read or stored at the right place. Both fixed; with an unlocked save
  the Castle map now shows the cleared (neon) gyms as in ares. ares takes a save via `ARES_REF_FLASH=<file>` (new in
  tools/ares_ref).
- Save byte order: the flash image is now byte-for-byte a cartridge save (N64 big-endian, as ares' save.flash and
  tools/save_editor; SAVE_FORMAT.md), so the same file works in the port, ares and the editor. The RAM banks stay
  host-native structs (game code unchanged, as in the decomp); save_data.c swaps every multi-byte record field at
  the bank boundary: `Save_AllocateBank` swaps both copies right after reading them from flash, and
  `Save_FlushBank` programs a swapped copy (`Save_PortFlashImage`, static 16 KiB buffer) instead of the RAM bank.
  Swapped (`Save_PortSwapBank`): every record's footer ('PO','KE' and the checksum, three u16 — the old port wrote
  "OPEK"), the DeckRecordHeader trainer ID/count of registered teams, Lab boxes and item boxes, the Victory Palace
  OT IDs, the saved battle's trainer ID and team count, the options record (sound, GB Tower speed and word,
  cleared-modes of both rounds, bonus Pokémon) and the gallery photos (f32 Y, s16 frame, two Vec3f, u32 at 0x68,
  u16 OT ID). Pokémon entries (GbPokemonNamedRecord, already big-endian byte arrays via GbData_Write/ReadU16BE)
  and the photos' particle slot data (packed byte by byte) are not touched; the checksum is a byte sum and
  independent of the order. Checked: the swap table equals the editor schema's multi-byte fields for all 111
  records (C table applied to an index pattern vs. stadium_save.save.RECORDS, 0 mismatches); an erased chip +
  Options gives exactly fixtures/first_save.flash (the real game's first save); the editor's mono/voice-off save
  shows MONO/OFF and leaving Options gives exactly ares' output; `stadium_save new` + `unlock` gives the night sky,
  Mewtwo over the Castle and the neon gyms on the Castle map; a save with teams in all three team banks, Lab
  boxes/items, statues, a saved battle, photos and non-zero option words, with one primary record per bank
  corrupted, comes back byte-identical after the game repairs and rewrites all four banks. tools/save_to_port_order.py
  (converter to the old host-order layout) is obsolete: hand ares/editor saves to the port as they are
  (`PORT_FLASH_FILE=<file>`, absolute or relative to build/). A pokestadium.flash written by an older build is in
  the old layout; its records fail the check and are reset on load, like on a blank chip.
- VS screen before a cup/Free Battle (user report: the 12 grid cells stayed empty; ares shows each team's Pokémon).
  battle_prep_39AC50.c renders every Pokémon's 3D model into its own 128x96 image (PokeIcon render list,
  poke_icon.c) and maps those images onto the grid. Four renderer faults, all in render-to-texture (RTT):
  1. Ordering (the empty cells): each picture takes two GPU passes (z pass, then fill + model). The first readback of
     a new framebuffer cannot wait (its command buffer sits behind the frame's own, committed at frame end), so
     gfx_metal moved later passes to the readback queue, and the model pass ran and was read back before the first
     pass, whose fresh-texture clear then wiped the GPU image (RDRAM kept the picture: `PORT_NO_RTT_SAMPLE=1` showed
     them). RTT framebuffers now start on the readback queue (`SetFramebufferReadback`), so passes run in order.
  2. The first picture was drawn into the depth buffer's image: switching back to an existing target did not cancel a
     pending target creation (`sPendingOffscreenAddr`), so the next scissor created and bound a target for the depth
     buffer.
  3. Opaque black backgrounds (ares: the cells' grid pattern shows through): the fill of a whole RTT image now clears
     with the fill colour's alpha bit (`ClearFramebufferColor`; opaque draws output alpha 1). `PORT_NO_RTT_FILL_ALPHA=1`
     turns it off.
  4. Missing leaves, petals and tail flames (translucent texels), and the Game Pak screen's WARNING stripes once the
     fill was transparent: the N64 colour image's alpha bit is coverage (any drawn pixel), but blending kept the old
     alpha (src factor Zero). Blended Metal pipelines now use dual-source blending (default.shader.metal `color1`):
     RGB blends by the texel alpha as before, alpha is written as 1 (0 for o_invisible draws, which keep it).
     `PORT_OLD_ALPHA_BLEND=1` restores alpha = dst.
  Checked against ares through the VS animation (all 12 Pokémon, grid behind them, leaves/flames). Re-ran the
  menus, hub, cups, Lab, gallery, Kids Club, Castle, options, Magikarp, intro, Victory Palace and Yellow Lab PC
  tours: identical to before, except the menu cards' outer shadow ring (was opaque black, now see-through, as in ares)
  and the restored WARNING sign. Battle replays unchanged. Debug: `PORT_TRACE_ICON=1` (PokeIcon slot renders),
  `PORT_TEXLOG=1` now also lists `[rtt] bind` and `[rtt] fill clear`.
- Debug switches: `PORT_TRACE_BLOB=1` (blob entry interpreter), `PORT_TRACE_VBLOB=1` (pseudo slot alloc/free),
  `PORT_DRAWLOG_SEQ=1` with `PORT_DRAWLOG=1` (commands after each SETTIMG, with branched sub-lists),
  `PORT_TRACE_FLASH=1` (flash commands, DMA, status reads), `PORT_TRACE_SAVE=1` (progress record reads).

## German Yellow on the Transfer Pak and in the GB Tower vs ares (Lab PC transfer, frames, sound)
Same inputs and saves on both sides; ares from boot (`ARES_TITLE_STATE=`), US-title-patched cartridge, `ARES_REF_GBBOARD=MBC5`,
Stadium flash from `stadium_save new`. New in tools/ares_ref: `ARES_REF_FLASH_OUT=<file>` (the Stadium FlashRAM after
the run) and `ARES_REF_WAV=<file>` (N64 AI output resampled to 48 kHz, raw s16le stereo; `<file>.frames` holds the
sample count after every VI frame).
- Lab PC transfer (Yellow save from `tools/gb_save.py --got-pokedex`, i.e. at the Viridian Pokémon Center: the Lab
  refuses saves made elsewhere, "hasn't been saved at a POKéMON CENTER"). Route from the title: `1500 START 300 A 200 A
  400 RIGHT 60 RIGHT 60 DOWN 60 LEFT 60 LEFT 60 LEFT 60 UP 90 A 900 A 300 A 300 A 300` reaches the Pokémon grid; there
  UP RIGHT A opens the right-box selector (DOWN A = "64 BOXes", A = box 1), DOWN LEFT = move icon, R/L flips the move
  direction, B on the grid = "Save and quit". Moved Pikachu party -> Stadium box 1, saved; then (from those saves) box 1
  -> party, saved; also party -> GB box. Every screen matched ares, and the Game Boy saves and Stadium flash images are
  byte-identical, except SRAM 0x00-0x1F: GbPak_DetectRam's third probe pattern is `osGetCount()` bytes, timing noise
  on hardware too.
  Fixed on the way (lab_pc_1AE680.c): the info card's Pokémon icon was noise (its buffer address kept in an s32);
  opening the box-type selector crashed (TableView over the three box banks with the N64 stride 0x10, host struct is
  0x18); the box-number cells read their counts through the bank layout instead of the box record's (wrong "nn/20").
- Lab PC → Items / See List and Pokédex → AREA (the evidence pilot's `lab_pcmenu.rec` / `lab_dex.rec` routes, save
  from `gb_save.py --got-pokedex --party PIKACHU:25,BULBASAUR:20,CHARMANDER:18`; both crashed): fixed, both routes run
  to their end, ASan clean. Items and See List match ares, as do the Pokédex list and data page.
  - Items (segfault in LabItem_DrawItemRow): LabPC_BindGridStridedData took the (s32, unk_func_8820BE14_06C) pair as
    `s32*` and bound the table at `arg1 + 1`, 4 bytes in; the table sits behind padding on the host. It takes the
    pair (`unk_func_8821421C_03C`) now, and unk_func_8821421C_038 holds one instead of `s32 unk_60` + `unk_64`.
  - See List: its box banks (`unk_func_8830867C_02C_0E4`, 0x30 on the host) were walked with the N64 stride 0x20
    (TableView) and through a 0x20-byte view (`..._144_000`, now laid out like the bank); the box records were built
    through `unk_func_8820BE14_06C_000_alt`, whose `unk_04` began with a pointer, so the list read box index and
    flags from the wrong words (empty list); the slots were sized 0x5C (host 0x60); the area cell read the record's
    bank pointer and box number as `s32[2]`; the column widths are one table read past its first 3 entries into the
    next three static arrays (adjacent on the N64 only; now one array); its view `..._0DC` had a pointer first.
    The screen's pool (`mem_pool_try_init(0x18000)`) ran out on the host: doubled.
  - AREA (signal 5, a fortified sprintf trap): `unk_D_88826A88.unk_00` was `char[1]` and gets "AREA UNKNOWN";
    `unk_D_88826CF0.unk_04` (one location pointer per entry) was `[1]` + padding, too small on the host.
  - Both (found by ASan): GbSave_BuildPortSummary wrote the player name at `&arg0[1]`, past its 12-byte struct, into
    whatever followed on the N64 (the See List compared it as a 4-byte stack array); the struct has a name field now.
  - Open: the AREA map draws without the sea and without ares's bluish fog tint (geo fog node 0x96C8FF, 985-1005),
    and its label boxes are pale instead of olive (fill rectangles in 1-cycle mode blending memory with itself).
    Not a game-struct problem as far as checked (no render target involved, same fog node as the battle stages).
- Lab PC → See List → a row's menu → "Group <species>" (or any other filter; the evidence pilot's
  `lab_seelist_mash.rec`, segfault in func_88304850 at frame 7804 on a garbage Pokémon pointer): PointerList_Filter
  counted the filtered list as the byte difference `>> 2` (4-byte N64 pointers), so the count was doubled and the grid
  drew entries past the filled ones. It counts entries now. The route runs to its end, ASan clean, and its See List
  screens (grouped list, move page) match ares.
- Lab PC list screen, two crashes on its main paths (evidence pilot, `l1.rec`):
  - TM/HM list (segfault in LabPCList_CompareTmHmEntries under PointerList_Sort as soon as the list opens with TMs,
    HMs, vitamins or PP Up in the trainer's item box): LabPCList_BuildTmHmList counted its entries as the byte
    difference `>> 4` (16-byte N64 entries, 24 on the host), so the sort ran over empty entries, and the comparator read
    the entries through unk_func_88200FA0_030_038, whose fields are the TM number / source only with 4-byte pointers.
    It counts entries and compares through the entry type now (the N64 order: by number, then by source).
  - "Check which Pokémon list" → cursor on GB BOXes / 64 BOXes (segfault in BattleInfoWidget_DrawEntry on a NULL cell):
    the box grids were bound through unk_func_8830867C_02C_144_000_alt, a char-padded view whose lists line up with
    banks 1 and 2 only with 0x20-byte banks; on the host the box count came from padding, so the 12-row grid drew
    cells past it with a NULL entry (on the N64 the count is always the 12 boxes). The grids take the banks' lists now.
    The cell draws of these grids and of the move-destination picker (box "count/capacity", the party's "03/06") read
    the box and bank through views with N64 offsets as well and now use the real structs.
  - Column header selection one cell off (R from the list lands on Nickname, the TYPE column and its "Group by TYPE"
    can't be selected; the same input in ares opens "Group by TYPE"): unk_func_8830867C_02C_04C_018 started with
    char[0x8] for the grid's first two cells, one pointer on the host, so LabPCList_SwitchColumnPage wrote every
    page's columns one cell too low. The two cells are pointers now. Behind it, two more 4-byte-pointer layouts:
    the type list's table had a hard-coded 8-byte stride ({type, color pointer} is 16 bytes on the host: segfault in
    BattleInfoWidget_DrawStatusListEntry), and the move list entry declared its move id as a pointer (16-byte
    entries read at 8, four of them written past the end of the widget).
  - Same kind of views, found while checking the headers: unk_func_8830867C_034_03C_038_02C_000 (a box with its
    owner pointer as char[0x8], so the selected flag and the 0x1C stride were wrong on the host) and
    unk_func_88313894 / _020 (the move-destination picker's "box full" check, through a char-padded view of the
    banks); both now use the real structs.
- Lab PC → Organize → BOXes → "Check BOX" (evidence pilot, `boxes_i2.rec`; segfault in LabPC_CheckBox_ShowBox on A
  over any box): func_882052F4 took the chosen box from the picker grid's list of box pointers (unk_A030) through an
  `s32*`, so the box pointer came out as its low 4 bytes. It indexes the pointer list now. Behind it, the box grid's
  cell draw (func_88201488) loaded the Pokémon icon from `arg2 + 2` on the entry's pointer type, 8 bytes in on the N64
  (after the Pokémon pointer and its padding) but 16 on the host: every icon 4 pixels off. Checked on GB BOX 1 (20
  Pokémon, then the summary of one) and 64 BOX 1 / 2 (6 / 3 Pokémon).
- Registered teams, registering a team (evidence pilot, Gym Leader Castle → Registration → Register Pokémon; froze on
  the screen after the team was saved): the registration manager (unk_D_84229EB0) keeps a ring of four team entries
  (unk_4D24/unk_4D28 link [i] to [(i ± 1) & 3]) but declared them as `unk_00028[3]` plus `char unk0E7B8[0x4D30]`, the
  N64 size of the fourth. The host entry is larger (two pointers), so TeamSelection_RegisteredTeam_Initialize's ring
  setup wrote the fourth entry's back link over unk_134E8, the team tray pointer, and RegisteredTeam_SaveFromTray /
  ..._HandleDiscardPrompt passed a team entry to Rental_TeamTrayResume / Rental_ResetTeamSlots: the tray never slid
  out, nothing ended the input loop. It is `unk_00028[4]` now. Checked: registering the 10th team returns to the
  Castle's registration menu; "register another" empties the tray and picking goes on.
- Pikachu Surf reward (Stadium_BonusPikachuReward; the evidence pilot's free battle + reward entry, segfault in
  ModelAnim_EvaluateScaleChannel at fault address 0x8811208e when the three surfing followers appear after "Pikachu
  learned Surf"): the followers' animation `D_8C2011C4` lies in the data fragment `_798CD0` (Fragment_Load, linked at
  0x8C200000, mapped through gFragments), not in an archive blob, so the animation accessors read its big-endian
  header as native (0x8C201188 came out as 0x8811208C) and followed it. Animation data is now raw when
  `Port_IsRawData` says so (blobs, segment- and fragment-mapped ROM data), like geo layouts. With a 4-move Pikachu
  (forget path) and a 3-move one the reward runs to PikachuReward_ShowSuccessEnding and saves Surf to the cart; in a
  battle and the Lab no other animation changes classification.
- GB Tower frames: German Yellow from the game's own new-game save (gb/base/yellow_de_newgame.sav), down the stairs,
  out of the house, north to Route 1, Oak's cutscene and the scripted battle (wild PIKACHU, HP bars, battle menu,
  Oak's Poké Ball). ares's Game Boy starts 1555 VI frames later (cartridge load), so its GB inputs are shifted by
  that. 420 pictures (every 3 frames, 1260 frames) compared on the 160x144 GB screen in 4x4-pixel blocks: every
  frame has a counterpart with the same content within +-2 pictures; the leftovers are the ±1 GB frame
  interleave of GB frames and VIs (walk-frame phase, battle slide-in, the battle-entry wipe caught mid-update) and
  half-tone pixels at 1-pixel vertical lines. Not content differences: ares's VI filters horizontally (a GB pixel is
  2.25 ares pixels; dither turns into blends, sample the ares picture 0.25 N64 px left of the nominal grid) and white
  is 248 in ares, 255 in the port (5-to-8-bit expansion). The trainer battle in Oak's lab was not reached: the scene
  waits for the player to take the Poké Ball and the route script did not get there.
- Sound: the tower's mixer sizes each buffer from osAiGetLength (`len = ((800 - aiLen + 0x80) & ~15) + 16`, clamped
  to 784..816). With host sound off (replays, `PORT_SPEED=max`, which is when `PORT_GB_WAV` dumps are made) the port
  reported an empty queue, so every buffer got 816 samples instead of the ~802.6 per VI frame the N64 AI drains: the
  dump ran ~1.5 % slow against ares (0.8 s behind after 55 s). gb_audio_out.cpp now keeps a virtual AI queue drained
  by VI ticks at the DAC rate (clock 48681812 / round(clock / 48000)) when the output is off. Afterwards, over the same
  83.7 s: levels equal (RMS ratio 1.001, +0.01 dB), envelope correlation 0.83 with the local lag wandering within
  ±0.15 s (input timing) instead of drifting; per 100 ms window the dominant note is the same (or an octave) in 639,
  a chord tone of the other side in 29, different in 59 (noise-channel hits and note onsets, no consistent semitone
  shift), both silent in 109. Frame comparison unchanged by the fix.
- A real German Yellow save from the web (found later): gb/yellow_de_projectpokemon_mew.sav, a cartridge dump posted
  without login in Project Pokémon's "Gen I - V Event Contributions Thread" (forum attachment "Pokémon Gelbe Edition
  Special Pikachu Edition Mew.sav", comment 268615; the 2000 Bremen Mew distribution). 32 KiB, checksum valid:
  player ASH, ID 40985, 8 badges, Pokédex 139 seen / 124 owned, party RATTFRATZ L3 and MEW L100 (OT HAMBURG).
  Checked on the port and ares with it: the GB Tower continues the game (title → WEITER with SPIELER ASH, ORDEN 8,
  POKéDEX 124, ZEIT 13:46 → Pokémon Center, start menu, Pokédex list) and the Lab PC (1P ASH, ID 40985, the party
  with MEW L100, GB box selector, box list 00/20): every capture matches ares (build/tour/mew_tower, mew_lab).
  Before that, saves the German game wrote itself were used (the new-game base save; the tower's in-game SAVE
  write-back was checked earlier).
- GB Tower in-game menu (C-Up) and layouts, compared with ares using the downloaded save (build/tour/towermenu,
  towermenu0, towerlay, towerload):
  - The menu showed its text straight over the Game Boy picture. The tower fades the Game Boy screen to black behind
    the menu with emu->unk_53FD (the frame task's word 0x5C bits 16-23), which the original RSP microcode applies;
    Port_GbPresent now takes it (brightness) and the frame fade unk_53FC (bits 24-31, used while switching frames).
  - The menu text was black/lilac: its colours have alpha bit 0, and BleedTransparentTexels recoloured alpha-0 texels
    of the CPU-presented frame (PresentCpuFrame draws it as an opaque texture) with their neighbours. Bleeding now
    only runs for filtered sampling (point-sampled texels never mix).
  - With the Doduo/Dodrio speed unlocked (options word 0x04 = 1/2) ares frames the tower with the Stadium art and Z
    cycles the frames (layout 0 = the game's own Super Game Boy border, 1 = Doduo art / Game Boy console, 2 = Dodrio
    art; ROM 0xF4920 Game Boy console, 0xF5450 Doduo, 0xFDE40 Dodrio, Yay0, 256 RGBA5551 + 256x224 CI8). The port
    always showed Yellow's Pikachu border and Z did nothing: (a) Port_GbPresent preferred the game's SGB border over
    the tower's frame; it now gets the frame only for layouts 1/2 (NULL for layout 0); (b) the switching starts once
    context byte 0x5404 is set, which the native core never did. As the N64 emulator does at each frame end
    (gb_tower_emulator_code_asm.s 0x8120A010): a CHR_TRN/PCT_TRN packet, then MASK_EN back to 0, raises it
    (gb_core.c, Port_GbEmuOffsets[8]). Now the Dodrio art at start, Z → Pikachu border (with the fade) → Doduo art,
    and in normal speed the frame position matches ares more closely (the Yellow walk moved closer to ares in all
    151 captures).
  - The loading screen's party pictures (rendered by gb_tower_team_prep into 40x40 images and copied with _bcopy)
    had coloured dots top left: the 16-byte render-target marker. The readback now writes all pixels and keeps those
    first 16 bytes as the target's stamp (RttInfo::stamp) instead of a marker in memory.
- Pokédex (Lab, third object; downloaded save): list, Bulbasaur's data page with the 3D model, menus: match ares.
- Verified in real time too (`--port-only PORT_SPEED=1`: the tower menu/layout route and the VS screen ran at 60 fps,
  same pictures as the max-speed runs). A capture placed on the same script frame as a button press shows that
  press on the port (input is applied before the frame is drawn) but not on ares: the tower menu looked "one step
  ahead". With captures between presses (build/tour/towerexact) all 7 menu/layout captures match ares, including
  the fade during the Z switch.
- Doduo / Dodrio fast-forward (C-Right; Normal tower: no effect, Doduo 1↔2, Dodrio 1→2→4→1 Game Boy frames per N64
  frame, emu->unk_548E) had no effect: the tower passes `unk_548E * 0x9A` lines to GbCpu_Execute and the original
  emulator runs that budget (context 0x53DE), while the native core ran one frame regardless. It now runs that many
  frames, each with its own frame end, and time-stamps APU writes as the original (0x8120B04C): time over the whole
  call divided by the speed, so the mixer plays them in one buffer and the music speeds up with the game. Measured
  from the press to Yellow's surfing scene: port 1.99x / 3.97x, ares 1.98x / ~3.6x (on the N64 the CPU cannot always
  fit 4 Game Boy frames in one VI, hence the "3x" of the manual; the port runs what the game asks for).
  ares_ref needs the Game Boy core for these runs: configure its build with `-DARES_CORES="n64;gb"`.
- Closing the window crashed (the user's 80-minute session ended with "mutex lock failed: Invalid argument", abort):
  main returned after the window loop, the C++ static destructors ran while game threads were still running, and one
  of them locked a destroyed mutex. main now ends with _exit(0) after the loop, as SIGTERM already did (saves are
  written through on every flash/Transfer Pak write). `PORT_QUIT_AT=<frame>` sends the quit event of a window close
  (debug); the old exit path crashed 3/3 times with it, the new one exits 0 3/3. Checked through the app bundle
  (launcher autoplay with the preselected ROM and saves): title reached in real time, clean quit, saves unchanged.
- Kids Club mini-games toured against ares (build/tour/mg_*: Snore War, Ekans' Hoop Hurl, Thundering Dynamo, Rock
  Harden, Clefairy Says, Rattata's Run, Sushi-Go-Round, Sandshrew's Dig; Magikarp was toured before). Found:
  - Clefairy Says crashed when it built its first pattern (GraphNode_AppendChild via ClefairyGame_SpawnPatternIconStep):
    ClefairyGame_GeneratePattern's weight total was "var_a3 = var_a3" (uninitialised; the caller's a3 on the N64),
    so ClefairyGame_ChoosePatternEntry ran past the table and returned an uninitialised pointer. Now starts at 0.
  - Sushi-Go-Round crashed once a player stacked 5+ plates: the 5-plate stack D_86806E08 had no NULL terminator (on
    the N64 the next table's first word, NULL, ends it). Terminated.
  - Sandshrew's Dig drew big black hexagons over the ground: the holes' depth-only mask (2-cycle blender IN*0 + MEM*1)
    is "invisible", but use_alpha was only set for B = 1-A, so it drew unblended in its black colour. Invisible draws
    now always blend (interpreter.cpp).
  - The holes' dirt mounds stayed flat: AnimFixedFrame (model_animation.c) split the 16.16 frame counter in N64 order,
    so on the host "whole" was the fraction and every animation at a fractional speed (here 0x5000) never advanced.
    Fields swapped. Re-ran all tours (menus, hub, cups, Lab, gallery, Kids Club, Castle, options, Magikarp, intro,
    Victory Palace, Mewtwo, Misty, battle replays, VS screen, GB Tower): unchanged; Clefairy Says moved closer to ares.
  The other games match ares (positions/outcomes differ by timing and the CPU players' randomness).
- Also toured against ares (build/tour/trade, trade2, gal2, galflash*): the Lab PC's Pokémon pages (party, stat
  pages with moves), the trade machine ("You need to have two Game Paks"), leaving and re-entering the Lab, and the
  Gallery (main menu → Gallery: menu, camera choice, rental list, snap viewfinder). All match. The Gallery's photo
  flash is white in both; on the port it shows right at the A press, in ares ~3 frames later (input latency), so
  captures placed on a press frame showed white only on the port.
- Gym Leader Castle map with the unlocked save (build/tour/glce4): the cursor path Brock → Misty → Surge (RIGHT),
  UP to Erika, LEFT Koga/Sabrina, UP Blaine, RIGHT Giovanni, RIGHT pans up to the Elite Four; the Elite Four line-up
  and the entry screen match ares (badge icons only differ in their pulse phase). The rival's room needs the Elite
  Four's four battles won in the Castle first (not scripted; ares' battles take other random paths). Its arena is
  stage file 16 (7 + room 9); `PORT_BOOT=battle PORT_BOOT_STAGE=16` renders it pixel-identical to stage 15, the
  Elite Four arena already compared with ares in a battle (build/tour/gym_elite); stage 14 differs.
- Ending credits (`PORT_BOOT=credits`, new: runs State_RunCredits; normally after beating Mewtwo) crashed at once:
  Credits_LoadBackgroundScene called the stadium scene file's entry (MIPS code) directly. Now Port_CallBlobEntry with
  Port_VResolve and big-endian fog data, as the battle stages do. Credits run to the end (flight through the
  stadiums, staff roll, logo).
- Gallery photos (Film screen thumbnails, build/tour/galphoto) were plain sky-blue squares; now match ares:
  - Gallery_LoadPhotoSceneGraph: the same direct call of the stage file's entry (now Port_CallBlobEntry).
  - Gallery_ConfigurePhotoCamera copied the photo's eye/at to N64 offset 0xA8 of the camera node; on the host that
    is another field, so the camera kept no view. Now unk_60.eye / unk_60.at.
  - The photo's fog node is an unk_D_8690A610 (as in gallery_camera); written through the N64-offset struct
    unk_func_80031270_024 the fog went into the wrong fields (washed-out photos).
  - 2nd and later photos drew the sky and cliffs with the previous photo's textures: Port_ConvertDesc's
    texture-descriptor cache (gfx_translate.cpp) was never dropped, so a stage file reloaded at the same address
    kept the old descriptors (pointing into the previous pseudo slot, now another blob). It is now cleared with the
    display-list cache (Port_GfxFlushCache).
  (In ares a Z press on the Film screen showed a mark; the film-select code only marks on A, so that came from ares'
  input timing.)
- Gallery enlarge (C-Down on the Film screen) showed noise, then a 2x-zoomed quarter: it renders the photo into a
  640x480 image and draws that as a texture, but the renderer took every 320/640-wide colour image for a frame
  buffer. Now an image the game never shows with osViSwapBuffer is a render-to-texture target
  (Port_ViFrameBufferState, os_devices.cpp; `PORT_NO_VIFB_CHECK=1` turns it off), and a 640-wide target is drawn in
  the 640x480 space. Matches ares; the full regression set (incl. GB Tower walk, battles, mini-games) is unchanged.
- Gallery album flow (build/tour/galalb, galmenu): marking a photo, "Go to Album" and its confirmation texts, the
  Gallery menu with the stored photos, the camera/rental screens and a second snap session all match ares. Also
  compared (build/tour/galmenu2): Move photo (album with page/trash), Pick photo ("Select a picture"), Sticker(s)
  (photo arrangement, selection) and the 4x4/16x1 layout switch: all match ares.
- Kids Club "Who's the Best?" (build/tour/wbest): player and COM-level screens, "3 win(s) to become the Champ"
  match ares; the first game is random (port Ekans' Hoop Hurl, ares Sushi-Go-Round; both matched ares on their own).
  Note for scripts: on the player screen two DOWN presses wrap back to the player row.
- Window resized to 16:9 (`PORT_DEBUG_RESIZE=100:1600x900`, to_battle route): VS screen centred with the starfield
  filling the sides, battle scene full width, HUD in the central 4:3 area; no stretching or gaps.
- Long battle (PORT_BOOT=battle, all four moves for 12 turns, build/tour/moves): crashed at frame 21622 (a tick
  callback slot held garbage). AddressSanitizer on the same input found three host-only memory errors, fixed:
  - BattleScene_* row enter callbacks clear gBattleSceneStateTickSlots[gBattleSceneStateTickSlotIndex] while the
    index is still 7 after the tick loop; on the N64 that hits the unused D_843900A4, on the host it overwrote battle
    data that later ran as a callback (the crash). The array has a spare 8th slot now.
  - BattlePrep_InitStarfield writes D_84B19910[400] (arg0[i + 1] for the last star): array is 401 now.
  - Text_UntranscodeName ran past an empty Transfer Pak slot's zeroed name field (no 0x50 terminator): bounded to
    the 11-byte field.
  - gen_frag_sections.py copied whole fragment sections with memcpy, tripping ASan on the redzones between globals:
    uninstrumented byte copies now (no change in normal builds).
  The battle now runs to "End Battle!" (60000 frames clean under ASan); attack effects (Quick Attack, Thunder Wave,
  Flamethrower, Thunderbolt, Leech Seed, Sleep Powder, Take Down), faints, switches and sleep look right.
- AddressSanitizer pass over the recorded routes (build_asan; Gallery, all Kids Club games, Who's the Best, cups,
  Lab, trade machine, Castle, Mewtwo, Misty, intro/demos, credits, VS screen, GB Tower, Yellow battle, Lab
  transfer, GB-party Free Battle): all clean now. Fixed, each an N64 layout assumption that does not hold for
  separate host globals:
  - widget_toolkit_2.c: D_8780FCCC[2] + D_8780FCD0[4] are one 6-entry array (pause menu grid, mini-games).
  - clefairy_game.c: the arrow (12x10, drawn 11x11) and 8x8 (drawn 9x9) textures got the bytes that follow them on
    the N64 (gsSPEndDisplayList, then the next texture's start) as an extra row.
  - gb_tower_emulator_7F9A0.c: sGbPakRegisterFill[0x1F] + D_8122C51F one 32-byte block (Transfer Pak register writes);
    sFramebuffer0/sFramebuffer1 one array (indexed as (&sFramebuffer0)[i]).
  - gb_tower_emulator_86920.c: the APU setup filled sGbAudioBufferLengths up to &D_812346F2: now the three entries directly.
  - battle_engine_3591E0.c: a ribbon effect without a free auxiliary-effect slot keeps index -1; Draw/Spawn/Update
    read element -1 (unrelated memory): negative indices are inactive now.
  Normal-build pictures of all these routes are unchanged (Clefairy's arrow edge by <0.05).
- Victory Palace with the unlocked save (build/tour/vpfull): hub, the Hall of Fame pedestals (No.001 Bulbasaur to
  No.003 Venusaur, empty here) and back to the hub and main menu: match ares.
- Attract sequence (no input, frames 600-8600, build/tour/attract), paired by tools/match_frames.py: 34 of 81 port
  frames have a close ares match (the intro). The rest are attract demos with random content that differs between
  the two runs: demo battles with other teams (port Golbat vs Magikarp, ares Psyduck vs Dragonair; same stadium,
  logo and camera framing), Rattata's Run shown only on the port (matched ares in mg_r0c2), one battle strip-wipe
  frame mid-transition. Viewed the worst and mid-range pairs: no rendering faults.
- Battle move effects (found via a crash in the attract-mode demo battle after the intro, frame ~12300): the effect
  lists checked byte by byte by IsByteInArray (D_8438ADEC/ADFC/AE00/AE04/AE20/AE2C, Gen 1 effect ids such as 0x28
  Super Fang, 0x29 fixed damage) were u32 words, so the host read their bytes in reverse order: short lists matched
  effect 0 (no effect), so BattleEffect_RunMoveEffect called gMoveEffectHandlers[-1] (crash), and listed effects were
  missed. They are byte arrays in N64 order now. Changes battle outcomes to the N64's (boot_battle_move: the turn now
  ends with PIKACHU at 1/115). Scanned all game sources for other word arrays passed as byte pointers
  (-Wincompatible-pointer-types without -w): only textures and image buffers, which the renderer handles.
- One regression run showed the Castle's entry screen (r3_glc3 captures 4-5) with the cartridge/Ditto animation at
  another phase; three later runs (alone and with 6 games in parallel) were identical to the earlier port captures.
- After the VS-screen renderer change (render-to-texture order, fill alpha, dual-source blending), re-ran with their
  saves: the Yellow GB Tower walk (151 captures, identical to before), the Yellow-party Route 1 battle (run with
  PORT_GB_SOUND=1 as originally: the same game path; the remaining differences are all inside the Game Boy screen and
  come from the capture landing one frame later on a 30 fps picture, e.g. mid map scroll; two runs of the current
  build are identical), the GB-party Free Battle, Misty's gym and the Mewtwo battle (only background
  animation phase differs).

## macOS launcher (tools/launcher, "Pokemon Stadium.app"): debug option and save editor

- `tools/make_app.sh` builds the app; `tools/launcher/Launcher.swift` is the launcher window (ROM/saves per Transfer
  Pak, PLAY, Auto-play).
- **Debug** checkbox (bottom bar, off by default, stored as UserDefaults key `debug`): when it is on, a **Save editor**
  button appears next to "Change save". It opens `tools/save_editor`'s web UI on the selected Stadium save: the launcher
  runs `python3 -m stadium_save serve <save> --port 0 --write --exit-with-parent` (cwd tools/save_editor, localhost,
  free port, stderr in saves/save_editor.log) and shows the page in a WKWebView window. The page
  (`tools/save_editor/stadium_save/editor.html`) has a form for every field the editor understands: overview/health,
  progress, options, registered teams, Lab boxes, item boxes, Victory Palace, saved cup run and gallery. Pokémon are
  edited in a dialog with tabs, and raw JSON stays under *Advanced* (details in the save editor README). *Save* (or ⌘S)
  overwrites the save and keeps the previous contents as `<save>.bak`. Closing the window with unsaved changes asks
  first. The launcher has Edit and Window menus, so copy/paste and ⌘W work in the page.
- Game Boy saves: the editor also opens the save of every enabled player card (`--gb "Player N|save|rom"`; tests:
  `LAUNCHER_GB="save|rom;..."`). Each cartridge gets a section (trainer, party, PC boxes, items, Pokédex), and
  *Pokémon Lab boxes* shows it next to the Lab to move Pokémon across. With Debug on, each card with a save has an
  *Edit save* button that opens the editor on that cartridge. Moves give the same bytes as the game's Lab PC
  (German Yellow, both directions: `tools/save_editor/fixtures/lab_transfer`, from the Lab PC runs that matched ares).
- The server stops when the editor window closes or the launcher quits; `--exit-with-parent` also ends it if the
  launcher is killed (checked with SIGKILL). The game and the editor never write the save at the same time: PLAY with
  the editor open asks to close it first, and the button is disabled while a game runs.
- Test hooks: `LAUNCHER_DEBUG=0|1` and `LAUNCHER_SAVE=<file>` override the stored settings for one run without saving
  them. `LAUNCHER_OPEN_EDITOR=1` opens the editor at start. `LAUNCHER_EDITOR_SNAPSHOT=<png>` saves a picture of the
  editor page once it has loaded, then quits; `LAUNCHER_EDITOR_JS=<js>` runs in the page before that picture is taken.
  Verified on a copy of saves/stadium.flash: Options → Mono → Save, clicked in the launcher's window, changed exactly
  4 bytes (the value and checksum in both flash copies), and no server was left running. In a browser, a Poké Cup team
  (MEWTWO with perfect stats), a Round 1 unlock and an item box were saved; only those records changed, and they
  became valid. The save editor tests cover the page's API (`/api/save` with base/edited, `/api/mon`, `/api/repair`,
  `/api/meta`). `tools/save_editor/tests/test_ui.py` drives every button and field of the page inside the built app
  (`LAUNCHER_EDITOR_TEST=<js>`, `LAUNCHER_AUTO_CONFIRM=yes|no` answer confirm() and the close prompt without showing
  them). It guards against a bug found by hand: WKWebView ignored confirm()/alert() until the launcher implemented the WKUIDelegate panels, so
  "Delete saved run", "Delete team" and the other confirmations did nothing. 25 tests pass.

## Built-in save editor (src/port/save_editor, the port's own launcher)

- The ImGui launcher (`launcher.cpp`) used to start `python -m stadium_save serve` and show `editor.html` (in a
  WKWebView on macOS, the browser elsewhere), so players needed Python 3.9; on Windows that rarely worked. The editor
  is now part of the game: `stadium_save.cpp` is a C++ port of the Python library (save.py, schema.py, pokemon.py,
  text.py, gbsave.py and the edit operations of web.py) on nlohmann::json, keeping its JSON model so the two can be
  compared; `gamedata.h` comes from `tools/save_editor/gen_data.py` (same tables as `gamedata.py`).
- `tools/save_editor/tests/test_native.py` runs `stadium_save_cli` (CMake target, not shipped) against the Python
  library on the fixtures, the verify saves, random and damaged saves: identical dumps, byte-identical builds, the same
  repair and checksum fixes, the same bytes after 13 kinds of editor edits on every fixture, the same new Pokémon (and
  the same refusals), the same names, and the same Game Boy edits; the Lab PC transfers give the bytes the game wrote.
- `save_editor_ui.cpp` draws the pages of `editor.html` without the raw JSON view: overview with repair, progress,
  options, registered teams, Lab boxes (moving Pokémon to and from the Transfer Pak cartridges), item boxes, Victory
  Palace, saved cup run, gallery, and each Game Boy save (trainer, party, boxes, items, Pokédex). Pokémon are edited
  on a page with Pokémon / Moves / Stats tabs. Species, moves and items are chosen on a picker page with a search
  field, and questions replace the page, so on the handheld layout every control is a gamepad focus target and B goes
  back. Save rebuilds the file from the dump it was opened with and commits only the records that changed, refuses a
  file that changed on disk since, and keeps the previous file as `.bak`, as the page did.
- Names: the launcher font has Latin-1 only, so ♂ and ♀ are typed and shown as `<M>` / `<F>` in name fields (PK and
  MN as `<PK>` / `<MN>`), and a *Symbols* button next to each name field picks them and é × ' ¥ ... from a list.
  Letters are no longer forced to upper case (é became É, which the Game Boy set lacks, so it could not be typed; Game
  Boy names may be lower case). `tools/save_editor/tests` round-trip every Game Boy byte through both codecs.
- Desktop: Debug → *Save editor* (and *Edit save* on a Transfer Pak card) replaces the launcher's panels until
  *Close*; ⌘S/Ctrl+S saves, Escape goes back. Handheld (Android, `PORT_LAUNCHER_HANDHELD=1`): *Files & help* →
  *Save editor*. The release zips and the macOS app no longer carry `save_editor/`.

## Linux and Windows

What made the code macOS-only, and what replaces it:

- **Symbols**: Mach-O prefixes C symbols with `_`, ELF and Win64 COFF do not. `abs_syms.S` (`SYM()`), the function
  table (`PORT_ASM_NAME`) and `LUS_osContStartReadData`'s asm label pick the prefix per platform.
- **Per-fragment data sections** (`tools/gen_frag_sections.py`): Mach-O `__DATA,__fdNN` sections with
  `getsectiondata`; ELF named sections with the linker's `__start_`/`__stop_` symbols; COFF `fdNN$a/$m/$z` grouped
  sections (`#pragma data_seg`) with marker variables in `$a` and `$z`.
- **Word arrays** (textures the RDP reads in N64 byte order): found with `dladdr` before, which only sees exported
  symbols; now each game file with such arrays is compiled through a generated wrapper that registers them
  (`tools/gen_word_arrays.py`, `Port_RegisterWordArray`).
- **Crash reports and debug helpers**: `src/port/port_platform.h` (image base, backtrace, readable-memory check) for
  all three. libultraship's own crash handler replaces signal handlers on Linux and Windows, so `main.cpp` installs the
  port's after `Port::Init`, each thread gets an alternate signal stack, and the report header is written before the
  backtrace (which can fault itself).
- **Address heuristics**: N64 addresses are told from host pointers by "below 4 GB". macOS and Linux put 64-bit
  programs above that; Windows may not, so `Port_ReserveLowAddressSpace` reserves the free space below 4 GB at start.
  The renderer's vertex check rejected Linux heap pointers (`0xaaaa…` on arm64, above 47 bits): the upper limit is now
  the 48-bit user space.
- **Linking (Linux)**: lld (GNU ld is single-pass; the game library uses libultra symbols from libultraship).

Bugs that only showed on Linux (other heap addresses and stack layouts than macOS):

- `BattlePrep_Align64PoolBlock` returned a pointer as `s32`: the cut address (sign-extended `0xffffffff…`) crashed
  the texture load of the badge carousel. On macOS the cut value fell below 4 GB and was taken as an N64 address.
  `Display_GetWorkerStatus` had the same pattern. A scan for such cuts: compile with `-Wshorten-64-to-32
  -Wpointer-to-int-cast` (most hits are ROM offsets and N64 address arithmetic, which are fine).
- `BattleAI_InsertRankedCandidateGroup` wrote 7 entries into 6- and 3x3-sized locals (room on the N64 frame); on
  Linux that overwrote the saved return address (crash at frame 504 of `PORT_BOOT=battle`).

Renderer: the fixes first made in the Metal backend are also in OpenGL and DirectX 11: dual-source blending (colour
by the blend alpha, the written alpha is the N64 coverage bit), RGBA render targets (OpenGL had RGB), the first pass
into a new render target clears it, `ClearFramebufferColor`, and the 8-bit screen readback for screenshots. OpenGL
keeps the compatibility context (GLSL 1.30) on Linux and binds the two outputs with `glBindFragDataLocationIndexed`;
switching to the 4.1 core profile (as macOS's OpenGL path) rendered wrong. `PORT_GL_NO_DUAL=1` turns dual-source off.

Checks: a Linux VM (Ubuntu 25.04 arm64, Xvfb + Mesa) runs the same tours as macOS (`REGRESS_RUNNER=linux`) against the
ares references; the Windows-only code (DX11 backend, COFF sections, crash handler) is syntax-checked with the
mingw-w64 headers, and the CI workflow builds with clang-cl + vcpkg.

Shader preprocessor (prism) pitfall: after an `@elseif` branch (or an `@if` nested in an `@else`) that is taken, it
drops the rest of the shader. The fragment outputs therefore use flat flags set in gfx_opengl.cpp (`dual_out`,
`single_out`). Before that, Linux's alpha shaders lost their tail: render-to-texture UI built in earlier frames (Game
Pak Check panels, menu buttons behind the Event Battle dialog) went missing, and macOS's OpenGL backend failed to link.
The port also never drops a frame now (`PORT_ALLOW_FRAME_DROP=1` restores libultraship's behaviour): the DXGI backend
paced by wall clock, and a dropped frame lost what the game draws only once.

Results (26 tours against ares, `REGRESS_RUNNER=linux`): Linux is within ±1 of macOS on every tour, no crashes or hangs.
German Yellow on both platforms (inputs: each tour's Game Boy save and Stadium flash from its ares run): Lab PC transfer
(`y_xfer1`) and Lab (`y_lab6`) 6-12 from ares; GB Tower (`y_twr1`, 151 pictures) 29-31 on average, where the high
ones are timing only (ares loads the cartridge later: the port shows the title while ares is on the copyright screen);
`y_bat` GB Tower frames within 4-6 between Linux and macOS. GB Tower tours need the regression runner's freeze exit off
(still pictures during dialogs).

OpenGL orientation: when the window matched the render size (Linux, 640x480), the game buffers were created without Y
inversion, and the GUI showed the game upside down in the window (only a grab of the X screen showed it: screenshots
read back bottom-up and looked right). They are now inverted whenever they are shown as a texture, and the GL readback
reverses the rows of non-inverted buffers (the window), so screenshots are the right way up on Linux and on macOS's
OpenGL backend. `PORT_GL_FBLOG=1` lists the GL frame buffers' sizes and inversion.

Open (macOS's optional OpenGL backend only, the 4.1 core profile; not Metal or Linux): the "Game Pak Check" label text
is missing (its box is drawn). Its render target holds the same pixels as on Linux, no GL errors are raised, and it
stays missing with dual-source blending off (`PORT_GL_NO_DUAL=1`). The generated GLSL is identical to Linux's apart from the output
declarations (`PORT_GL_DUMP_SHADERS=<dir>` writes every shader), and texture filtering makes no difference, so the cause
is in Apple's OpenGL driver state; not pursued further since Metal is the macOS backend.

Against ares, what is not a rendering difference:
- Opponent choice (gyms, cups, Mewtwo, and every tour that fights one): `session.c` burns `osGetCount() & 0xF` random
  numbers before picking, i.e. the low 4 bits of the CPU cycle counter. The port has no cycle-accurate CPU, so it picks
  a different trainer than ares from the same inputs, and those tours then show different Pokémon (44-81 from ares).
  Battles do the same: the AI and the damage rolls mix CPU-count bits into guRandom's fixed-seed sequence
  (`osGetCount()` in battle_engine_359F90.c, battle_engine_2F6930.c, battle_engine_361050.c), so outcomes and the
  following trainers differ from ares even with identical input and saves (gym_misty and gym_koga fight a different
  trainer; with the reference saves the other battle tours line up: mewtwo 11, final_mewtwo 27, cup_prime 23).
  Matching that would need a cycle-accurate CPU. The regression runner now uses each tour's ares saves and cartridge
  (`ares.log`), which removed the save-related differences (trade 62 -> 8, dex 51 -> 11).
- GB Tower: ares loads the cartridge later. Paired with the nearest ares picture, `y_bat` (420 pictures) is median
  10.8 (Linux) / 11.9 (macOS) from ares, max 14.4 / 15.8, and `y_twr1` 8.9 / 11.7; the rest is ares's horizontal VI
  filter.

Windows run test: cross-built for Windows on ARM with llvm-mingw (cmake/toolchain-mingw-clang.cmake, vcpkg
arm64-mingw-static) and run under Wine 11 built from source with PE builtins (Ubuntu's Wine 9 cannot unwind
pointer-authenticated code; Debian's Wine 8 has ELF builtins, so printf's variadic calls break; Rosetta cannot run Wine
at all). It reaches the title screen and matches macOS (0.0-0.7 at frames 400/700, 3.9 at the title, the blinking
PRESS START). The DX11 backend stops at a Wine shader-compiler limit ("Structured buffers store is not implemented",
vkd3d-shader); Windows itself compiles that shader with Microsoft's compiler, so the test used the OpenGL backend.
Fixed on the way: bzero/bcmp/bcopy (not in the Windows C runtime), libultraship includes (cstdlib, lowercase Windows
headers, WM_GETDPISCALEDSIZE), the system libraries MSVC links by pragma (d3dcompiler, dwmapi, ksuser), and a static
C++ runtime (one exe, no DLLs).

Widescreen margins: with a window wider than 4:3, the game frame buffers have margins the N64 picture does not cover.
3D scenes draw into them (the trailer's clouds), 2D screens (title, menus) do not, so each of the rotating frame
buffers kept what an earlier scene left there and the margins flickered as the buffers took turns (reported on Windows,
same on every backend). The first frame buffer the game selects in a frame gets two black fill rectangles left and
right of the picture (interpreter GfxDpSetColorImage); 3D scenes redraw the margins on top, and a 4:3 window is not
touched. `PORT_NO_MARGIN_CLEAR=1` restores the old behaviour.

Battle HUD at the widescreen edges: the status boxes, portraits and menus were laid out for the 4:3 frame and cut at
its sides, so in 16:9 they slid in and out of the middle of the picture (from the 4:3 edge, the rest of the box
missing) and sat inside it. The game now brackets each side's HUD with markers (`gDPPortAnchorLeft/Right/End` in
StadionPort.h, a G_NOOP of type 0x52): `Battle_RenderHud` around `BattleHud_DrawOwnSideStatus` (left: the player's
box, portrait frame, command bar and move menu) and `BattleHud_DrawOpponentSideStatus` (right, with its label bar),
`Battle_DrawCompactStatusBoxesIfVisible` (the small boxes shown during moves), and `Battle_FillActivePokemonThumbnail`
for the portraits, which are captured into a render target only when they change and blitted every frame at the
portrait camera's viewport (`Port_SetHudAnchorCameras` / `Port_HudAnchorSide` tell which camera is which side; a
registration older than 4 retraces does not count). Between the markers the renderer (interpreter `sAnchor`) moves
the viewport and scissor by the margin beside the 4:3 frame and does not cut at the 4:3 sides, so each side sits at
the screen's edge and slides in from it; what the game parks off screen stays past the screen's edge. At 4:3 nothing
changes. `PORT_NO_HUD_ANCHOR=1` turns it off.
Elite Four in widescreen: the Elite Four room (stage file 15) and the rival's room (16, the same room) were drawn
only inside the 4:3 area, with empty sides, in every frame of their battles, while all other stages filled 16:9. Their
sunset sky is the one stage backdrop drawn as a texture (`Battle_DrawStageBackdrop`: two rectangles over the whole
screen, each half its height), before the arena and with nothing 3D before it (the cup stadiums draw their crowd first
in `BgStage_DrawFrame`), so the 2D-screen check (interpreter `s2DRectArea`: textured rectangles over 90% of the picture
before any 3D) took the frame for a menu and kept all of it inside the 4:3 area. The backdrop now marks the frame as a
3D scene's first (`gDPPort3DScene` in StadionPort.h, a G_NOOP of type 0x53, sets the interpreter's `sSeen3DThisRun`), so
the sky is widened as the other stages' backdrops are and the arena fills the picture. Checked over all 18 stage files
in 16:9 (`PORT_BOOT=battle PORT_BOOT_STAGE=n`: only 15 and 16 change) and the Castle's Elite Four and rival route; at
4:3 the pictures are identical. `PORT_NO_3D_SCENE_MARK=1` ignores the marker.
The gallery camera's film reel (Pokémon Snap station) ran off the N64 screen's right edge, so in 16:9 its strip stopped
at the 4:3 line in the middle of the picture. Moving it to the edge would put the canister on the viewfinder's
corner, so it stays where the game puts it and only the strip goes on: `gDPPortNoCut` (marker value 3: not moved, not
cut, and the edge-strip drop does not apply), the game's own clip rectangle (`D_800A7440`, its right edge is the field
`y1`) widened by `Port_WideMarginN64()` (54 N64 pixels at 16:9) while it draws, and as many more 8-pixel strip pieces
(`Gallery_DrawPhotoCounterReel` / `...Exit`). Found by the Smooth motion + 16:9 sweep over the function-evidence
routes (seam detector: a sharp edge exactly on the 4:3 line with picture on both sides); the sweep found no other
case. Before/after pictures of each fix: `tools/widescreen_review.py` (add a case there for every new one).

Patterned menu backdrops in widescreen: the main menu, Game Pak Check, Battle Now and Options draw their wallpaper
(the "POKéMON" pattern, the green pattern of Options, the cream pattern of the fades) as one textured
rectangle over the whole N64 screen with a small repeating tile (WRAP or MIRROR, the rectangle several periods wide).
In a picture wider than 4:3 that rectangle reaches both screen edges with its texture coordinates continued
(interpreter GfxDpTextureRectangle, `sWidePattern`), so the pattern goes on at its own size instead of being
stretched, and only it is drawn past the 2D screen's 4:3 lock: panels, text, icons and cursors keep their 4:3 place
and size. Screens with a picture backdrop (title, the hub map, Kids Club, JPEG backgrounds; clamped tiles) stay
pillarboxed, and so do patterns drawn as full-width bands (the GB screens' shaded "GB" wallpaper, the gallery's film
screen, where panels fill the rows between the bands and widened bands would leave gaps).

So that the switch between such a widescreen menu and a 4:3 screen does not flicker: full-screen fades (one colour,
every vertex on a side of the 4:3 frame) cover the whole picture on 2D screens too, past their 4:3 lock (over a
widened pattern only the middle used to fade; around a 4:3 picture the margins now fade with it), while wipes with a
textured edge or over part of the screen keep the lock. The single-colour frames the game draws with the CPU between
screens (white, black; `PresentCpuFrame`) are shown full width when the frame before covered the whole picture
(`Port_LastFrameFilledWide`), instead of a 4:3 frame between two widescreen ones. Nothing changes at 4:3.
`PORT_NO_WIDE_PATTERN=1` turns all of this off.

## Internal resolution and scaling (launcher > Graphics)

The game can be drawn at the window's pixels, at the N64's own size or a multiple of it, or at a fixed height, and
that picture is scaled to the window. The panel follows the usual PC-game and emulator settings pages (Dolphin,
DuckStation; layout reviewed with Fable 5.2): Display, one Internal resolution list, Scaling, Anti-aliasing, VSync,
Smooth motion, Keep full speed, and two presets.

- Settings: `Port::ResolutionSetting` (Engine.h), CVars `gPort.Resolution.Mode` (0 Match window, 1 N64 multiple,
  2 fixed height), `.Multiple` (1-6), `.Height` (480-2160), `.Upscale` (0 Integer, 1 Sharp, 2 Smooth).
  `PORT_RESOLUTION=window|window:50|native|2x..6x|480p..2160p` and `PORT_UPSCALE=exact|sharp|smooth` override them
  for a run. `ApplyAspect` hands them to libultraship's fixed-aspect ("advanced") resolution CVars together with the
  4:3 / 16:9 shape: the N64 and fixed-height modes set `VerticalResolutionToggle` + `VerticalPixelCount` and give the
  width as the aspect ratio (`AspectRatioX/Y` = width/height), so the size is exact.
- Sizes. 4:3: 320 x 240 times n; heights get width = height x 4/3 (720p: 960 x 720). 16:9: the N64's 240 lines get
  426 x 240 (16:9 is 426.7): the 4:3 middle then starts on a whole pixel (53 each side), so the 2D screens and HUD
  are not shifted by half a pixel. 428 (Port_WideMarginN64's 54 a side) would not fit 3x into 1280 x 720, 6x into
  2560 x 1440 or 9x into 3840 x 2160 for Integer scaling. Fixed heights use 16:9 widths with an even margin (480p:
  854, not 853). The few 640 x 480 screens (Trade Machine) are drawn at half their detail at Native: use 2x.
- Match window (`gAdvancedResolution.MatchPixels`, libultraship's `Fast3dGui::CalculateGameViewport`)
  counts the window's pixels, not ImGui's points: on a Retina Mac it used to draw 1280 x 720 in a 2560 x 1440 window
  and scale that up. It now draws every pixel (four times the work there; Low-power or a fixed size for old Intel
  Macs). The old percentages are gone: on the first start with this version a configuration below 100 % (the
  slow-chip preset's 50 %) becomes 2x, anything else Match window, and a new configuration gets the Recommended
  preset (MSAA 4x, Smooth motion on). The web build's devicePixelRatio scale is covered by MatchPixels now (untested
  there).
- Scaling (`Fast3dGui::DrawGame`). Integer (`PixelPerfectMode`): the largest whole multiple of the picture that fits
  the window, measured in pixels (libultraship measured in points, so HiDPI displays could get uneven pixels), centred
  with black borders; a picture larger than the window (2160p in a 1080p window) is fitted, scaled down, instead of
  drawn at 1x and cut off. Sharp and Integer magnify nearest (`gAdvancedResolution.NearestUpscale`); shrinking stays
  linear. Match window's picture is not scaled; Scaling is greyed out there.
- Nearest sampling where ImGui draws the game picture into the window, per backend (gfx_rendering_api.h
  `GetFramebufferTextureIdNearest`, `UseNearestSamplingForImGui`, called from an ImGui draw callback before the
  picture; `ImDrawCallback_ResetRenderState` after it): OpenGL binds a sampler object to unit 0 (`glBindSampler`,
  looked up at run time: GL 3.3 / ES 3), DirectX 11 sets a point-mag sampler (`PSSetSamplers`), Metal sets a copy of
  imgui_impl_metal's pipeline whose shader samples nearest (imgui_impl_metal's sampler is constexpr in its shader),
  Vulkan hands ImGui a second descriptor set for the framebuffer with a nearest sampler.
- Display: `Window.Fullscreen.Enabled` in the config, applied on Play (the launcher keeps its own window size);
  `gSdlWindowedFullscreen` = 1 on the desktop makes SDL's fullscreen borderless (DXGI's already is; macOS uses its own
  fullscreen). F11 toggles in-game as before.
- A fixed size taller than the display's pixels (`SDL_GetDesktopDisplayMode` height times pixel density; 1912 on an
  M5 Air's default scaled mode) is drawn and then shrunk: the hint then says so and names a size that fits. 2160p
  draws four times 1080p's pixels (menus: 4.6 against 1.6 ms of GPU time per picture on an M5 Air).
- Launcher: the Internal resolution list is an ImGui combo (the first one in the launcher). The launcher brings its
  window to the front every frame, so the list's popup brings itself to the front too. Left/Right on the closed list
  step to the neighbouring entry. `PORT_LAUNCHER_OPEN_RESOLUTION=1` opens it for a `PORT_LAUNCHER_SHOT` picture.

Checked on an M-series Mac (2560 x 1440 pixel window, 1280 x 720 points), Metal, OpenGL and Vulkan (MoltenVK;
`-DLUS_ENABLE_VULKAN=ON`): every resolution and scaling in 4:3 and 16:9 in a battle (boot_battle_move.rec, frames
1300 and 1800). The three backends gave the same picture rectangles: Native Integer = 6x (1920 x 1440 / 2556 x 1440),
runs of exactly 6 identical pixels; 2x -> 3x; 3x -> 2x; 4x (960 lines) -> 1x with borders; 720p -> 2x; Smooth has no
such runs. Also: MSAA 2x/4x with Native, 2x, 3x and 720p; Smooth motion at normal speed at Native and 720p; a window
resized during the battle (Integer re-centres: 1024 x 768 -> 4x, 1704 x 960); the Elite Four room at Native 16:9;
HUD, text boxes and the 4:3 middle line up at Native in 16:9. The replay suite: all 22 pixel-identical to the
references at the old default size (`PORT_RESOLUTION=window:50` on Retina = the old points size, MSAA off) and runs through at Native (4:3 Metal,
16:9 Vulkan) and 720p (16:9 OpenGL); at Native `tour_play`'s freeze check stops it on the Kids Club "Who wants to
play?" screen, whose slow idle animation repeats few distinct pictures at 320 x 240 (the same screens as at full
size; with `PORT_FREEZE_CHECK=0` it reaches its stop frame). Not checked: DirectX 11 and Windows (the DX11 sampler
code is not even compiled here), Linux, Android, the web build, Display/fullscreen on Play, gamepad use of the list.

## Vulkan failures: OpenGL for the next start instead of a crash

A swapchain that cannot be created already ended the game with a message and OpenGL selected for the next start
(`Fast::VulkanSwapchainError`, `Port::ExitAfterVulkanFailure`). Other Vulkan calls only logged their errors and went
on: a user report (Windows, Intel HD driver `igvk64.dll`) had `vkAllocateCommandBuffers` fail with
VK_ERROR_OUT_OF_HOST_MEMORY in `EnsurePassActive`, and the next call, `vkBeginCommandBuffer` with the null handle,
crashed in the driver (0xc0000005). Now `VK_CHECK` throws `Fast::VulkanFatalError` (the swapchain error is one) on
errors that leave the device unusable (out of host or device memory, device lost, initialization failed), and
`main` takes the same path: OpenGL for the next start, a dialog naming the call ("vkAllocateCommandBuffers failed:
the graphics driver ran out of memory"), exit. Only on the drawing thread: the pipeline prebuild workers keep
logging, since an exception there would end in std::terminate. The dialog is left out with `PORT_HIDDEN_WINDOW`.
`PORT_DEBUG_VK_OOM=<n>` makes the n-th command buffer request (`AcquireCommandBuffer`) fail that way (test).

The cause of that report was a leak: each frame allocated new command buffers from its slot's pool (one per
framebuffer drawn, one per synchronous readback, one for uploads, about 2 per game frame) and never freed them, while
`vkResetCommandPool` resets buffers without freeing them. So every pool grew for as long as the game ran (16,840
buffers over the 8,200 frames of the gbtower_quit replay) until the Intel driver ran out of host memory, after a
long session in the GB Tower. Now each `FrameSlot` keeps its buffers (`commandBuffers`) and hands them out again
after the reset. That replay allocates 30 in total (at most 18 per slot), and `PORT_VK_STATS=1` prints the counts.
Checked on MoltenVK (`-DLUS_ENABLE_VULKAN=ON`). `PORT_DEBUG_VK_OOM=12000` in that replay (in the tower) exits with
the dialog's text and OpenGL saved.

## Game sound: music, sound effects, voices

The game's own sound (everything but the GB Tower, which has its own path, above) used to be compiled out
(`PORT_NO_AUDIO`: every entry point of the sound engine returned at once). It now runs, from sequence player to DAC:

- **libnumus** (`game/src/libnumus/player.c`, the SDK's music player) interprets songs and sound effects and drives
  **libnaudio** (`game/src/libnaudio`, the SDK's `n_audio` synthesizer), which builds an audio command list per frame
  (552 or 368 samples at 32 kHz). The audio thread submits it as an `M_AUDTASK`.
- **Audio microcode, emulated** (`src/port/audio_hle.cpp`): the game's aspMain is the "naudio" ABI (ucode data word
  0x127c at +0x10, as mupen64plus-rsp-hle identifies it). DMEM is modelled byte for byte in N64 order; the commands
  (ADPCM, RESAMPLE, ENVMIXER, MIXER, INTERLEAVE, LOAD/SAVEBUFF, DMEMMOVE, ...) follow the microcode's arithmetic.
  The resampler's 64x4 tap table is read from the ROM's microcode data (0x7F670 + 0xB0). Command 14 is `A_POLEF` in
  the library, but this microcode's jump table sends it into the middle of SETVOL (IMEM 0x12b0, replacing the low half
  of the right ramp's rate), so it does that here too.
- **Audio interface** (`src/port/audio_out.cpp`): `osAiSetFrequency` computes the DAC divider as libultra does (32000
  -> 32006 Hz), and the AI is a virtual two-entry DMA FIFO drained by VI frames at the DAC rate (NTSC VI ~59.826 Hz).
  The game reads the remaining length (`AI_LEN`, now `osAiGetLength`) to choose each frame's length, so what it produces
  depends only on game time: lockstep runs stay deterministic, and nothing waits for the host's sound device. Accepted
  buffers go to an SDL3 stream whose playback speed follows its fill level (PI control around 70 ms, at most ±3 %; a
  real device needs ~0.3 % because the host paces frames at 60 Hz); far behind, the queue is dropped.
- **Voices**: Pokémon cries and the announcer are GSM 06.10 streams decoded on the CPU (`audio_stream.c`,
  `gsm_decoder.c`); the hand-written MIPS decoder kernels (`libgsm.s`) are translated to C in
  `game/src/libgsm/libgsm.c` with the same arithmetic (no saturation where the assembly has none, 32-bit products).
  The Lab's Game Boy-style cries (`gb_audio.c`, `gb_audio_render.c`) render on the CPU too. Both are mixed in by
  the synthesizer's main bus from CPU-written buffers.

Switches: `PORT_SOUND=0/1` (host output; default on at `PORT_SPEED=1`, off otherwise so fast runs never pile up or
stall; also decides the GB Tower's output unless `PORT_GB_SOUND` is set), `PORT_GAME_AUDIO=0/1` (the engine itself,
see below), `PORT_AUDIO_DUMP=<file>` (everything the AI accepts, s16 stereo host order at the AI rate, plus
`<file>.frames`), traces `PORT_TRACE_ACMD` (first command lists), `PORT_TRACE_SFX` (songs, effects, fades, streams with
callers), `PORT_TRACE_ACTIVITY` (the engine's activity score and channels once a second), `PORT_TRACE_AUDIO_OUT`
(SDL queue, ratio, measured playback rate).

### What had to change for a 64-bit little-endian host
- **ROM data**: sound banks ("N64 PtrTablesV2"), songs, FX tables, sequence files, archive headers and the GB music
  table are big-endian blobs whose tables hold 32-bit offsets the game relocated in place into absolute addresses.
  Headers and offset tables are byte-swapped once on load (`game/src/port_audio.c`, the relocation functions in
  player.c and gb_audio.c) and stay offsets: the readers add the blob's address (`PORT_BLOB_AT`). Wave tables, which
  the synthesizer reads through `ALWaveTable` pointers, are rebuilt as host structures per bank (cached by blob and
  content, so a reloaded bank reuses them); their ADPCM books and loop states stay in the blob, byte-swapped.
  `ALSeqFile` (`alSeqFileNew`) is converted in place to the host layout (its entries hold a pointer: twice the size;
  buffers sized with `PORT_SEQFILE_BYTES`). The GSM stream header and bit stream, the noise samples of the GB cries
  and the GB noise table's ids are read big-endian.
- **Command lists**: `Acmd` has 32-bit words, and several address fields only 24 bits. The `n_abi.h` macros (and
  `aSetLoop`/`aLoadADPCM`) record each command's full host pointer in a side table keyed by the command's slot
  (`Port_AcmdAddr`); `K0_TO_PHYS` (a 32-bit cut) is gone from the synthesizer. Sample data DMA'd from ROM is marked as
  N64 byte order; every other DRAM buffer the microcode touches holds host-order samples.
- **Pointers in 32-bit places**: the DMA callback's return value and `dc_memin`, `sv_dramout`, `Rom_DmaRead`'s buffer,
  the player's command queue (32-bit ids now: `PortA_PtrId`), a channel's wave table (`channel_t.sample`), the GB music
  engine's stream pointers, the announcer's DMA callback (also called through a prototype with the wrong types).
  `ALParam` gained a pointer member: the start-voice parameters share its pool and are larger on the host (they
  overwrote the next entry). Reverb: `&input[-delay]` with an unsigned delay wrapped on the N64 and went 16 GB ahead
  here. A cleared range in `Audio_ResetVoiceState` and the GSM state's offset were N64 struct offsets.
- **Missing return values**: five decomp functions end without `return` and the MIPS code leaves the value in v0
  (checked in the original binary). `Audio_PlayDirectSoundCommand` returned garbage, so the crowd-noise loop in battle
  could never be stopped; when its table was reloaded the channel ran into other data (a jump to address 0 there).
- **Interior data symbols** (`undefined_syms.ld` in the decomp): the loop-point and music tables are addressed by
  offset into their arrays (two of them are big-endian u16 tables kept as bytes).
- **Spin-waits**: four places spin until the engine is quiet (`while (Audio_GetActivityScore() != 0) {}`); on the N64
  the audio thread preempts them. Here they block one frame per pass (`PortA_SpinWait`).
- **Title screen**: it switches the DMA callback to "waves in RAM" while ROM-wave voices may still sound; those read
  unmapped RDRAM on the N64 and get silence here.
- The audio heap is twice the size (host structures are larger).

### Timing and old recordings
With the engine running, the game waits for fades and sounds as on the N64 (e.g. the title screen starts the attract
demo only when the title music has ended, at frame ~3320 from boot, as in ares; it used to start 600 frames into the
title). Input recordings made without sound therefore desync. Recordings now carry a `# game audio` line; replaying one
without it turns the engine off (`[audio] game sound off` in the log), so all recordings in `replays/` and the tours
replay exactly as before (checked: `to_battle.rec` reaches the same frames and the same picture). `tools/tour.py`
writes the marker (its port runs have sound, like ares) and `--audio` records both sides' sound.

### Compared with ares
`tools/audio_compare.py <port.pcm> <ares.pcm>` aligns the port's dump (resampled to 48 kHz) with an `ARES_REF_WAV`
recording by loudness envelope, then to the sample, and reports per second the level of both, the envelope and the
waveform correlation and the best local offset; `--align-from/--align-to` align on one scene, `--wav` writes both for
listening.
- Boot, N64 logo, intro and title music (1500 frames from boot): level within 0.2 dB every second; waveforms correlate
  at 0.99-0.999 at the best local offset. That offset is 0 or 184 samples (5.75 ms): now and then a frame's length
  choice (552 vs 368) falls one frame apart, i.e. timing of when the audio thread reads `AI_LEN`, not synthesis.
- Battle Now (menus, rental selection, battle start with announcer and cries, battle music; `tools/tour.py bn ...
  --audio`): aligned on the battle intro, envelope correlation 0.96-0.98, waveform up to 0.92, level within 0.1 dB; the
  battle music's level is within 0.5 dB throughout. Waveforms there correlate less (0.2-0.4): the crowd-noise loop's
  phase differs between the runs, and the computer's lead Pokémon is random (see above), so cries differ.
- Linux (Xvfb, OpenGL) produces a bit-identical dump to macOS for the same run.

### Sound settings (launcher > Sound)

A panel next to Graphics (`SoundPanel`, launcher.cpp), stored as CVars and handed to the audio paths at start and on
every change (`Port::ApplySoundSettings`, Engine.cpp; state in sound_settings.cpp, read by the game and audio threads):
- `gSound.Music`, `gSound.Effects` (0-100): the music player's voice volume (`Audio_ComputeVoiceVolume`,
  libnumus/player.c), by the voice's `IsFX`, on top of the game's own master volumes and fades.
- `gSound.Voices`: the stream mix in audio_stream.c (`func_80044EA4`). Cries (`Cry_Play` starts stream 0) and the
  announcer play through those two streams.
- `gSound.Master`, `gSound.GameBoy`, `gSound.On`, `gSound.MuteInBackground`: the output streams' gain
  (`SDL_SetAudioStreamGain`; audio_out.cpp for the game, gb_audio_out.cpp for the GB Tower, which also takes the Game
  Boy volume). The window thread reports the focus every frame (`NoteSoundWindowFocus`, Engine.cpp).
- `gSound.Delay` (0 normal, 1 short, 2 long): the output queue target when the stream opens (`SoundQueueTarget`):
  short 60 % of the platform's (at least 20 ms), long 120 ms, which the adaptive target reaches on a slow PC anyway.
  macOS: 22.9 / 20.0 / 120 ms (`PORT_TRACE_AUDIO_OUT=1` prints it).
Volumes follow a square curve (50 % = 1/4 amplitude, -12 dB, about half as loud). Music and effects scale the music
player's voice *volume*, which libnaudio squares itself (`n_env.c`: `em_volume = volume^2 >> 15`), so their gain is the
linear fraction; applying the square there made 50 % come out at 1/16 (-24 dB) and 25 % inaudible (tester report). Measured
(`PORT_AUDIO_DUMP`, main menu, RMS relative to 100 %): 75 % 0.56, 50 % 0.25, 25 % 0.063, 10 % 0.014. Voices scale
samples, so they use the square. At 100 % nothing is multiplied: the mix is
bit-identical to before. Stereo or mono stays the game's own Options setting. The handheld (Android) launcher has no
Sound panel yet. `PORT_LAUNCHER_OPEN_SOUND=1` opens it (with `PORT_LAUNCHER_SHOT` for a picture).

Checked: `PORT_AUDIO_DUMP` of the rental route (Music 0 and Effects 0 each remove their part; defaults identical) and
of `boot_battle_move` with `PORT_BOOT=battle` (Voices 0 removes the cries and announcer, the rest unchanged); SDL's
disk driver (`SDL_AUDIO_DRIVER=disk`) for the output: Master 50 % gives 1/4 of the amplitude, Sound off and Mute in the
background (a hidden window has no focus) are silent. Battle runs with game sound on vary from run to run (an announcer
line at frame 1443 sometimes does not start); that is older than these settings.

## Shader warm-up

Fast3D builds a shader program the first time a combiner/state combination is drawn, and that draw waits for it. With a
warm Metal cache that is ~1 ms; on a fresh install or after an update (new binary, empty Metal cache) it is 300-700 ms
per program, a visible hitch the first time each effect appears. The stored recordings draw 113 distinct programs
(`to_battle` alone 48). So the port builds them ahead (`src/port/shader_warmup.cpp`, started at the end of
`Port::Init`):

- **The list**: `src/port/shader_warmup.txt` (packed into `pokestadium.o2r` as `port/shader_warmup.txt`), the programs
  the recordings draw, ordered by how many recordings draw them (boot and menus first). `tools/shader_list.py --run`
  plays all recordings with `PORT_TRACE_SHADERS=1` and rewrites it; lists recorded while playing can be added
  (`tools/shader_list.py <shaders.txt>`). Plus `shaders.txt` next to `pokestadium.cfg.json`: each program this
  installation draws that is in neither list is appended there, and later runs build it ahead too. Ids that a newer
  build no longer draws only cost a build; a program is always built from its id by the current code.
- **Metal**: worker threads (half the cores, 2-6) build each program's library and pipeline (`PrebuildShadersAsync` in
  `gfx_metal.cpp`; Metal device calls are thread-safe). `CreateAndLoadNewShader` takes a finished one, waits for one
  that is being built, or builds it itself (and the workers then skip it). The source generator
  (`gfx_metal_build_shader`) keeps state in statics, so it runs under a lock; that part takes well under 1 ms. A
  program built for another filter mode is dropped and built again. The launcher stays responsive (no launcher frame
  later than 40 ms while it builds), and with Auto-play the game starts at once while the workers build.
- **OpenGL, DX11**: no builder thread (an OpenGL context belongs to one thread). The interpreter builds the queued
  programs on the render thread before each frame's first draw, up to 4 ms per frame (at least one), i.e. during the
  launcher, or during the boot logos with Auto-play (`Interpreter::WarmupStep`). Checked on macOS's OpenGL backend:
  the same picture with and without warm-up, all programs of `to_battle` built ahead. DX11 is not tested.
- `PORT_NO_SHADER_WARMUP=1` turns it off (programs are still recorded). `PORT_SHADER_LIST=<file>` records into another
  file (`tools/run_replays.py` gives each run its own, so test runs use the shipped list only).
  `PORT_SHADER_THREADS=<n>` sets the number of Metal workers. `PORT_SHADER_SALT=<text>` adds a comment to every Metal
  source so that each build misses the Metal cache (to measure cold builds: the cache of an unbundled binary is shared
  and keyed by the source, so a copied binary is not reliably cold).
- `PORT_TRACE_SHADERS=1` (or `PORT_TRACE_PACING=1`): `[shader] <id0>/<id1> built|prebuilt in X ms` per program at its
  first draw (the time that draw waited), the size of the list, and when the warm-up is done.
- First Poke Ball throw stutter (user report, first start of a freshly built app): that session's `shaders.txt` got 8
  programs the list lacked, translucent and fogged variants (option bits ALPHA+FOG, some with texture clamp or edge)
  of shipped programs, such as a Pokemon fading in on a send-out in a fogged arena; with the new binary's empty
  Metal cache each was built at its first draw (500-700 ms cold, measured with `PORT_SHADER_SALT`). They are in the
  list now (121 programs; cold warm-up 13.4 s, done during boot). With a warm cache the first throw shows no hitch: a
  real-time visible-window run of `PORT_BOOT=battle` through the send-outs, and max-speed runs of `to_battle` and the
  battle with every frame drawn, have no retrace, draw or game-thread spike at the throw (`PORT_TRACE_PACING`).

Measured on an M-series Mac (10 cores), cold (salted) Metal cache, `to_battle` at 1x, no launcher (Auto-play):

| | first draws | waited in total | longest wait | graphics tasks > 100 ms |
|---|---|---|---|---|
| without warm-up | 34, all built | 11.7 s | 352 ms | 21 (max 1368 ms) |
| warm-up, Auto-play | 34, 32 prebuilt | 1.7 s, all in the first second of boot (black screen) | 608 ms | 0 |
| warm-up, launcher shown 8 s | 34, all prebuilt | 0 ms | 0 ms | 0 |

All 113 programs take 8.5-13.5 s to build cold with 4-8 workers (35 s with one), ~0.1 s warm. Replays are unchanged
(`tools/run_replays.py --all`: difference 0.00 everywhere).

## Frame interpolation ("Smooth motion")

### Design review (2026-11): how it works end to end, how it failed, what it does now

Smooth motion kept breaking in new places (ceremonies "fast-forwarding", the Castle ending, sleep Zs, flames, heavy
moves). Instead of another per-scene fix, the whole path was reviewed. There were no per-scene special cases to remove:
`git log -S Port_InterpTagMtx`/`--grep` finds only the original design (a55aab1), the 4-retrace gap (d7bdac9), the
lag drop (a79c60e) and the menu/2D work (e095102, c445344, 605a4ed); no Koga or Elite Four code exists in this repository
or the libultraship fork. The generic rules below are what each scene ran into.

**The pipeline.**
1. *Recording* (game thread). Every `gSPMatrix` goes through `Port_GfxPrepareMtx`, which calls `Port_InterpTagMtx(mtx,
   call site)`; the scene graph sets the current node (`Port_InterpNode`); `Gfx_SwapDisplayListBuffer` starts a frame
   record (`Port_InterpBeginFrame`, keyed by the display-list buffer). A matrix's group is (call site, node).
2. *Matching* (window thread, once per game frame, `Plan`). The newer frame's matrices are paired with the older
   frame's, per group, and each pair is checked for continuity (`Blend`'s cuts). The frame as a whole may be a scene cut.
3. *Presentation* (`RunGameFrame` / `PumpInterpolation`). The frame's display list is drawn k times: k-1 times with
   blended matrices (t = 1/k ... (k-1)/k), the last time exactly; the first drawing happens when the task arrives (it
   is what the game "sees": render-target readbacks, the CPU-frame detection), the rest on the next refreshes.
4. *Replay passes* (libultraship interpreter: `ReplayBegin/ReplayPassStart/ReplayEnd`, `mReplayPass`). Each repeat starts
   from the RSP/RDP/TMEM/segment state the first drawing started from and writes nothing back to game memory; 2D
   rectangles are paired and placed in between (`Interp2DPrepare`, `mReplayT`); frames that draw over the previous
   picture are redrawn from a copy of it (`ReplayBegin(restorePicture)`).

**Failure classes found** (and what the redesign does about each):

| class | what happened | now |
|---|---|---|
| draw cost (the big one) | every in-between picture is a full draw. When two draws no longer fit in a game frame (Intel HD 520: ~25 ms per picture; heavier scenes such as ceremonies, Psychic's and Fire Blast's full-screen layers, 4x MSAA, high resolutions push mid-range GPUs there too), the window thread delays the game, the lag catch-up skips whole game frames, and the screen freezes and then jumps ahead. Measured (`PORT_DEBUG_DRAW_MS=25`, cup-clear ceremony, real time, 15 s): with Smooth motion 92 freezes over 50 ms and 37 jumps of more than 4 retraces; without it 1 and 0. | a picture budget (`PicturesAllowed`, `NoteOverrun`): a series that overran (a picture finishing after the next game frame was due, the first picture alone taking two refreshes, pictures dropped in 3 frames in a row) drops to one picture per game frame, exactly as with Smooth motion off, for a hold that doubles per overrun (2 s to 5 min) and halves after a calm minute; then in-between pictures are tried again |
| object identity | matrices were paired by "n-th matrix of (site, node)". Effects that recycle their slots (sparkles, sleep Zs, flames, confetti: the oldest goes, a new one comes, the list shifts) or change count paired different objects; a pair moving less than 300 units was blended, so every in-between picture slid objects half-way to a neighbour's place. Example: a 20-matrix group in the send-out sparkles shifts by 2 every frame (B[0] = A[2]); the n-th pairing blended B[0] with A[0], 100 units away. | `MatchGroup`: the n-th pairing only when every matrix's own partner is its nearest one; otherwise nearest partners, accepted only when unambiguous (nearer than half the spacing between candidates and clearly nearer than the second best), a partner taken twice by none; groups over 96 by number only when the count is unchanged; anything else drawn as built |
| scene cuts | per-matrix cuts (60 degrees, 300 units, projection scale) left the matrices that survived a cut (small objects near the camera) blending into the old scene | `Plan`: a projection cut, a third of the matched matrices cut, or most matrices new makes the whole frame a cut: no in-between movement at all |
| data the game rewrites in place | the in-between and final pictures are drawn after the game was told the frame is done (`PumpRspTasks` posts `OS_EVENT_DP` after the first drawing) and builds the next frame meanwhile; anything the list reads outside the double-buffered display-list buffer may already hold the next frame's state. The test harness (`PORT_INTERP_IMMEDIATE=1`) drew all pictures before posting, so it never saw this. | every recorded matrix is pinned to the value the first drawing used, in every picture (`Build`); `PORT_INTERP_IMMEDIATE=late` draws the pictures only after the game has built the next frame (the real-time worst case) for tests. Checked: the ceremony with `late` gives byte-identical pictures, i.e. no scene found so far rewrites vertices or textures in place |
| frames submitted again / out of order | a display list drawn again (or an older one) became the "newer" frame and blended backwards | frame records carry a build serial; only a newer frame is blended, and only a newer one becomes the reference |
| CPU-built geometry, particles under an identity matrix, texture animation | battle effects build their vertices on the CPU each frame under a shared identity modelview (`BattleAnim_*_Draw`, `MistHaze_DrawPool`); texture cycles (Moltres' wings, flames) | unchanged by design: shown at the game's rate while the camera moves smoothly around them (interpolating them would need per-effect game changes) |
| 2D | rectangles paired by texture/size/texcoords and moved in between | unchanged (`Interp2DPrepare`); its cut rules (48 pixels, new rectangles) already fall back to the exact place |

**Checked** (scripts in `tools/interp_check`: `sweep.sh`, `pacing.sh`; a machine shared with other builds, load 40-50):
- Real time, cup-clear ceremony, 15 s, `PORT_DEBUG_DRAW_MS=25`, two trials: old design 29 and 46 jumps of more than 4
  retraces (33 and 49 freezes over 50 ms); new 4 and 12 (8, 13); Smooth motion off 1 and 4 (3, 9). With 14 ms draws 5
  and 5 jumps. Without a delay the new design shows 830 pictures in 15 s (off: 448). Repeated on a quieter machine
  (`tools/interp_check/pacing.sh`): old design 90 freezes / 51 jumps, new 5 / 2, off 1 / 0; with 14 ms draws the new
  design keeps every in-between picture (897 in 15 s) with no freeze or jump.
- Old vs new pictures (`PORT_INTERP_MATCH=index`): Fire Blast's star-shaped burst collapses and the sleep Zs jump in
  the old in-between pictures (frames 1970, 1986 of a Gengar vs sleeping Snorlax battle); the sparkles in Koga's gym
  vanish (frame 146); the new ones lie between their neighbours. `PORT_INTERP_LOG=1` counts how many pairs the old
  rule got wrong: about 11 per frame in the cup-clear ceremony, 9,700 in a castle-ending run, 2,000-3,400 per battle.
- Every in-between picture of 19 scenes (the demo scenes, the castle ending, battles in arenas 2, 11 Koga and 15
  Elite Four with Psychic and Fire Blast, 8 replays) in 16:9 and 4:3 scored against its neighbours: at most 2
  pictures per scene outside the range, all of them HUD panels or icons sliding (2D blending).
- Real-time pictures equal immediate-mode pictures byte for byte (1,185 of 1,185, ceremony); Metal, OpenGL and Vulkan
  give the same in-between pictures; `late` equals immediate.
- `tools/run_replays.py`: all 20 recordings OK with Smooth motion off, and with it on (`PORT_INTERPOLATE=1
  PORT_INTERP_IMMEDIATE=1 PORT_RENDER_EVERY=1`, `PORT_REPLAY_TIMEOUT=900`) the same stop pictures (0.00, `tour_fb`
  0.08 dither).
- Psychic and Fire Blast with Smooth motion off match ares frame for frame (battle sweep cases 092-095, 126-131).
- Not reproduced literally: the reported ceremony and Castle-ending breakage (no clip of it was at hand; on this Mac
  the scenes look right in every mode). Draw cost and identity are the two mechanisms found that break exactly these
  scenes. Not checked: Windows (no vblank: the 1/60 s clock), DX11, a visible window with vsync.

**Rules** the code now follows: interpolate only what is unambiguous and continuous, show the exact frame otherwise;
decide once per game frame, not per picture; never let in-between pictures cost the game time; never let a picture
depend on game memory written after the first drawing; anything Smooth motion cannot do safely looks exactly like
Smooth motion off.

**Test tools added**: `PORT_INTERP_LOG=1` (per frame: matched, regrouped, reordered, ambiguous, misindexed = pairs the
old rule would have blended with another object, scene cuts), `=2` (each reordered group with positions), `=3` (every
picture shown with its game frame and wall-clock time: what a real-time run puts on screen); `PORT_INTERP_IMMEDIATE=late`;
`PORT_INTERP_BUDGET=0` (no budget); `PORT_BOOT=demo:<n>` / `PORT_BOOT=castle_clear` (the ceremonies and the Castle
ending without playing there: 0x11 cup clear, 0x15/0x1A completion, 0x16/0x17 first-clear bonus, 0x18 Mewtwo intro).

### Picture budget misfires (2026-11-13): menus below 60 at 2160p, an HD 520 stuck at 20

Reports after v0.0.13: on an M5 MacBook Air the menus did not reach 60 at 2160p (windowed, Integer, MSAA off, VSync,
Smooth motion, Keep full speed); on an Intel HD 520 (Windows) the title showed 20 FPS at any setting, also OpenGL at
720p with 2x MSAA in the Pokédex (v0.0.12: 60). Measured on the M5 Air (`PORT_TRACE_FRAMETIME`, below), the 2160p menus
cost 4.6 ms of GPU time per picture (1.6 at 1080p, 2.7 at Match window): two pictures fit a 30 fps game frame easily.
The budget fell back to one picture per game frame anyway, for three reasons, and its doubling hold kept it there:

- **Self-inflicted lag counted as "behind".** The window thread runs the game's RSP tasks (audio, the next frame); a
  picture of 9-10 ms delays them, so the next retrace comes a few ms late (made up at once). `PumpInterpolation` dropped
  the pending in-between picture at a clock lag over 4 ms, and `PicturesAllowed` renewed the hold at the same 4 ms, so a
  slow GPU never left one picture per frame. Both now allow one refresh (16.7 ms; `PORT_INTERP_LAG_MS`).
- **"3 drops in a row" was "3 drops ever".** The count was reset only by a series still pending when the next game frame
  came; a completed series never is (DrawNext ends it). It is now reset when a series shows all its pictures, and a
  drop counts only when the dropped picture was already due (a game frame arriving before an in-between's refresh,
  as in the start-up logos, is the game's pace, not a lack of time) or game time is a refresh behind.
- **One late picture was an overrun.** A scene load, a shader build or a busy host made one picture late and put the
  game on 30 for 2 s, doubling the next hold. A late picture (ended after the next game frame was due; first picture
  over two refreshes) now counts only when another came within the last second (`NoteLatePicture`); a GPU that is
  really too slow is late on every frame.

`PORT_INTERP_OLD_BUDGET=1` restores the old rules for comparisons. `PORT_INTERP_LOG=1` also logs each dropped picture
and why, and late pictures not counted. Results (M5 Air, Metal, a busy machine; scripts and logs in
`~/Projects/Pokemon/scratch/menu-4k-perf`):

| 2160p menus (tour_play replay, real time, 4 interleaved runs each) | pictures/s, median | budget holds |
|---|---|---|
| old rules / new rules | 55.5 / 59.7 | 8 / 0 |
| `PORT_DEBUG_DRAW_MS=9` (a picture as costly as on an M1/M2), old / new | 55.7 / 59.8 | 8 / 0 |

`tools/interp_check/pacing.sh` (cup-clear ceremony, 2 trials): at 14 ms per picture 894-897 pictures in 15 s (all of
them) and no freezes, against 697-742 with the old rules; at 25 ms (HD 520) 421-440 pictures with 7-8 freezes and 5-7
jumps, close to Smooth motion off (440-444, 1-6 freezes) and the old rules (427-448, 6-8 freezes, 1-3 jumps), far from
no budget at all (76-100 freezes). Keep full speed holds game time when drawing cannot keep up even at one picture per
frame (`PORT_DEBUG_DRAW_MS=40`: ~1800 retraces in 30 s with it, ~1200 without).

Not the cause, measured: the Metal backend's empty render pass after each frame start (StartDraw then Clear) and the
depth buffer stored at the end of every picture. Lazy encoders and a depth store decided at the end of the pass gave
3.52 against 3.55 ms per picture at 2160p with the GPU saturated (Apple's driver skips the empty pass), so they were
left out. On the HD 520, 8x MSAA is a real cost: every Vulkan/Metal pass loads and stores the multisampled attachments
and resolves at its end (~120 MB per pass at 720p 8x).

`PORT_TRACE_FRAMETIME=1` (libultraship Fast3dWindow): every 2 s, pictures per second and per picture the wait for a
drawable, the interpreter, the GUI, submit (average/p95 ms) and the GPU's busy time (Metal command buffers'
GPUStartTime..GPUEndTime); `=2` also per command buffer label (FrameBuffer 0 is the window, the game's frame buffers
follow). GPU times are inflated at low load (the GPU clocks down): compare within one run or saturate it.

The game draws a new frame every 2nd or 3rd VI retrace (30 or 20 fps; menus mostly 30). With interpolation on, 3D
scenes are shown at the display's refresh rate: between two game frames the renderer draws the newer frame's display
list again with matrices blended between the two frames, once per refresh. Off by default; the launcher's "Smooth
motion" (Graphics panel, `gPort.Interpolate`) or `PORT_INTERPOLATE=1/0` (overrides the setting). Normal speed only.

**Menus and other screens that draw over the previous picture** (most menus: the N64 frame buffer keeps its picture
and they repaint only what changes) used to be shown at the game's rate. They now get in-between frames too: before the
frame's first drawing the renderer copies each game frame buffer it draws into (`CopyFramebufferInStream`, ordered in
that framebuffer's own command stream on Metal/Vulkan), and copies it back before each repeat, so every picture starts
from the same one and the last, exact drawing leaves what the N64 would (`ReplayBegin(restorePicture)`; when a newer
frame arrives early, the exact drawing is done first, `FinishPending`). With MSAA the copy has the game buffer's MSAA
level and both images are copied (Metal/Vulkan: the multisampled attachment and its resolve target; DX11: the
multisampled texture as it is; OpenGL: a same-size multisample blit). `PORT_INTERP_INCREMENTAL=0` turns it off. 2D also moves in between: `Interp2DPrepare` pairs the frame's textured and fill rectangles with the last
exact frame's by texture, size and texture coordinates (or fill colour), same place first, then the most common
moves (a panel's background, border and text move alike), and in-between drawings place each one between its old and
new place (cursors, panels, text boxes; `PORT_INTERP_2D=0` off). Not blended: moves over 48 pixels, new rectangles,
and anything touching a new rectangle (a panel whose edge is still entering from off screen moves at the game's rate
rather than tearing). Measured on `to_battle` (menus to the battle): of the in-between frames whose neighbours differ,
796 show a blended picture and 355 the new one; `tour_galmenu2`'s stop picture with interpolation on equals the
reference (difference 0.001). With 2x/4x MSAA (Metal, Vulkan, OpenGL) the same route gets the same in-between frames
as without (593 incremental frames blended); DX11 compiled only on Windows (untested here). Known: a one-refresh misplacement when a unique piece reappears near its old place at a
screen change (the rental screen's tab bar, frame 856).

**What libultraship has and what Ship of Harkinian adds.** libultraship (this version) has the renderer hooks only:
`Interpreter::Run(commands, mtx_replacements)` / `Fast3dWindow::DrawAndRunGraphicsCommands` take a map from `Mtx*` to
the `MtxF` to use instead (looked up in `GfxSpMatrix`), plus `G_MW_SEGMENT_INTERP` / `mInterpolationIndex` (segment
swaps for a given in-between frame) and `G_SETTILESIZE_INTERP` / `mInterpolationT` (tile scrolling blended at draw
time). Recording matrices with ids and building the map is game code: SoH keeps it in its own repository
(`soh/frame_interpolation.cpp`), which records every `Matrix_*` operation in a tree keyed by the actor and
`OPEN_DISPS` scope and replays both frames' operations with blended parameters. Stadium's scene graph (`geo_render.c`)
builds each matrix as a product on its own `MtxF` stack, and battle effects, menus and minigames emit matrices in many
other places (107 `gSPMatrix` sites), so the port blends the final matrices instead (`src/port/interp.cpp`):

- **Ids.** Every `gSPMatrix` in game code already goes through `Port_GfxPrepareMtx` (`src/port/sdk/ultra64.h`,
  `gfx_translate.cpp`); it now also records the matrix with an id: the call site (`__builtin_return_address`), the
  scene-graph node being drawn (`Port_InterpNode`, set in `Geo_ProcessNodeChildren`, `Geo_NodeReference`,
  `Geo_NodeSwitchCase`); matrices of one site/node pair form a group, paired with the previous frame's by identity
  (`MatchGroup`, see the design review above), no longer by their number in the group. Frames are delimited
  by `Gfx_SwapDisplayListBuffer` (`Port_InterpBeginFrame`, keyed by the display-list buffer, which is what the graphics
  task points at). Matrices from ROM data (translated, the same every frame) are not recorded. The game-side changes are
  these four calls (game repository, `geo_render.c`, `gfx_buffer.c`).
- **Blending** (`Blend`): rows 0-2 of an affine matrix (the axes) are blended as direction (normalised lerp) and length
  separately, so a turning part keeps its size; row 3 (translation) linearly; projections element-wise. Not blended
  (shown as the game drew it): a matrix that turned more than 60 degrees in one game frame, moved more than 300 units
  and more than half its distance from the origin (a camera cut, a reused node), a projection whose scale changed by
  half, a matrix with no unambiguous partner in the previous frame, and every matrix of a frame that is a scene cut.
  Render targets (UI text, portraits) are always
  drawn with the game's own matrices, so their pixels, which are read back into game memory, never depend on it.
- **Presentation** (window thread, `Port::RunGameFrame` / `Port::PumpInterpolation`). A graphics task is drawn when
  it arrives, as before, but as the first of k pictures, k = retraces since the previous game frame x refreshes per
  retrace (2 at 60 Hz for a 30 fps scene, 4 at 120 Hz), with t = 1/k; the others follow 1 ms after each later vblank
  (from the display clock, `Port_DisplayVblank`), the last one exactly as built. The picture is therefore one game
  frame later than without (16.7 ms at 30 fps / 60 Hz), as in SoH. A frame more than 4 retraces after the previous
  one (a hitch, or a fast run drawing every n-th frame) is not blended: over such a gap many of a model's matrices
  turn past the 60-degree cut while the ones skinned to them still blend, and limbs stretch across the screen
  (Clefairy in the title's fall, blended from 20 retraces back). A new graphics task, a CPU-drawn frame, a frame
  drawn for its state only, or a change of the game's segment/fragment maps (`Port_GfxFlushCache`: the scene unloads
  data the list uses) ends the series. libultraship's frame limiter follows the display's rate while interpolating.
- **Only lists that repaint the screen** are drawn again (`Interpreter::FrameRepaintsScreen`: a full-screen fill
  rectangle in fill mode into a frame buffer). Menus that draw over the previous picture would pile up; they have
  (almost) no matrices anyway. In the recordings every frame with recorded matrices repaints the screen.
- **The game is not touched.** The display list stays valid: the game builds the next frame in its other buffer and
  waits for that frame's task before reusing this one. In-between passes (`Interpreter::mReplayPass`) write nothing
  to game memory (no render-target readback), leave the CPU-drawn-frame detection's draw stamps alone, and start from
  the RSP/RDP state (segments, TMEM model, tiles, modes) the frame first started from; afterwards the state the first
  drawing left is put back (`ReplayBegin` / `ReplayPassStart` / `ReplayEnd`), so the next frame renders as without
  interpolation. Lockstep timing is unchanged (the passes run on the window thread between retraces). Every recorded
  matrix is pinned to its first-drawing value in the later passes, and the picture budget keeps the passes from
  delaying the game (design review above).

Pitfalls found on the way: Metal reads the screen back one frame late (the blit is encoded in the next `EndFrame`); an
in-between frame consumed or expired that pending readback, and screenshots and the freeze detector then saw a stale
picture (looked like a desync: white stop pictures, "picture unchanged" freezes). In-between frames now leave it for the
next real frame (`gPortReplayFrame`), unless the port reads them back itself. Skipping the render-target draws in the
in-between passes lost the portraits (the target is set up again in each pass), so they are drawn too.

**Measured** (MacBook built-in display, 60 Hz, `boot_battle_move` with `PORT_BOOT=battle`, battle frames 1200-1800,
`PORT_TRACE_PACING=1`, window visible): without interpolation 300 frames shown in 10 s, every one for 33.3 ms; with it
599, each 16.7 ms (1 refresh: 99.8%, 0% off the refresh grid); retraces unchanged (600, p99 16.7 ms). No 120 Hz
display was at hand: k would be 4 there.

**Checked:**
- `tools/run_replays.py` with interpolation off: difference 0.00 for all 20 recordings (the two GB Tower recordings
  need a cartridge dump and were skipped).
- With it on (`PORT_INTERPOLATE=1 PORT_INTERP_IMMEDIATE=1 PORT_RENDER_EVERY=1 PORT_SCREENSHOT_NOSTOP=1`): the same
  stop pictures (0.00; `tour_fb` 0.01 and `tour_mg_r1c2` 0.05, as when every frame is drawn without interpolation: the
  dither noise follows the number of frames drawn), i.e. the game frames are the same.
- In-between pictures of the whole battle (frames 900-2400, 750 in-between frames, compared with the
  neighbouring real frames): the in-between frame lies between its neighbours (median of min(prev-mid, mid-cur) /
  prev-cur = 0.56); at the 5 large picture changes (camera cuts, flashes) it equals the new frame (no blend across a
  cut). Camera pans, the Pokemon's moves and the team-select / VS screens' 3D models move in between; HUD boxes and
  text stay put.
- Coverage (`PORT_INTERP_LOG=1`, all recordings): every game frame with recorded matrices repaints the screen and
  gets in-between frames (e.g. `boot_battle_move` 1810 of 1906 frames, the minigame tours ~1920 of ~4000, of which
  ~1870 blend at least one matrix); frames without 3D (menus, cup grid, text screens) are shown as before.

**Not interpolated / known limits** (all shown at the game's rate, as before): particles that come and go in groups
too dense to tell apart (a burst from one point: ambiguous partners), anything turning faster than 60 degrees per game frame,
vertex-animated or CPU-built geometry (fade quads, 2D sprites, text), texture scrolling (`G_SETTILESIZE_INTERP` would
need game-side changes per effect), and menus that draw over the previous picture. Data a list reads that the game
changes in place while building the next frame (single-buffered vertex arrays or textures, none found so far;
matrices are pinned) would show the newer state in the in-between frame. Only macOS reports vblanks; on Linux/Windows the in-between frames follow a 1/60 s clock after
the game frame (so only up to 60 fps). DX11/OpenGL: the replay passes use the same interpreter path, not measured.

Debug switches: `PORT_INTERP_LOG=1` (per game frame: matrices recorded / matched / blended, cuts and why;
`=2` lists jumps), `PORT_INTERP_IMMEDIATE=1` (draw the in-between frames right after the frame: hidden-window and
`PORT_SPEED=max` runs, with `PORT_RENDER_EVERY=1`), `PORT_INTERP_SHOTS=<first>-<last>` (every picture of those game
frames as `interp_<frame>_<i>of<k>.ppm` in `PORT_SCREENSHOT_DIR`; `PORT_SHOT_SCALE=4` saves them at 1/4 size),
`PORT_INTERP_JUMP=<units>` (the move threshold), `PORT_LAUNCHER_SHOT=<file.ppm>` (saves the launcher's picture and
closes it).

## Battle boot: straight into a battle from a case file (tools/battle_boot.py)
The port can start the battle engine directly with a given setup, with no menus and no replay, for 1:1 comparisons with
other renderers (and quick battle experiments). A run to the end of two turns takes about 25 s, most of it the battle
itself; the old route (the menu replay of tools/battle_xcheck.py) first spends ~2300 game frames in the menus.

    tools/battle_boot.py --out <dir> --p1 25,4,7 --p2 113 --known2 1,34 --arena 2 --moves 85,85 --seed 1 \
        --turns 2 --frames-every 2 --res 640x480 --trace --camlog --partlog [--twice]
    pokestadium <rom> --battle <case file> [--frames <dir>] [--every <n>] [--res <WxH>] [--max-frames <n>]   # or PORT_BATTLE=<file>

`--twice` repeats the run and compares trace, camera log, particle log and every frame byte for byte. Run it hidden
(`PORT_HIDDEN_WINDOW=1 PORT_SPEED=max PORT_INTERPOLATE=0`, which the script sets) and with an own `SHIP_HOME` (the
window size is part of the settings).

The case file is the xcheck case format (src/port/xcheck.cpp: `mon`, `rule`, `stop`, `nomusic`) plus:

    arena <id>               stadium_models file: 0 Free Battle, 1-3 Cups 97-99, 4 Petit, 5 Pika, 6 Prime, 7-16 Castle, 17 Mewtwo
    seed <n>                 the random seed (PORT_XCHECK_RNG; default 1: a battle boot is always seeded)
    intro <0|1>              0: the send-out intro runs undrawn and frame dumping starts after it
    think <n>                game frames a scripted side's menu is up before its choice (default 20)
    mode <m> [<rule>]        session mode / rule category (default 9 6 = Battle Now!)
    trainer <side> <NAME>
    act <side> m0 m1 s2 ...  scripted decisions in order: m<slot> a move slot, s<slot> a switch to that party slot
    mon <side> <slot> species=<dex> level=50 nick=PIKA hp=.. maxhp=.. status=.. m1..m4=<move> p1..p4=<PP> atk=.. def=.. spd=.. spc=..
    dump <dir> / dump_every <n> / dump_res <WxH> / frames <n>    (or the PORT_BATTLE_* variables)

A side with `act` lines is a player: it has a controller, its menus are drawn, and the scripted choice is made after
`think` frames the way the move time limit makes one (BattleScene_AutoSelectAvailableMove / ..ReplacementMon). A side
without is the computer. Decisions are consumed one per move decision; after a faint the replacement takes the next
entry only if it is a switch (s<slot>), else the first healthy Pokemon. An unusable choice (no PP, fainted target) falls
back to the first usable move. A party slot exists through a `mon` line that names it with species=; `*` lines apply to
all slots. Species are Pokedex numbers, moves the Gen 1 ids of the move table (85 Thunderbolt, 34 Body Slam).

What it sets (all in game/src/game_state.c `State_PortBattle`, state 0x90, entered from `Game_Thread` after the same
initialisation as PORT_BOOT): `Session_SetMode`, then per side `Trainer_Create` + `Pokemon_CreateDefault` (species, level,
stats) + the case's moves and fields (`Pokemon_PrepareBattleMon` derives PP and HP, then the explicit fields win) +
`Trainer_AddPokemon` + `Team_AddTrainer`, as `Team_BuildQuickBattleTeams` does; no battle prep, no order select. In the
engine: the arena index in `BattleScene_Init`, the computer's roster in `Battle_SelectComRoster` (the AI is initialised by
its pick as always, but the party is kept whole and in order), and the scripted decisions (`BattleBoot_Decide`, battle_engine_2F74E0.c).
The run ends when the battle engine returns, after `stop <turns>` turns, or after `frames <n>` dumped frames.

Outputs: `PORT_XCHECK=<file>` (the xcheck trace: events, random values, with `B`/`S` lines for start and end),
`PORT_XCHECK_ANIM=<dir>` (160x120 frames per move animation), `PORT_CAMLOG=<file>` (one line per camera drawn: game frame,
frame since the animation start, VI tick, viewport, eye, target, up, fov, near; game/src/geo_render.c) and
`PORT_PARTLOG=<file>` (one line per drawn particle; particles are numbered in order of first drawing, particle_math_269820.c),
and the whole-battle frame dump (`frames/f_<n>.ppm`, n = battle game frame since the start, one per `dump_every`, as drawn
or box-scaled to `dump_res`; the window is set to that size first, so the picture is rendered at it where the display allows).
The logs and frames are byte-identical between runs.

Limits: only the Stadium-style battle (1P vs computer or scripted) is booted: no rules screens, no Poke Cup rounds,
no result screens, 2P menus untested; a battle that needs a trainer from the ROM tables is not built (parties are all from
the case). Arenas 0, 1, 2, 4, 5, 7, 16 and 17 were run and render; the Castle arenas show their stage only (no gym leader or
round scenes) and Vs. Mewtwo no Mewtwo scene.
The send-out intro cannot be skipped, only left undrawn. Music/announcer run unless `nomusic` (audio is not part of the
determinism check).

## Battle cross-check against ares (tools/battle_xcheck.py)
The same battle runs in the port and in the original game in ares, with the same random numbers, and is compared
event by event; then the move animations are compared frame by frame.
    tools/ares_ref/build.sh build-ares               # ares + ares_ref with tools/ares_ref/ares-host-hooks.patch
    tools/battle_xcheck.py run <dir> --seed 7 --port-bin build-xcheck/pokestadium [--anim]
    tools/battle_xcheck.py compare <dir>             # <dir>/report.md
    tools/battle_xcheck.py anim <dir>                # <dir>/anim.md + <dir>/anim/*.png (deletes the raw frames)
How the two sides play the same battle:
- Setup: one input script for both (replays/to_battle.rec up to the Poké Cup screen, then the team picks at frame 2200+,
  late enough for ares, whose loads take a few hundred frames longer) and the same save (replays/default.flash).
- Randomness: Battle_Random, Battle_RandomAccuracy and BattleAI_RandomBelowInclusive step their generators by
  osGetCount, so CPU timing decides the battle; BattleAnim_RandomRange/IntWithWarmup steer cameras and effects.
  With PORT_XCHECK_RNG=<seed> / ARES_REF_XCHECK_RNG=<seed> both sides return the same seeded stream instead
  (splitmix64 of seed, stream, call index; optional queue file of forced values first). The animation streams are kept
  per argument: the 1-in-50 idle animation check is polled every frame, and one frame of timing difference must not
  shift the effects' draws. BattleAI_SeedRandomState is skipped.
- ares hooks: ares-host-hooks.patch adds a sorted list of guest addresses to the CPU, checked when a block is dispatched
  (recompiled blocks end at hooked addresses like at breakpoints; the JIT does not chain blocks), with a callback
  that may return from the function. The battle engine is fragment 51 (link address 0x84300000, relocated): the
  driver hooks Memmap_SetFragmentMap (main code) and places the battle hooks at gFragments[51] + (link - 0x84300000)
  as soon as the fragment is mapped, before its init runs (the computer picks its team order there). Addresses come
  from the decomp's build/pokestadium-us.map and symbol_addrs.txt.
- Reading RDRAM: the VR4300 data cache is emulated, and the newest game writes sit in dirty lines; the driver reads
  through the cache (without that every event showed stale fields, e.g. the chosen move still 0 at turn start).
- Input in battle: none. PORT_XCHECK_COM / ARES_REF_XCHECK_COM make BattleScene_IsActionSelectionTimerExpired answer
  "not yet" then "expired" for each decision, so the game's own time-limit fallback picks the player's first usable move
  and first healthy replacement (answering "expired" at once skipped the party menu that sets the action to "switch":
  both games stalled after the first faint). Making side 0 a computer player instead does not work: the AI only keeps
  state for its own side; its switch picked garbage slots (port crash in Battle_InitPokemonRuntimeState, N64 stuck).
- Events: Battle_StartTurn ("turn"), Battle_ResolveMonAction entry ("resolve") and return ("resolved"; ares hooks the
  caller's return address). Each records both ActivePokemonState (0..0xC8: header, the BattleMonRuntime with HP,
  status, moves, PP, stat stages, used move), the BattleScene up to its camera pointer, sBattleDamage and the message
  queues as raw bytes; the script decodes them with each side's byte order (the layouts are pointer-free and the
  same) and compares per event, reporting the first difference with the previous event and the random calls before it.
  Fields that follow frame timing (ActivePokemonState.unk_36: the model's idle-animation state) and the camera floats are
  reported apart from the battle logic, with a table of both sides' frame for every event.
Results (seeds 1, 7, 3; Poké Cup round 1, Charmander/Venusaur/Ivysaur vs the Biker's team; 30000 frames each):
- Battle logic identical on all three: every event's state (105, 71 and 24 events: long battles with faints,
  switch-ins, Leech Seed drain, poison, misses and critical hits), every forced random call (160/29/198 battle-logic
  calls in seed 1: same functions, arguments, values and order) and the same move animations in the same order.
  A negative control (port seed 1 vs ares seed 2) is caught at event 0 (other lead, stats, moves, message text) and
  in the random-call order at entry 3. Reruns of a seed give the same traces.
- Timing (presentation, not logic): the port runs its own clock and reaches each event earlier. Within a turn the gap
  grows ~2 frames; each faint/switch-in adds ~20-30 frames (the N64 loads the new Pokémon); a Leech Seed turn adds
  76-97 frames and a Razor Leaf turn ~97 (seed 1 events 23 and 48, seed 7 events 21 and 54): those presentations are
  much shorter in the port, past the first 200 frames of the animation (below). The idle-animation state and the
  camera-shake angle differ at a few events because of that drift.
Animations (milestone 2, seed 7 with --anim, 18 move animations: Flamethrower x4, Karate Chop, Dig, Leech Seed x6,
Thrash x3, Body Slam x2, Blizzard):
- Each animation is captured for 200 game frames from BattleScene_EnterRow3StartMoveAnim / Row19 on both sides, at
  320x240 (the port: PORT_RENDER_EVERY=1 in a hidden window, frames named by game frame; the game draws every second
  VI frame, gaps repeat the previous picture). All 18 align at the same offset, +4 frames (the port's capture latency),
  so no animation starts or runs at a different speed within those 200 frames.
- SSIM (luma, 128x96) is 0.73-0.83 for 17 of them: the sheets show the same poses, effects and camera frame for frame;
  the remainder is fine detail (crowd texture, model edges at the port's higher render resolution, hit-spark colours).
  That range is the noise floor of this metric. Blizzard is the outlier (0.53, minimum 0.26 in its radial-streak
  phase): candidate for a closer look (the streak texture may come from the unforced main-code MathUtil_Random16).
- The longer N64 turns (above) are outside these 200 frames: the next step is capturing until the next event.
- Effect frame counter: sParticleFrameCounter (particle_math, sParticleFrameMod8 and the rest) counts battle game
  frames and picks effect texture frames, e.g. Thundershock's eight bolt frames (asset 0x13 + Mod8 * 0x600). The N64
  can spend a game frame more before an animation than the port (the move's effect assets load by DMA: Dragon Rage
  loads two resources, then starts at gf 480 in ares vs 479 in the port), and every later animation then ran one texture
  frame out of phase: Thundershock after Dragon Rage showed other bolt shapes (effect_p90 169 vs ~24 alone or after
  String Shot). Load timing, not a port bug: with PORT_XCHECK_RNG / ARES_REF_XCHECK_RNG both sides set the counter to 0
  at each animation start, as they reseed guRandom; the A trace lines carry its value before that (pfc=).
- Model texture counter: D_800AF7AE (battle_hud, 0x800AF7AE) goes up once for every Pokémon model drawn
  (ModelAnim_FinalizeDisplayObjectAnimation, so +2 per battle frame with both on screen) and is only cleared when a
  model is set up. Animated model parts take their texture frame (& 7) and texture scroll from it
  (DisplayList_InitAnimatedFrameSegment / func_81002830): Rapidash's flame mane, Moltres's flaming wings. The same load
  timing put it out of phase: in the full sweep of 34f5e46, Mega Kick 25 / Sand-Attack 28 / Headbutt 29 (Rapidash, after
  ares spent one frame more before Mega Kick: mtc 524 vs 526) and Fire Spin 83 (ares starts 2 frames later, mtc 586 vs
  590). The xcheck path now zeroes it at animation start on both sides, too (trace field mtc=). Mega Kick 60 -> 37,
  Headbutt 56 -> 37, Fire Spin 91 -> 28 (floor ~36-40); Stomp, first in its battle, was 37 before and after.
  Sand-Attack stays at 66: that is its sand cloud (smooth in the port, grainy in ares), not the mane.
  D_8006F084 (geo_render, 0x8006F084, u16: GeoRender_AdvanceFrameCounter, one per game frame since boot) is zeroed
  as well (gfc=): effects pick frames with D_8006F084 & 7, and the two sides' menus leave it ~200 apart at the first
  animation. No swept move changed with it, though.
  Not this: Psybeam 60 (62) and Sing 47 (49) stay as they were. Their battles have no game-frame drift (same gf
  at every animation, and every counter above equal), so the colour-cycle/scroll difference there has another cause.
  Sing (fixed): its overlay is Trail40Variant (battle_engine_34F1A0.c), a rippling plane in front of the effect camera
  drawn with G_TEXTURE_GEN and the 32x32 texture asset 0x48, which wraps ~16 times over the normal range, so a texel of
  texgen error recolours the screen; in the port it flickered between two looks on alternate frames. The game sends the
  same commands on both kinds of frame, so it was the texgen result: the RSP keeps the transformed lookat directions as
  signed bytes, libultraship kept floats, and lookat X's transformed y component (the difference of two nearly equal
  terms) ran a sawtooth (-0.0059, -0.0002, -0.0032, ...). The libultraship fork (Interpreter::GfxSpVertex) now
  floors current_lookat_coeffs to 1/128 steps (clamped at 127/128) after CalculateNormalDir. Floor (an arithmetic
  shift) gives Sing 49 -> 33-37; round to nearest 56.5, truncation toward zero 60.6. The other texgen effects (Barrier,
  Light Screen, Reflect) stay at ~34; the light directions (diffuse shading) stay float. Psybeam's remaining
  difference was the glow at the user's mouth, fixed with the FPU rounding mode (below).
- Effect asset loads: the move presentation (battle_engine_309ED0.c, step 1) retries BattleAnim_TryRegisterAssetTables
  each game frame until the move's effect assets are in; instant in the port, 0-2 game frames on the N64 (Fire Spin:
  animation at gf 464 in ares, 462 in the port). Whatever runs on meanwhile then enters the animation that much further
  along: Moltres' flame mesh (object 0x92: a lagged soft-body vertex simulation, 33FE0.c) and its flame particles
  (Particle31, spawned 1 in 60 per frame) made Fire Spin's user look different for its whole length (effect_p90 91).
  battle_sweep.py now compares the A lines' gf after a run and, where ares was later, runs the port again with
  PORT_XCHECK_LOAD_WAIT=<n0>,<n1>,... (before animation k the port reports "still loading" n_k more frames); result.json
  records it as load_wait. Not a port bug: the N64's DMA time. (The model texture counter reset below fixes Fire
  Spin's wing texture on its own; the wait also lines up everything else that runs through the load, like the mesh,
  and needs no per-state reset.)
- Strands (HyperBeamIceBeamTrail_UpdateSlot: Hyper Beam, Ice Beam): a real port bug. The function reads a local
  (spA8, the wave phase of each strand node) that it never writes; on the N64 it is whatever an earlier call left in
  that stack slot (addresses like 0x801B31F0, 159, 143, ... from some loop, 0 on seed frames), and clang folded the
  undefined value so every sinf() term was sinf(0): the strands left the mouth without their wave. The port sets it
  to 0 (any fixed value looks the same: it only shifts the random r % n phase); with forced randomness the ares driver
  writes 0 into that slot at the function's entry (sp-8), so both sides draw the same strands. clang's
  -Wuninitialized finds no other such read in battle_engine or particle_*. Hyper Beam 135 -> 38; Ice Beam 128 -> 68
  in the same battle (moves 55-58, Blastoise), 45 alone (Articuno).
- FPU rounding mode (Ice Beam's mouth flare, frames ~56-64 and ~84-99): a real port bug. Every game thread starts with
  __osSetFpcCsr(0x01000C01): round toward zero (and flush denormal results, trap invalid / divide by zero). The port's
  __osSetFpcCsr was a stub, so the game's float math rounded to nearest and inexact results came out 1 ULP higher
  half the time. The flare is start effect 47's descriptor 0x53 sprite (BattleAnim_CallbackSharedDescriptor53Scale-
  PulseCycle: IA8 starburst, frame 1, scale pulsing between 0.2 and 0.7 x sParticleRenderContext.y in steps of 0.1 x y).
  With truncation the N64's scale ended 1 ULP short of its (also truncated) target, so ParticleMath_ApproachFloat
  reported "reached" one frame later at the top of each swing: a 12 game frame period on the N64, 11 in the port,
  and by frame 84 the pulse was 2 frames out of phase (big starburst in the port where ares showed a small glow).
  The port now sets the host rounding mode from the CSR's RM bits (fesetround; per host thread, and each OSThread is
  one, so the renderer keeps round to nearest). Ice Beam 68.2 -> 30.2 in the 4-move battle, 44.6 -> 31.7 alone;
  controls unchanged (Tackle 36.5, Ember 36.8, Thundershock 39.3, Razor Leaf 30.1, Hyper Beam 36.6, Fire Spin 28.4).
  (Start effect 101, suspected first, is not part of Ice Beam: its effects are start 102 + 47, end 67, start 125.)
  How it was found: switching parts of start effect 47 off in the port isolated the sprite; scanning ares' RDRAM for
  particles with its update callback showed the 1-ULP-lower scale values (3F2E147A vs the port's 3F2E147B for
  0.4 x 1.7) and the extra frame at each maximum.
- ares_ref probe for such cases: ARES_REF_XCHECK_PROBE=<link>:<offset>,... logs "P <gf> <link> <value>" at those
  battle engine function entries: the word at sp+offset, or register offset-1000 (e.g. 1005 for a1). Not on an
  address that is already hooked. Arguments the caller sets in the jal/jalr delay slot read as stale there (a
  particle callback's a0 read 0).
- RDP noise (port bug, fixed): the colour combiner's NOISE input came out in [0.5, 1) instead of [0, 1). Every backend's
  RAND_NOISE (libultraship fork: gfx_metal_shader.cpp, gfx_opengl.cpp (Vulkan uses its formulas),
  gfx_direct3d11.cpp) mapped random() as if it were in [-1, 1] with (r + 1) / 2, but the shaders' random() is
  fract(...), already in [0, 1). Sand-Attack's sand cloud (ParticleGfx_SetPrimEnvNoiseCombine: cycle 1 NOISE, cycle 2
  lerps from ENVIRONMENT to PRIMITIVE by it) was therefore smooth and pulled toward the bright primitive colour; ares
  shows it grainy and darker. Sand-Attack 66 -> 47 (two independent per-pixel noise fields can't match pixel for pixel,
  so noise effects stay a little above the floor), Poison Powder 77 41 -> 32, Sleep Powder 79 43 -> 30; the other
  moves of those battles unchanged (~27-37). tools/run_replays.py: all 20 identical (none of their stop frames uses noise).
- Sound mismatches (8 moves in the full sweep of 34f5e46, 9 in f4f43b7, envelope correlation 0.22-0.57 with level and
  spectrum within ~4 dB) were not wrong move sounds: the periodic "background" sound was the announcer. Its lines
  (Audio_QueueSequenceId, queued by ProcessAudioCommandQueue & co. from the battle events) are chosen with osGetTime() % n
  (3D140_2.c: AudioFrameUpdate, ProcessAudioInfo, RemovePlayer, PlayerDefeated, Audio_SelectSoundAvoidingRecent's
  shuffle), so the two games said different lines on the same game frames (0x5b vs 0x5d, 0x258 vs 0x28e, ...). And
  a line starts when the previous one has played out, in sound time, so the N64's slowdown moves it in game frames
  (Leech Seed's turn: the line at gf 1761 in the port, 1756 in ares). Now, in the cross-check only:
  - with "nomusic" the announcer is silent too (Audio_QueueSequenceId returns; ares: a hook that returns), as the
    battle music: the sweep compares the move's sounds alone;
  - with the forced randomness, osGetTime() in those functions and in Audio_PlayCategory12SoundCommand (a pitch
    variation) returns the stream value of the call's index (from the battle's first game frame and each animation
    start), on both sides (Port_XcheckOsGetTime; ares: an osGetTime hook for those callers' address ranges), so the
    chosen lines still match and can be compared in the trace.
  Full sweep after that: sound match 155, mismatch 1, no sound 8 (before: 147 / 9 / 8); the animations unchanged (all
  164 match, median 35.3, no move's effect_p90 moved by more than 3).
  Left: Razor Leaf 75 (envelope 0.45). Its sound is effect 0 of the move's bank, started on the animation's first
  game frame on both sides and timed by the sound player; the first 2.4 s agree (0.80 at a 40 ms offset). From game
  frame ~68 on the N64 takes 3 VI frames per game frame (581 vs 450 VI frames in all) and the rest no longer lines up
  in time; mapping ares' sound onto game frames raised that part to ~0.55 but not the whole, so it is not used. A
  limit of the comparison while the port does not reproduce the N64's slowdown, not a sound bug.
  Trace aids (both sides): `PORT_XCHECK_SFX=1` / `ARES_REF_XCHECK_SFX=1` write "U <game frame> <kind> <args>" for
  Audio_PlayCommand (0), Audio_PlaySoundEffectById (1), Audio_QueueSequenceId (2, the announcer) and
  Audio_PlaySoundEffect (3, every sound effect started: id, priority, volume); "V <game frame> <VI frame>" lines
  (always) show where each battle game frame starts, e.g. where the N64 lags; `XCHECK_KEEP_FRAMES=1` keeps the sound
  dumps (port.pcm, ares.pcm) as well as the frames. Checked on the way and not the cause: libnumus'
  mus_random_seed (Audio_RandomScale, the effects' random detune/volume/pan) is never seeded in the ROM either (0
  on both sides, 0x800FC7E4), and silencing the crowd (Audio_PlayCommand 0x15) changed nothing.
- Dream Eater's sparkles (effect_p90 130 -> 23.7): the nine star models (effect 95, models 0x9A/0x9B) play their
  animation backwards from the last frame, and BattleAnim_BindModelAnimReversed read that frame as
  `data->endFrame` straight from the ROM's big-endian animation header: the port started them thousands of frames
  past the end. Code outside model_animation.c reads it through ModelAnim_GetEndFrame now, and event tracks through
  ModelAnim_GetEventEndFrame (model_animation_events.c). The same raw reads were in Drowzee (Snore), Dynamo, the
  minigame actor framework (Ekans' Digletts started their animation past its end and stayed in their holes) and two
  battle spots (Battle_ResetEventTrack's event track, the multi-hit HP transition); all read 256x the real value.
  The two battle spots were not reached by the sweep or the replays.
- Full sweep of f4f43b7 (after the fixes above): all 164 uses match (median effect_p90 35.4, highest Sand-Attack 46),
  9 sound mismatches (the announcer, above; Focus Energy 116 at envelope 0.22); 1411 s wall time with
  `--jobs 4` on a 10-core M-series Mac (8 busy processes; more jobs only add contention). While working on a fix,
  `battle_sweep.py run --open` reruns just the cases whose cached result is not a match (the report's verdicts and
  sound): 6 cases in 123 s here. A full run before merging is still the check that nothing that matched regressed.
- Replay references for a branch must come from a build of its base commit. `origin/main` moved during this work
  (dfe6826 fixed the fog colour of Geo_NodeFog / GeoRender_SetRenderMode, which changes the gallery photo's sky), and
  references built from the newer main made tour_galenl3 (1.77) and tour_galmenu2 (0.02) look like an effect of this
  change. Against 34f5e46 both are identical.

### Milestone 3 (proposal): every move
- Moves: all 165 (1..0xA5). The player's time-limit fallback always uses the first usable move, so the move under
  test goes into slot 1 of the player's lead (BattleMon in the TeamRoster before the battle engine copies it, and
  BattleMonRuntime.moves[0]); slots 2-4 hold three more test moves with PP 1 each, so one battle uses four moves on
  consecutive turns (slot 1 runs out of PP, the fallback moves on). The opponent gets a sturdy lead (level 100,
  high HP) knowing only Splash, so its own animation stays out of the way; a second opponent covers OHKO/faint cases.
  Written on both sides at the same point: in ares at the Memmap_SetFragmentMap hook (RDRAM through the data cache;
  the driver needs a write that also updates a cached line), in the port in the same hook as a PORT_XCHECK_PARTY file.
- User species matter for the animation (models, anchor points): one learner per move from the rental list first,
  then a sample of other species for the moves whose animation follows the user's model.
- Runtime: ares runs a battle at ~375 VI frames/s, the port headless faster; from boot to the first turn ~3000 frames
  (8 s; an ares save state at the order screen cuts that to ~2 s, the port gets there in ~3 s), then ~600 frames per
  move with capture (1.5-2 s ares, the port about the same in a hidden window). 42 battles of 4 moves: ~10 min with
  frame capture on one core pair, ~3 min with 4 in parallel; scoring (pure Python) ~5 s per move: ~15 min, or
  parallel per move. Frames ~0.3 GB per battle, deleted after scoring.
- Report: one row per move: logic match (first differing field, if any), event timing (frames from "resolve" to
  the next event on both sides: the Razor Leaf / switch-in kind of gap), animation offset, SSIM 128x96 and 64x48 with
  the worst frame, and the side-by-side sheet. Flag: any logic difference; offset other than the common +4; a timing
  gap above a few frames; SSIM below the noise floor of the run (median minus a margin, ~0.65 here).

## Pokédex AREA map vs ares (sea, fog, label boxes)
The AREA map (Pokédex with a Game Boy cartridge; `lab_dex.rec` frames 9500/9800 in the evidence routes) drew a black
sea, no fog tint and pale label boxes. Three separate causes:
- Sea: the relocated `pokedex_area_model` blob gives its two sea display-list nodes geo callbacks that are MIPS code in
  the blob (0x8C000118, 0x8C000214). `Port_ResolveCode` found no host function, so the nodes stayed empty. They now
  have host versions (`Pokedex_AreaSeaCallback0/1` in `pokedex_2190D0.c`): each frame they step the blob's wave
  counters (0x8C000250..0x25C) and build a list that calls the sea material, sets the scrolled tile sizes and branches
  to the geometry. `Port_ResolveCode` maps a blob's pseudo address back to its link address (`Port_VLinkOf`) and
  looks such blob code up in a small table by fragment ROM. For that, `Fragment_LoadAndRelocateFromRomRange` now
  records the slot's ROM too (before, only code fragments did).
- Fog colour: `GeoNode_CreateFog` keeps the colour as bytes r,g,b,a in memory order, but `Geo_NodeFog` and
  `GeoRender_SetRenderMode` passed that word straight to `G_SETFOGCOLOR`, so 0x96C8FF arrived as (FF,FF,C8) with
  alpha 0x96. Both now swap it (as the other fog colour site already did). Battle stages were not visibly affected:
  the field's fog (0x50D7FF, 996–1000) lies past its geometry, and the Free Battle menu backdrop's fog is near black;
  checked against ares and with the swap on and off.
- Label boxes: they are texture rectangles drawn while the map's 2-cycle render mode (`G_RM_FOG_SHADE_A`) is still
  set. A rectangle has no shade on the RDP (0), so the blender's fog by shade alpha leaves it as it is. The renderer
  used the rectangle's stale vertex alpha (0xFF) and painted the boxes in the fog colour. Rectangles now fog by 0
  (libultraship fork, `GfxSpTri1`).
After these fixes, region averages of the 9500/9800 frames match ares within a few levels (what remains is ares'
320x240 VI output).

## Victory Palace completion scene and the badge carousel after a Free Battle (crashes)
Found by the function-evidence runs (branch evidence-pilot: Stadium flash variants and Game Pak Free Battles).
- Completion scene (Victory Palace with every statue: `stadium_save statue set all`, `replays/tour_vp1.rec`, frame
  3506): segfault in `Completion_InitCamera` (inlined in `Completion_InitScene`), the geo root `D_86B0EBC0` was
  garbage. A watchpoint showed the writer: `Completion_LoadModels` DMAs a 0xBC0-byte animation record (as the battle
  engine's) into `u8 D_86B0EBE8[0xA60]`; on the N64 it continues into the next bss array `D_86B0F648[0x180]` (the part
  the scene reads through `*D_86B0F648`), on the host the copy ran over the statics after `D_86B0EBE8`. Both are one
  block now (demo_scenes_161E60.c). The scene then matches ares ("For your 151 Pokémon HALL OF FAME, you won AMNESIA
  PSYDUCK!", same pose and lights). The other 0xBC0 record DMAs (intro, gallery camera, battle engine) have
  0xBE0-byte buffers.
- Badge carousel (`BattlePrep_LoadBadgeTextures`, battle_prep_397B10.c): every rules confirmation builds the badge
  carousel, but its archive index `sp44` is only set for session modes 1-8; for Free Battle (mode 0) it is an
  uninitialised stack slot (in the original code too). Its trainer records are read once `roundOrDemoId > 0`, so after
  a Free Battle the host read a wild record and crashed (frame 29642 of the evidence Game Pak Free Battle, in the
  instrumented build: the stack contents decide whether it crashes). Modes without a badge set now get the neutral
  texture (file 0), as unplayed rounds do; Free Battle does not show the carousel (the Record screen after the battle
  is unchanged).

## Byte-order sweep: unions and casts that read narrow fields as a wider value
A Game Pak team could hold the same Pokémon twice (Mew picked twice in a Free Battle; ares marks it chosen). The
rental team header `unk_D_838067F0_0168_0000` (gallery_rental_viewer.h) overlays the bytes unk_000..unk_003 with
`s32 raw`, and fragment 61 / rental_team_selection set, clear and test a flag as `raw & 0x80000000` and pick an icon
with `raw & 7`: on the N64 that is unk_000's top bit and unk_003's low bits; read on the host the flag tests hit the
species byte, so species >= 128 (Mew 151) were never "already chosen". The bytes are declared in reverse on a
little-endian host, so `raw` has the N64's value and every use stays as it is.
`tools/endian_union_sweep.py` then listed every union that overlays narrow fields with a wider member (game/src,
game/include) and every cast that reads a field through a wider type; each use was checked:
- Fixed: `LabPCList_CompareBySpeciesGroup` (the Lab PC list's sort by the Pokémon column) compared a record's first
  word (species, byte 0x01, u16 at 0x02) as one s32: on the host it sorted by the u16 first. The key is built from
  the fields in the N64's order now (not checked at run time: no recorded route sorts that column).
- Already handled by earlier port changes: AnimFixedFrame, the GB APU register bitfields and phase union, the
  Color_RGBA8 `.rgba` uses (PORT_BE32), PokeIcon's ptr/raw union, the model renderer's s16/pointer slot.
- Safe as used: BinArchive.raw (never used), gallery.h's s16 pairs (never used by name), the Color_RGBA16/14 bitfield
  unions (unused), same-width stores and loads through casts (lab_pc_list widgets), zeroing, word ORs for non-zero
  tests (libleo).
- Open: `Gallery_InitializePhotoModel` (gallery.c) sets the photo model's height from a record's first four bytes
  (u8, u8, u16) read as f32 (the decomp marks it `//?`); whether the host reads the N64's value depends on how the
  record is loaded. Needs a run with a gallery photo against ares.
- 64DD only: rsp.h overlays the low byte of `lba` with `mode` (dead in the retail game without a 64DD).

## Vulkan: minimized window

A minimized window on Windows has a surface with `currentExtent` 0x0 while SDL still reports the last pixel size, so
the Vulkan backend created a zero-sized swapchain (a tester's Intel iGPU crashed the launcher). `NewFrame`
(libultraship `gfx_vulkan.cpp`, Team Hippodrome fork) now makes no frame, and marks the swapchain for recreation, while
SDL reports the window minimized or the surface has no area (`SurfaceHasNoArea`, also on the retry after
`VK_ERROR_OUT_OF_DATE_KHR`). `PORT_DEBUG_MINIMIZE=<frame>:<restore frame>` minimizes and restores the window at those
frames (game: VI frame; launcher: its own frame counter; needs a visible window). Checked on MoltenVK
(`{"Window": {"Backend": {"Id": 4, "Name": "Vulkan"}}}`) in the game (minimized 120-360: no errors, picture back after the
restore) and in the launcher. Windows itself is untested.

## Cup grid: defeated trainers crossed out (OpenGL)

The Cup trainer grid (Poké/Prime/Petit/Pika Cup) marks each beaten trainer by drawing the red cross into that
trainer's portrait image in RDRAM (`BattlePrep_RenderBadgeGlowTexture`, called once per won battle in
`BattlePrep_LoadBadgeTextures`, all in one frame; `gDPPortPreserveColorImage` keeps the portrait under it). On OpenGL
only the first portrait got its cross (a tester's Poké Cup battle 6: 1 of 5). Cause: the GL backend's `SelectTexture`
skips `glBindTexture` when `mLastBoundTextures` already names the texture, but `CreateFramebuffer` and
`UpdateFramebufferParameters` bound the new render target's colour texture and then texture 0 behind that cache. From
the second portrait on, the cross texture was a cache hit and the draw sampled texture 0 (nothing). Both now bind back
the texture the cache records for the active unit (libultraship `gfx_opengl.cpp`, Team Hippodrome fork). Metal and
Vulkan were never affected; DirectX 11 is untested.

Checked with `replays/cup_giveup.rec` (plus Re-enter, OK: the second visit to the grid) and a temporary local override
that set `roundOrDemoId` to 6 and `opponentHistory` to 0..7 in `BattlePrep_LoadBadgeTextures`. OpenGL before: 1 cross;
after: 5, with Smooth motion on and off, at full speed and in real time with `PORT_DEBUG_DRAW_MS=25`. Metal and Vulkan
also give 5. ares (original ROM, same inputs, RDRAM writes `fill 1500 1690 800AE542 2 0006` for the round and
`800AF724..800AF72A` for the opponents) draws the same 5 crosses. The red share per cell matches within 1-2 %.
