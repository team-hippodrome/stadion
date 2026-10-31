#!/usr/bin/env python3 -I
"""Which battle particle descriptors (and so which particle draw / texture callbacks) the shipped data can select, and
which particle and battle-animation functions only unselectable descriptors or dead callers reach
(evidence/whole_game/particle_descriptors.md, evidence/whole_game/data_dead_particles.json).

Usage:
  particle_descriptors.py <callgraph.json> <frontier.json> <rom.z64 (US)> <data_dead.json[,more]> <out.md> <out.json>
                          <batch or run dir>...
The batch / run dirs are the ones the frontier merged (their coverage.json, and merged.profdata or coverage.profraw);
they tell a function the game ran from one only dead_calls.md's forced direct call ran (that run, runs4/dead_calls,
is inside batch4's merged.profdata): forced, absent from every other run's coverage.json, profile count 1; and the
functions those forced calls ran below them (in the frontier's reached set, in no other run's coverage.json).

The particle data: gParticleDescriptorChildren[] (u8, u8, s16, texture callback, draw callback, asset) and
gBattleAnimParticleDescriptors[] (kind 1: a child; kind 3: a draw function) in battle_engine_315D50.c. They are the
only particle descriptor tables in the decomp (besides particle_math's gDefaultParticleDescriptor, a no-op draw); the
minigames' particles (MiniFx_*) carry no descriptor table. Every code reference to a descriptor is a literal
`&gBattleAnimParticleDescriptors[N]` (checked: no other use of either table), so descriptor N can be drawn only if a
function holding that literal runs; the callbacks of a child only if a selectable descriptor names it.

A function is dead here when every static reference to it (call graph edges; the two descriptor tables replaced by the
functions holding a literal of a descriptor naming it) comes from dead code, where dead code is:
  - referenced by nothing: no caller or table in the decomp and no ROM word equal to its N64 address (deadcode.py's
    test). Some of these count as "reached" in the batches only because dead_calls.md called each of them once
    directly (a forced call with placeholder arguments); a forced call is not a way the game runs them
  - the classes dead_classes.py already proves (data_dead.json and its siblings, PAL-only, reachable only from dead)
  - functions this test finds dead, repeated until nothing changes.
The runs are the check: no function found dead may have run except by a forced direct call.
"""
import collections, json, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
GAME = os.path.join(ROOT, "game", "src")
sys.path.insert(0, os.path.join(ROOT, "tools", "evidence"))
import deadcode  # noqa: E402  (addresses(): name -> N64 address)

TABLES = ("gBattleAnimParticleDescriptors", "gParticleDescriptorChildren")


def strip_comments(s):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", s, flags=re.S)


def body(src, decl):
    j = src.index("{", src.index(decl))
    depth, k = 0, j
    while True:
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return src[j + 1:k]
        k += 1


def functions_of(src):
    """[(name, start, end)] of the function bodies in a C file (top-level definitions)"""
    out = []
    for m in re.finditer(r"^[A-Za-z_][\w \*]*?\b(\w+)\(([^;{]*?)\)\s*\{", src, re.M):
        name = m.group(1)
        if name in ("if", "for", "while", "switch"):
            continue
        j = m.end() - 1
        depth, k = 0, j
        while True:
            depth += {"{": 1, "}": -1}.get(src[k], 0)
            if depth == 0:
                break
            k += 1
        out.append((name, j, k))
    return out


LIT = r"(0x[0-9A-Fa-f]+|\d+)"


def src_of(*parts):
    return strip_comments(open(os.path.join(GAME, *parts)).read())


def func_body(src, name):
    m = re.search(r"^\w[\w \*]*?\b%s\([^;{]*\)\s*\{" % name, src, re.M)
    return body(src[m.start():], name + "(")


def call_args(src, fn):
    """argument lists of every call of fn in src (not its definition)"""
    out = []
    for m in re.finditer(r"\b%s\(" % fn, src):
        if re.match(r"[A-Za-z_][\w\* ]*$", src[src.rfind("\n", 0, m.start()) + 1:m.start()]):
            continue  # a definition or declaration ("<type> fn(" at the start of a line; calls are indented)
        depth, cur, args = 0, "", []
        for ch in src[m.end() - 1:]:
            if ch == "(":
                depth += 1
                if depth == 1:
                    continue
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    args.append(cur.strip())
                    break
            if ch == "," and depth == 1:
                args.append(cur.strip())
                cur = ""
                continue
            cur += ch
        out.append(args)
    return out


def lits(values):
    return sorted({int(v, 0) for v in values if re.fullmatch(LIT, v)}), sorted({v for v in values if not re.fullmatch(LIT, v)})


def value_proofs(children, descs, users):
    """Call sites whose condition no shipped value meets, each checked on the source: [{title, text, edges, names, ok}].
    edges: (caller, callee) call sites that never execute; names: functions dead outright by the claim."""
    be = os.path.join("fragments", "battle_engine")
    allbe = "\n".join(src_of(be, f) for f in sorted(os.listdir(os.path.join(GAME, be))) if f.endswith(".c"))
    P = []

    # 1. the slot mode: BattleAnimEffectSlot.unk_16 is written only by BattleAnim_CreateEffectSlot (arg8); the
    #    wrappers pass their own argument through
    writers = re.findall(r"(\w+)->unk_16 = (\w+);", src_of(be, "battle_engine_317E70.c"))
    pos = {"BattleAnim_CreateEffectSlot": 8, "BattleAnim_CreateEffectSlotForCurrentOwner": 5,
           "BattleAnim_CreateRepeatingEffectSlotForCurrentOwner": 7}
    vals = []
    for fn, k in pos.items():
        vals += [a[k] for a in call_args(allbe, fn) if len(a) > k]
    lv, other = lits(vals)
    P.append({"title": "slot mode 18", "edges": [("BattleAnim_DispatchParticleEmission", "BattleAnim_SpawnBatchedParticles")],
              "ok": writers == [("sp1C", "arg8")] and 18 not in lv and set(other) <= {"arg5", "arg7"},
              "text": "`BattleAnim_DispatchParticleEmission` calls `BattleAnim_SpawnBatchedParticles` for slot mode 18 "
                      "(`BattleAnimEffectSlot.unk_16`). The only store to `unk_16` is `BattleAnim_CreateEffectSlot`'s "
                      "(`sp1C->unk_16 = arg8`, written on every allocation); its mode argument at every call of it and "
                      "of its two wrappers is one of %s (the wrappers' own pass-throughs: %s): never 18." % (lv, other)})

    # 2. the six-slot trail's mode (case 1 computes the camera-relative offset)
    lv, other = lits([a[0] for a in call_args(allbe, "BattleAnim_SixSlotTrail_Spawn")])
    P.append({"title": "six-slot trail mode 1",
              "edges": [("BattleAnim_SixSlotTrail_DrawAll", "BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset")],
              "ok": lv == [0] and not other and
                    re.findall(r"(\w+)->unk_002 = (\w+);", src_of(be, "battle_engine_357C30.c")) == [("var_v0", "arg0")],
              "text": "`BattleAnim_SixSlotTrail_DrawAll` calls `BattleAnim_SixSlotTrail_ComputeCameraRelativeOffset` in "
                      "its case 1 of the trail's mode (`unk_002`), stored only by `BattleAnim_SixSlotTrail_Spawn` from "
                      "its first argument; both calls pass %s." % lv})

    # 3. the recolor variant of the shared descriptor-0x19 callback (its particle field CF = slot unk_1B = the
    #    CreateEffectSlot argA)
    cb = "BattleAnim_CallbackSharedDescriptor19RecolorAnimatedFade"
    cf = []
    for fn, k in (("BattleAnim_CreateEffectSlot", 10), ("BattleAnim_CreateEffectSlotForCurrentOwner", 7),
                  ("BattleAnim_CreateRepeatingEffectSlotForCurrentOwner", 9)):
        cf += [a[k] for a in call_args(allbe, fn) if cb in a and len(a) > k]
    lv, other = lits(cf)
    other_uses = [m.start() for m in re.finditer(r"\b%s\b(?!\()" % cb, allbe)]
    ok = (2 not in lv and not other and "arg0->unk_CF = arg2->unk_1B;" in src_of(be, "battle_engine_317E70.c")
          and len(other_uses) == len(cf))
    P.append({"title": "descriptor-0x19 recolor variant 2",
              "edges": [(cb, "ParticleGfx_RecolorPrimEnvFromPaletteSetB"),
                        (cb + "_InitParticle", "ParticleGfx_RecolorPrimEnvFromPaletteSetB")], "ok": ok,
              "text": "`%s` and its `_InitParticle` call `ParticleGfx_RecolorPrimEnvFromPaletteSetB` only in case 2 of "
                      "the particle's field CF, which `BattleAnim_InitializeParticleFromEffect` copies from the slot's "
                      "`unk_1B` (`BattleAnim_CreateEffectSlot`'s argA). The callback is registered at %d call sites, "
                      "all with that argument in %s." % (cb, len(cf), lv)})

    # 4. the battle half of Particle_AllocChain: a kind-1 descriptor whose child has unk_00 == 1
    chain_children = [j for j, c in enumerate(children) if c.get("u0") == 1]
    chain_descs = [i for i, d in enumerate(descs) if d["kind"] == 1 and d["child"] in chain_children]
    P.append({"title": "chained-particle descriptors",
              "edges": [("BattleAnim_SpawnEffectParticles", "Particle_AllocChain")],
              "ok": bool(chain_descs) and not any(users.get(i) for i in chain_descs),
              "text": "`BattleAnim_SpawnEffectParticles` calls `Particle_AllocChain` only when the slot's descriptor is "
                      "kind 1 and its child's first byte is 1: children %s, named by descriptors %s, which no code "
                      "references." % (chain_children, chain_descs)})

    # 5. the minigame emitters: active only when unk_2B != 0, set only in MiniFx_AllocEmitter
    mf = src_of("fragments", "minigame_actor_framework", "minigame_actor_framework_119FB0.c")
    sets = [(m.group(1), m.group(2)) for m in re.finditer(r"(\w+)->unk_2B = (\w+);", mf)]
    alloc_line = func_body(mf, "MiniFx_AllocEmitter")
    P.append({"title": "minigame emitters never active",
              "edges": [("MiniFx_TickEmitters", "MiniFx_TryEmit"), ("MiniFx_TickEmitters", "MiniFx_FreeEmitter")],
              "ok": "unk_2B = 1;" in alloc_line and all(v == "0" for _, v in sets if "unk_2B = %s;" % v not in alloc_line)
                    and "bzero(gMiniFxEmitters" in mf,
              "text": "`MiniFx_TickEmitters` emits (`MiniFx_TryEmit`) and frees (`MiniFx_FreeEmitter`) only for an "
                      "emitter with `unk_2B != 0`; the pool is zeroed (`bzero(gMiniFxEmitters, ...)` in `MiniFx_Init`) "
                      "and `unk_2B` is set non-zero only in `MiniFx_AllocEmitter`, whose only caller "
                      "`MiniFx_ConfigureEmitter` is reached only from the two `MiniFx_Configure*Emitter` wrappers, "
                      "which nothing references (dead_classes: referenced by nothing / reachable only from dead "
                      "code). The other stores: %s." % ", ".join("`%s->unk_2B = %s`" % s for s in sets)})

    # 6. intro step 10 (camera mode 4): each step adds 1 to unk_20 once; step 9 adds 2 (its own += 1 and
    #    BattleScene_BeginBattlePhase's), so the sequence goes 9 -> 11
    s313 = src_of(be, "battle_engine_313250.c")
    settle = func_body(s313, "BattleScene_IntroSecondMonSettle")
    begin = func_body(src_of(be, "battle_engine_309ED0.c"), "BattleScene_BeginBattlePhase")
    begin_callers = [m.start() for m in re.finditer(r"BattleScene_BeginBattlePhase\(\);", allbe)]
    P.append({"title": "intro step 10",
              "edges": [("BattleScene_UpdateIntroSequence", "BattleScene_IntroStepNop")],
              "ok": "BattleScene_BeginBattlePhase();" in settle and "unk_20 += 1;" in settle and
                    "unk_20 += 1;" in begin and len(begin_callers) == 2,
              "text": "Camera mode 4 (`BattleScene_UpdateIntroSequence`) starts at step 0 "
                      "(`BattleScene_SetCameraModeAndStep(4, 0)` is the only way in) and each step function adds 1 to "
                      "`unk_20` when done; step 9 (`BattleScene_IntroSecondMonSettle`) calls "
                      "`BattleScene_BeginBattlePhase`, which adds 1 itself, then adds its own 1: 9 -> 11, and no step "
                      "has 11. Nothing else in mode 4 writes `unk_20` (the call-graph closure of the step functions and "
                      "of `BattleScene_Update`'s per-frame calls: only the step functions, `BattleScene_BeginBattlePhase` "
                      "and `BattleScene_ApplyCameraPhaseDefaults`, which leaves mode 4); `BattleScene_BeginBattlePhase`'s "
                      "other caller is `BattleScene_Phase4` (camera mode 2). So step 10, `BattleScene_IntroStepNop`, "
                      "never runs."})

    # 7. send-out step 5: step 4 (BattleScene_CompleteSendOutPresentation) adds 1 and then always stores another step
    cso = func_body(src_of(be, "battle_engine_309ED0.c"), "BattleScene_CompleteSendOutPresentation")
    fin = func_body(src_of(be, "battle_engine_309ED0.c"), "BattleScene_SendOutPresentationFinalize")
    ok = (cso.count("unk_20++") == 1 and fin.rstrip().endswith("sBattleScene.unk_00->unk_20 = 6;") and
          "unk_20 = 8;\n        return;" in fin)
    P.append({"title": "send-out step 5", "edges": [("BattleScene_PhaseSendOut", "func_84321D34")], "ok": ok,
              "text": "`BattleScene_PhaseSendOut`'s step 4 (`BattleScene_CompleteSendOutPresentation`) does `unk_20++` "
                      "and then, in every branch, stores step 0 (with phase 5 or 4), step 8, or calls "
                      "`BattleScene_SendOutPresentationFinalize`, which ends in step 8 or step 6; so step 5 "
                      "(`func_84321D34`) is never current at the next frame."})

    # 8. battle_anim_slots.md's values no code produces (anim_slots.py checks them on the source)
    P.append({"title": "battle_anim_slots.md: values no code produces", "ok": True,
              "edges": [("BattleScene_UpdatePhase", "BattleScene_PhaseCheckFaintedEndTurn"),
                        ("BattleScene_UpdatePhase", "BattleScene_PhaseSendOutAfterFaint"),
                        ("BattleScene_SelectNextIntroCameraShot", "BattleScene_IntroCameraStep12Or14"),
                        ("BattleScene_UpdateIntroCameraSequence", "BattleScene_IntroCameraWideHoldStep"),
                        ("BattleScene_Update", "func_843297E8"), ("BattleScene_Update", "func_843297F0"),
                        ("BattleScene_RunFinalPresentation", "BattleScene_FinalPresentationFrameActiveOwner"),
                        ("BattleScene_RunFinalPresentation", "BattleScene_FinalPresentationFrameOwner0"),
                        ("Battle_FrameCallback", "BattleScene_UpdatePausedFrame")],
              "names": ["BattleAnim_DefaultStartEffect_Draw", "BattleAnim_DefaultEndEffect_Draw",
                        "BattleAnim_EndEffect16_NoOpDraw"],
              "text": "Scene phase 33 (and 34, set only inside 33), turn-camera steps 12-15, scene camera modes 0 and 3, "
                      "the pause flag (only ever 0), and the draws paired with the default updates that finish on their "
                      "first call: proven in battle_anim_slots.md, section \"Values no code produces\". Checked again "
                      "here: no `BattleScene*` pointer other than `sBattleScene.unk_00` exists in the decomp, so those "
                      "fields have no other writer."})
    return P


def main():
    cg_path, fr_path, rom_path, dd_paths, out_md, out_json = sys.argv[1:7]
    run_dirs = sys.argv[7:]
    cg = json.load(open(cg_path))
    F = cg["functions"]
    fr = json.load(open(fr_path))
    unreached = set(fr["unreached"])
    internal = {n for n in F if not F[n]["external"] and re.match(r"(game/)?src/(?!port/)", F[n]["file"])}
    reached = internal - unreached

    s315 = strip_comments(open(os.path.join(GAME, "fragments", "battle_engine", "battle_engine_315D50.c")).read())
    children = []
    for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(\w+),\s*(\w+),"
                         r"\s*&?(\w+(?:\[\d+\])?),?\s*\}", body(s315, "ParticleDescriptorChild gParticleDescriptorChildren[]")):
        children.append({"u0": int(m.group(1), 0), "tex": m.group(4), "draw": m.group(5), "asset": m.group(6)})
    descs = []
    for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+|\d+),\s*([^,}]+?),?\s*\}",
                         body(s315, "ParticleDescriptor gBattleAnimParticleDescriptors[]")):
        kind, val = int(m.group(1), 0), m.group(2).strip()
        c = re.fullmatch(r"&gParticleDescriptorChildren\[(\d+)\]", val)
        descs.append({"kind": kind, "child": int(c.group(1)) if c else None, "func": None if c else val})
    assert descs[-1]["kind"] == 0, "descriptor table does not end with the {0, NULL} terminator"
    descs = descs[:-1]

    # literal descriptor references, by enclosing function, in the whole decomp
    users = collections.defaultdict(set)   # descriptor index -> functions holding &gBattleAnimParticleDescriptors[N]
    other_uses = []
    for dp, _, fs in os.walk(GAME):
        for f in fs:
            if not f.endswith(".c"):
                continue
            src = strip_comments(open(os.path.join(dp, f)).read())
            if not any(t in src for t in TABLES):
                continue
            fns = functions_of(src)
            for m in re.finditer(r"\b(%s)\b(\[[^\]]*\])?" % "|".join(TABLES), src):
                pos = m.start()
                enc = next((n for n, a, b in fns if a <= pos <= b), None)
                lit = re.fullmatch(r"\[(0x[0-9A-Fa-f]+|\d+)\]", m.group(2) or "")
                pre = src[max(0, pos - 1):pos]
                if enc is None:
                    if f == "battle_engine_315D50.c":
                        continue  # the tables themselves (definition, children pointers)
                    other_uses.append("%s: %s (outside a function)" % (f, m.group(0)))
                elif m.group(1) == TABLES[0] and lit and pre == "&":
                    users[int(lit.group(1), 0)].add(enc)
                else:
                    other_uses.append("%s: %s in %s" % (f, m.group(0), enc))
    for i in users:
        assert i < len(descs), "descriptor %d past the table" % i

    proofs = value_proofs(children, descs, users)
    dead_edges = {e for p in proofs if p["ok"] for e in p["edges"]}
    cut = {b for a, b in dead_edges}  # (a function whose every reference is such a call site is dead too)

    # static references, the descriptor tables replaced by the descriptor users, minus the call sites the value
    # proofs show never execute
    refs = collections.defaultdict(set)
    for a, b, k, via in cg["edges"]:
        if a != b and not (k == "table" and via in TABLES) and (a, b) not in dead_edges:
            refs[b].add(a)
    for t, fns in cg["tables"].items():
        if t in TABLES:
            continue
        for fn in fns:
            refs[fn].add("table " + t)
    named_by = collections.defaultdict(set)  # function -> descriptor indices naming it
    for i, d in enumerate(descs):
        fns = [d["func"]] if d["func"] else [children[d["child"]]["tex"], children[d["child"]]["draw"]]
        for fn in fns:
            named_by[fn].add(i)
    for fn, idx in named_by.items():
        for i in idx:
            refs[fn] |= {"desc %d" % i}
    desc_users = {"desc %d" % i: users.get(i, set()) for i in range(len(descs))}

    # dead seeds
    rom = open(rom_path, "rb").read()
    addr = deadcode.addresses()
    all_refs = collections.defaultdict(set)
    for a, b, k, via in cg["edges"]:
        if a != b:
            all_refs[b].add(a)
    for t, fns in cg["tables"].items():
        for fn in fns:
            all_refs[fn].add(t)
    forced = set(re.findall(r"^\| (\w+) \| [^|]+ \| `[^`]*` \|", open(os.path.join(
        ROOT, "evidence", "whole_game", "dead_calls.md")).read(), re.M))
    # (an entry point, e.g. a fragment's main, has no reference either: among reached functions only the ones
    # dead_calls.md called by force count)
    genuine, count = set(), collections.Counter()
    for r in run_dirs:
        for dp, _, fs in os.walk(r):
            if "dead_calls" in dp:
                continue
            if "coverage.json" in fs:
                genuine |= set(json.load(open(os.path.join(dp, "coverage.json")))["functions"])
            for f in fs:
                if f.endswith(".profdata") or f.startswith("coverage.profraw"):
                    out = subprocess.run(["xcrun", "llvm-profdata", "show", "--all-functions", os.path.join(dp, f)],
                                         capture_output=True, text=True).stdout
                    name = None
                    for line in out.splitlines():
                        m = re.match(r"  (\S+):$", line)
                        if m:
                            name = m.group(1)
                        m = re.match(r"\s+Function count: (\d+)", line)
                        if m and name:
                            count[name] += int(m.group(1))
    forced_only = {n for n in forced if n in reached and n not in genuine and count[n] == 1}
    def rom_word(v):
        """offsets of the 4-aligned ROM words equal to v (the battle and particle fragments sit uncompressed in the
        ROM: their tables' function pointers are aligned words, e.g. the descriptor table at 0x370A90, so a pointer
        to a function is an aligned word; unaligned matches are bytes inside other data)"""
        out, i = [], rom.find(struct.pack(">I", v))
        while i >= 0:
            if i % 4 == 0:
                out.append(i)
            i = rom.find(struct.pack(">I", v), i + 1)
        return out
    noref = {n for n in internal if not all_refs.get(n) and (n not in addr or not rom_word(addr[n]))
             and (n in unreached or n in forced_only)}
    under_forced = reached - forced_only - genuine  # ran only below a forced call (in no other run's coverage.json)
    reached -= forced_only | under_forced
    data_dead = set()
    for p in dd_paths.split(","):
        for names in json.load(open(p)).values():
            data_dead |= set(names)
    seed = noref | data_dead | {n for n in internal if "PAL" in n and n in unreached}
    seed |= {n for p in proofs if p["ok"] for n in p.get("names", [])}
    dead, why = set(seed), {}
    for p in proofs:
        for n in p.get("names", []):
            why[n] = "value proof: " + p["title"]
    for n in noref:
        why[n] = "referenced by nothing" + (" (ran only by the forced call of dead_calls.md)" if n in forced_only else "")
    for n in data_dead - noref:
        why[n] = "data_dead.json"
    is_dead = lambda r: (r in dead) if not r.startswith(("desc ", "table ")) else (
        r.startswith("desc ") and all(u in dead for u in desc_users[r]))
    changed = True
    while changed:
        changed = False
        for n in internal:
            if n in dead:
                continue
            rs = refs.get(n, set())
            if (rs or n in cut) and all(is_dead(r) for r in rs):
                dead.add(n)
                why[n] = ", ".join(sorted(rs))
                changed = True

    # report
    desc_state = {}
    for i in range(len(descs)):
        u = users.get(i, set())
        live_u = sorted(x for x in u if x not in dead)
        desc_state[i] = ("ran" if any(x in reached for x in live_u) else
                         "live caller" if live_u else "dead callers" if u else "no reference")
    contradictions = sorted(n for n in dead if n in reached)

    removed = collections.defaultdict(list)  # callee -> the value proofs that removed a call site of it
    for p in proofs:
        for a, b in p["edges"]:
            removed[b].append(p["title"])
    tdc = open(os.path.join(ROOT, "evidence", "whole_game", "dead_classes.md")).read()
    targets = []
    for area in ("battle_engine", "particle_math", "particle_data_library"):
        m = re.search(r"^\| %s \| \d+ \| (.*) \|$" % area, tdc, re.M)
        targets += [(area, x.strip()) for x in m.group(1).split(",")]
    live_all = []  # every live function of dead_classes.md (all areas)
    sec = tdc[tdc.index("## Live, per area"):]
    sec = sec[:sec.index("\n## ", 5)] if "\n## " in sec[5:] else sec
    for m in re.finditer(r"^\| (\S+) \| \d+ \| (.*) \|$", sec, re.M):
        live_all += [(m.group(1), x.strip()) for x in m.group(2).split(",")]
    tnames = {n for _, n in targets}
    extra = [(a, n) for a, n in live_all if n not in tnames and n in dead and n not in reached]

    def reason(n):
        r = why.get(n, "")
        if n not in noref and n not in data_dead:  # a list of references: say why each is dead
            tag = lambda x: ("descriptor %s (%s)" % (x[5:], desc_state[int(x[5:])]) if x.startswith("desc ") else
                             x + (" (ran only by its forced call)" if x in forced_only else
                                  " (referenced by nothing)" if x in noref else ""))
            r = ", ".join(tag(x.strip()) for x in r.split(",")) if r else r
        if removed.get(n):
            r += ("; " if r else "") + "call sites removed by: " + ", ".join(sorted(set(removed[n])))
        return r

    L = []
    w = L.append
    w("# Battle particle descriptors the shipped data can select")
    w("")
    w("Generated by `routes/particles/particle_descriptors.py <cg_all4.json> <frontier.json> <US ROM> "
      "<data_dead.json,...> <out.md> <out.json> <scratchpad with the runs>` from the particle tables in `battle_engine_315D50.c` and the "
      "literal descriptor references in the whole decomp, checked against the runs (frontier: %d of %d functions "
      "reached)." % (len(reached), len(internal)))
    w("")
    w("## The data")
    w("")
    w("`gBattleAnimParticleDescriptors` (%d entries and the `{0, NULL}` terminator): kind 1 points at a child of "
      "`gParticleDescriptorChildren` (%d entries: texture-load callback, draw callback, asset), kind 3 holds a draw "
      "function. These are the only particle descriptor tables in the decomp: `particle_math` has only "
      "`gDefaultParticleDescriptor` (`Particle_NoopDraw`), and the minigame particles (`MiniFx_*`) use emitters "
      "without descriptors. A descriptor is chosen only by a literal `&gBattleAnimParticleDescriptors[N]` passed to "
      "an emitter or effect-slot call (%d literals in %d functions); other uses of the tables: %s." % (
          len(descs), len(children), sum(len(v) for v in users.values()), len({x for v in users.values() for x in v}),
          "; ".join(other_uses) or "none"))
    w("")
    w("A descriptor's state: **ran** (a function holding its literal ran in a run), live caller (a holder is not "
      "proven dead but did not run), dead callers (every holder is dead code), no reference.")
    w("")
    w("| # | kind | child | texture callback | draw callback | held by | state |")
    w("|---|---|---|---|---|---|---|")
    mark = lambda f: "`%s`%s" % (f, " **ran**" if f in reached else
                                 " (dead)" if f in dead else "")
    for i, d in enumerate(descs):
        if d["func"]:
            tex, drw, ch = "-", mark(d["func"]), "-"
        else:
            c = children[d["child"]]
            tex, drw, ch = mark(c["tex"]), mark(c["draw"]), str(d["child"])
        u = sorted(users.get(i, set()))
        held = ", ".join(mark(x) for x in u[:4]) + (", ... (%d)" % len(u) if len(u) > 4 else "") if u else "-"
        w("| %d | %d | %s | %s | %s | %s | %s |" % (i, d["kind"], ch, tex, drw, held, desc_state[i]))
    w("")
    unused_children = [j for j in range(len(children)) if not any(d["child"] == j for d in descs)]
    w("Children no descriptor names: %s." % (", ".join("%d (`%s`, `%s`)" % (j, children[j]["tex"], children[j]["draw"])
                                                       for j in unused_children) or "none"))
    w("")
    # callbacks by descriptor selectability
    cb = sorted(named_by)
    w("## Particle callbacks and the descriptors naming them")
    w("")
    w("| callback | descriptors | selectable (a holder not dead) | ran |")
    w("|---|---|---|---|")
    for fn in cb:
        idx = sorted(named_by[fn])
        sel = [i for i in idx if desc_state[i] in ("ran", "live caller")]
        w("| `%s` | %s | %s | %s |" % (fn, ", ".join(map(str, idx)), ", ".join(map(str, sel)) or "**none**",
                                      "yes" if fn in reached else "no"))
    w("")
    w("## Call sites no shipped value reaches")
    w("")
    w("Each claim is checked on the decomp source by the script (a failed check drops the claim); the listed call "
      "sites are then left out of the references.")
    w("")
    for p in proofs:
        w("- **%s**%s: %s Call sites: %s.%s" % (
            p["title"], "" if p["ok"] else " (CHECK FAILED, not used)", p["text"],
            ", ".join("`%s` -> `%s`" % e for e in p["edges"]) or "-",
            (" Dead outright: %s." % ", ".join("`%s`" % n for n in p["names"])) if p.get("names") else ""))
    w("")
    w("## The live functions of the task (dead_classes.md: battle_engine, particle_math, particle_data_library)")
    w("")
    w("| area | function | verdict | why (its references, all dead) / its live references |")
    w("|---|---|---|---|")
    data_dead_out = []
    for area, n in targets:
        if n in reached:
            w("| %s | `%s` | ran | |" % (area, n))
        elif n in dead:
            data_dead_out.append(n)
            w("| %s | `%s` | **dead** | %s |" % (area, n, reason(n)))
        else:
            rs = sorted(r for r in refs.get(n, set()) if not is_dead(r))
            w("| %s | `%s` | live | %s |" % (area, n, ", ".join(rs) or "(no reference)"))
    w("")
    nlive = sum(1 for _, n in targets if n not in reached and n not in dead)
    w("Left live: %d (what selects them and the runs that tried: `routes/particles/README.md`)." % nlive)
    w("")
    w("Other live functions of dead_classes.md the same test settles (%d): %s." % (
        len(extra), "; ".join("`%s` (%s: %s)" % (n, a, reason(n)) for a, n in extra) or "none"))
    w("")
    w("Check against the runs: %s. Functions the frontier counts as reached only because dead_calls.md called them "
      "by force: %d, and %d more that ran only below such a call (counted as not run here)." % (
          "no function found dead ran, except by a forced call" if not contradictions else
          "CONTRADICTION, ran: " + ", ".join(contradictions), len(forced_only), len(under_forced)))
    w("")
    open(out_md, "w").write("\n".join(L) + "\n")
    if not contradictions:
        json.dump({"particle descriptors no code selects, callbacks only dead callers register, and call sites no "
                   "shipped value reaches (particle_descriptors.md)":
                       sorted(set(data_dead_out) | {n for a, n in extra if a == "minigame_actor_framework"}),
                   "callers reached only by dead_calls.md's forced calls, or no aligned ROM word with the address "
                   "(particle_descriptors.md, other areas)":
                       sorted(n for a, n in extra if a != "minigame_actor_framework")},
                  open(out_json, "w"), indent=1)
        open(out_json, "a").write("\n")
    print("forced-only %d, below them %d; descriptors %d (ran %d, live caller %d, dead callers %d, unreferenced %d); targets dead %d of %d; "
          "other areas %d; contradictions %s" % (len(forced_only), len(under_forced), len(descs),
                                             *[sum(1 for s in desc_state.values() if s == k) for k in
                                               ("ran", "live caller", "dead callers", "no reference")],
                                             len(data_dead_out), len(targets), len(extra), contradictions or 0))


main()
