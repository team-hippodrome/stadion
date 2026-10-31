#!/usr/bin/env python3 -I
"""Which unreached functions does nothing reference: no caller or address-taking in the C code of the whole decomp
(callgraph.py over all game sources) and no word in the ROM equal to their N64 address (raw function addresses in
data: tables the port resolves through src/port/generated/port_func_table.c). Those are dead in the shipped game
unless asm code reaches them; runtime evidence for them needs a direct call (run.py --call).

Usage:
  tools/evidence/deadcode.py <whole-game callgraph.json> <rom.z64> <out.md> <name list file | -> 
The name list: the functions to check (one per line, e.g. the unreached ones from frontier.json); "-": all.
"""
import collections, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def addresses():
    """name -> N64 link address, from the port's function table"""
    src = open(os.path.join(ROOT, "src", "port", "generated", "port_func_table.c")).read()
    names = dict(re.findall(r"extern void (port_fn_\d+)\(\) __asm__\(PORT_ASM_NAME\(\"(\w+)\"\)\);", src))
    out = {}
    for base, vaddr, fn in re.findall(r"\{ (0x[0-9A-F]+), (0x[0-9A-F]+), \(void\*\)(port_fn_\d+) \}", src):
        if fn in names:
            out[names[fn]] = int(vaddr, 16)
    return out


def main():
    cg_path, rom_path, out, names_path = sys.argv[1:5]
    cg = json.load(open(cg_path))
    F = cg["functions"]
    refs = collections.defaultdict(set)
    for a, b, k, v in cg["edges"]:
        if a != b:
            refs[b].add(a)
    for t, fns in cg["tables"].items():
        for f in fns:
            refs[f].add("table " + t)
    rom = open(rom_path, "rb").read()
    addr = addresses()
    names = [n for n in F if not F[n]["external"]] if names_path == "-" else \
        [l.strip() for l in open(names_path) if l.strip()]
    c_ref, data_ref, none, unknown = [], [], [], []
    for n in names:
        if refs.get(n):
            c_ref.append(n)
        elif n not in addr:
            unknown.append(n)
        elif rom.find(struct.pack(">I", addr[n])) >= 0:
            data_ref.append(n)
        else:
            none.append(n)
    area = lambda n: (re.search(r"fragments/([^/]+)/", F.get(n, {}).get("file", "")) or
                      re.search(r"([^/]+)$", F.get(n, {}).get("file", "?"))).group(1)
    md = ["# Functions nothing references", "", "Checked: %d. With a C reference (caller, address taken or pointer "
          "table): %d. Referenced only by a raw address word in the ROM: %d. **No reference at all: %d.** Not in the "
          "port's function table: %d." % (len(names), len(c_ref), len(data_ref), len(none), len(unknown)), ""]
    by = collections.defaultdict(list)
    for n in none:
        by[area(n)].append(n)
    md += ["## No reference at all, per area", "", "| area | functions | names |", "|---|---|---|"]
    for a, ns in sorted(by.items(), key=lambda kv: -len(kv[1])):
        md.append("| %s | %d | %s |" % (a, len(ns), ", ".join(sorted(ns))))
    md += ["", "## Referenced only from data (%d)" % len(data_ref), "", ", ".join(sorted(data_ref))]
    open(out, "w").write("\n".join(md) + "\n")
    print("%d checked: %d C-referenced, %d data-only, %d no reference, %d unknown" % (
        len(names), len(c_ref), len(data_ref), len(none), len(unknown)))


if __name__ == "__main__":
    main()
