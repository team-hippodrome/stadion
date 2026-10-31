#!/usr/bin/env python3 -I
"""Finds code that depends on the N64's byte order through a union or a pointer cast, in the game sources.

Usage: tools/endian_union_sweep.py [dir ...] [--all]      (default: game/src game/include)

1. Unions that overlay a struct of narrower fields (u8/s8/u16/s16 or bitfields) with a wider integer member: on the
   N64 (big-endian, IDO allocating bitfields from the top bit) the wide member's value is the fields in declaration
   order; on a little-endian host it is the reverse. Every use of the wide member is listed and classified:
     safe:      zeroing, copying or comparing it with another value of the same union (order does not matter)
     sensitive: masks, shifts, OR/AND-assign, comparisons or assignments with constants, arithmetic, passing it on
   Unions whose wide member only has safe uses are left out unless --all.
2. Casts that read a narrower field or array through a wider type: *(u32*)&x.byteField, *(u16*)&array[i], ...
Output: one block per union (file:line, members, uses with their class) and the cast list. It is a source scan
(regular expressions), not a compiler pass: review every hit.
"""
import os, re, sys

NARROW = {"u8", "s8", "u16", "s16", "char", "unsigned char", "signed char"}
WIDTH = {"u8": 1, "s8": 1, "char": 1, "unsigned char": 1, "signed char": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4,
         "u64": 8, "s64": 8, "int": 4, "unsigned int": 4, "uintptr_t": 8}
RE_MEMBER = re.compile(r"^\s*(?:/\*[^*]*\*/\s*)?((?:unsigned |signed )?\w+)\s+(\w+)\s*(?::\s*(\d+))?\s*(\[[^\]]*\])?\s*;")


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def matching_brace(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "{":
            depth += 1
        elif s[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    return -1


def parse_union(body):
    """-> (narrow fields [(type, name, bits)], wide members [(type, name)])"""
    narrow, wide = [], []
    # inner structs
    k = 0
    while True:
        m = re.search(r"\bstruct\s*\{", body[k:])
        if not m:
            break
        a = k + m.end() - 1
        b = matching_brace(body, a)
        for line in body[a + 1:b].split(";"):
            mm = RE_MEMBER.match(line.strip() + ";")
            if mm:
                t, n, bits, arr = mm.groups()
                if bits or t in NARROW:
                    narrow.append((t, n, bits))
        body = body[:m.start() + k] + " " * (b + 1 - (m.start() + k)) + body[b + 1:]
        k = 0
    for line in body.split(";"):
        mm = RE_MEMBER.match(line.strip() + ";")
        if mm:
            t, n, bits, arr = mm.groups()
            if t in WIDTH and not arr:
                wide.append((t, n))
    return narrow, wide


SAFE = [
    r"\.\s*NAME\s*=\s*0\s*;", r"->\s*NAME\s*=\s*0\s*;",                       # zeroing
    r"(\.|->)\s*NAME\s*==\s*[\w\]\[\.\->()]+(\.|->)\s*NAME\b",                # same-type compare
    r"(\.|->)\s*NAME\s*!=\s*[\w\]\[\.\->()]+(\.|->)\s*NAME\b",
    r"(\.|->)\s*NAME\s*=\s*[\w\]\[\.\->()]+(\.|->)\s*NAME\s*;",               # copy
    r"(\.|->)\s*NAME\s*(==|!=)\s*0\b", r"(\.|->)\s*NAME\s*\)",                # zero test, if (x.raw)
]


def classify(line, name):
    for rx in SAFE:
        if re.search(rx.replace("NAME", re.escape(name)), line):
            return "safe"
    return "sensitive"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    roots = args or ["game/src", "game/include"]
    show_all = "--all" in sys.argv
    files = []
    for root in roots:
        for d, _, fs in os.walk(root):
            for f in fs:
                if f.endswith((".c", ".h")):
                    files.append(os.path.join(d, f))
    texts = {f: open(f, errors="replace").read() for f in files}
    clean = {f: strip_comments(t) for f, t in texts.items()}
    unions = []
    for f, s in clean.items():
        for m in re.finditer(r"\bunion\s*\w*\s*\{", s):
            a = m.end() - 1
            b = matching_brace(s, a)
            if b < 0:
                continue
            narrow, wide = parse_union(s[a + 1:b])
            if len(narrow) >= 2 and wide:
                tail = s[b + 1:b + 120]
                tname = re.match(r"\s*(\w*)", tail).group(1)
                # the enclosing typedef name, for reporting
                td = re.search(r"\}\s*(\w+)\s*;", s[b:])
                line = s[:m.start()].count("\n") + 1
                unions.append((f, line, narrow, wide, tname, td.group(1) if td else ""))
    out = []
    n_sens = 0
    for f, line, narrow, wide, tname, td in unions:
        uses = []
        for t, name in wide:
            if name in ("unk_00", "unk_000", "unk_0", "data", "value"):  # too generic to grep reliably alone
                pat = re.compile(r"(\.|->)\s*%s\b" % re.escape(name))
            else:
                pat = re.compile(r"(\.|->)\s*%s\b" % re.escape(name))
            for g, s in clean.items():
                for ln, text in enumerate(s.split("\n"), 1):
                    if pat.search(text):
                        uses.append((g, ln, name, classify(text, name), texts[g].split("\n")[ln - 1].strip()))
        sens = [u for u in uses if u[3] == "sensitive"]
        if not sens and not show_all:
            continue
        n_sens += len(sens)
        out.append("## %s:%d  (union in %s)" % (f, line, td or tname or "?"))
        out.append("   narrow: " + ", ".join("%s %s%s" % (t, n, ":" + b if b else "") for t, n, b in narrow))
        out.append("   wide:   " + ", ".join("%s %s" % w for w in wide))
        for g, ln, name, cls, text in uses:
            out.append("   %-9s %s:%d  %s" % (cls, g, ln, text[:150]))
        out.append("")
    casts = []
    rx = re.compile(r"\*\s*\(\s*(u16|s16|u32|s32|u64|s64)\s*\*\s*\)\s*&\s*([\w\[\]\.\->]+)")
    for g, s in clean.items():
        for ln, text in enumerate(s.split("\n"), 1):
            for m in rx.finditer(text):
                casts.append("   %s:%d  %s" % (g, ln, texts[g].split("\n")[ln - 1].strip()[:150]))
    print("# Byte-order sweep: %d unions with narrow fields under a wide member, %d listed, %d uses to review; "
          "%d pointer casts to a wider type\n" % (len(unions), len(out) and sum(1 for l in out if l.startswith("## ")),
                                                 n_sens, len(casts)))
    print("\n".join(out))
    print("# Pointer casts to a wider type\n")
    print("\n".join(casts))


if __name__ == "__main__":
    main()
