#!/usr/bin/env python3 -I
"""What a set of runs reaches that a baseline does not: functions and source lines (llvm-cov line counts of the
instrumented files). For judging a lever: a Game Pak party, a save, forced memory.

Usage:
  tools/evidence/covdiff.py <binary> <out.md> --base <run dir>... --new <run dir>...
"""
import collections, json, os, subprocess, sys, tempfile


def tool(name):
    return subprocess.run(["xcrun", "-f", name], capture_output=True, text=True, check=True).stdout.strip()


def covered(binary, runs):
    """{file: set(lines with count > 0)}, set(functions with count > 0) for the instrumented files"""
    raws = [os.path.join(r, "coverage.profraw") for r in runs if os.path.exists(os.path.join(r, "coverage.profraw"))]
    fd, prof = tempfile.mkstemp(suffix=".profdata")
    os.close(fd)
    subprocess.run([tool("llvm-profdata"), "merge", "-sparse", "-o", prof] + raws, check=True)
    lcov = subprocess.run([tool("llvm-cov"), "export", "-format=lcov", "-instr-profile", prof, binary],
                          capture_output=True, text=True, check=True).stdout
    os.remove(prof)
    lines, fns, cur = collections.defaultdict(set), set(), None
    for l in lcov.splitlines():
        if l.startswith("SF:"):
            cur = l[3:] if "fragments/" in l else None
        elif cur and l.startswith("DA:"):
            n, c = l[3:].split(",")[:2]
            if int(c) > 0:
                lines[cur].add(int(n))
        elif cur and l.startswith("FNDA:"):
            c, name = l[5:].split(",", 1)
            if int(c) > 0:
                fns.add(name.split(":")[-1])
    return lines, fns


def main():
    a = sys.argv[1:]
    binary, out = a[0], a[1]
    base = a[a.index("--base") + 1:a.index("--new")]
    new = a[a.index("--new") + 1:]
    bl, bf = covered(binary, base)
    nl, nf = covered(binary, new)
    add_f = sorted(nf - bf)
    md = ["# Coverage added", "", "Baseline: %d runs; new: %s." % (len(base), ", ".join(os.path.basename(r.rstrip("/"))
                                                                                      for r in new)), "",
          "Functions: baseline %d, new runs %d, **%d only in the new runs**." % (len(bf), len(nf), len(add_f)), ""]
    md += ["%s" % ", ".join(add_f) if add_f else "(none)", "", "## Source lines only the new runs reached", ""]
    total = 0
    for f in sorted(nl):
        extra = sorted(nl[f] - bl.get(f, set()))
        if not extra:
            continue
        total += len(extra)
        src = open(f, errors="replace").read().split("\n")
        md.append("### %s (%d lines)" % (f.split("/game/", 1)[-1], len(extra)))
        md.append("")
        md.append("```")
        prev = None
        for n in extra[:60]:
            if prev is not None and n != prev + 1:
                md.append("...")
            md.append("%5d  %s" % (n, src[n - 1].rstrip()[:110]))
            prev = n
        if len(extra) > 60:
            md.append("... %d more" % (len(extra) - 60))
        md.append("```")
        md.append("")
    md.insert(5, "Lines: %d lines only in the new runs." % total)
    open(out, "w").write("\n".join(md) + "\n")
    print("%d functions and %d lines only in the new runs" % (len(add_f), total))


if __name__ == "__main__":
    main()
