# Geo layout commands, node types and slot-model data the shipped game uses

15 functions in `geo_layout.c`, `geo_node.c` and `geo_render.c` never ran in any evidence run. They are the handlers of
six geo layout commands and of six graph node types. This file shows that no layout in the shipped game uses those
commands or produces those node types. The last section does the same for 14 slot-model functions in `33FE0.c`.
All 29 are listed in `data_dead_geo.json`, in the same format as `data_dead.json`.

## How the layouts were found

`routes/engine_left/geo_scan.py` reads the layouts from two independent sources:

* **ROM**: the US cartridge (md5 `ed1378bc12115f71209a77844965ba50`, the port's ROM). The scan covers the raw image
  and each of its 410 PERS-SZP and 634 other Yay0 blobs, decompressed (1045 buffers in all). Every 4-aligned word
  is tried as the start of a packed layout (big-endian words, command byte first). Code fragments, data fragments,
  stage files and model archives are all included, and no entry points are needed.
* **Sources**: every word array in `game/src` (356 arrays), read in the slot form the port compiles (one element
  per original word, with a symbol as a pointer slot).

A candidate is a command stream that meets all of these:

* It uses only table commands (byte < 0x27), with sizes from `geo_layout.c`'s `GL_CMDSIZE`.
* The commands without operands have their fixed words (`01/04/05/06 000000`).
* Branch, jump, attach and reference targets are plausible link addresses.
* It never closes more nodes than it opened and opens at least one node.
* It ends where a layout ends: `01` at depth 0, `04` at depth 0, or `02`.

Words before the root are split off as "prefix". The root is the last node-creating command before the first `05`
from which the parse reaches the same end. Prefix words are data that happens to parse as commands. Of 293 prefixes,
289 are a zero word followed by `09000000`.

The scan is not tuned to the coverage. Its counts are a test of it: every command that occurs in a candidate also
ran in the evidence runs, and every command with no runtime evidence occurs in no candidate.

```
python3 -I evidence/whole_game/routes/engine_left/geo_scan.py "<US ROM .z64>" game/src --json geo_scan.json
```

ROM: 787 candidate layouts. Sources: 113. The ROM count includes the source arrays, because they ship in the code
fragments.

| cmd | handler | ROM: commands / layouts | sources: commands / layouts |
|---|---|---|---|
| 00 | geo_layout_cmd_branch_and_link | 10 / 9 | 10 / 9 |
| 01 | geo_layout_cmd_end | 446 / 446 | 107 / 107 |
| 02 | geo_layout_cmd_jump **unused** | 0 / 0 | 0 / 0 |
| 03 | geo_layout_cmd_branch | 492 / 375 | 12 / 12 |
| 04 | geo_layout_cmd_return | 341 / 341 | 6 / 6 |
| 05 | geo_layout_cmd_open_node | 9315 / 787 | 361 / 113 |
| 06 | geo_layout_cmd_close_node | 9315 / 787 | 361 / 113 |
| 07 | geo_layout_cmd_attach_node | 23 / 7 | 23 / 7 |
| 08 | geo_layout_cmd_set_callback | 411 / 145 | 112 / 72 |
| 09 | geo_layout_cmd_create_container | 348 / 342 | 15 / 9 |
| 0A | geo_layout_cmd_create_reference | 43 / 37 | 41 / 35 |
| 0B | geo_layout_cmd_create_camera | 39 / 38 | 37 / 36 |
| 0C | geo_layout_cmd_create_type3 | 40 / 40 | 38 / 38 |
| 0D | geo_layout_cmd_create_type4 | 45 / 41 | 43 / 39 |
| 0E | geo_layout_cmd_create_ortho | 2 / 2 | 2 / 2 |
| 0F | geo_layout_cmd_create_perspective | 76 / 41 | 73 / 39 |
| 10 | geo_layout_cmd_create_background | 1 / 1 | 1 / 1 |
| 11 | geo_layout_cmd_create_clear_depth **unused** | 0 / 0 | 0 / 0 |
| 12 | geo_layout_cmd_noop **unused** | 0 / 0 | 0 / 0 |
| 13 | geo_layout_cmd_create_fog | 4 / 4 | 4 / 4 |
| 14 | geo_layout_cmd_create_light | 44 / 36 | 42 / 34 |
| 15 | geo_layout_cmd_skip **unused** | 0 / 0 | 0 / 0 |
| 16 | geo_layout_cmd_create_ambient_light | 36 / 36 | 34 / 34 |
| 17 | geo_layout_cmd_create_shadow_context | 405 / 405 | 69 / 69 |
| 18 | geo_layout_cmd_create_shadow | 195 / 173 | 0 / 0 |
| 19 | geo_layout_cmd_create_cull_distance **unused** | 0 / 0 | 0 / 0 |
| 1A | geo_layout_cmd_create_switch_case | 1 / 1 | 1 / 1 |
| 1B | geo_layout_cmd_create_translate_rotate | 29 / 21 | 28 / 20 |
| 1C | geo_layout_cmd_create_translate | 340 / 340 | 5 / 5 |
| 1D | geo_layout_cmd_create_animated_part | 8131 / 291 | 24 / 3 |
| 1E | geo_layout_cmd_create_display_list_part | 5484 / 171 | 0 / 0 |
| 1F | geo_layout_cmd_create_model_part | 31 / 19 | 31 / 19 |
| 20 | geo_layout_cmd_create_display_list_matrix | 1 / 1 | 1 / 1 |
| 21 | geo_layout_cmd_create_scale | 10 / 6 | 0 / 0 |
| 22 | geo_layout_cmd_create_display_list | 11142 / 395 | 124 / 71 |
| 23 | geo_layout_cmd_create_shadow_texture | 7141 / 296 | 26 / 5 |
| 24 | geo_layout_cmd_create_anchor | 1193 / 167 | 0 / 0 |
| 25 | geo_layout_cmd_set_node_flag4 | 244 / 121 | 11 / 11 |
| 26 | geo_layout_cmd_create_group **unused** | 0 / 0 | 0 / 0 |

Command `0x12` (`geo_layout_cmd_noop`) does not advance the command pointer. A layout that reached it would hang the
game in `process_geo_layout`, so it cannot occur in a layout the game runs.

Only two of the six commands occur anywhere in the scan, and both times as prefix words that are not part of a layout:

* `19` at ROM `0x2d49f0`: `191e0f14` is inside a table of segment-3 addresses (`03028ec0 03029c40 ...`). The real
  layout starts at `0x2d4a0c` with `0c000000 05000000 0b00002d`.
* `15` in `PERS-SZP@0x9025b0` at `0x2624`: `150cff7f` follows `fc272260` (a `G_SETCOMBINE`) and is followed by
  `df000000` (`G_ENDDL`). It is the tail of a display list. The layout starts at `0x2630`.

## Commands with no layout (6 handlers, 3 node constructors)

| function | why it cannot run |
|---|---|
| geo_layout_cmd_jump | command `02`: no layout uses it |
| geo_layout_cmd_create_clear_depth | command `11`: no layout uses it |
| geo_layout_cmd_noop | command `12`: no layout uses it (and one that did would hang) |
| geo_layout_cmd_skip | command `15`: no layout uses it |
| geo_layout_cmd_create_cull_distance | command `19`: no layout uses it |
| geo_layout_cmd_create_group | command `26`: no layout uses it |
| GeoNode_CreateClearDepth | its only caller is geo_layout_cmd_create_clear_depth |
| GeoNode_CreateCullDistance | its only caller is geo_layout_cmd_create_cull_distance |
| GeoNode_CreateGroup | its only caller is geo_layout_cmd_create_group |

(Checked with `grep -rn "GeoNode_Create\(ClearDepth\|CullDistance\|Group\|Type3\)\b" game/src`: there are no calls
outside `geo_layout.c` and `geo_node.c`.)

## Node types with no node (6 render handlers)

`Geo_ProcessNodeChildren` and `Geo_NodeReference` render a node through `D_8006F0A4[node->type]`. These are the
only two reads of that table (`geo_render.c:394`, `:411`).

A node's type is set only by `GeoNode_Init(node, type)`. No code writes `->type` anywhere else in `game/src`
(`grep -rn "[>.]type = "`). `GeoNode_Init` is called only from the `GeoNode_Create*` constructors in `geo_node.c`,
and each constructor uses a fixed type.

| function | node type | why no such node is rendered |
|---|---|---|
| Geo_NodeClearDepth | 8 | only GeoNode_CreateClearDepth makes it (command 11, unused) |
| Geo_NodeCullDistance | 0x10 | only GeoNode_CreateCullDistance makes it (command 19, unused) |
| Geo_NodeGroup | 0x1C | only GeoNode_CreateGroup makes it (command 26, unused) |
| Geo_NodeType9Empty | 9 | no `GeoNode_Init(..., 9)` exists: no node of type 9 is ever made |
| Geo_NodeType12Empty | 0xC | no `GeoNode_Init(..., 0xC)` exists: no node of type 12 is ever made |
| Geo_NodeType3Pass | 3 | type 3 nodes exist, but only as layout roots, and a root is never in the table's path (below) |

**Type 3 in detail.** Only `GeoNode_CreateType3` makes a type 3 node, and only command `0C` calls it.

* In all 40 raw-ROM and 38 source layouts that contain `0C`, it is the first command, at depth 0. So it is the
  root (`register_graph_node` with index 0) and is never appended as a child. The scan counts 0 cases where `0C`
  is not the first command.
* Roots are drawn by `Geo_RenderRootNode`, which calls `Geo_ProcessNodeChildren` on the root's children. It never
  calls the table for the root itself.
* Code adds layout results as children in only two kinds of place:
  * `GraphNode_AppendChild(..., process_geo_layout(...))`, in `gallery.c`, `gallery_camera`, `demo_scenes`,
    `credits` and `battle_engine_main.c`. These append stage-file layouts (`Port_CallBlobEntry` on a stage blob),
    which ship compressed.
  * The display-object sites (`model_renderer.c`, the minigames), which append static nodes, not layout results.
* All 670 compressed layouts have root `09` (container, 332), `17` (shadow context, 335) or `21` (scale, 3), never `0C`.
* Reference targets (command `0A`) and attach targets (command `07`) are static nodes named in the data. Their type
  comes from a `GeoNode_Create*` call in code, and no code calls `GeoNode_CreateType3`.

So no type 3 node ever becomes a child or a reference target, and `Geo_NodeType3Pass` cannot run.

## Slot models in 33FE0.c (14 functions)

`func_800361C4` drives the vertex models that `model_renderer_callbacks.c` draws for display objects. The model's
segment comes only from `func_80033B2C`, which copies one of two built-in templates into the model and points
`model->modelSegment` at the copy. `33FE0.c:602` is the only write of `modelSegment`. The two templates are:

* `D_800777A8`: type 10, used for every object except one.
* `D_80077950`: type 11, used when `objectType == 0x92` (Moltres).

So `segment->type` is 10 or 11, and `func_800361C4`'s other cases (1-6 and 9) never run. Type 10 runs in every
battle. Type 11 ran in the `moltres` run (`routes/engine_left`), which reached `func_80035E2C`.

Both templates' vertex tables (`D_80077790` and `D_80077938`: slots 0xCE, 0xCB, 0xCA, 0xCD, 0x98, 0x97, 0x95, 0x94,
0x92, 0x91) look up these classes in `Model_GetVertexClass`'s tables:

* slots 0x92 and 0x91: class 0 (`D_800769BC[slot] > 0`)
* the other eight slots: class 6 (only `D_800773D0` is > 0 for them)

`func_80035248` and `Model_InitializeVertexPositions` therefore never take their class 1-4 cases.

| function | why it cannot run |
|---|---|
| func_80034348 | only in cases 1-6 and 9 of `func_800361C4` |
| Model_OffsetVertexHeights | only in cases 2, 3 and 4 |
| func_80034BD4 | only in case 3 |
| Model_ApplyTransformCommands | only in case 3 and from Model_ApplyVertexTransforms |
| Model_ApplyVertexTransforms | only in case 9 |
| func_80034824 | only from Model_ApplyVertexTransforms |
| Model_ExtrapolateVertexPosition | only from Model_ApplyTransformCommands |
| func_80033568 | only from func_80034BD4 and the unreferenced func_80033D44 |
| Model_AccumulateVertexOffset | vertex class 1 |
| Model_GetVertexMergeClass | vertex class 1 |
| Model_CopyPositionBuffer | vertex classes 2 and 3 |
| Model_GetVertexTargetClass | vertex class 2 |
| Model_GetVertexSourceClass | vertex class 3 |
| Model_GetVertexMatrixClass | vertex class 4 (both callers) |
