# Compiler diagnostics and type safety

How the game code is built with warnings, which warning groups are still ratcheting down, and the conventions used
when a subsystem is fixed for the 64-bit host and joins `cmake/strict_sources.txt`. (Moved here from
`docs/archive/PORTING_NOTES.md`, which is no longer kept up to date.)

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
| Geo/model rendering (scene graph, geo layouts, model draw/animation, display-list buffers, textures) | 11 | 20/20 runnable replays, normal and ASan builds |

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
- Segmented and fragment addresses become pointers through `SEGMENTED_TO_VIRTUAL(addr)` (`memmap.h`, the
  HarbourMasters name) and `ADDR_TO_VIRTUAL(addr)` (`util.h`); both return `void*`, so no cast is needed at the use.
  They take the address as an integer (`uintptr_t`). A pointer-typed value that may still hold a segmented address
  (a geo-layout `GL_PTR`, a decomp `void*` parameter) goes through `SEGMENTED_PTR_TO_VIRTUAL(p)` /
  `ADDR_PTR_TO_VIRTUAL(p)` instead. They are inline functions, not casting macros, so a strict file that passes the
  wrong kind of value still fails to compile.
- Scene-graph nodes that start with a `GraphNode node` are passed as `&x->node`. A `(GraphNode*)` cast stays where
  the value may be NULL (`register_graph_node(GeoNode_Create...(...))`, as in the SM64 decomp) or where the node is a
  separate decomp type with the same layout (`DisplayNodeHeader`, `ModelNodeView`) until those types are merged.

`tools/ptr_warnings.sh` lists the pointer-truncation subset (490 sites). When fixing these, pair a replay run
(`tools/run_replays.py`) with an AddressSanitizer build (`cmake -DPORT_ASAN=ON`): a truncated pointer or a
32-bit size can survive a normal replay by luck of heap layout and fail later on Windows or Android.
