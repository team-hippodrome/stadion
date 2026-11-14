#!/usr/bin/env python3 -I
"""Every move and its animation / code paths, run in the port and in the original game in ares with the same forced
random values, compared event by event and frame by frame (the method of tools/battle_xcheck.py).

Usage:
  tools/battle_sweep.py paths [--move N|NAME]            the path catalog derived from the decomp (no runs)
  tools/battle_sweep.py run [--move N|NAME | --case ID] [--path P | --all-paths] [--user SPECIES] [--jobs 2] [--force]
                            [--open] [--port-bin build-xcheck/pokestadium]
  tools/battle_sweep.py report [--all-paths]             build-xcheck-sweep/report.md from the cached cases
By default each move runs on its base path, with additional injured-user healing cases and its sound compared
(the battle music is switched off on both sides, so the move's sound effects are what is heard); --all-paths runs
every path of the catalog.

Work and results go to build-xcheck-sweep/ (XCHECK_SWEEP_DIR overrides): cases/<id>/result.json (+ sheets of
mismatching animations). Finished cases are cached: a rerun only runs cases that are missing or whose spec changed
(--force reruns them). --open reruns only the cases that are not finished yet or whose last result has a use
that is not a match (the report's verdict: animation, logic or sound mismatch, not reached, error), e.g. after a
fix, without the cases that already match; a full run is still the check that nothing that matched regressed.
Frames and sound dumps are deleted after each case (XCHECK_KEEP_FRAMES=1 keeps them).

Paths per move come from the decomp: the move table gMoveDataWithZeroth (effect, power, type, accuracy) and the
type chart sTypeEffects decide which variants exist (crit, miss, super / not very effective, immune, secondary
effect lands / fails, multi-hit counts, two-turn, recharge, self-KO, Substitute, Metronome / Mirror Move, status on
an already statused target, ...). Each variant is forced through the case file (src/port/xcheck.cpp has the format):
the parties (user and target written when the battle engine is mapped) and forced random values per caller
(Battle_Random@Battle_CheckCriticalHit 0 = critical hit, Battle_RandomAccuracy@HandleMoveEffectFlags 255 = miss, ...).
The player's lead knows up to four test moves with 1 PP each: the game's time-limit fallback (PORT_XCHECK_COM) uses
them on consecutive turns. The opponent is a sturdy target (999 HP) that only knows Splash unless the path needs it
to act (Substitute, Mirror Move, Counter, invulnerable turn).
"""
import hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile, time, zipfile
from pathlib import Path
from functools import lru_cache
from concurrent.futures import ProcessPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import battle_xcheck as bx  # noqa: E402
from runner_platform import BACKENDS, actual_backend

WORK = os.path.abspath(os.environ.get("XCHECK_SWEEP_DIR", os.path.join(ROOT, "build-xcheck-sweep")))
SEED = "1"
# game frames of each move animation that are captured and scored (its length is compared in full, from the trace).
# Everything is counted in battle game frames (BattleScene_UpdateFrame calls): the N64 slows down in busy effects (2-3
# VI frames per game frame) and the port does not, so VI frame counts differ although both play the same frames.
ANIM_FRAMES = 150
STATE_FRAME = 2240  # ares save state: after the team picks, before the confirming A (port frame 2263)

# pokered effect constants (the decomp's move table uses them; checked against e.g. Fly 0x2B, Bind 0x2A, Guillotine 0x26)
EFFECTS = {
    0x00: "none", 0x02: "poison_chance", 0x03: "drain", 0x04: "burn_chance", 0x05: "freeze_chance",
    0x06: "paralyze_chance", 0x07: "explode", 0x08: "dream_eater", 0x09: "mirror_move", 0x0A: "attack_up",
    0x0B: "defense_up", 0x0C: "speed_up", 0x0D: "special_up", 0x0E: "accuracy_up", 0x0F: "evasion_up",
    0x10: "pay_day", 0x11: "swift", 0x12: "attack_down", 0x13: "defense_down", 0x14: "speed_down",
    0x15: "special_down", 0x16: "accuracy_down", 0x17: "evasion_down", 0x18: "conversion", 0x19: "haze",
    0x1A: "bide", 0x1B: "thrash", 0x1C: "switch_teleport", 0x1D: "multi_hit", 0x1E: "effect_1e",
    0x1F: "flinch_chance", 0x20: "sleep", 0x21: "poison_chance2", 0x22: "burn_chance2", 0x23: "freeze_chance2",
    0x24: "paralyze_chance2", 0x25: "flinch_chance2", 0x26: "ohko", 0x27: "charge", 0x28: "super_fang",
    0x29: "fixed_damage", 0x2A: "trap", 0x2B: "fly_dig", 0x2C: "attack_twice", 0x2D: "jump_kick", 0x2E: "mist",
    0x2F: "focus_energy", 0x30: "recoil", 0x31: "confuse", 0x32: "attack_up2", 0x33: "defense_up2",
    0x34: "speed_up2", 0x35: "special_up2", 0x36: "accuracy_up2", 0x37: "evasion_up2", 0x38: "heal",
    0x39: "transform", 0x3A: "attack_down2", 0x3B: "defense_down2", 0x3C: "speed_down2", 0x3D: "special_down2",
    0x3E: "accuracy_down2", 0x3F: "evasion_down2", 0x40: "light_screen", 0x41: "reflect", 0x42: "poison",
    0x43: "paralyze", 0x44: "attack_down_chance", 0x45: "defense_down_chance", 0x46: "speed_down_chance",
    0x47: "special_down_chance", 0x4C: "confuse_chance", 0x4D: "twineedle", 0x4F: "substitute", 0x50: "hyper_beam",
    0x51: "rage", 0x52: "mimic", 0x53: "metronome", 0x54: "leech_seed", 0x55: "splash", 0x56: "disable",
}
CHANCE = {0x02, 0x04, 0x05, 0x06, 0x1F, 0x21, 0x22, 0x23, 0x24, 0x25, 0x44, 0x45, 0x46, 0x47, 0x4C}
STATUS_ON_TARGET = {0x20: ("status", 3), 0x42: ("status", 0x08), 0x43: ("status", 0x40)}  # sleep 3 turns, PSN, PAR
NO_CRIT = {0x26, 0x28, 0x29}  # OHKO, Super Fang, fixed damage (and moves without power)
NEVER_MISS = {0x11}
# effects that need their own battle (lock the user, end it, change the battle) and the turns they take
SOLO = {0x07: 2, 0x51: 3, 0x1A: 5, 0x1B: 4, 0x2A: 4, 0x27: 3, 0x2B: 3, 0x50: 3, 0x39: 2, 0x53: 3, 0x09: 2, 0x52: 2,
        0x1C: 2, 0x56: 2, 0x08: 2, 0x26: 2, 0x4F: 2}
COUNTER = 0x44  # Counter's move id (effect 0)
KNOWN = {0x49: "Leech Seed: its turn is 76-97 frames longer on the N64",
         0x4B: "Razor Leaf: its turn is ~97 frames longer on the N64",
         0x3B: "Blizzard: lowest animation similarity (0.53) in the first sweep"}
TACKLE, SPLASH, SUBSTITUTE = 0x21, 0x96, 0xA4
TYPES = {0: "Normal", 1: "Fighting", 2: "Flying", 3: "Poison", 4: "Ground", 5: "Rock", 7: "Bug", 8: "Ghost",
         0x14: "Fire", 0x15: "Water", 0x16: "Grass", 0x17: "Electric", 0x18: "Psychic", 0x19: "Ice", 0x1A: "Dragon"}
# targets, in order of preference (sturdy models): Chansey, Snorlax, Slowbro, ...; one per needed matchup is picked
TARGET_POOL = [113, 143, 80, 76, 112, 131, 91, 130, 149, 94, 59, 103, 45, 34, 26, 121, 68, 62, 101, 18, 22, 136, 135, 134,
               87, 148, 142, 127, 123, 115, 110, 105, 85, 75, 73, 71, 51, 31, 6, 3, 9]


# ---- decomp tables ----
def decomp_tables():
    src = Path(ROOT, "game", "src", "gb_data.c").read_text(encoding="utf-8")
    body = src[src.index("gMoveDataWithZeroth[166] = {"):]
    body = body[:body.index("};")]
    moves = {}
    for m in re.finditer(r"\{\s*(\d+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+)\s*\}", body):
        i, eff, pw, ty, acc, pp = (int(x, 0) for x in m.groups())
        moves[i] = {"id": i, "effect": eff, "power": pw, "type": ty, "acc": acc, "pp": pp}
    body = src[src.index("SpeciesData gSpeciesData[152] = {"):]
    body = body[:body.index("};")]
    species = {}
    for m in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*\w+,\s*\w+,\s*"
                         r"\{([^}]*)\}", body):
        g = [int(x, 0) for x in m.groups()[:8]]
        species[g[0]] = {"id": g[0], "hp": g[1], "atk": g[2], "def": g[3], "spd": g[4], "spc": g[5], "types": (g[6], g[7]),
                         "start": [int(x, 0) for x in m.group(9).split(",")]}
    be = Path(ROOT, "game", "src", "fragments", "battle_engine", "battle_engine_mechanics.c").read_text(encoding="utf-8")
    body = be[be.index("u8 sTypeEffects[] = {"):]
    nums = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", body[body.index("{"):body.index("};")])]
    chart = {}
    for i in range(0, len(nums) - 2, 3):
        if nums[i] == 0xFF:
            break
        chart[(nums[i], nums[i + 1])] = {0x14: 2.0, 5: 0.5, 0: 0.0}.get(nums[i + 2], 1.0)
    return moves, species, chart


def effectiveness(chart, mtype, types):
    m = 1.0
    for t in set(types):
        m *= chart.get((mtype, t), 1.0)
    return m


# ---- path catalog ----
def paths_for(mv, chart, species):
    """[(path, why)] for one move, from its effect, power, type and accuracy"""
    e, power = mv["effect"], mv["power"]
    out = [("base", "every move: used normally (hit, no critical hit, secondary effect lands)")]
    targets_opponent = power > 0 or e in STATUS_ON_TARGET or e in (0x31, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x3A, 0x3B,
                                                                   0x3C, 0x3D, 0x3E, 0x3F, 0x54, 0x56, 0x08, 0x1C, 0x52)
    if power > 0 and e not in NO_CRIT:
        out.append(("crit", "damaging move: Battle_CheckCriticalHit decides"))
    if targets_opponent and e not in NEVER_MISS and mv["acc"] < 0x100:
        out.append(("miss", "accuracy check (HandleMoveEffectFlags: Battle_RandomAccuracy >= accuracy)"))
    if mv["id"] == COUNTER:  # damage from the last hit taken: no type chart, no critical hit
        return [o for o in out if o[0] in ("base", "miss")] + [("after_tackle", "the target hit the user with a Normal "
                                                                "move first (Counter returns double)")]
    if e == 0x54:
        out.append(("immune", "Leech Seed does not affect Grass types"))
    mults = {effectiveness(chart, mv["type"], s["types"]) for s in species.values()}
    if e in STATUS_ON_TARGET and 0.0 in mults:
        out.append(("immune", "type chart: the target's type is immune (e.g. Thunder Wave vs Ground)"))
    if e == 0x42:
        out.append(("immune", "Poison types cannot be poisoned"))
    if power > 0:
        if e not in (0x29, 0x28):  # fixed damage / Super Fang ignore effectiveness (immunity still applies)
            if 2.0 in mults or 4.0 in mults:
                out.append(("super_effective", "type chart: a target type takes 2x"))
            if 0.5 in mults or 0.25 in mults:
                out.append(("not_very_effective", "type chart: a target type takes 0.5x"))
        if 0.0 in mults:
            out.append(("immune", "type chart: a target type takes 0x"))
    if e in CHANCE:
        out.append(("effect_fails", "secondary effect chance roll fails"))
    if e == 0x1D:
        out += [("hits_%d" % n, "BattleEffect_MultiHit: %d hits" % n) for n in (2, 3, 4, 5)]
    if e in STATUS_ON_TARGET:
        out.append(("already_statused", "target already has the status (move fails)"))
        if e == 0x20:
            out.append(("sleep_clause", "Stadium's Sleep Clause (another opponent Pokemon asleep): not set up"))
    if e == 0x08:
        out.append(("target_asleep", "Dream Eater works only on a sleeping target"))
    if e == 0x2B or mv["id"] == 0x5B:  # Fly; Dig is a charge move whose charge turn is semi-invulnerable
        out.append(("invulnerable", "the target attacks during the semi-invulnerable turn and misses"))
    if e == 0x26:
        out.append(("target_faster", "OHKO fails against a faster target"))
    if e == 0x30:
        out.append(("recoil_faint", "the user faints from recoil"))
    if e == 0x09:
        out.append(("mirrors_tackle", "the target used Tackle first; Mirror Move copies it"))
    if e == 0x53:
        out += [("calls_%d" % m, "Battle_Effect_Metronome draws move %d" % m) for m in (0x55, 0x13, 0x0E, 0x78)]
    if power > 0:
        out.append(("target_faints", "the target faints from the hit (1 HP)"))
        out.append(("substitute", "the target's Substitute takes the hit"))
    elif targets_opponent and e not in (0x1C,):
        out.append(("substitute", "the target's Substitute blocks the move"))
    seen, uniq = set(), []
    for p_, w in out:
        if p_ not in seen:
            seen.add(p_)
            uniq.append((p_, w))
    return uniq


GENERIC = [("user_paralyzed", TACKLE, "the user is fully paralyzed (Battle_CheckMonCanAct)"),
           ("user_asleep", TACKLE, "the user is asleep and cannot move"),
           ("user_frozen", TACKLE, "the user is frozen"),
           ("struggle", TACKLE, "no PP left: Struggle")]


# ---- cases ----
def pick_user(mv, species):
    for s in species.values():
        if mv["id"] in s["start"] and s["id"] <= 151:
            return s["id"], "starting move"
    for s in species.values():
        if mv["type"] in s["types"] and s["id"] <= 151:
            return s["id"], "same type"
    return 151, "Mew"


def pick_target(mv, chart, species, want):
    """a target species for the matchup: 'neutral', 'se', 'nve', 'immune', or a fixed type"""
    for sid in TARGET_POOL:
        m = effectiveness(chart, mv["type"], species[sid]["types"])
        if (want == "neutral" and m == 1.0) or (want == "se" and m >= 2.0) or (want == "nve" and 0 < m <= 0.5) or \
                (want == "immune" and m == 0.0):
            return sid
    for sid, s in species.items():
        m = effectiveness(chart, mv["type"], s["types"])
        if (want == "neutral" and m == 1.0) or (want == "se" and m >= 2.0) or (want == "nve" and 0 < m <= 0.5) or \
                (want == "immune" and m == 0.0):
            return sid
    return None


def plan(moves, species, chart, only_move=None, only_path=None, user_override=None):
    """the cases: each a battle with up to four (move, path) uses sharing the same setup"""
    groups = {}  # key -> case dict
    open_group = {}  # setup key -> the group still taking uses
    skipped = []
    order = sorted(moves, key=lambda i: (i not in KNOWN, i))
    for mid in order:
        if mid == 0xA5:  # Struggle: reached through the generic 'struggle' case
            continue
        mv = moves[mid]
        if only_move and mid != only_move:
            continue
        for path, why in paths_for(mv, chart, species):
            if only_path and path != only_path:
                continue
            e = mv["effect"]
            want = {"super_effective": "se", "not_very_effective": "nve", "immune": "immune"}.get(path, "neutral")
            if path == "immune" and e == 0x54:
                target = next(sid for sid in TARGET_POOL if 0x16 in species[sid]["types"])  # a Grass type
            elif path == "immune" and e == 0x42:
                target = next(sid for sid in TARGET_POOL if 3 in species[sid]["types"])     # a Poison type
            else:
                target = pick_target(mv, chart, species, want)
            if path == "sleep_clause":
                skipped.append((mid, path, "needs a second opponent put to sleep first; not set up"))
                continue
            if target is None:
                skipped.append((mid, path, "no target species with that matchup"))
                continue
            user, user_why = (user_override, "--user") if user_override else pick_user(mv, species)
            tgt = {"species": target, "moves": [SPLASH], "spd": 20, "hp": 999, "status": 0}
            usr = {"species": user, "hp": 300, "status": 0}
            rules = {"Battle_RandomAccuracy@HandleMoveEffectFlags": [0], "Battle_Random@Battle_CheckCriticalHit": [255]}
            for caller in ("BattleEffect_SecondaryStatusChance", "BattleEffect_Flinch", "BattleEffect_LowerStat",
                           "BattleEffect_ConfuseChance", "BattleEffect_Poison"):
                rules["Battle_Random@" + caller] = [0]
            turns = SOLO.get(e, 1)
            solo = e in SOLO or mid == COUNTER or path in ("recoil_faint", "invulnerable", "mirrors_tackle", "after_tackle",
                                                           "target_faster", "target_asleep") or path.startswith("calls_")
            limit = 3 if path == "target_faints" else 4  # each use knocks out one of the opponent's three
            if path == "crit":
                rules["Battle_Random@Battle_CheckCriticalHit"] = [0]
            if path == "miss":
                rules["Battle_RandomAccuracy@HandleMoveEffectFlags"] = [255]
                turns = min(turns, 2)
            if path == "effect_fails":
                for k in list(rules):
                    if k.startswith("Battle_Random@Battle_Effect"):
                        rules[k] = [255]
            if path.startswith("hits_"):
                rules["Battle_Random@BattleEffect_MultiHit"] = {2: [0], 3: [1], 4: [2, 2], 5: [3, 3]}[int(path[5:])]
            if path == "already_statused":
                tgt["status"] = 7 if e == 0x20 else STATUS_ON_TARGET[e][1]  # asleep for the whole case
            if path == "target_asleep":
                tgt["status"] = 3
            if path == "base" and e == 0x08:
                tgt["status"] = 3  # Dream Eater needs a sleeping target to play its animation.
            if path in ("invulnerable", "mirrors_tackle", "after_tackle"):
                tgt["moves"] = [TACKLE]
                tgt["spd"] = 300 if path != "invulnerable" else 20
            if path == "base" and e == 0x09:
                tgt["moves"] = [TACKLE]
                tgt["spd"] = 300
            if e == 0x1A:  # Bide needs damage to store
                tgt["moves"] = [TACKLE]
            if path == "target_faster":
                tgt["spd"] = 300
            if path == "recoil_faint":
                usr["hp"] = 1
            if path == "target_faints":
                tgt["hp"] = 1
            if path == "substitute":
                tgt["moves"] = [SUBSTITUTE]
                tgt["spd"] = 300
                turns = max(turns, 1)
            if path.startswith("calls_"):
                rules["Battle_Random@Battle_Effect_Metronome"] = [int(path[6:])]
            if path == "base" and e == 0x53:
                rules["Battle_Random@Battle_Effect_Metronome"] = [0x55]
            if e == 0x1B:
                rules["Battle_Random@BattleEffect_Thrash"] = [0]
            if e == 0x2A:
                rules["Battle_Random@BattleEffect_Trap"] = [0]
            if e == 0x1A:
                rules["Battle_Random@BattleEffect_Bide"] = [0]
            key = json.dumps([path if solo else "batch:" + path, tgt, usr["status"], usr["hp"], sorted(rules.items()),
                              mid if solo else None], sort_keys=True)
            c = open_group.get(key)
            if c is None or len(c["uses"]) >= limit:
                c = open_group[key] = {"path": path, "target": tgt, "user": usr, "rules": rules, "uses": [], "turns": 0}
                groups["%s#%d" % (key, len(groups))] = c
            c["uses"].append({"move": mid, "path": path, "why": why, "user": user, "user_why": user_why})
            if path == "base" and e in (0x1A, 0x08, 0x09, 0x53):
                c["uses"][-1]["require_animation"] = True
            if e == 0x09 and path in ("base", "mirrors_tackle"):
                c["uses"][-1]["animation_move"] = TACKLE
            if e == 0x53 and path == "base":
                c["uses"][-1]["animation_move"] = 0x55
            if e == 0x1A:
                # The automatic move picker must keep Bide selected while energy is stored and released.
                c["pp"] = turns
            c["turns"] += turns
    for gp, mid, why in GENERIC:
        move_id = 0xA5 if gp == "struggle" else mid
        # Struggle has no ordinary move-slot setup; its zero-PP case is its base
        # path and must be included when the default sweep checks every move.
        if (only_move and move_id != only_move) or (only_path and gp != only_path and
                                                not (gp == "struggle" and only_path == "base")):
            continue
        usr = {"species": 113, "hp": 300, "status": {"user_paralyzed": 0x40, "user_asleep": 3, "user_frozen": 0x20,
                                                      "struggle": 0}[gp]}
        rules = {"Battle_RandomAccuracy@HandleMoveEffectFlags": [0], "Battle_Random@Battle_CheckCriticalHit": [255],
                 "Battle_Random@Battle_CheckMonCanAct": [0]}
        groups[gp] = {"path": gp, "target": {"species": 143, "moves": [SPLASH], "spd": 20, "hp": 999, "status": 0},
                      "user": usr, "rules": rules, "turns": 3 if gp != "struggle" else 2,
                      "uses": [{"move": mid if gp != "struggle" else 0xA5, "path": gp, "why": why, "user": 113,
                                "user_why": "generic case"}], "pp0": gp == "struggle"}
    # Keep the original batches stable for cached comparisons. Healing at full HP takes the failure path, so
    # additionally exercise each healing move alone with missing HP; batching healers would refill the next user's HP.
    if only_path in (None, "base", "injured"):
        for mid, mv in moves.items():
            if mv["effect"] != 0x38 or (only_move and mid != only_move):
                continue
            user, user_why = (user_override, "--user") if user_override else pick_user(mv, species)
            groups["injured:%d" % mid] = {
                "path": "injured", "target": {"species": 113, "moves": [SPLASH], "spd": 20, "hp": 999, "status": 0},
                "user": {"species": user, "hp": 150, "status": 0},
                "rules": {"Battle_RandomAccuracy@HandleMoveEffectFlags": [0], "Battle_Random@Battle_CheckCriticalHit": [255]},
                "turns": 1, "uses": [{"move": mid, "path": "injured", "why": "healing succeeds with missing HP",
                                      "user": user, "user_why": user_why, "require_animation": True}]}
    cases = []
    for c in groups.values():
        u = c["uses"]
        # the user of the case: the first use's learner (animations follow the user's model)
        c["user"]["species"] = u[0]["user"]
        spec = json.dumps({k: c[k] for k in ("path", "target", "user", "rules", "turns")} |
                          {"moves": [x["move"] for x in u]} | ({"pp": c["pp"]} if "pp" in c else {}) |
                          ({"animation_moves": [x.get("animation_move", x["move"]) for x in u]}
                           if any("animation_move" in x for x in u) else {}), sort_keys=True)
        c["id"] = "%s_%s_%s" % ("-".join("%03d" % x["move"] for x in u), c["path"],
                                hashlib.sha1(spec.encode()).hexdigest()[:8])
        c["spec"] = spec
        cases.append(c)
    return cases, skipped


def case_file(c, species):
    us, ts = species[c["user"]["species"]], species[c["target"]["species"]]
    mv = [x["move"] for x in c["uses"]] + [0] * (4 - len(c["uses"]))
    pp = [0 if c.get("pp0") else (c.get("pp", 1) if m else 0) for m in mv]
    lines = ["# %s" % c["id"]]
    lines.append("mon 0 * species=%d type1=%d type2=%d m1=%d m2=%d m3=%d m4=%d p1=%d p2=%d p3=%d p4=%d hp=%d maxhp=300 "
                 "level=50 status=%d atk=150 def=150 spd=150 spc=150" % (
                     us["id"], us["types"][0], us["types"][1], *mv, *pp, c["user"]["hp"], c["user"]["status"]))
    tm = c["target"]["moves"] + [0] * (4 - len(c["target"]["moves"]))
    lines.append("mon 1 * species=%d type1=%d type2=%d m1=%d m2=%d m3=%d m4=%d p1=%d p2=%d p3=%d p4=%d hp=%d maxhp=999 "
                 "level=50 status=%d atk=20 def=300 spd=%d spc=300" % (
                     ts["id"], ts["types"][0], ts["types"][1], *tm, *[40 if m else 0 for m in tm], c["target"]["hp"],
                     c["target"]["status"], c["target"]["spd"]))
    for k, v in sorted(c["rules"].items()):
        lines.append("rule %s %s" % (k, ",".join(str(x) for x in v)))
    lines.append("stop %d" % (c["turns"] + 1))
    lines.append("nomusic")  # the sound effects alone
    return "\n".join(lines) + "\n"


# ---- running ----
def ares_paths():
    build = os.environ.get("ARES_REF_BUILD", os.path.join(ROOT, "build-ares", "build"))
    fw = os.environ.get("ARES_REF_FIRMWARE") or os.path.join(
        os.environ.get("ARES_REF_SRC", os.path.join(ROOT, "build-ares", "ares")), "mia", "Firmware", "Nintendo 64")
    return bx.reference_exe(build), fw


def ares_env(fw, extra):
    return dict(os.environ, ARES_REF_FIRMWARE=fw, DYLD_LIBRARY_PATH="/Applications/ares.app/Contents/Frameworks", **extra)


def rom_link():
    link = os.path.join(WORK, "rom.z64")  # nall rewrites path arguments with spaces
    if not os.path.lexists(link):
        bx.reference_rom(bx.ROM, link)
    return link


def start_state():
    """ares save state at STATE_FRAME on the route (made once)"""
    st = os.path.join(WORK, "route_%d.state" % STATE_FRAME)
    if os.path.exists(st):
        return st
    exe, fw = ares_paths()
    tmp = os.path.join(WORK, "state_tmp")
    os.makedirs(tmp, exist_ok=True)
    bx.write_inputs(tmp, STATE_FRAME)
    script = [l for l in open(os.path.join(tmp, "ares.txt")) if l.startswith("hold") and int(l.split()[1]) <= STATE_FRAME]
    script += ["save %d %s\n" % (STATE_FRAME, st), "end %d\n" % STATE_FRAME]
    open(os.path.join(tmp, "s.txt"), "w").writelines(script)
    shutil.copyfile(os.path.join(ROOT, "replays", "default.flash"), os.path.join(tmp, "f.flash"))
    with open(os.path.join(tmp, "ares.log"), "w") as log:
        subprocess.run([exe, rom_link(), os.path.join(tmp, "s.txt")],
                       env=ares_env(fw, {"ARES_REF_FLASH": os.path.join(tmp, "f.flash")}),
                       stdout=log, stderr=subprocess.STDOUT, **bx.process_options(), check=True, timeout=900)
    shutil.rmtree(tmp)
    return st


def symbols_file():
    # (from the decomp map, see its header; XCHECK_SYMS: a fuller list, e.g. every function of the map)
    return os.environ.get("XCHECK_SYMS") or os.path.join(HERE, "ares_ref", "battle_engine.syms")


def load_waits(d):
    """per animation, the game frames the port should wait for its assets as long as ares did: the growth of ares'
    lead in battle game frames at each animation start (A lines' gf), 0 where the port is not ahead"""
    try:
        an = {s: [r for r in bx.parse_trace(os.path.join(d, s + ".trace")) if r["t"] == "A"] for s in ("port", "ares")}
    except (OSError, ValueError, KeyError):
        return []
    wait, added = [], 0
    for p, a in zip(an["port"], an["ares"]):
        if p["move"] != a["move"] or p["gf"] < 0 or a["gf"] < 0:
            break
        w = max(0, a["gf"] - (p["gf"] + added))
        wait.append(w)
        added += w
    return wait


def reference_vi_schedule(directory):
    """Replay observed retrace gaps while retaining the mixer's original sample clock."""
    if os.environ.get("XCHECK_REFERENCE_TIMING") == "0":
        return None
    rows = []
    with open(os.path.join(directory, "ares.trace")) as source:
        for line in source:
            if line.startswith("V "):
                frame, retrace = map(int, line.split()[1:])
                if frame != len(rows) + 1 or retrace < 0 or (rows and retrace < rows[-1][1]):
                    raise ValueError("non-contiguous reference battle-frame timing")
                rows.append((frame, retrace))
    if not rows:
        raise ValueError("reference has no battle-frame timing")
    path = os.path.join(directory, "reference-vi.txt")
    with open(path, "w") as output:
        output.writelines("%d %d\n" % row for row in rows)
    return path


def reference_archive(d, exe, max_frames):
    """Optional shared oracle captures, keyed by every input and the reference executable."""
    cache = os.environ.get("XCHECK_REFERENCE_CACHE")
    if not cache:
        return None
    key = hashlib.sha256()
    _, firmware = ares_paths()
    paths = [bx.ROM, exe, symbols_file(), start_state(), os.path.join(ROOT, "replays", "default.flash"),
             os.path.join(d, "case.txt"), os.path.join(d, "ares.txt")]
    paths += sorted(str(p) for p in Path(firmware).rglob("*") if p.is_file())
    for path in paths:
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1024 * 1024), b""):
                key.update(block)
    key.update(repr((max_frames, STATE_FRAME, ANIM_FRAMES, SEED,
                     sorted((k, v) for k, v in os.environ.items() if k.startswith("ARES_REF_")))).encode())
    os.makedirs(cache, exist_ok=True)
    return os.path.join(cache, key.hexdigest() + ".zip")


def publish_reference_archive(archive, directory):
    # Renderer workers can finish the same oracle case together. Each writer needs
    # its own staging file; readers must only see a complete archive.
    descriptor, temporary = tempfile.mkstemp(prefix=Path(archive).name + ".", suffix=".tmp",
                                            dir=os.path.dirname(archive))
    os.close(descriptor)
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as z:
            files = list(Path(directory, "ares_anim").rglob("*.ppm"))
            files += [Path(directory, name) for name in ("ares.trace", "ares.log", "ares.pcm", "ares.pcm.frames")]
            for path in files:
                if path.is_file():
                    z.write(path, path.relative_to(directory).as_posix())
        try:
            # A hard link publishes once without replacing a capture another
            # worker may already have open (replacement is denied on Windows).
            os.link(temporary, archive)
        except FileExistsError:
            try:
                with zipfile.ZipFile(archive) as existing:
                    if "ares.trace" not in existing.namelist() or existing.testzip() is not None:
                        raise ValueError("incomplete reference archive")
            except (ValueError, zipfile.BadZipFile):
                os.replace(temporary, archive)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@lru_cache(maxsize=8)
def binary_fingerprint(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def clear_port_outputs(directory):
    for name in ("coverage.profraw", "calls.txt", "seq.txt", "port.trace", "port.pcm", "port.pcm.frames"):
        path = os.path.join(directory, name)
        if os.path.isfile(path):
            os.unlink(path)


def wait_case_process(process):
    try:
        process.wait(timeout=900)
        return False
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        return True


def run_case(c, species, port_bin, max_frames):
    d = os.path.join(WORK, "cases", c["id"])
    os.makedirs(d, exist_ok=True)
    clear_port_outputs(d)
    open(os.path.join(d, "case.txt"), "w").write(case_file(c, species))
    for sub in ("port_anim", "ares_anim"):
        shutil.rmtree(os.path.join(d, sub), ignore_errors=True)
        os.makedirs(os.path.join(d, sub))
    # port: from boot (fast until a capture), the route's input
    bx.write_inputs(d, max_frames)
    shutil.copyfile(os.path.join(ROOT, "replays", "default.flash"), os.path.join(d, "port.flash"))
    penv = dict(os.environ, PORT_REPLAY=os.path.join(d, "port.rec"), PORT_STOP_AT=str(max_frames), PORT_SPEED="max",
                PORT_FLASH_FILE=os.path.join(d, "port.flash"), PORT_RECORD=os.path.join(d, "port_last.rec"),
                PORT_XCHECK=os.path.join(d, "port.trace"), PORT_XCHECK_RNG=SEED, PORT_XCHECK_COM="1",
                PORT_XCHECK_CASE=os.path.join(d, "case.txt"), PORT_XCHECK_ANIM=os.path.join(d, "port_anim"),
                PORT_HIDDEN_WINDOW="1", PORT_EXIT_ON_FREEZE="1", PORT_XCHECK_ANIM_FRAMES=str(ANIM_FRAMES),
                PORT_AUDIO_DUMP=os.path.join(d, "port.pcm"))
    # ares: from the route state; the rest of the route's input shifted to it
    exe, fw = ares_paths()
    script = []
    for line in open(os.path.join(d, "ares.txt")):
        p = line.split()
        if p[0] == "hold" and int(p[2]) > STATE_FRAME:
            script.append("hold %d %d %s\n" % (max(1, int(p[1]) - STATE_FRAME), int(p[2]) - STATE_FRAME, p[3]))
    script.append("end %d\n" % (max_frames - STATE_FRAME))
    open(os.path.join(d, "ares.txt"), "w").writelines(script)
    shutil.copyfile(os.path.join(ROOT, "replays", "default.flash"), os.path.join(d, "ares.flash"))
    aenv = ares_env(fw, {"ARES_REF_FLASH": os.path.join(d, "ares.flash"), "ARES_REF_XCHECK": os.path.join(d, "ares.trace"),
                         "ARES_REF_XCHECK_RNG": SEED, "ARES_REF_XCHECK_COM": "1",
                         "ARES_REF_XCHECK_CASE": os.path.join(d, "case.txt"), "ARES_REF_XCHECK_SYMS": symbols_file(),
                         "ARES_REF_XCHECK_ANIM": os.path.join(d, "ares_anim"), "ARES_REF_XCHECK_ANIM_FRAMES": str(ANIM_FRAMES),
                         "ARES_REF_WAV": os.path.join(d, "ares.pcm")})
    bx.backend_home(penv, os.path.join(d, "home"))
    if os.environ.get("XCHECK_EVIDENCE"):
        penv.update(PORT_EVID=os.path.join(d, "calls.txt"), PORT_EVID_SEQ=os.path.join(d, "seq.txt"),
                    PORT_EVID_SEQ_MAX="2", PORT_EVID_DUMP_EVERY="300",
                    LLVM_PROFILE_FILE=os.path.join(d, "coverage.profraw"))
    archive = reference_archive(d, exe, max_frames)
    cached_reference = bool(archive and os.path.exists(archive))
    if cached_reference:
        try:
            with zipfile.ZipFile(archive) as z:
                if "ares.trace" not in z.namelist() or z.testzip() is not None:
                    raise ValueError("incomplete reference archive")
                for member in z.namelist():
                    target = os.path.abspath(os.path.join(d, member))
                    if os.path.commonpath([target, os.path.abspath(d)]) != os.path.abspath(d):
                        raise ValueError("reference archive contains an unsafe path")
                z.extractall(d)
        except (OSError, ValueError, zipfile.BadZipFile) as error:
            print("reference cache miss: %s" % error, flush=True)
            cached_reference = False
    t0 = time.time()
    with open(os.path.join(d, "port.log"), "w") as pl, open(os.devnull if cached_reference else os.path.join(d, "ares.log"), "w") as al:
        pp = subprocess.Popen([port_bin, bx.ROM], cwd=os.path.dirname(port_bin), env=penv, stdout=pl,
                              stderr=subprocess.STDOUT, **bx.process_options())
        ap = None if cached_reference else subprocess.Popen(
            [exe, rom_link(), os.path.join(d, "ares.txt"), "--load", start_state()], env=aenv,
            stdout=al, stderr=subprocess.STDOUT, **bx.process_options())
        port_timed_out = wait_case_process(pp)
        reference_timed_out = wait_case_process(ap) if ap is not None else False
    if (archive and not cached_reference and ap.returncode == 0
            and os.path.isfile(os.path.join(d, "ares.trace")) and os.path.getsize(os.path.join(d, "ares.trace"))):
        publish_reference_archive(archive, d)
    # the N64 can take game frames longer to load a move's effect assets (DMA) and starts the animation later; what
    # runs on meanwhile (Moltres' simulated flames, idle motions) then enters the animation in another state. The
    # port runs again and waits as long before each such animation (PORT_XCHECK_LOAD_WAIT, src/port/xcheck.cpp).
    wait = load_waits(d)
    timing = reference_vi_schedule(d) if not reference_timed_out and (ap is None or ap.returncode == 0) else None
    if any(wait) or timing:
        shutil.rmtree(os.path.join(d, "port_anim"), ignore_errors=True)
        os.makedirs(os.path.join(d, "port_anim"))
        shutil.copyfile(os.path.join(ROOT, "replays", "default.flash"), os.path.join(d, "port.flash"))
        clear_port_outputs(d)
        penv["PORT_XCHECK_LOAD_WAIT"] = ",".join(map(str, wait))
        if timing:
            penv["PORT_XCHECK_VI_TIMING"] = timing
        with open(os.path.join(d, "port.log"), "w") as pl:
            pp = subprocess.Popen([port_bin, bx.ROM], cwd=os.path.dirname(port_bin), env=penv, stdout=pl,
                                  stderr=subprocess.STDOUT, **bx.process_options())
            port_timed_out = wait_case_process(pp)
    secs = time.time() - t0
    try:
        res = evaluate(c, d)
    except Exception as ex:  # a broken run is a result too
        res = {"error": "%s: %s" % (type(ex).__name__, ex), "uses": []}
    res["port_exit_code"] = pp.returncode
    res["reference_exit_code"] = ap.returncode if ap is not None else 0
    if port_timed_out or reference_timed_out:
        res["error"] = "port or reference timed out"
    elif pp.returncode != 0 or res["reference_exit_code"] != 0:
        res["error"] = "port exit %s, reference exit %s" % (pp.returncode, res["reference_exit_code"])
    res["seconds"] = round(secs, 1)
    res["timing_mode"] = "reference-vi-v1" if timing else "native"
    if timing:
        with open(timing, "rb") as source:
            res["reference_vi_sha256"] = hashlib.file_digest(source, "sha256").hexdigest()
    res["reference_cached"] = cached_reference
    res["port_binary_sha256"] = binary_fingerprint(port_bin)
    res["evidence_profile"] = os.path.isfile(os.path.join(d, "coverage.profraw")) if os.environ.get("XCHECK_EVIDENCE") else False
    backend = os.environ.get("PORT_TEST_BACKEND")
    if backend:
        res["requested_backend"] = backend
        res["actual_backend"] = actual_backend(os.path.join(d, "home"))
        if res["actual_backend"] != BACKENDS[backend][0]:
            res["error"] = "requested %s, actual backend id %s" % (backend, res["actual_backend"])
    if any(wait):
        res["load_wait"] = wait
    res["spec"] = c["spec"]
    res["id"] = c["id"]
    json.dump(res, open(os.path.join(d, "result.json"), "w"), indent=1)
    for sub in ("port_anim", "ares_anim") if not os.environ.get("XCHECK_KEEP_FRAMES") else ():  # frames are large
        shutil.rmtree(os.path.join(d, sub), ignore_errors=True)
    keep = ("port.pcm", "port.pcm.frames", "ares.pcm", "ares.pcm.frames") if os.environ.get("XCHECK_KEEP_FRAMES") else ()
    for f in ("port.flash", "ares.flash", "port_last.rec", "port.pcm", "port.pcm.frames", "ares.pcm", "ares.pcm.frames"):
        if f in keep:
            continue
        if os.path.exists(os.path.join(d, f)):
            os.remove(os.path.join(d, f))
    return res


# ---- evaluation ----
EFFECT_LIMIT = 40.0  # default effect difference limit; the report sets it from the run's median:
EFFECT_FACTOR, EFFECT_MARGIN = 2.0, 15.0  # mismatch above max(median x factor, median + margin)
DURATION_SLACK = 2   # game frames of animation length difference tolerated


def evaluate(c, d):
    tr = {s: bx.parse_trace(os.path.join(d, s + ".trace")) for s in ("port", "ares")}
    ev = {s: [r for r in tr[s] if r["t"] == "E"] for s in tr}
    dec = {s: [bx.decode_event(e, bx.SIDE_ENDIAN[s]) for e in ev[s]] for s in ev}
    first, first_field = None, None
    for i in range(min(len(dec["port"]), len(dec["ares"]))):
        diff = [k for k in dec["port"][i] if dec["port"][i][k] != dec["ares"][i].get(k) and k not in bx.PRESENTATION]
        if diff:
            first, first_field = i, diff[:6]
            break
    if first is None and len(ev["port"]) != len(ev["ares"]):
        first, first_field = min(len(ev["port"]), len(ev["ares"])), ["event count %d vs %d" % (len(ev["port"]), len(ev["ares"]))]
    # random-call agreement (logic functions), with callers
    def rcalls(s):
        return [(r["fn"], r["arg"], r["val"]) for r in tr[s] if r["t"] == "R" and r["fn"] in bx.LOGIC_FNS]
    ra, rb = rcalls("port"), rcalls("ares")
    rdiv = next((i for i, (x, y) in enumerate(zip(ra, rb)) if x != y), None)
    callers = {}
    for s in tr:
        for line in open(os.path.join(d, s + ".trace")):
            m = re.search(r"^R (\S+) .* caller=(\S+)", line)
            if m and m.group(1) in bx.LOGIC_FNS:
                callers.setdefault(s, []).append((m.group(1), m.group(2)))
    caller_div = next((i for i, (x, y) in enumerate(zip(callers.get("port", []), callers.get("ares", []))) if x != y), None)
    # actions: who used what, the messages after it
    actions = {}
    for s in dec:
        acts = []
        for i, e in enumerate(dec[s]):
            if e["event"] == "resolve":
                att = e["scene.attacker"]
                nxt = dec[s][i + 1] if i + 1 < len(dec[s]) and dec[s][i + 1]["event"] == "resolved" else None
                msgs = " / ".join(bx.text(nxt[k]) for k in sorted(nxt) if k.startswith("mq.") and bx.text(nxt[k])) if nxt else ""
                acts.append({"event": i, "side": att, "move": e["%d.rt.used_move" % att], "msgs": msgs})
        actions[s] = acts
    anims = {s: [r for r in tr[s] if r["t"] == "A"] for s in tr}

    stops = {}
    for s in tr:
        for line in open(os.path.join(d, s + ".trace")):
            if line.startswith("S "):
                kv = dict(x.split("=", 1) for x in line.split() if "=" in x)
                stops[s] = (int(kv["frame"]), int(kv.get("gf", -1)))

    def next_event(s, frame):
        """(VI frame, game frame) of the first event after a VI frame"""
        for e in ev[s]:
            if e["frame"] > frame:
                return e["frame"], e["gf"]
        return stops.get(s, (None, None))
    uses_out = []
    audio = load_audio(d)
    taken = {"port": set(), "ares": set()}
    for u in c["uses"]:
        r = {"move": u["move"], "path": u["path"], "why": u["why"], "user": u["user"], "user_why": u["user_why"]}
        if u.get("require_animation"):
            r["require_animation"] = True
        act = next((a for a in actions["port"] if a["side"] == 0 and a["move"] == u["move"]), None)
        r["used"] = act is not None
        r["msgs"] = act["msgs"] if act else ""
        aa = next((a for a in actions["ares"] if a["side"] == 0 and a["move"] == u["move"]), None)
        r["msgs_ares"] = aa["msgs"] if aa else ""
        # its animation on each side (Metronome: the called move's)
        target_move = int(u["path"][6:]) if u["path"].startswith("calls_") else u.get("animation_move", u["move"])
        if target_move != u["move"]:
            r["animation_move"] = target_move
        got = {}
        for s in ("port", "ares"):
            a = next((x for x in anims[s] if x["side"] == 0 and x["move"] == target_move and x["k"] not in taken[s]), None)
            if a:
                taken[s].add(a["k"])
                nf, ngf = next_event(s, a["frame"])
                # duration: battle game frames to the next event; vi_duration: VI frames (N64 slowdown included)
                got[s] = {"k": a["k"], "frame": a["frame"], "gf": a["gf"],
                          "duration": (ngf - a["gf"]) if ngf is not None and a["gf"] >= 0 else None,
                          "vi_duration": (nf - a["frame"]) if nf else None}
        r["anim"] = got
        if "port" in got and "ares" in got:
            r["score"] = score_anim(d, got["port"]["k"], got["ares"]["k"], target_move)
            try:
                r["audio"] = score_audio(d, audio, got["port"]["frame"], got["port"]["frame"] + got["port"]["vi_duration"],
                                         got["ares"]["frame"], got["ares"]["frame"] + got["ares"]["vi_duration"], target_move)
            except Exception as ex:  # (no audio dump, a cut-off run)
                r["audio"] = {"error": str(ex)}
            if audio_verdict(r["audio"]).startswith("sound mismatch"):  # both sides' sound, for listening
                for side, (f0, f1) in (("port", (got["port"]["frame"], got["port"]["frame"] + got["port"]["vi_duration"])),
                                       ("ares", (got["ares"]["frame"], got["ares"]["frame"] + got["ares"]["vi_duration"]))):
                    write_wav(os.path.join(d, "sound_%03d_%s.wav" % (target_move, side)), audio, side, f0, f1)
                r["audio"]["wav"] = "sound_%03d_{port,ares}.wav" % target_move
        if first is not None:
            r["logic"] = "after" if act and act["event"] > first else ("mismatch" if act and act["event"] == first or
                                                                       (act and act["event"] + 1 == first) else "ok")
        else:
            r["logic"] = "ok"
        uses_out.append(r)
    # the first logic difference, attributed to the action it happened in
    if first is not None:
        owner = None
        for u in uses_out:
            a = next((x for x in actions["port"] if x["side"] == 0 and x["move"] == u["move"]), None)
            if a and a["event"] <= first <= a["event"] + 1:
                owner = u
        if owner:
            owner["logic"] = "mismatch"
    return {"first_logic_diff": None if first is None else {"event": first, "fields": first_field,
                                                            "port": {k: str(dec["port"][first].get(k)) for k in first_field[:6]} if first < len(dec["port"]) else {},
                                                            "ares": {k: str(dec["ares"][first].get(k)) for k in first_field[:6]} if first < len(dec["ares"]) else {}},
            "random_divergence": rdiv, "caller_divergence": caller_div,
            "events": [len(ev["port"]), len(ev["ares"])], "uses": uses_out,
            "port_ended": bool([l for l in open(os.path.join(d, "port.log")) if "turns done" in l]),
            "ares_ended": bool([l for l in open(os.path.join(d, "ares.log")) if "turns done" in l])}


def block_colors(path, bw=40, bh=30):
    """the frame as bw x bh average colours (4x4 pixel blocks of the 160x120 capture): texture noise averages out, a
    missing or differently coloured effect does not"""
    w, h, rgb = bx.read_ppm(path)
    sx, sy = w // bw, h // bh
    out = []
    for by in range(bh):
        rows = [rgb[((by * sy + y) * w) * 3:((by * sy + y) * w + bw * sx) * 3] for y in range(sy)]
        for bx_ in range(bw):
            r = g = b = 0
            for row in rows:
                seg = row[bx_ * sx * 3:(bx_ + 1) * sx * 3]
                r += sum(seg[0::3])
                g += sum(seg[1::3])
                b += sum(seg[2::3])
            n = sx * sy
            out.append((r / n, g / n, b / n))
    return out


EDGE = 2  # blocks at the picture's edges left out: the port and ares frame the picture's border differently


def effect_diff(ca, cb, top=3, bw=40, bh=30):
    """per frame: the mean colour difference of the `top` most different blocks (0-255), inside the border"""
    ds = sorted((abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])) / 3
                for k, (a, b) in enumerate(zip(ca, cb))
                if EDGE <= k % bw < bw - EDGE and EDGE <= k // bw < bh - EDGE)
    return sum(ds[-top:]) / top


def score_anim(d, kp, ka, move):
    pf = bx.frames_of(os.path.join(d, "port_anim", "m%02d_%03d" % (kp, move)))
    af = bx.frames_of(os.path.join(d, "ares_anim", "m%02d_%03d" % (ka, move)))
    if not pf or not af:
        return {"frames": [len(pf), len(af)]}
    # (frames are 160x120: SSIM at 80x60, alignment at 40x30)
    lp = [bx.luma(f, 80, 60) for f in pf]
    la = [bx.luma(f, 80, 60) for f in af]
    tp, ta = [bx.half(x, 80, 60) for x in lp], [bx.half(x, 80, 60) for x in la]
    best = None
    for off in range(-12, 13):
        pairs = [(i, i + off) for i in range(len(tp)) if 0 <= i + off < len(ta)]
        if len(pairs) < max(4, min(len(tp), len(ta)) // 2):
            continue
        e = sum(sum(abs(x - y) for x, y in zip(tp[i], ta[j])) for i, j in pairs) / (len(pairs) * 40 * 30)
        if best is None or e < best[0]:
            best = (e, off, pairs)
    if best is None:
        return {"frames": [len(pf), len(af)]}
    err, off, pairs = best
    sc = [bx.ssim(lp[i], la[j], 80, 60) for i, j in pairs]
    # special effects: local colour differences (block averages), per aligned frame
    cache = {}

    def colors(path):
        if path not in cache:
            cache[path] = block_colors(path)
        return cache[path]
    ed = [effect_diff(colors(pf[i]), colors(af[j])) for i, j in pairs]
    srt = sorted(ed)
    mi = max(range(len(ed)), key=lambda k: ed[k])  # the frame where the effects differ most
    res = {"frames": [len(pf), len(af)], "offset": off, "ssim": round(sum(sc) / len(sc), 3),
           "ssim_min": round(min(sc), 3), "absdiff": round(err, 1),
           "effect_p90": round(srt[int(0.9 * (len(srt) - 1))], 1), "effect_max": round(srt[-1], 1),
           "worst_frame": pairs[mi][0]}
    if True:  # sheet for every move: port | ares | difference x4, evenly over the animation with the worst frame
        tw, th = 160, 120
        n = 16  # evenly over the whole animation, with the worst frame
        pick = sorted({pairs[round(k * (len(pairs) - 1) / (n - 2))] for k in range(n - 1)} | {pairs[mi]})
        W, H = tw * 3, th * len(pick)
        img = bytearray(W * H * 3)
        for r, (i, j) in enumerate(pick):
            t1, t2 = bx.rgb_tile(pf[i], tw, th), bx.rgb_tile(af[j], tw, th)
            t3 = bytes(min(255, abs(x - y) * 4) for x, y in zip(t1, t2))
            for col, pix in enumerate((t1, t2, t3)):
                for y in range(th):
                    o = ((r * th + y) * W + col * tw) * 3
                    img[o:o + tw * 3] = pix[y * tw * 3:(y + 1) * tw * 3]
        res["sheet"] = "sheet_%03d_k%d.png" % (move, kp)
        bx.write_png(os.path.join(d, res["sheet"]), W, H, img)
    return res


# ---- sound ----
AUDIO_RATE = 8000  # both sides are compared as 8 kHz mono


def load_audio(d):
    """both sides' sound with a frame -> sample map: the port's PORT_AUDIO_DUMP (AI rate, .frames: VI frame and samples
    of each buffer) and ares' ARES_REF_WAV (48 kHz, .frames: u32 samples written after each frame)"""
    import array
    out = {}
    pa = array.array("h")
    pa.frombytes(open(os.path.join(d, "port.pcm"), "rb").read())
    rate = 32000
    for line in open(os.path.join(d, "port.log"), errors="replace"):
        m = re.search(r"\[audio\] rate (\d+)", line)
        if m:
            rate = int(m.group(1))
    starts, pos = [], 0
    for line in open(os.path.join(d, "port.pcm.frames")):
        t, n = map(int, line.split())
        starts.append((t, pos))
        pos += n
    out["port"] = (pa, rate, starts)
    aa = array.array("h")
    aa.frombytes(open(os.path.join(d, "ares.pcm"), "rb").read())
    fr = array.array("I")
    fr.frombytes(open(os.path.join(d, "ares.pcm.frames"), "rb").read())
    out["ares"] = (aa, 48000, fr)
    return out


def segment(audio, side, f0, f1):
    """[f0, f1) VI frames of one side, as 8 kHz mono floats"""
    pcm, rate, idx = audio[side]
    if side == "port":
        def at(f):
            for t, p in idx:
                if t >= f:
                    return p
            return len(pcm) // 2
    else:
        def at(f):
            return idx[f - 2] if 2 <= f < len(idx) + 2 else (0 if f < 2 else len(pcm) // 2)
    a, b = at(f0), at(f1)
    left, right = pcm[a * 2:b * 2:2], pcm[a * 2 + 1:b * 2:2]
    n = len(left)
    step = rate / AUDIO_RATE
    out = []
    for i in range(int(n / step)):
        j0, j1 = int(i * step), max(int(i * step) + 1, int((i + 1) * step))
        s = 0
        for j in range(j0, min(j1, n)):
            s += left[j] + right[j]
        out.append(s / (2 * (j1 - j0) * 32768.0))
    return out


def envelope(x, block):
    import math
    return [10 * math.log10(max(1e-10, sum(v * v for v in x[i:i + block]) / block)) for i in range(0, len(x) - block + 1, block)]


def bands(x, block=160, freqs=(125, 250, 500, 1000, 2000, 3000)):
    """log band energies (Goertzel) per 20 ms block"""
    import math
    out = []
    coefs = [2 * math.cos(2 * math.pi * f / AUDIO_RATE) for f in freqs]
    for i in range(0, len(x) - block + 1, block):
        row = []
        for c in coefs:
            s1 = s2 = 0.0
            for v in x[i:i + block]:
                s1, s2 = v + c * s1 - s2, s1
            row.append(10 * math.log10(max(1e-10, s1 * s1 + s2 * s2 - c * s1 * s2)))
        out.append(row)
    return out


def pearson(a, b):
    n = len(a)
    if n < 3:
        return None
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va <= 1e-9 or vb <= 1e-9:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (va * vb) ** 0.5


def score_audio(d, audio, p0, p1, a0, a1, move):
    """one move's sound from its animation start to the next event, port vs ares: aligned by the loudness envelope
    (10 ms blocks, +-300 ms), then level, envelope and band-energy (timbre) agreement, and how much of one side's
    sound the other lacks"""
    xp, xa = segment(audio, "port", p0, p1), segment(audio, "ares", a0, a1)
    ep, ea = envelope(xp, 80), envelope(xa, 80)
    if not ep or not ea:
        return {"error": "no sound captured"}
    lin_p = [10 ** (v / 20) for v in ep]
    lin_a = [10 ** (v / 20) for v in ea]
    best = None
    for lag in range(-30, 31):
        pairs = [(i, i + lag) for i in range(len(lin_p)) if 0 <= i + lag < len(lin_a)]
        if len(pairs) < 10:
            continue
        c = pearson([lin_p[i] for i, _ in pairs], [lin_a[j] for _, j in pairs])
        if c is not None and (best is None or c > best[0]):
            best = (c, lag, pairs)
    loud_a = max(ea)
    if best is None:
        return {"silent": True, "ares_peak_db": round(loud_a, 1), "port_peak_db": round(max(ep), 1)}
    _, lag, pairs = best
    active = [(i, j) for i, j in pairs if max(ep[i], ea[j]) > -45]
    res = {"lag_ms": lag * 10, "ares_peak_db": round(loud_a, 1), "port_peak_db": round(max(ep), 1),
           "active_blocks": len(active)}
    if len(active) < 5:
        res["silent"] = True
        return res
    res["env_corr"] = round(pearson([ep[i] for i, _ in active], [ea[j] for _, j in active]) or 0, 3)
    la = [(i, j) for i, j in active if ea[j] > -40]
    res["level_db"] = round(sum(ep[i] - ea[j] for i, j in la) / len(la), 1) if la else None
    ma = [(i, j) for i, j in pairs if ea[j] > -35]
    res["missing"] = round(sum(1 for i, j in ma if ep[i] < ea[j] - 15) / len(ma), 3) if ma else 0.0
    mp = [(i, j) for i, j in pairs if ep[i] > -35]
    res["extra"] = round(sum(1 for i, j in mp if ea[j] < ep[i] - 15) / len(mp), 3) if mp else 0.0
    # timbre: band energies per 20 ms on the aligned overlap
    off = lag * 80
    bp = bands(xp[max(0, -off):])
    ba = bands(xa[max(0, off):])
    n = min(len(bp), len(ba))
    rows = [(bp[k], ba[k]) for k in range(n) if max(max(bp[k]), max(ba[k])) > -20]
    cs = [pearson([r[0][b] for r in rows], [r[1][b] for r in rows]) for b in range(6)] if len(rows) >= 5 else []
    cs = [c for c in cs if c is not None]
    res["band_corr"] = round(sum(cs) / len(cs), 3) if cs else None  # (informational: low for noise-like sound)
    if rows:  # the average spectrum: per band, port minus ares (dB)
        prof = [sum(r[0][b] - r[1][b] for r in rows) / len(rows) for b in range(6)]
        res["band_diff_db"] = [round(v, 1) for v in prof]
        res["timbre_db"] = round(sum(abs(v) for v in prof) / 6, 1)
    res["move"] = move
    return res


def write_wav(path, audio, side, f0, f1):
    pcm, rate, idx = audio[side]
    if side == "port":
        a = next((p for t, p in idx if t >= f0), len(pcm) // 2)
        b = next((p for t, p in idx if t >= f1), len(pcm) // 2)
    else:
        a = idx[f0 - 2] if f0 >= 2 else 0
        b = idx[f1 - 2] if f1 - 2 < len(idx) else len(pcm) // 2
    data = pcm[a * 2:b * 2].tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16) + b"data" + struct.pack("<I", len(data)) + data)


AUDIO_LIMITS = {"missing": 0.2, "extra": 0.2, "env_corr": 0.6, "timbre_db": 4.0, "level_db": 6.0}


def audio_verdict(a):
    if not a:
        return "-"
    if "error" in a:
        return "no audio (%s)" % a["error"]
    if a.get("silent"):
        return "silent on both" if max(a.get("ares_peak_db", -99), a.get("port_peak_db", -99)) < -45 else \
            "sound mismatch (one side silent: port peak %s dB, ares peak %s dB)" % (a.get("port_peak_db"), a.get("ares_peak_db"))
    notes = []
    if a.get("missing", 0) > AUDIO_LIMITS["missing"]:
        notes.append("port lacks %d%% of the sound" % round(100 * a["missing"]))
    if a.get("extra", 0) > AUDIO_LIMITS["extra"]:
        notes.append("port has %d%% extra sound" % round(100 * a["extra"]))
    if a.get("env_corr") is not None and a["env_corr"] < AUDIO_LIMITS["env_corr"]:
        notes.append("envelope %.2f" % a["env_corr"])
    if a.get("timbre_db") is not None and a["timbre_db"] > AUDIO_LIMITS["timbre_db"]:
        notes.append("spectrum off by %.1f dB per band" % a["timbre_db"])
    if a.get("level_db") is not None and abs(a["level_db"]) > AUDIO_LIMITS["level_db"]:
        notes.append("level %+.1f dB" % a["level_db"])
    return "sound mismatch (%s)" % ", ".join(notes) if notes else "sound match"


def classify(u, common_offset, floor=None):
    """one use's verdict: match / logic mismatch / animation mismatch / not reached"""
    if u.get("logic") == "mismatch":
        return "logic mismatch"
    if u.get("logic") == "after":
        return "after a logic mismatch"
    a = u.get("anim", {})
    if not u.get("used"):
        return "not reached"
    if "port" not in a and "ares" not in a:
        if u.get("require_animation"):
            return "animation mismatch (required animation absent)"
        return "match (no animation)"
    if ("port" in a) != ("ares" in a):
        return "animation mismatch (only on %s)" % ("port" if "port" in a else "ares")
    s = u.get("score", {})
    if u.get("require_animation") and (len(s.get("frames", [])) != 2 or not all(s["frames"]) or
                                       not all(k in s for k in ("offset", "ssim", "effect_p90"))):
        return "animation mismatch (required capture or comparison absent)"
    notes = []
    if "effect_p90" in s and s["effect_p90"] > (EFFECT_LIMIT if floor is None else floor):
        notes.append("effects differ: %.0f (worst frame %d: %.0f)" % (s["effect_p90"], s["worst_frame"], s["effect_max"]))
    if "offset" in s and common_offset is not None and abs(s["offset"] - common_offset) > 1:
        notes.append("offset %+d" % s["offset"])
    dp, da = a["port"].get("duration"), a["ares"].get("duration")
    faint = "fainted" in u.get("msgs", "") or "fainted" in u.get("msgs_ares", "")
    if dp is not None and da is not None and abs(dp - da) > DURATION_SLACK:
        if faint and not notes and 0 < da - dp <= 20:  # the length runs to the next event: faint and switch-in included
            return "timing (faint/switch-in %+d game frames)" % (da - dp)
        notes.append("length %d vs %d game frames" % (dp, da))
    return "animation mismatch (%s)" % ", ".join(notes) if notes else "match"


# ---- commands ----
def parse_args(argv):
    args, i = {}, 0
    while i < len(argv):
        if argv[i] in ("--move", "--path", "--jobs", "--port-bin", "--frames", "--case", "--user", "--moves") and i + 1 < len(argv):
            args[argv[i]] = argv[i + 1]
            i += 2
        else:
            args[argv[i]] = True
            i += 1
    return args


def move_arg(v, names):
    if v is None:
        return None
    if str(v).isdigit():
        return int(v)
    for k, n in names.items():
        if n.lower().replace(" ", "") == str(v).lower().replace(" ", "").replace("_", ""):
            return k
    sys.exit("unknown move %s" % v)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], parse_args(sys.argv[2:])
    moves, species, chart = decomp_tables()
    spn, mvn = bx.names()
    only = move_arg(args.get("--move"), mvn)
    # each move once (its base path) unless --all-paths or --path asks for more
    cases, skipped = plan(moves, species, chart, only, args.get("--path") or (None if args.get("--all-paths") else "base"),
                          int(args["--user"]) if args.get("--user") else None)
    if args.get("--moves"):  # one battle with these moves in this order (base path), e.g. to find carried-over state
        seq = [move_arg(x, mvn) for x in args["--moves"].split(",")][:4]
        one, _ = plan(moves, species, chart, seq[0], "base", int(args["--user"]) if args.get("--user") else None)
        c = one[0]
        c["uses"] = [dict(c["uses"][0], move=m, why="--moves sequence") for m in seq]
        c["turns"] = len(seq)
        spec = json.dumps({k: c[k] for k in ("path", "target", "user", "rules", "turns")} | {"moves": seq}, sort_keys=True)
        c["id"] = "seq_%s_%s" % ("-".join("%03d" % m for m in seq), hashlib.sha1(spec.encode()).hexdigest()[:8])
        c["spec"] = spec
        cases = [c]
    if args.get("--case"):  # one battle of the plan, by (the start of) its id
        cases = [c for c in cases if c["id"].startswith(args["--case"])]
    os.makedirs(os.path.join(WORK, "cases"), exist_ok=True)
    if cmd == "paths":
        for mid in sorted(moves):
            if only and mid != only:
                continue
            mv = moves[mid]
            print("%3d %-14s %-18s pw %3d %-8s acc %3d: %s" % (mid, mvn.get(mid, "?"), EFFECTS.get(mv["effect"], hex(mv["effect"])),
                                                         mv["power"], TYPES.get(mv["type"], "?"), mv["acc"],
                                                         " ".join(p for p, _ in paths_for(mv, chart, species))))
        print("%d cases, %d uses, %d skipped" % (len(cases), sum(len(c["uses"]) for c in cases), len(skipped)))
        return
    if cmd == "run":
        port_bin = os.path.abspath(args.get("--port-bin", os.path.join(ROOT, "build", "pokestadium")))
        frames = int(args.get("--frames", "12000"))
        todo = open_cases(cases, port_bin) if args.get("--open") else []
        for c in ([] if args.get("--open") else cases):
            rp = os.path.join(WORK, "cases", c["id"], "result.json")
            if os.path.exists(rp) and not args.get("--force"):
                try:
                    cached = json.load(open(rp))
                    if result_matches_build(cached, c, port_bin, os.path.dirname(rp)):
                        continue
                except ValueError:
                    pass
            todo.append(c)
        print("%d cases (%d %s), %d to run" % (len(cases), len(cases) - len(todo), "matching, skipped" if args.get("--open")
                                                else "cached", len(todo)), flush=True)
        start_state()
        t0 = time.time()
        # worker processes (the scoring is pure Python); each case runs the port and ares side by side
        with ProcessPoolExecutor(max_workers=int(args.get("--jobs", "3"))) as ex:
            futs = {ex.submit(run_case, c, species, port_bin, frames): c for c in todo}
            for n, f in enumerate(as_completed(futs), 1):
                c, r = futs[f], f.result()
                verdicts = ["%s, %s" % (classify(u, None), audio_verdict(u.get("audio"))) for u in r.get("uses", [])]
                print("[%d/%d %.0fs] %s: %s%s" % (n, len(todo), time.time() - t0, c["id"], "; ".join(
                    "%s %s: %s" % (mvn.get(u["move"], u["move"]), u["path"], v) for u, v in zip(r.get("uses", []), verdicts)),
                    " ERROR " + r["error"] if "error" in r else ""), flush=True)
        print("ran %d cases in %.0f s" % (len(todo), time.time() - t0))
        cmd = "report"
    if cmd == "report":
        report(cases, skipped, moves, mvn, spn, args.get("--port-bin"))
        print("(report of %s)" % ("every path" if args.get("--all-paths") else "the base paths and successful healing"))


def run_limits(rows):
    """(common animation offset, median effect_p90, effect limit) of the results in rows [(use, result, case)]"""
    offsets = [u["score"]["offset"] for u, r, c in rows if r and "offset" in u.get("score", {})]
    common = max(set(offsets), key=offsets.count) if offsets else None
    scores = sorted(u["score"]["effect_p90"] for u, r, c in rows if r and "effect_p90" in u.get("score", {}))
    median = scores[len(scores) // 2] if scores else None
    floor = round(max(median * EFFECT_FACTOR, median + EFFECT_MARGIN), 1) if median is not None else EFFECT_LIMIT
    return common, median, floor


def result_matches_build(result, case, port_bin, directory):
    return (result is not None and result.get("spec") == case["spec"] and
            result.get("timing_mode") == ("native" if os.environ.get("XCHECK_REFERENCE_TIMING") == "0" else "reference-vi-v1") and
            (not port_bin or result.get("port_binary_sha256") == binary_fingerprint(os.path.abspath(port_bin))) and
            (not os.environ.get("XCHECK_EVIDENCE") or os.path.isfile(os.path.join(directory, "coverage.profraw"))))


def open_cases(cases, port_bin):
    """the cases --open reruns: not run yet, spec changed, an error, or a use whose verdict (as in the report) or sound
    is not a match"""
    results = {}
    for c in cases:
        rp = os.path.join(WORK, "cases", c["id"], "result.json")
        try:
            r = json.load(open(rp)) if os.path.exists(rp) else None
            results[c["id"]] = r if result_matches_build(r, c, port_bin, os.path.dirname(rp)) else None
        except ValueError:
            results[c["id"]] = None
    rows = [(u, r, c) for c in cases for r in [results[c["id"]]] if r for u in r.get("uses", [])]
    common, _, floor = run_limits(rows)
    todo = []
    for c in cases:
        r = results[c["id"]]
        if r is None or r.get("spec") != c["spec"] or "error" in r or not r.get("uses") or any(
                not classify(u, common, floor).startswith("match") or audio_verdict(u.get("audio")).startswith(
                    ("sound mismatch", "no audio")) for u in r["uses"]):
            todo.append(c)
    return todo


def report(cases, skipped, moves, mvn, spn, port_bin=None):
    rows, missing = [], 0
    secs = 0.0
    for c in cases:
        rp = os.path.join(WORK, "cases", c["id"], "result.json")
        if not os.path.exists(rp):
            missing += 1
            for u in c["uses"]:
                rows.append((u, None, c))
            continue
        try:
            r = json.load(open(rp))
        except ValueError:
            r = None
        if not result_matches_build(r, c, port_bin, os.path.dirname(rp)):
            missing += 1
            rows.extend((u, None, c) for u in c["uses"])
            continue
        secs += r.get("seconds", 0)
        for u in r.get("uses") or c["uses"]:
            rows.append((u, r, c))
    common, median, floor = run_limits(rows)
    out = ["# Move sweep: port vs ares", ""]
    verdicts = []
    for u, r, c in rows:
        v = "not run" if r is None else ("error: " + r["error"] if "error" in r else classify(u, common, floor))
        if r is not None and v == "not reached":
            ended = r.get("port_ended") and r.get("ares_ended")
            v += " (%s)" % ("the move was not used before the planned turns ended" if ended else
                            "the run ended early (freeze, crash or frame limit)")
        verdicts.append(v)
    counts = {}
    for v in verdicts:
        counts[v.split(" (")[0]] = counts.get(v.split(" (")[0], 0) + 1
    reached_moves = {u["move"] for (u, r, c), v in zip(rows, verdicts) if not v.startswith(("not ", "error"))}
    all_moves = {m for m in moves if m != 0xA5} | {0xA5}
    out.append("Cases: %d (%d not run yet), uses: %d, runner time: %.0f s (sum of case wall times; cases run in "
               "parallel). Common animation offset: %s frames. Special effects (first %d frames of each animation, "
               "aligned): per frame the mean colour difference of the 3 most different 4x4-pixel blocks (40x30 grid, "
               "0-255), per move its 90th percentile; median over the moves %s, mismatch above %s." % (
                   len(cases), missing, len(rows), secs, common, ANIM_FRAMES, median, floor))
    out.append("")
    out.append("Coverage: %d of %d moves reached, %d of %d planned move/path uses reached; %d paths not set up."
               % (len(reached_moves), len(all_moves), sum(1 for v in verdicts if not v.startswith(("not ", "error"))),
                  len(rows), len(skipped)))
    out.append("")
    out.append("Verdicts: " + ", ".join("%s %d" % kv for kv in sorted(counts.items())))
    out.append("")
    sv = [audio_verdict(u.get("audio")) if r else "not run" for (u, r, c) in rows]
    scount = {}
    for v in sv:
        scount[v.split(" (")[0]] = scount.get(v.split(" (")[0], 0) + 1
    out.append("Sound: " + ", ".join("%s %d" % kv for kv in sorted(scount.items())) + ". Each move's sound from its "
               "animation start to the next battle event, battle music off on both sides, aligned by the loudness "
               "envelope; mismatch: port lacks or adds >%d%% of the loud 10 ms blocks, envelope correlation <%.1f, the "
               "average spectrum off by >%.0f dB per band, or the level off by >%.0f dB. The overlap is compared; a "
               "different length shows in the animation verdict." % (
                   100 * AUDIO_LIMITS["missing"], AUDIO_LIMITS["env_corr"], AUDIO_LIMITS["timbre_db"], AUDIO_LIMITS["level_db"]))
    out.append("")
    out.append("## Sound mismatches")
    out.append("")
    out.append("| move | path | sound | level dB | envelope | missing / extra | band diff dB (125..3000 Hz) | listen |")
    out.append("|---|---|---|---|---|---|---|---|")
    for (u, r, c), v in zip(rows, sv):
        if not v.startswith("sound mismatch") and not v.startswith("no audio"):
            continue
        a = u.get("audio", {})
        out.append("| %s (%d) | %s | %s | %s | %s | %s / %s | %s | %s |" % (
            mvn.get(u["move"], "?"), u["move"], u["path"], v, a.get("level_db", "-"), a.get("env_corr", "-"),
            a.get("missing", "-"), a.get("extra", "-"), a.get("band_diff_db", "-"),
            "cases/%s/%s" % (c["id"], a["wav"]) if a.get("wav") else "-"))
    out.append("")
    out.append("## Known findings (run first)")
    out.append("")
    for mid, note in KNOWN.items():
        vs = ["%s: %s" % (u["path"], v) for (u, r, c), v in zip(rows, verdicts) if u["move"] == mid]
        out.append("- %s (%d): %s. This sweep: %s" % (mvn.get(mid, mid), mid, note, "; ".join(vs) or "not run"))
    out.append("")
    out.append("## Mismatches")
    out.append("")
    out.append("| move | path | verdict | effect diff (max) | offset | length port / ares | messages (port) | case |")
    out.append("|---|---|---|---|---|---|---|---|")
    for (u, r, c), v in zip(rows, verdicts):
        if "mismatch" not in v and not v.startswith("error"):
            continue
        s, a = u.get("score", {}), u.get("anim", {})
        sheet = " [sheet](cases/%s/%s)" % (c["id"], s["sheet"]) if s.get("sheet") else ""
        out.append("| %s (%d) | %s | %s | %s | %s | %s / %s | %s | %s%s |" % (
            mvn.get(u["move"], "?"), u["move"], u["path"], v, "%.0f (%.0f)" % (s["effect_p90"], s["effect_max"]) if "effect_p90" in s else "-",
            s.get("offset", "-"), a.get("port", {}).get("duration", "-"), a.get("ares", {}).get("duration", "-"),
            u.get("msgs", "")[:50], c["id"], sheet))
        if r and r.get("first_logic_diff") and v.startswith("logic"):
            f = r["first_logic_diff"]
            out.append("|  |  | event %s: %s |  |  |  | port %s / ares %s |  |" % (
                f["event"], ", ".join(f["fields"]), json.dumps(f.get("port")), json.dumps(f.get("ares"))))
    out.append("")
    out.append("## Every move and path")
    out.append("")
    out.append("| move | effect | path | user | verdict | effect diff | game frames port / ares (VI frames) | sound | messages |")
    out.append("|---|---|---|---|---|---|---|---|---|")
    for (u, r, c), v, s_ in sorted(zip(rows, verdicts, sv), key=lambda x: (x[0][0]["move"], x[0][0]["path"])):
        s, a = u.get("score", {}), u.get("anim", {})
        mv = moves.get(u["move"], {"effect": 0})
        out.append("| %s (%d) | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            mvn.get(u["move"], "?"), u["move"], EFFECTS.get(mv["effect"], hex(mv["effect"])), u["path"],
            spn.get(u.get("user"), u.get("user")), v + (" [sheet](cases/%s/%s)" % (c["id"], s["sheet"]) if s.get("sheet") else ""),
            "%.0f" % s["effect_p90"] if "effect_p90" in s else "-",
            "%s / %s (%s / %s)" % (a.get("port", {}).get("duration", "-"), a.get("ares", {}).get("duration", "-"),
                                   a.get("port", {}).get("vi_duration", "-"), a.get("ares", {}).get("vi_duration", "-")),
            s_, u.get("msgs", "")[:60]))
    out.append("")
    out.append("## Paths not set up")
    out.append("")
    for mid, path, why in skipped:
        out.append("- %s (%d) %s: %s" % (mvn.get(mid, "?"), mid, path, why))
    open(os.path.join(WORK, "report.md"), "w").write("\n".join(out) + "\n")
    print("\n".join(out[:8]))
    print(os.path.join(WORK, "report.md"))


if __name__ == "__main__":
    main()
