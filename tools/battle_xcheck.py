#!/usr/bin/env python3 -I
"""Cross-checks a battle in the port against the original game in ares: same battle, same random numbers, compared
turn by turn, then the move animations frame by frame.

Usage:
  tools/battle_xcheck.py run <out dir> [--seed N] [--frames N] [--anim [--anim-frames N]] [--port-only|--ares-only]
  tools/battle_xcheck.py compare <out dir>      per-turn comparison: <out>/report.md
  tools/battle_xcheck.py anim <out dir>         move animations side by side: <out>/anim/*.png, <out>/anim.md

How the two sides are made to play the same battle (see also src/port/xcheck.cpp and tools/ares_ref/ares_ref.cpp):
- Setup: the same controller input on both (ROUTE: Poke Cup, first battle, the same three rentals; the team picks
  come late enough that both sides have the order screen up, ares loads slower) and the same save
  (replays/default.flash).
- Randomness: the battle's random functions draw from their osGetCount-timed generators, so CPU timing decides the
  battle. Both sides replace them with the same seeded stream per function (PORT_XCHECK_RNG / ARES_REF_XCHECK_RNG).
  ares hooks the functions at their relocated addresses (the battle engine is fragment 51; its load address is
  read from gFragments when Memmap_SetFragmentMap runs).
- Input in battle: none. The opponent is the computer; the player's move time limit counts as run out at once, so
  the game itself picks its first usable move and first healthy replacement (PORT_XCHECK_COM / ARES_REF_XCHECK_COM).
- Sync: on battle events, not frames: Battle_StartTurn ("turn"), Battle_ResolveMonAction on entry ("resolve") and
  after it ("resolved"); each records both Pokemon's ActivePokemonState, the BattleScene, sBattleDamage and the
  message queues as raw bytes, which this script decodes with each side's byte order.
- Animations: each move animation (BattleScene_EnterRow3StartMoveAnim / Row19) saves its frames on both sides; they
  are aligned in time (best offset of small thumbnails), scored with SSIM and put side by side.

Needs: the port built with this change (--port-bin, default build/pokestadium), ares_ref built with
tools/ares_ref/build.sh (ARES_REF_BUILD: its build directory; ARES_REF_FIRMWARE or ARES_REF_SRC for the PIF files).
Frame dumps are large: `anim` deletes the raw frames after making the sheets unless --keep-frames.
"""
import os, shutil, struct, subprocess, sys, zlib
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runner_platform import process_options, backend_home, reference_rom, reference_exe

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM = os.environ.get("ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
RANDOM_FNS = ["Battle_Random", "Battle_RandomAccuracy", "BattleAI_RandomBelowInclusive", "BattleAnim_RandomRange",
              "BattleAnim_RandomIntWithWarmup"]
LOGIC_FNS = RANDOM_FNS[:3]  # the animation ones only steer cameras and effects

# ---- the battle setup: input from boot (port frames; ares frame = port frame + 1) ----
A, B, START, CU, CL = 0x8000, 0x4000, 0x1000, 0x8, 0x2
BUTTON_NAMES = {0x8000: "A", 0x4000: "B", 0x2000: "Z", 0x1000: "START", 0x0800: "UP", 0x0400: "DOWN", 0x0200: "LEFT",
                0x0100: "RIGHT", 0x20: "L", 0x10: "R", 8: "CU", 4: "CD", 2: "CL", 1: "CR"}


def route():
    """[(frame, buttons)]: replays/to_battle.rec to the Poke Cup screen (it starts the first battle with A at 1692),
    then the team order picks A (Charmander), C-up, C-left and the confirming A"""
    ev = []
    for line in open(os.path.join(ROOT, "replays", "to_battle.rec")):
        if line.startswith("#") or not line.strip():
            continue
        f, b = map(int, line.split()[:2])
        if f < 1700:
            ev.append((f, b))
    for f, b in [(2200, A), (2213, CU), (2226, CL), (2263, A)]:
        ev += [(f, b), (f + 6, 0)]
    return ev


def write_inputs(out, frames):
    ev = route()
    lines = ["# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4\n", "# game audio\n"]
    lines += ["%d %d 0 0 0 0 0 0 0 0 0 0 0\n" % (f, b) for f, b in ev]
    lines.append("# end %d\n" % (ev[-1][0] + 1))
    open(os.path.join(out, "port.rec"), "w").writelines(lines)
    script = []
    for i, (f, b) in enumerate(ev):
        if b:
            nxt = ev[i + 1][0] if i + 1 < len(ev) else f + 6
            script.append("hold %d %d %s\n" % (f + 1, nxt, "+".join(n for m, n in BUTTON_NAMES.items() if b & m)))
    script.append("end %d\n" % frames)
    open(os.path.join(out, "ares.txt"), "w").writelines(script)


def low_priority():
    # (not /usr/bin/nice: macOS drops DYLD_LIBRARY_PATH for system binaries, and ares needs it for MoltenVK)
    os.setpriority(os.PRIO_PROCESS, 0, 15)


def cmd_run(out, args):
    seed = args.get("--seed", "1")
    frames = int(args.get("--frames", "20000"))
    anim = "--anim" in args
    os.makedirs(out, exist_ok=True)
    write_inputs(out, frames)
    procs = []
    if "--ares-only" not in args:
        port = os.path.abspath(args.get("--port-bin", os.path.join(ROOT, "build", "pokestadium")))
        shutil.copyfile(os.path.join(ROOT, "replays", "default.flash"), os.path.join(out, "port.flash"))
        env = dict(os.environ, PORT_REPLAY=os.path.join(out, "port.rec"), PORT_STOP_AT=str(frames), PORT_SPEED="max",
                   PORT_FLASH_FILE=os.path.join(out, "port.flash"), PORT_RECORD=os.path.join(out, "port_last.rec"),
                   PORT_XCHECK=os.path.join(out, "port.trace"), PORT_XCHECK_RNG=seed, PORT_XCHECK_COM="1")
        if anim:
            env["PORT_XCHECK_ANIM"] = os.path.join(out, "port_anim")
            env["PORT_HIDDEN_WINDOW"] = "1"  # drawn, not shown
            env["PORT_RENDER_EVERY"] = "1"   # every frame drawn (max speed otherwise draws one in 8)
            env["PORT_XCHECK_ANIM_FRAMES"] = args.get("--anim-frames", "200")
        else:
            env["PORT_HEADLESS"] = "1"  # nothing to draw
        backend_home(env, os.path.join(out, "home"))
        log = open(os.path.join(out, "port.log"), "w")
        procs.append(("port", subprocess.Popen([port, ROM], cwd=os.path.dirname(port), env=env, stdout=log,
                                               stderr=subprocess.STDOUT, **process_options())))
    if "--port-only" not in args:
        build = os.environ.get("ARES_REF_BUILD", os.path.join(ROOT, "build-ares", "build"))
        fw = os.environ.get("ARES_REF_FIRMWARE") or os.path.join(
            os.environ.get("ARES_REF_SRC", os.path.join(ROOT, "build-ares", "ares")), "mia", "Firmware", "Nintendo 64")
        shutil.copyfile(os.path.join(ROOT, "replays", "default.flash"), os.path.join(out, "ares.flash"))
        link = os.path.join(out, "rom.z64")  # nall rewrites path arguments with spaces
        if os.path.lexists(link):
            os.remove(link)
        reference_rom(ROM, link)
        env = dict(os.environ, ARES_REF_FIRMWARE=fw, DYLD_LIBRARY_PATH="/Applications/ares.app/Contents/Frameworks",
                   ARES_REF_FLASH=os.path.join(out, "ares.flash"), ARES_REF_XCHECK=os.path.join(out, "ares.trace"),
                   ARES_REF_XCHECK_RNG=seed, ARES_REF_XCHECK_COM="1")
        if anim:
            os.makedirs(os.path.join(out, "ares_anim"), exist_ok=True)
            env["ARES_REF_XCHECK_ANIM"] = os.path.join(out, "ares_anim")
            env["ARES_REF_XCHECK_ANIM_FRAMES"] = args.get("--anim-frames", "200")
        log = open(os.path.join(out, "ares.log"), "w")
        procs.append(("ares", subprocess.Popen([reference_exe(build), link, os.path.join(out, "ares.txt")],
                                               env=env, stdout=log, stderr=subprocess.STDOUT,
                                               **process_options())))
    for name, p in procs:
        print("%s: exit %d" % (name, p.wait()))


# ---- traces and the shared decoder ----
def parse_trace(path):
    recs = []
    for line in open(path):
        p = line.split()
        if not p or not line.endswith("\n"):  # (a run stopped mid-line)
            continue
        kv = dict(x.split("=", 1) for x in p if "=" in x)
        if p[0] == "R":
            recs.append({"t": "R", "fn": p[1], "k": int(p[2]), "arg": int(p[3]), "val": int(p[4]),
                         "frame": int(kv.get("frame", 0))})
        elif p[0] == "E":
            recs.append({"t": "E", "n": int(p[1]), "name": p[2], "frame": int(kv["frame"]), "gf": int(kv.get("gf", -1)),
                         "st": [bytes.fromhex(kv["st0"]), bytes.fromhex(kv["st1"])],
                         "scene": bytes.fromhex(kv["scene"]), "dmg": int(kv["dmg"]), "mq": bytes.fromhex(kv["mq"])})
        elif p[0] == "A":
            recs.append({"t": "A", "k": int(p[1]), "move": int(kv["move"]), "side": int(kv["side"]),
                         "frame": int(kv["frame"]), "gf": int(kv.get("gf", -1))})
    return recs


# (name, offset, type) with type in u8 s8 u16 s16 u32 s32 f32 or bytes:<n>; offsets are the N64 layout, which the port
# keeps for these pointer-free ranges (game/src/3D140.h, battle_engine.h)
RUNTIME = [("unk_00", 0x00, "u8"), ("unk_01", 0x01, "u8"), ("unk02", 0x02, "bytes:2"), ("unk_04", 0x04, "u8"),
           ("unk_05", 0x05, "u8"), ("unk06", 0x06, "bytes:5"), ("species", 0x0B, "u8"), ("hp", 0x0C, "u16"),
           ("unk_0E", 0x0E, "u16"), ("unk_10", 0x10, "u16"), ("unk_12", 0x12, "u16"), ("unk_14", 0x14, "u8"),
           ("status", 0x15, "u8"), ("unk_16", 0x16, "bytes:6"), ("type1", 0x1C, "u8"), ("type2", 0x1D, "u8"),
           ("unk_1E", 0x1E, "u8"), ("move1", 0x1F, "u8"), ("move2", 0x20, "u8"), ("move3", 0x21, "u8"),
           ("move4", 0x22, "u8"), ("unk23", 0x23, "u8"), ("unk_24", 0x24, "u16"), ("level", 0x26, "u8"),
           ("max_hp", 0x28, "u16"), ("atk", 0x2A, "u16"), ("def", 0x2C, "u16"), ("spd", 0x2E, "u16"),
           ("spc", 0x30, "u16"), ("pp1", 0x32, "u8"), ("pp2", 0x33, "u8"), ("pp3", 0x34, "u8"), ("pp4", 0x35, "u8"),
           ("unk_36", 0x36, "u16"), ("unk_38", 0x38, "u16"), ("unk_3A", 0x3A, "u16"), ("unk_3C", 0x3C, "u16"),
           ("unk_3E", 0x3E, "u16"), ("unk40", 0x40, "bytes:4"), ("last_move", 0x44, "u8"),
           ("moveCache", 0x45, "bytes:5")] + \
          [("unk_%02X" % o, o, "u8") for o in range(0x4A, 0x54)] + [("unk_54", 0x54, "u16")] + \
          [("unk_%02X" % o, o, "u8") for o in range(0x56, 0x5A)] + \
          [("used_move", 0x5A, "u8"), ("unk_5B", 0x5B, "u8"), ("stage_atk", 0x5C, "u8"), ("stage_def", 0x5D, "u8"),
           ("stage_spd", 0x5E, "u8"), ("stage_spc", 0x5F, "u8"), ("stage_acc", 0x60, "u8"), ("stage_eva", 0x61, "u8"),
           ("unk62", 0x62, "bytes:2")]
STATE = [("unk00", 0x00, "bytes:4"), ("action", 0x04, "u32"), ("party_index", 0x08, "s32"), ("unk_0C", 0x0C, "s32"),
         ("action_state", 0x10, "u32"), ("unk_14", 0x14, "s32"), ("unk_18", 0x18, "u32"), ("unk_1C", 0x1C, "u8"),
         ("unk_1E", 0x1E, "u16"), ("unk20", 0x20, "bytes:8"), ("unk_28", 0x28, "u16")] + \
        [("unk_%02X" % o, o, "u8") for o in range(0x2A, 0x30)] + \
        [("unk_30", 0x30, "u16"), ("unk32", 0x32, "bytes:2"), ("unk_34", 0x34, "u16"), ("idle_anim", 0x36, "u16")] + \
        [("rt." + n, 0x38 + o, t) for n, o, t in RUNTIME] + \
        [("unk_9C", 0x9C, "bytes:16"), ("unk_AC", 0xAC, "bytes:16"), ("model", 0xBC, "s16"), ("unk_BE", 0xBE, "s16"),
         ("unkC0", 0xC0, "u8"), ("illegal1", 0xC1, "u8"), ("illegal2", 0xC2, "u8"), ("illegal3", 0xC3, "u8"),
         ("illegal4", 0xC4, "u8")]
SCENE = [("unk_08", 0x08, "s8"), ("unk_0C", 0x0C, "s32"), ("unk_10", 0x10, "s32"), ("unk_14", 0x14, "s32"),
         ("unk_18", 0x18, "u16"), ("unk_1A", 0x1A, "u8"), ("phase", 0x1C, "s32"), ("step", 0x20, "s32"),
         ("unk_24", 0x24, "s32"), ("unk_2A", 0x2A, "u16"), ("attacker", 0x2C, "u16"), ("unk_2E", 0x2E, "u16")] + \
        [("unk_%02X" % o, o, "s32") for o in range(0x30, 0x54, 4)] + \
        [("unk_54", 0x54, "u8"), ("unk_56", 0x56, "s16"), ("unk_58", 0x58, "s16"), ("unk_78", 0x78, "u8"),
         ("unk_7A", 0x7A, "s16")]
# fields that follow the presentation's frame timing, not the battle logic: unk_36 is the model's event-track state
# (an idle animation started by a per-frame BattleAnim_RandomRange draw, cleared near its end); BattleScene.unk_7A is
# the camera shake's angle, advanced by 0x38E0 every frame (battle_engine_3055E0.c)
PRESENTATION = {"0.idle_anim", "1.idle_anim", "scene.unk_7A"}
# presentation: camera and effect floats (not battle logic; float math may differ slightly between the CPUs)
SCENE_FLOATS = [("f_%02X" % o, o, "f32") for o in (0x5C, 0x60, 0x68, 0x6C, 0x70, 0x74, 0x84)] + \
               [("v_%02X.%s" % (o, "xyz"[i]), o + 4 * i, "f32") for o in (0x94, 0xA0, 0xB8, 0xC4, 0xD0) for i in range(3)]
# only the bytes the port fills like the N64 (the rest are padding or unused)
MQ_NAMES = [(0x048 + 0x40 * i, "q%02X" % (0x048 + 0x40 * i)) for i in range(16)] + [(0x448, "q448a"), (0x488, "q448b")]

FMT = {"u8": "B", "s8": "b", "u16": "H", "s16": "h", "u32": "I", "s32": "i", "f32": "f"}


def decode(layout, data, endian):
    out = {}
    for name, off, typ in layout:
        if typ.startswith("bytes:"):
            n = int(typ[6:])
            out[name] = data[off:off + n].hex()
        else:
            out[name] = struct.unpack_from(endian + FMT[typ], data, off)[0]
    return out


def decode_event(e, endian):
    d = {"event": e["name"], "dmg": e["dmg"]}
    for s in (0, 1):
        for k, v in decode(STATE, e["st"][s], endian).items():
            d["%d.%s" % (s, k)] = v
    for k, v in decode(SCENE, e["scene"], endian).items():
        d["scene." + k] = v
    for off, name in MQ_NAMES:
        q = e["mq"][off - 0x48:off - 0x48 + 0x40]
        end = len(q.rstrip(b"\0"))
        d["mq." + name] = q[:end].hex()
    return d


def decode_floats(e, endian):
    return decode(SCENE_FLOATS, e["scene"], endian)


SIDE_ENDIAN = {"port": "<", "ares": ">"}


def names():
    species, moves = {}, {}
    import re
    for line in Path(ROOT, "game", "include", "pokemon.h").read_text(encoding="utf-8").splitlines():
        m = re.match(r"#define (\w+)\s+(\d+)\s*$", line)
        if m and 1 <= int(m.group(2)) <= 151:
            species.setdefault(int(m.group(2)), m.group(1).replace("_", " ").title())
    for line in Path(ROOT, "game", "include", "moves.h").read_text(encoding="utf-8").splitlines():
        m = re.match(r"#define (\w+)\s+(0x[0-9A-Fa-f]+|\d+)", line)
        if m:
            moves.setdefault(int(m.group(2), 0), m.group(1).replace("_", " ").title())
    return species, moves


# ---- compare ----
def cmd_compare(out, args):
    tr = {s: parse_trace(os.path.join(out, s + ".trace")) for s in ("port", "ares")}
    species, moves = names()
    rep = ["# Battle cross-check: port vs ares", ""]
    ev = {s: [r for r in tr[s] if r["t"] == "E"] for s in tr}
    rep.append("Events: port %d, ares %d. Forced random calls: %s." % (len(ev["port"]), len(ev["ares"]), ", ".join(
        "%s port %d / ares %d" % (fn, sum(1 for r in tr["port"] if r["t"] == "R" and r["fn"] == fn),
                                  sum(1 for r in tr["ares"] if r["t"] == "R" and r["fn"] == fn)) for fn in RANDOM_FNS)))
    rep.append("")

    # 1. the random streams: same calls, same arguments (the argument comes from the battle state), in the same order
    rep.append("## Random calls")
    rep.append("")
    first_rand = None
    streams = [(fn, None) for fn in LOGIC_FNS]
    for fn in RANDOM_FNS[3:]:  # the animation streams are kept per argument
        args_ = sorted({r["arg"] for s in tr for r in tr[s] if r["t"] == "R" and r["fn"] == fn})
        streams += [(fn, x) for x in args_]
    for fn, arg in streams:
        a = [r for r in tr["port"] if r["t"] == "R" and r["fn"] == fn and arg in (None, r["arg"])]
        b = [r for r in tr["ares"] if r["t"] == "R" and r["fn"] == fn and arg in (None, r["arg"])]
        if arg is not None:
            fn = "%s(%d)" % (fn, arg)
        bad = next((i for i, (x, y) in enumerate(zip(a, b)) if (x["arg"], x["val"]) != (y["arg"], y["val"])), None)
        n = min(len(a), len(b))
        if bad is None:
            rep.append("- %s: %d calls compared, all identical%s" % (
                fn, n, "" if len(a) == len(b) else " (port made %d, ares %d)" % (len(a), len(b))))
        else:
            x, y = a[bad], b[bad]
            rep.append("- %s: call %d differs: port arg %d -> %d, ares arg %d -> %d" % (fn, bad, x["arg"], x["val"],
                                                                                    y["arg"], y["val"]))
            if fn in LOGIC_FNS and first_rand is None:
                first_rand = (fn, bad)
    # logic order: random calls and events interleaved
    def seq(s):
        return [(r["t"], r.get("fn") or r.get("name"), r.get("arg")) for r in tr[s]
                if r["t"] == "E" or (r["t"] == "R" and r["fn"] in LOGIC_FNS)]
    sa, sb = seq("port"), seq("ares")
    div = next((i for i, (x, y) in enumerate(zip(sa, sb)) if x != y), None)
    rep.append("- order of battle-logic random calls and events: " +
               ("identical over %d entries" % min(len(sa), len(sb)) if div is None else
                "diverges at entry %d: port %s, ares %s" % (div, sa[div], sb[div])))
    rep.append("")

    # 2. per event state
    dec = {s: [decode_event(e, SIDE_ENDIAN[s]) for e in ev[s]] for s in ev}
    first = None
    diffs_per_event = []
    pres_diffs = []
    for i in range(min(len(dec["port"]), len(dec["ares"]))):
        p, a = dec["port"][i], dec["ares"][i]
        diff = [k for k in p if p[k] != a.get(k) and k not in PRESENTATION]
        diffs_per_event.append(diff)
        pres = [k for k in p if p[k] != a.get(k) and k in PRESENTATION]
        if pres:
            pres_diffs.append((i, pres, [(p[k], a.get(k)) for k in pres]))
        if diff and first is None:
            first = i
    # turn table
    rep.append("## Turns")
    rep.append("")
    rep.append("| # | event | attacker | side 0 (HP, status, move, PP) | side 1 (HP, status, move, PP) | damage | "
               "messages | match |")
    rep.append("|---|---|---|---|---|---|---|---|")

    def mon(d, s):
        sp = species.get(d["%d.rt.species" % s], "#%d" % d["%d.rt.species" % s])
        mv = d["%d.rt.used_move" % s]
        pps = "/".join(str(d["%d.rt.pp%d" % (s, j)] & 0x3F) for j in range(1, 5))
        return "%s %d/%d st%02x %s pp %s" % (sp, d["%d.rt.hp" % s], d["%d.rt.max_hp" % s], d["%d.rt.status" % s],
                                            moves.get(mv, "#%d" % mv) if mv not in (0, 0xFF) else "-", pps)
    turn = 0
    for i, d in enumerate(dec["port"]):
        if d["event"] == "turn":
            turn += 1
        a = dec["ares"][i] if i < len(dec["ares"]) else None
        msgs = " / ".join(text(d[k]) for k in sorted(d) if k.startswith("mq.") and text(d[k]))[:60]
        status = "-" if a is None else ("yes" if not diffs_per_event[i] else "**%d fields**" % len(diffs_per_event[i]))
        rep.append("| %d | t%d %s | %d | %s | %s | %d | %s | %s |" % (i, turn, d["event"], d["scene.attacker"],
                                                                     mon(d, 0), mon(d, 1), d["dmg"], msgs, status))
    rep.append("")

    # 3. the first difference, with context
    rep.append("## First difference")
    rep.append("")
    if first is None and first_rand is None and len(dec["port"]) == len(dec["ares"]):
        rep.append("None: every event's battle state is identical on both sides (%d events; presentation fields "
                   "excluded, see below)." % len(diffs_per_event))
    if first is not None:
        p, a = dec["port"][first], dec["ares"][first]
        e = ev["port"][first]
        rep.append("Event %d (%s; port frame %d, ares frame %d):" % (first, p["event"], e["frame"],
                                                                   ev["ares"][first]["frame"]))
        rep.append("")
        rep.append("| field | port | ares | previous event (both) |")
        rep.append("|---|---|---|---|")
        prev = dec["port"][first - 1] if first else {}
        for k in diffs_per_event[first][:40]:
            rep.append("| %s | %s | %s | %s |" % (k, fmt(p[k]), fmt(a.get(k)), fmt(prev.get(k, ""))))
        rep.append("")
        # random calls leading up to it
        def calls_before(s, idx):
            out_ = []
            for r in tr[s]:
                if r["t"] == "E" and r["n"] == idx:
                    break
                if r["t"] == "R" and r["fn"] in LOGIC_FNS:
                    out_.append("%s(%d)=%d" % (r["fn"].replace("Battle", "B"), r["arg"], r["val"]))
            return out_[-8:]
        rep.append("Last random calls before it: port %s; ares %s" % (" ".join(calls_before("port", first)),
                                                                     " ".join(calls_before("ares", first))))
        rep.append("")
        later = [i for i, d in enumerate(diffs_per_event) if d]
        rep.append("Events with differences: %d of %d (%s)" % (len(later), len(diffs_per_event),
                                                              " ".join(map(str, later[:30]))))
        # fields that ever differ
        fields = {}
        for d in diffs_per_event:
            for k in d:
                fields[k] = fields.get(k, 0) + 1
        rep.append("")
        rep.append("Fields that differ (events): " + ", ".join("%s (%d)" % kv for kv in sorted(fields.items())))
    if first_rand:
        rep.append("")
        rep.append("First random-call difference: %s call %d." % first_rand)
    rep.append("")
    rep.append("## Presentation and timing (not battle logic)")
    rep.append("")
    for i, keys, vals in pres_diffs:
        rep.append("- event %d (%s): %s" % (i, dec["port"][i]["event"], ", ".join(
            "%s port %s / ares %s" % (k, v[0], v[1]) for k, v in zip(keys, vals))))
    # frame timing: both sides' frame of each event and animation start (the port runs its own clock)
    rep.append("")
    rep.append("Frame of each event (ares minus port; growth = where the port is faster):")
    rep.append("")
    rep.append("| # | event | port frame | ares frame | ares - port | growth |")
    rep.append("|---|---|---|---|---|---|")
    last = None
    for i in range(min(len(ev["port"]), len(ev["ares"]))):
        d = ev["ares"][i]["frame"] - ev["port"][i]["frame"]
        rep.append("| %d | %s | %d | %d | %d | %s |" % (i, ev["port"][i]["name"], ev["port"][i]["frame"],
                                                      ev["ares"][i]["frame"], d, "" if last is None else "%+d" % (d - last)))
        last = d
    # presentation floats (informational)
    fd = []
    for i in range(min(len(ev["port"]), len(ev["ares"]))):
        p, a = decode_floats(ev["port"][i], "<"), decode_floats(ev["ares"][i], ">")
        worst = max(((abs(p[k] - a[k]), k) for k in p), default=(0, ""))
        fd.append(worst)
    if fd:
        w = max(fd)
        rep.append("")
        rep.append("Presentation (scene camera floats, not compared above): largest difference %.4g in %s." % w)
    # animations seen
    an = {s: [r for r in tr[s] if r["t"] == "A"] for s in tr}
    rep.append("")
    rep.append("Move animations: port %s; ares %s" % (
        " ".join(moves.get(r["move"], str(r["move"])) for r in an["port"]),
        " ".join(moves.get(r["move"], str(r["move"])) for r in an["ares"])))
    open(os.path.join(out, "report.md"), "w").write("\n".join(rep) + "\n")
    print("\n".join(rep[:6]))
    print("... %s" % os.path.join(out, "report.md"))


def text(hexstr):
    """a message queue as text (its bytes are characters; the first is a code)"""
    b = bytes.fromhex(hexstr)[1:]
    return "".join(chr(c) if 32 <= c < 127 and c != 124 else "" for c in b.split(b"\0")[0]).strip()


def fmt(v):
    return ("0x%x" % v if isinstance(v, int) and v > 9 else str(v)) if v is not None else "-"


# ---- animations ----
def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def luma(path, tw, th):
    """the picture as tw x th luma (box-averaged 4:3; a wider port picture is cropped to its centre 4:3)"""
    w, h, rgb = read_ppm(path)
    cw = min(w, h * 4 // 3)
    x0 = (w - cw) // 2
    out = []
    for y in range(th):
        y0, y1 = y * h // th, max(y * h // th + 1, (y + 1) * h // th)
        for x in range(tw):
            xa, xb = x0 + x * cw // tw, max(x0 + x * cw // tw + 1, x0 + (x + 1) * cw // tw)
            s = n = 0
            for yy in range(y0, y1, max(1, (y1 - y0) // 2)):
                row = yy * w * 3
                for xx in range(xa, xb, max(1, (xb - xa) // 2)):
                    i = row + xx * 3
                    s += 299 * rgb[i] + 587 * rgb[i + 1] + 114 * rgb[i + 2]
                    n += 1
            out.append(s // (n * 1000))
    return out


def half(pic, w, h):
    """a luma picture at half size (2x2 averages)"""
    return [(pic[y * 2 * w + x * 2] + pic[y * 2 * w + x * 2 + 1] + pic[(y * 2 + 1) * w + x * 2] +
             pic[(y * 2 + 1) * w + x * 2 + 1]) // 4 for y in range(h // 2) for x in range(w // 2)]


def rgb_tile(path, tw, th):
    w, h, rgb = read_ppm(path)
    cw = min(w, h * 4 // 3)
    x0 = (w - cw) // 2
    out = bytearray(tw * th * 3)
    for y in range(th):
        sy = y * h // th
        for x in range(tw):
            i = (sy * w + x0 + x * cw // tw) * 3
            out[(y * tw + x) * 3:(y * tw + x) * 3 + 3] = rgb[i:i + 3]
    return out


def ssim(a, b, w, h, win=8):
    """mean SSIM over win x win blocks of two luma pictures"""
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    tot = n = 0
    for by in range(0, h - win + 1, win):
        for bx in range(0, w - win + 1, win):
            xs = [a[(by + y) * w + bx + x] for y in range(win) for x in range(win)]
            ys = [b[(by + y) * w + bx + x] for y in range(win) for x in range(win)]
            m = len(xs)
            mx, my = sum(xs) / m, sum(ys) / m
            vx = sum((v - mx) ** 2 for v in xs) / m
            vy = sum((v - my) ** 2 for v in ys) / m
            cov = sum((xs[i] - mx) * (ys[i] - my) for i in range(m)) / m
            tot += ((2 * mx * my + c1) * (2 * cov + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))
            n += 1
    return tot / n if n else 0


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def frames_of(d):
    """the frames f_<n>.ppm by n (game frames since the animation started); a missing frame repeats the one before"""
    if not os.path.isdir(d):
        return []
    got = {int(f[2:-4]): os.path.join(d, f) for f in os.listdir(d) if f.startswith("f_") and f.endswith(".ppm")}
    out = []
    for n in range(max(got) + 1 if got else 0):
        out.append(got.get(n, out[-1] if out else got[min(got)]))
    return out


def cmd_anim(out, args):
    species, moves = names()
    pd, ad = os.path.join(out, "port_anim"), os.path.join(out, "ares_anim")
    dst = os.path.join(out, "anim")
    os.makedirs(dst, exist_ok=True)
    pm = sorted(os.listdir(pd)) if os.path.isdir(pd) else []
    am = sorted(os.listdir(ad)) if os.path.isdir(ad) else []
    maxshift = int(args.get("--max-shift", "12"))
    limit = int(args.get("--moves", "0")) or len(pm)
    rows = ["# Move animations: port vs ares", "",
            "Each move's frames from the start of its animation phase on both sides, aligned by the best time offset "
            "(ares frame = port frame + offset, searched in +-%d on 64x48 luma), then scored at 128x96 with SSIM "
            "(1 = identical), and at 64x48 (structure, less sensitive to the port's higher render resolution). Sheets: "
            "anim/<move>.png, every 6th aligned frame: port | ares | difference x4." % maxshift,
            "", "| # | move | frames | offset | SSIM 128x96 | min (frame) | SSIM 64x48 | min (frame) | mean abs diff |",
            "|---|---|---|---|---|---|---|---|---|"]
    for name in pm[:limit]:
        if name not in am:
            rows.append("| %s | (no ares capture) | | | | | |" % name)
            continue
        pf, af = frames_of(os.path.join(pd, name)), frames_of(os.path.join(ad, name))
        if not pf or not af:
            continue
        move = int(name.split("_")[1])
        lp = [luma(f, 128, 96) for f in pf]
        la = [luma(f, 128, 96) for f in af]
        tp, ta = [half(x, 128, 96) for x in lp], [half(x, 128, 96) for x in la]
        best = None
        for d in range(-maxshift, maxshift + 1):
            pairs = [(i, i + d) for i in range(len(tp)) if 0 <= i + d < len(ta)]
            if len(pairs) < len(tp) // 2:
                continue
            e = sum(sum(abs(x - y) for x, y in zip(tp[i], ta[j])) for i, j in pairs) / (len(pairs) * 64 * 48)
            if best is None or e < best[0]:
                best = (e, d, pairs)
        err, off, pairs = best
        scores, coarse = [], []
        for i, j in pairs:
            scores.append(ssim(lp[i], la[j], 128, 96))
            coarse.append(ssim(tp[i], ta[j], 64, 48))
        mean = sum(scores) / len(scores)
        mi = min(range(len(scores)), key=lambda k: scores[k])
        cmean = sum(coarse) / len(coarse)
        cmi = min(range(len(coarse)), key=lambda k: coarse[k])
        rows.append("| %s | %s (%d) | %d | %+d | %.3f | %.3f (%d) | %.3f | %.3f (%d) | %.1f |" % (
            name[:3], moves.get(move, "#%d" % move), move, len(pairs), off, mean, scores[mi], pairs[mi][0], cmean,
            coarse[cmi], pairs[cmi][0], err))
        # sheet: port | ares | |difference| x4
        tw, th = 160, 120
        pick = pairs[::6][:16]
        W, H = tw * 3, th * len(pick)
        img = bytearray(W * H * 3)
        for r, (i, j) in enumerate(pick):
            tpix, apix = rgb_tile(pf[i], tw, th), rgb_tile(af[j], tw, th)
            dpix = bytes(min(255, abs(x - y) * 4) for x, y in zip(tpix, apix))
            for c, pix in enumerate((tpix, apix, dpix)):
                for y in range(th):
                    o = ((r * th + y) * W + c * tw) * 3
                    img[o:o + tw * 3] = pix[y * tw * 3:(y + 1) * tw * 3]
        write_png(os.path.join(dst, "%s.png" % name), W, H, img)
        print(rows[-1])
    open(os.path.join(out, "anim.md"), "w").write("\n".join(rows) + "\n")
    if "--keep-frames" not in args:
        shutil.rmtree(pd, ignore_errors=True)
        shutil.rmtree(ad, ignore_errors=True)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, out = sys.argv[1], os.path.abspath(sys.argv[2])
    args, rest = {}, sys.argv[3:]
    i = 0
    while i < len(rest):
        if rest[i].startswith("--") and i + 1 < len(rest) and not rest[i + 1].startswith("--") and \
                rest[i] in ("--seed", "--frames", "--port-bin", "--max-shift", "--moves", "--anim-frames"):
            args[rest[i]] = rest[i + 1]
            i += 2
        else:
            args[rest[i]] = True
            i += 1
    {"run": cmd_run, "compare": cmd_compare, "anim": cmd_anim}[cmd](out, args)


if __name__ == "__main__":
    main()
