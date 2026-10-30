#!/usr/bin/env python3 -I
"""Runs one reproducible battle in the instrumented port (build-evidence, cmake/evidence.cmake) and resolves its
call data to function names.

Usage:
  tools/evidence/run.py <out dir> --bin build-evidence/pokestadium [--seed N] [--queue FILE] [--frames N]
                        [--watch Name1,Name2] [--seq-max N] [--ares] [--moves m1,m2,m3,m4[:p1,p2,p3,p4]]
                        [--set "Fn[#n]:sym[*][+off]...:size=value;..."]
                        [--call "AtFn[#n][@exit]:Target:ret:args;..." [--call-file F] [--call-fork] [--call-buf HEX]]
                        [--gb ROM:SAVE] [--gb2 ROM:SAVE] [--rec RECORDING [--flash STADIUM.flash] [--natural]] [--dump-every N]
--gb puts a Game Boy cartridge in the Transfer Pak (a copy of SAVE; tools/evidence/gb_party.py writes any party into
a save); --rec replaces the battle_xcheck route with another input recording (from boot), e.g. a Free Battle with a
Game Pak party.
--set forces a condition in memory at entry of Fn ("Fn[#n]@exit": when that call returns) (src/port/evidence/evidence_rt.c PORT_EVID_SET), symbols by name.
coverage.profraw: branch/line counters (llvm-cov; tools/evidence/frontier.py).

The battle is tools/battle_xcheck.py's (Poke Cup round 1, the same rentals, the player's moves picked by the
game's time-limit fallback; every battle random value forced from the seed, after the values queued in FILE:
lines "<random function> <value>"). With --ares the same battle also runs in ares (tools/ares_ref, ARES_REF_BUILD)
for the event-by-event comparison (tools/battle_xcheck.py compare <out dir>).
Output in <out dir>: calls.txt / seq.txt (raw, from src/port/evidence/evidence_rt.c), port.trace (xcheck events),
coverage.json: {"functions": {name: {"count", "first_frame",
                                     "sites": [[instrumented caller, return-address function, count, frame, ctx]]}},
                "paths": [[count, first_frame, [innermost..outer]]], "seq": [...], "watch": [...]}
"""
import bisect, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BASE = 0x100000000


def symbols(binary):
    """sorted [(offset, name)] of the text symbols (local ones too: nm, not dladdr)"""
    out = subprocess.run(["nm", "-n", binary], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tT":
            name = p[2][1:] if p[2].startswith("_") else p[2]
            syms.append((int(p[0], 16) - BASE, name))
    return syms


class Resolver:
    def __init__(self, syms):
        self.syms = syms
        self.addrs = [a for a, _ in syms]
        self.by_name = {}
        for a, n in syms:
            self.by_name.setdefault(n, a)

    def fn(self, off):
        """the function containing offset off"""
        i = bisect.bisect_right(self.addrs, off) - 1
        return self.syms[i][1] if i >= 0 else "?"


def data_symbols(binary):
    out = subprocess.run(["nm", binary], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "dDbBsS":
            syms[p[2][1:] if p[2].startswith("_") else p[2]] = int(p[0], 16) - BASE
    return syms


DATA = {}


def low_priority():
    """niceness 15, or the current one when the run already has a lower priority (raising it is not allowed)"""
    os.setpriority(os.PRIO_PROCESS, 0, max(15, os.getpriority(os.PRIO_PROCESS, 0)))


def translate_set(spec, res):
    """"Fn[#n]:sym*+0x5A:1=68" -> "<fn off>[#n]:<sym off>*+0x5A:1=68" """
    import re
    fn, addr, rest = spec.split(":", 2)
    at = ""
    if fn.endswith("@exit"):
        fn, at = fn[:-5], "@exit"
    nth = ""
    if "#" in fn:
        fn, nth = fn.split("#")
        nth = "#" + nth
    m = re.match(r"([A-Za-z_]\w*)(.*)$", addr)
    sym, steps = m.group(1), m.group(2)
    off = DATA.get(sym, res.by_name.get(sym))
    if off is None:
        sys.exit("unknown symbol %s" % sym)
    if fn not in res.by_name:
        sys.exit("unknown function %s" % fn)
    return "0x%x%s%s:0x%x%s:%s" % (res.by_name[fn], nth, at, off, steps, rest)


def parse(out, res):
    funcs, paths = {}, []
    for line in open(os.path.join(out, "calls.txt")):
        p = line.split()
        if p[0] == "F":
            fn, parent, site = res.fn(int(p[1], 16)), int(p[2], 16), int(p[3], 16)
            count, frame = int(p[4]), int(p[5])
            ctx = p[6] if len(p) > 6 else ""
            f = funcs.setdefault(fn, {"count": 0, "first_frame": frame, "sites": []})
            f["count"] += count
            f["first_frame"] = min(f["first_frame"], frame)
            # [instrumented caller or None, function holding the return address (site-1: inside the call), count,
            #  first frame, ctx]
            f["sites"].append([res.fn(parent) if parent else None, res.fn(site - 1), count, frame, ctx])
        elif p[0] == "P":
            paths.append([int(p[1]), int(p[2]), [res.fn(int(x, 16)) for x in p[3:]]])
    seq, watch = [], []
    sp = os.path.join(out, "seq.txt")
    if os.path.exists(sp):
        for line in open(sp):
            p = line.split()
            if not p or not line.endswith("\n"):
                continue
            if p[0] == "C":
                seq.append([int(p[1]), int(p[2]), res.fn(int(p[3], 16)), res.fn(int(p[4], 16) - 1),
                            p[5] if len(p) > 5 else ""])
            elif p[0] == "W":
                kv = dict(x.split("=", 1) for x in p[5:] if "=" in x)
                watch.append({"what": p[1], "frame": int(p[2]), "fn": res.fn(int(p[3], 16)),
                              "caller": res.fn(int(p[4], 16) - 1), **kv})
    return {"functions": funcs, "paths": paths, "seq": seq, "watch": watch}


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        print(__doc__)
        sys.exit(1)
    out = os.path.abspath(args[0])
    opt = lambda k, d=None: args[args.index(k) + 1] if k in args else d
    binary = os.path.abspath(opt("--bin", os.path.join(ROOT, "build-evidence", "pokestadium")))
    os.makedirs(out, exist_ok=True)
    res = Resolver(symbols(binary))
    DATA.update(data_symbols(binary))
    env = dict(os.environ, PORT_EVID=os.path.join(out, "calls.txt"), PORT_EVID_SEQ=os.path.join(out, "seq.txt"),
               PORT_EVID_SEQ_MAX=opt("--seq-max", "40"))
    if opt("--watch"):
        offs = []
        for name in opt("--watch").split(","):
            if name not in res.by_name:
                sys.exit("unknown function %s" % name)
            offs.append("0x%x" % res.by_name[name])
        env["PORT_EVID_WATCH"] = ",".join(offs)
    if opt("--moves"):  # port-only: the player's active Pokemon gets these moves (src/port/evidence/evidence_rt.c)
        env["PORT_EVID_MOVES"] = opt("--moves")
        env["PORT_EVID_INJECT_AT"] = "0x%x" % res.by_name[opt("--inject-at", "BattleScene_HandlePlayerActionInput")]
    if opt("--dump-every"):
        env["PORT_EVID_DUMP_EVERY"] = opt("--dump-every")
    if opt("--set"):
        env["PORT_EVID_SET"] = ";".join(translate_set(x, res) for x in opt("--set").split(";") if x)
    call_specs = opt("--call").split(";") if opt("--call") else []
    if opt("--call-file"):  # one spec per line (tools/evidence/dead_calls.py), "#" lines ignored
        call_specs += [l.strip() for l in open(opt("--call-file")) if l.strip() and not l.startswith("#")]
    if "--call-fork" in args:
        env["PORT_EVID_CALL_FORK"] = "1"
    if call_specs:  # direct calls (evidence_rt.c PORT_EVID_CALL): "AtFn#1:Target:i:p0,i20,i2"
        specs = []
        for x in call_specs:
            at, target, rest = x.split(":", 2)
            when = ""
            if at.endswith("@exit"):  # when that call of AtFn returns
                at, when = at[:-5], "@exit"
            nth = ""
            if "#" in at:
                at, nth = at.split("#")
                nth = "#" + nth
            if target not in res.by_name:
                continue  # (inlined everywhere or stripped: nothing to call)
            specs.append("0x%x%s%s:0x%x:%s" % (res.by_name[at], nth, when, res.by_name[target], rest))
        env["PORT_EVID_CALL"] = ";".join(specs)
        if opt("--call-buf"):
            env["PORT_EVID_CALL_BUF"] = opt("--call-buf")
    env["LLVM_PROFILE_FILE"] = os.path.join(out, "coverage.profraw")
    seed = opt("--seed", "1")
    if opt("--queue"):
        seed += ":" + os.path.abspath(opt("--queue"))
    cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "battle_xcheck.py"), "run", out, "--seed", seed,
           "--frames", opt("--frames", "20000"), "--port-bin", binary]
    if "--ares" not in args:
        cmd.append("--port-only")
    json.dump({"seed": opt("--seed", "1"), "queue": open(opt("--queue")).read().split("\n") if opt("--queue") else [],
               "moves": opt("--moves"), "set": opt("--set"), "frames": opt("--frames", "20000"), "ares": "--ares" in args,
               "gb": opt("--gb"), "gb2": opt("--gb2"), "rec": opt("--rec"), "flash": opt("--flash"),
               "natural": "--natural" in args, "call": opt("--call"), "call_buf": opt("--call-buf"),
               "setup": ("own recording " + os.path.basename(opt("--rec"))) if opt("--rec") else
               "tools/battle_xcheck.py route(): Poke Cup round 1, Charmander/Venusaur/Ivysaur vs the Biker"},
              open(os.path.join(out, "scenario.json"), "w"), indent=1)
    if opt("--gb"):  # Transfer Pak cartridge: ROM and a copy of the save (the game writes it back)
        import shutil
        rom, sav = opt("--gb").split(":", 1)
        shutil.copyfile(sav, os.path.join(out, "gb.sav"))
        env["PORT_GB_ROM"] = os.path.abspath(rom)
        env["PORT_GB_SAVE"] = os.path.join(out, "gb.sav")
    if opt("--gb2"):  # a second cartridge (controller 2): the Trade Machine needs two
        import shutil
        rom, sav = opt("--gb2").split(":", 1)
        shutil.copyfile(sav, os.path.join(out, "gb2.sav"))
        env["PORT_GB_ROM2"] = os.path.abspath(rom)
        env["PORT_GB_SAVE2"] = os.path.join(out, "gb2.sav")
    if opt("--rec"):
        # another route (e.g. a Free Battle with a Game Pak party): run the port directly, with the battle_xcheck
        # switches (forced random stream, the player's automatic choices, the event trace)
        import shutil
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import battle_xcheck as bx
        shutil.copyfile(opt("--flash", os.path.join(ROOT, "replays", "default.flash")), os.path.join(out, "port.flash"))
        env.update(PORT_REPLAY=os.path.abspath(opt("--rec")), PORT_STOP_AT=opt("--frames", "20000"), PORT_SPEED="max",
                   PORT_FLASH_FILE=os.path.join(out, "port.flash"), PORT_XCHECK=os.path.join(out, "port.trace"),
                   PORT_XCHECK_RNG=seed, PORT_XCHECK_COM="1",
                   # in the background: the window is drawn but never shown (Engine.cpp), and the game is a macOS
                   # accessory app, without a Dock icon and not taking the focus (evidence_rt.c)
                   PORT_HIDDEN_WINDOW="1", PORT_EVID_BACKGROUND="1")
        if "--natural" in args:  # a recording made with the game's own randomness and input: no forcing
            for k in ("PORT_XCHECK_RNG", "PORT_XCHECK_COM"):
                env.pop(k, None)
        with open(os.path.join(out, "port.log"), "w") as log:
            subprocess.run([binary, bx.ROM], cwd=os.path.dirname(binary), env=env, stdout=log, stderr=subprocess.STDOUT,
                           preexec_fn=low_priority)
    else:
        subprocess.run(cmd, env=env, check=True)
    cov = parse(out, res)
    json.dump(cov, open(os.path.join(out, "coverage.json"), "w"))
    print("%d functions reached, %d call paths, %d sequence lines, %d watch records" % (
        len(cov["functions"]), len(cov["paths"]), len(cov["seq"]), len(cov["watch"])))


if __name__ == "__main__":
    main()
