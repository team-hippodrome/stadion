#!/usr/bin/env python3 -I
"""Host values of C expressions (offsetof, sizeof) in the context of one game source file, compiled with that file's
own flags from the instrumented build's compile_commands.json (its statics and types resolve), for run.py --set.

Usage: offs.py <build dir> <source path suffix> "<expr>" ...
e.g.   offs.py build-evidence-all battle_engine_309ED0.c "__builtin_offsetof(__typeof__(*sBattleScene.unk_00), unk_38)"
Prints one "<value>\t<expr>" line per expression (integer constant expressions only).
"""
import json, os, re, subprocess, sys, tempfile


def main():
    build, suffix, exprs = sys.argv[1], sys.argv[2], sys.argv[3:]
    cc = json.load(open(os.path.join(build, "compile_commands.json")))
    entry = next(e for e in cc if e["file"].endswith(suffix))
    lines = ['#include "%s"' % entry["file"]]
    lines += ["long probe_v_%d = (long)(%s);" % (i, x) for i, x in enumerate(exprs)]
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, "probe.c")
        open(probe, "w").write("\n".join(lines) + "\n")
        cmd = entry["command"].split(" -o ")[0].split() + ["-O1", "-S", "-emit-llvm", "-o", "-", probe]
        cmd = [a for a in cmd if not a.startswith(("-finstrument", "-fprofile-instr", "-fcoverage"))]
        r = subprocess.run(cmd, cwd=entry["directory"], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(r.stderr[-3000:])
    vals = dict(re.findall(r"^@probe_v_(\d+) = .*? i64 (-?\d+)", r.stdout, re.M))
    for i, x in enumerate(exprs):
        print("%s\t%s" % (vals.get(str(i), "?"), x))


if __name__ == "__main__":
    main()
