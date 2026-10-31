#!/usr/bin/env python3 -I
"""Which move-animation effect slots and battle-scene state rows the shipped data can select, and which
battle-engine functions only unselectable slots reach (evidence/whole_game/battle_anim_slots.md).

Usage:
  anim_slots.py <callgraph.json> <rom.z64 (US)> <out.md> <run dir or coverage.json>...

Effect scripts: the u8 lists in game/src/fragments/battle_engine/battle_engine_315D50.c, read the way
BattleAnim_RunEffectList reads them (each list runs until a 0 byte; start lists index the start slot tables, end lists
the end slot tables). The list tables and how a row is selected:
  list 0, 1, 2  gMoveAnimEffectScripts[move][0], [1] (start), [2] (end)   row = the move used (every move ran)
  list 3        D_84386D44[row][0] (start)       BattleAnim_QueueEffectList3(row): literals, the mode of an entered
                                                 battle-scene state row (BattleScene_QueueRowEffectList3), and a
                                                 species' idle cue (MovePresentationCue unk_A50.unk_05, ROM 0x70D3A0)
  list 4        gBattleAnimMode4StartEffectLists  BattleAnim_QueueEffectList(row): literals only
  list 6        D_84386DEC (end)                  BattleAnim_QueueEffectList6(row): literals only
  list 9        gBattleAnimMode9StartEffectLists  particle descriptor field unk_19: both rows counted
State rows: sBattleSceneStateRowTable[row], entered only by BattleScene_EnterStateRow(pokemon, <literal>).
A function is "data-dead" when every static reference to it (the call graph's call / address / table edges, with the
slot tables and the state-row table replaced by their selectable entries) comes from data-dead code or from an
entry nothing can select. The runs are the check: no data-dead function may appear in any of them.
"""
import glob, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SRC = os.path.join(ROOT, "game", "src", "fragments", "battle_engine")
START_TABLES = ["gMoveAnimEffectStartInitFuncs", "gMoveAnimEffectStartFuncs", "gMoveAnimEffectStartUpdateFuncs",
                "gMoveAnimEffectStartDrawFuncs"]
END_TABLES = ["gMoveAnimEffectEndInitFuncs", "gMoveAnimEffectEndFuncs", "gMoveAnimEffectEndUpdateFuncs",
              "gMoveAnimEffectEndDrawFuncs"]
ROW_TABLE = "sBattleSceneStateRowTable"
LIT = r"(0x[0-9A-Fa-f]+|\d+)"


def body(src, decl):
    j = src.index("{", src.index(decl))
    depth, k = 0, j
    while True:
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return src[j + 1:k]
        k += 1


def strip_comments(s):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", s, flags=re.S)


def func_body(src, name):
    m = re.search(r"\n\w[\w\s\*]*\b%s\([^)]*\)\s*\{" % name, src)
    return body(src[m.start():], name + "(")


def cases(src, name):
    return sorted({int(x, 0) for x in re.findall(r"case\s+" + LIT + ":", func_body(src, name))})


def constant_gates(w, srcs, allsrc, reached):
    """switch cases and flags whose values no code produces (checked on the source, like the slot rows)"""
    ran = lambda fns: ", ".join("`%s`%s" % (f, " **ran**" if f in reached else "") for f in fns)
    s = srcs["battle_engine_309ED0.c"]
    set_ = set()
    for pat in (r"BattleScene_SetPhase\([^;]*?,\s*" + LIT + r"\)", r"BattleScene_TransitionToPhase\w*\(\s*" + LIT + r"\)",
                r"sBattleScene\.unk_00->unk_38 = " + LIT + ";"):
        set_ |= {int(x, 0) for x in re.findall(pat, allsrc)}
    saved = {int(x, 0) for x in re.findall(r"Battle_ActSecondAttacker\(\s*" + LIT + r"\)", allsrc)}
    phase_cases = cases(s, "BattleScene_UpdatePhase")
    never = [c for c in phase_cases if c not in set_ | saved and c != 0]
    w("## Values no code produces")
    w("")
    w("- **Scene phases** (`BattleScene_UpdatePhase`, `sBattleScene.unk_00->unk_38`): the cases are %s; the phase is "
      "written only by `BattleScene_SetPhase`, `BattleScene_TransitionToPhase*` and direct stores, all with literals "
      "(%s), and restored from `unk_78`, which only `Battle_ActSecondAttacker(%s)` sets. Never produced: %s. Phase 34 "
      "is set only inside phase 33 (`BattleScene_PhaseCheckFaintedEndTurn`, `BattleScene_SetPhase(arg0, 0x22)`), so "
      "both are code the shipped game cannot run: %s." % (
          phase_cases, sorted(set_), sorted(saved), never,
          ran(["BattleScene_PhaseCheckFaintedEndTurn", "BattleScene_PhaseSendOutAfterFaint",
               "BattleScene_CompleteSendOutFollowup"])))
    s = srcs["battle_engine_304060.c"]
    table = [int(x, 0) for x in re.findall(LIT, re.search(r"s32 sp20\[\] = \{([^}]*)\}",
                                                           func_body(s, "BattleScene_SelectNextIntroCameraShot")).group(1))]
    lits = {int(x, 0) for x in re.findall(r"unk_20 = " + LIT + ";", s)}
    lits |= {int(x, 0) for x in re.findall(r"BattleScene_SetCameraModeAndStep\(\s*\w+\s*,\s*" + LIT + r"\)", allsrc)}
    starts = {v for v in table if v != 0xFF} | lits
    produced = starts | {v + 1 for v in starts if v % 2 == 0 and v < 50}  # 50: the selector itself  # an even step's setup ends with unk_20++
    steps = sorted(set(cases(s, "BattleScene_UpdateIntroCameraSequence")) |
                   set(cases(s, "BattleScene_RunIntroCameraSetupStep") if "BattleScene_RunIntroCameraSetupStep(" in s
                       else []))
    w("- **Turn camera shots** (`sBattleScene.unk_00->unk_20` while the scene camera mode is 1): "
      "`BattleScene_SelectNextIntroCameraShot` takes the next step from its table %s (0xFF wraps), or the literals "
      "%s; each even setup step ends with `unk_20++` into its odd hold step. Steps produced: %s; the steps 12-15 "
      "(`BattleScene_IntroCameraStep12Or14`, its hold `BattleScene_IntroCameraWideHoldStep`, and "
      "`BattleScene_SetCameraSendOutShot`, called only by step 12/14) are never selected: %s." % (
          table, sorted(lits), sorted(produced), ran(["BattleScene_IntroCameraStep12Or14",
                                                      "BattleScene_IntroCameraWideHoldStep",
                                                      "BattleScene_SetCameraSendOutShot"])))
    modes = sorted({int(x, 0) for x in re.findall(r"BattleScene_SetCameraModeAndStep\(\s*" + LIT, allsrc)})
    w("- **Scene camera mode** (`sBattleScene.unk_00->unk_1C`): set only by `BattleScene_SetCameraModeAndStep` with "
      "%s; `BattleScene_Update`'s cases 0 and 3 and `BattleScene_RunFinalPresentation`'s cases 0 and 3 never run: %s." %
      (modes, ran(["func_843297E8", "func_843297F0", "BattleScene_FinalPresentationFrameActiveOwner",
                   "BattleScene_FinalPresentationFrameOwner0"])))
    paused = sorted(set(re.findall(r"sIsPaused = (\w+);", allsrc)))
    w("- **Pause** (`sIsPaused`, `battle_engine_main.c`): only ever assigned %s, so `BattleScene_UpdatePausedFrame` "
      "never runs: %s." % (paused, ran(["BattleScene_UpdatePausedFrame"])))
    w("- **Default draw callbacks**: `BattleAnim_UpdateEffectCallbacks` runs before `BattleAnim_DrawEffectCallbacks` "
      "each frame and clears a slot's draw callback as soon as its update returns non-zero; the default updates "
      "(`BattleAnim_DefaultStartEffect_CompleteImmediately`, `BattleAnim_DefaultEndEffect_CompleteImmediately`, "
      "`BattleAnim_EndEffect16_CompleteImmediately`) return -1 on their first call, so their paired draws never run: "
      "%s." % ran(["BattleAnim_DefaultStartEffect_Draw", "BattleAnim_DefaultEndEffect_Draw",
                   "BattleAnim_EndEffect16_NoOpDraw"]))
    w("")


def main():
    cg_path, rom_path, out_md, runs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    srcs = {os.path.basename(p): strip_comments(open(p).read()) for p in glob.glob(os.path.join(SRC, "*.c"))}
    allsrc = "\n".join(srcs.values())
    a, b = srcs["battle_engine_315D50.c"], srcs["battle_engine_347330.c"]

    # effect-script bytes, arrays in source (= address) order
    arrays, order = {}, []
    for m in re.finditer(r"static u8 (D_[0-9A-F]{8})\[\] = \{([^}]*)\};", a):
        arrays[m.group(1)] = [int(x, 0) for x in re.findall(LIT, m.group(2))]
        order.append(m.group(1))
    assert order == sorted(order), "u8 arrays not in address order"
    stream, base = [], {}
    for n in order:
        base[n] = len(stream)
        stream += arrays[n]
    overruns = set()

    def read(name):
        out, k = [], base[name]
        while stream[k] != 0:
            out.append(stream[k])
            k += 1
        if k >= base[name] + len(arrays[name]):
            overruns.add(name)
        return out

    def rows(decl, width):
        names = re.findall(r"\b(D_[0-9A-F]{8})\b", body(a, decl))
        return [names[i:i + width] for i in range(0, len(names), width)]

    scripts = rows("gMoveAnimEffectScripts[][5]", 5)
    lists = {3: [r[0] for r in rows("D_84386D44[][2]", 2)], 4: [r[0] for r in rows("gBattleAnimMode4StartEffectLists[]", 1)],
             6: [r[0] for r in rows("D_84386DEC[]", 1)], 9: [r[0] for r in rows("gBattleAnimMode9StartEffectLists[]", 1)]}

    # state rows: callbacks and modes; entered rows = literal arguments of BattleScene_EnterStateRow
    rt = body(srcs["battle_engine_2F3CB0.c"], "BattleSceneStateRow %s[]" % ROW_TABLE)
    state_rows = [([f.strip() for f in m.group(2).split(",") if f.strip()], int(m.group(4), 0))
                  for m in re.finditer(r"\{\s*(-?\w+),\s*\{([^}]*)\},\s*(-?\w+),\s*(-?\w+),\s*(-?\w+),?\s*\}", rt)]
    entered = sorted({int(x, 0) for x in re.findall(r"BattleScene_EnterStateRow\([^;]*?,\s*" + LIT + r"\)", allsrc)})
    other_enter = [m.group(0) for m in re.finditer(r"BattleScene_EnterStateRow\([^;]*?,\s*([^)\s]+)\)", allsrc)
                   if not re.fullmatch(LIT, m.group(1)) and "s32 arg1" not in m.group(0)]

    # selectable rows per list
    lit = lambda fn: sorted({int(x, 0) for x in re.findall(r"\b%s\(\s*%s\s*," % (fn, LIT), allsrc)})
    nonlit = lambda fn: [m.group(0) for m in re.finditer(r"\b%s\(\s*([^,\s][^,]*?)\s*," % fn, allsrc)
                         if not re.fullmatch(LIT, m.group(1)) and not m.group(1).startswith(("s32", "u8"))]
    rom = open(rom_path, "rb").read()
    idle = {}
    for i in range(153):  # D_80075BD0 (src/lab_item.c): 0xB90 per species record, cue unk_A50 at +0xA50
        v = rom[0x70D3A0 + 0xB90 * i + 0xA50 + 5]
        if v:
            idle.setdefault(v, []).append(i + 1)
    modes = sorted({state_rows[r][1] for r in entered if r < len(state_rows) and state_rows[r][1] != 255})
    sel = {3: sorted(set(lit("BattleAnim_QueueEffectList3")) | set(modes) | set(idle)),
           4: lit("BattleAnim_QueueEffectList"), 6: lit("BattleAnim_QueueEffectList6"), 9: [0, 1]}
    dyn = {3: nonlit("BattleAnim_QueueEffectList3"), 4: nonlit("BattleAnim_QueueEffectList"),
           6: nonlit("BattleAnim_QueueEffectList6")}

    start_users, end_users = {}, {}
    unsel_users = {"start": {}, "end": {}}

    def use(kind, name, where, selectable):
        d = (start_users if kind == "start" else end_users) if selectable else unsel_users[kind]
        for s in read(name):
            d.setdefault(s, []).append(where)

    for mv, r in enumerate(scripts):
        use("start", r[0], "move %d list 0" % mv, True)
        use("start", r[1], "move %d list 1" % mv, True)
        use("end", r[2], "move %d list 2" % mv, True)
    for li, tab in lists.items():
        for i, name in enumerate(tab):
            use("end" if li == 6 else "start", name, "list %d row %d" % (li, i), i in sel[li])

    tables = {}
    for t in START_TABLES + END_TABLES:
        init = t.endswith("InitFuncs")
        tables[t] = re.findall(r"\b(\w+)\b", body(a if init else b, ("static func_D_84390320 %s[]" if init else
                                                                     "func_D_84390320 %s[]") % t))

    cg = json.load(open(cg_path))
    funcs = cg["functions"]
    reached = set()
    for r in runs:
        for p in ([r] if r.endswith(".json") else glob.glob(os.path.join(r, "**", "coverage.json"), recursive=True)):
            reached |= set(json.load(open(p))["functions"])

    skip = set(START_TABLES + END_TABLES + [ROW_TABLE])
    refs = {}
    for caller, callee, kind, via in cg["edges"]:
        if kind == "table" and via in skip:
            continue
        refs.setdefault(callee, set()).add(caller)
    live, entries = set(), set()  # functions in selectable entries; functions in any entry
    for t in START_TABLES + END_TABLES:
        users = start_users if t in START_TABLES else end_users
        for i, fn in enumerate(tables[t]):
            entries.add(fn)
            if i in users and i != 0:
                live.add(fn)
    for r, (fns, _) in enumerate(state_rows):
        entries |= set(fns)
        if r in entered:
            live |= set(fns)
    dead = set()
    changed = True
    while changed:
        changed = False
        for f in set(refs) | entries:
            if f in dead or f in live or f not in funcs:
                continue
            callers = refs.get(f, set())
            if (callers or f in entries) and all(c in dead for c in callers):
                dead.add(f)
                changed = True
    in_be = lambda f: "fragments/battle_engine/" in funcs.get(f, {}).get("file", "")
    contradictions = sorted(f for f in dead if f in reached)

    L = []
    w = L.append
    nstart, nend = len(tables["gMoveAnimEffectStartFuncs"]), len(tables["gMoveAnimEffectEndFuncs"])
    us = sorted(s for s in start_users if 0 < s < nstart)
    ue = sorted(s for s in end_users if 0 < s < nend)
    w("# Battle animation slots and scene rows the shipped data can select")
    w("")
    w("Generated by `routes/battle_gates/anim_slots.py <cg_all4.json> <US ROM> <out>` from the battle engine's data "
      "(the effect scripts and slot tables in `battle_engine_315D50.c` / `battle_engine_347330.c`, the state-row table "
      "in `battle_engine_2F3CB0.c`, the per-species idle cue in the ROM at 0x70D3A0) and checked against the runs. "
      "The proof is the one the Game Boy music commands got from playing all 151 cries: every move's animation ran "
      "(the batch4 sweep, completed by single-move runs of 12, 60, 100, 154, 155 in `battle_gates`), every row index "
      "the code can pass is listed below, so a slot that no selectable list names is code the shipped data cannot run.")
    w("")
    w("## How a slot is selected")
    w("")
    w("`BattleAnim_RunEffectList(row, list, mode)` reads a byte list until a 0 and, for each byte s, calls "
      "`gMoveAnimEffectStartInitFuncs[s]`, `gMoveAnimEffectStartFuncs[s]` and registers "
      "`gMoveAnimEffectStartUpdateFuncs[s]` / `gMoveAnimEffectStartDrawFuncs[s]` (end lists: the four End tables).")
    w("")
    w("| list | table | rows | rows the code can select | how |")
    w("|---|---|---|---|---|")
    w("| 0, 1 (start), 2 (end) | `gMoveAnimEffectScripts[move][0..2]` | %d | all (the used move) | "
      "`BattleAnim_QueueMoveEffect*` with the move id |" % len(scripts))
    w("| 3 (start) | `D_84386D44[row][0]` | %d | %s | literals of `BattleAnim_QueueEffectList3` %s; modes %s of the "
      "entered state rows; the species idle cue: %s; dynamic call sites: %s |" % (
          len(lists[3]), sel[3], lit("BattleAnim_QueueEffectList3"), modes,
          ", ".join("%d (dex %s)" % (k, v) for k, v in idle.items()) or "none",
          "; ".join("`%s`" % x for x in dyn[3])))
    w("| 4 (start) | `gBattleAnimMode4StartEffectLists` | %d | %s | literals of `BattleAnim_QueueEffectList`; "
      "dynamic call sites: %s |" % (len(lists[4]), sel[4], "; ".join("`%s`" % x for x in dyn[4]) or "none"))
    w("| 6 (end) | `D_84386DEC` | %d | %s | literals of `BattleAnim_QueueEffectList6` (its other two callers, "
      "`BattleAnim_QueueEffectList5` / `BattleAnim_QueueEffectList6Alt`, have no caller); dynamic call sites: %s |" % (
          len(lists[6]), sel[6], "; ".join("`%s`" % x for x in dyn[6] if "arg0" not in x) or "none"))
    w("| 9 (start) | `gBattleAnimMode9StartEffectLists` | %d | both (counted) | particle descriptor field |" %
      len(lists[9]))
    w("")
    w("State rows (`%s`, %d rows) are entered only by `BattleScene_EnterStateRow(pokemon, <literal>)`; the literals "
      "are %s, so rows %s are never entered%s. The modes of entered rows feed list 3 "
      "(`BattleScene_QueueRowEffectList3`)." % (ROW_TABLE, len(state_rows), entered,
                                               [r for r in range(len(state_rows)) if r not in entered],
                                               ("; other call forms: " + ", ".join(other_enter)) if other_enter else ""))
    w("")
    w("| | slots | named by a selectable list | never |")
    w("|---|---|---|---|")
    w("| start | %d | %d | %d (slot 0 is the terminator) |" % (nstart, len(us), nstart - 1 - len(us)))
    w("| end | %d | %d | %d (slot 0 is the terminator) |" % (nend, len(ue), nend - 1 - len(ue)))
    w("")
    bad = sorted({s for s in start_users if s >= nstart} | {s for s in end_users if s >= nend})
    w("Slot bytes past a table: %s. Lists that run past their own array: %s. `gMoveAnimEffectStartInitFuncs` has %d "
      "entries for %d start slots: slots %d-%d have no init entry and no list names them." % (
          bad or "none", sorted(overruns) or "none", len(tables["gMoveAnimEffectStartInitFuncs"]), nstart,
          len(tables["gMoveAnimEffectStartInitFuncs"]), nstart - 1))
    w("")
    for kind, users, tabs, n in (("start", start_users, START_TABLES, nstart), ("end", end_users, END_TABLES, nend)):
        w("## %s slots no selectable list names" % kind.capitalize())
        w("")
        w("| slot | named only by | %s |" % " | ".join("`%s`" % t.replace("gMoveAnimEffect", "") for t in tabs))
        w("|---|---|%s" % ("---|" * len(tabs)))
        for i in range(1, n):
            if i in users:
                continue
            cells = []
            for t in tabs:
                fn = tables[t][i] if i < len(tables[t]) else None
                if fn is None:
                    cells.append("-")
                    continue
                note = " (shared)" if fn in live else ""
                note += " **ran**" if fn in reached else ""
                cells.append("`%s`%s" % (fn, note))
            w("| %d | %s | %s |" % (i, ", ".join(unsel_users[kind].get(i, [])) or "nothing", " | ".join(cells)))
        w("")
    w("## State rows never entered")
    w("")
    w("| row | mode | callbacks |")
    w("|---|---|---|")
    for r, (fns, mode) in enumerate(state_rows):
        if r not in entered:
            w("| %d | %d | %s |" % (r, mode, ", ".join("`%s`%s" % (f, " **ran**" if f in reached else "")
                                                      for f in fns)))
    w("")
    dbe = sorted(f for f in dead if in_be(f))
    w("## Battle-engine functions only unselectable entries reach (%d)" % len(dbe))
    w("")
    w("Every static reference to these is an unselectable slot or state-row entry or another function in this list "
      "(call graph `cg_all4.json`). Check against the runs (batch4-13 and `battle_gates`, %d functions of the whole game reached): %s." %
      (len(reached), "none of them ran" if not contradictions else "CONTRADICTION, ran: " + ", ".join(contradictions)))
    w("")
    by_file = {}
    for f in dbe:
        by_file.setdefault(funcs[f]["file"].split("/")[-1], []).append(f)
    for fl in sorted(by_file):
        w("- `%s`: %s" % (fl, ", ".join("`%s`" % f for f in by_file[fl])))
    w("")
    constant_gates(w, srcs, allsrc, reached)
    w("## Slot use")
    w("")
    w("| slot | start: selectable lists naming it | end: selectable lists naming it |")
    w("|---|---|---|")
    fmt = lambda u: ("%d: %s%s" % (len(u), ", ".join(u[:4]), ", ..." if len(u) > 4 else "")) if u else "-"
    for i in range(1, max(nstart, nend)):
        w("| %d | %s | %s |" % (i, fmt(start_users.get(i, [])) if i < nstart else "n/a",
                                fmt(end_users.get(i, [])) if i < nend else "n/a"))
    open(out_md, "w").write("\n".join(L) + "\n")
    json.dump(sorted(dead), open(os.path.splitext(out_md)[0] + ".dead.json", "w"))
    print("start %d/%d, end %d/%d named by selectable lists; rows entered %s; data-dead battle-engine functions %d; "
          "contradictions %s" % (len(us), nstart - 1, len(ue), nend - 1, entered, len(dbe), contradictions or 0))


main()
