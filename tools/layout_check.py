#!/usr/bin/env python3 -I
"""Struct layout checker: finds the places where the 64-bit host layout breaks code that relied on the N64 layout.

The decomp annotates every struct field with its N64 offset (`/* 0x18 */ s16 unk_18;`). On the host, pointers are 8
bytes, so every field after a pointer moves. That alone is harmless; it breaks code when the same memory is accessed
through two struct types whose fields agree on the N64 but not on the host (a "view" pair), e.g. BattleSessionTeams
passed to a function taking unk_func_8001C248*.

How it works:
  1. clang -fdump-record-layouts on every game source: the real host offset of every field.
  2. the same with a 32-bit big-endian MIPS target: the exact N64 offset of every field. (The decomp's offset
     comments are only cross-checked against it: they contain typos.)
  3. clang -ast-dump on every game source: every conversion between two different struct pointer types (explicit
     casts and implicit ones such as passing an argument of another type), with its file:line.
  4. for each such pair, every N64 offset where both types start a field must map to the same host offset in both.

Output: build/layout/report.txt (pairs with mismatching fields, most-used first), build/layout/summary.txt and
build/layout/comment_errors.txt (offset comments in the decomp that disagree with the real N64 layout).
Usage: tools/layout_check.py [-j N] [file.c ...]     (default: all game sources)
"""
import collections, concurrent.futures, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "layout")
FLAGS = [l.strip() for l in open(os.path.join(ROOT, "game", "compile_flags.txt")) if l.strip()]
FLAGS = [f for f in FLAGS if f != "-w"] + ["-w", "-DPORT_NO_AUDIO", "-DPORT_STATIC_FRAGMENTS"]


def clang(args, src, mips=False):
    # mips=True compiles for a 32-bit big-endian MIPS target: clang then lays structs out exactly as on the N64
    target = ["--target=mips-unknown-linux-gnu", "-idirafter", "tools/mips_sysroot"] if mips else []
    r = subprocess.run(["clang", "-fsyntax-only"] + target + FLAGS + args + [src], cwd=ROOT, capture_output=True,
                       text=True, errors="replace")
    return r.stdout


# ---- 1. host layouts ---------------------------------------------------------------------------------------------
REC_HEAD = re.compile(r"^\s+0 \| (struct|union) (.+?)\s*$")
FIELD = re.compile(r"^\s*(\d+)(?::\d+-\d+)? \|(\s+)(.*?)\s*$")


def parse_layouts(text):
    """{record name: {"size": n, "fields": {name: host offset}}} (nested anonymous members by their own names)."""
    out = {}
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        lines = block.strip("\n").split("\n")
        m = REC_HEAD.match(lines[0])
        if not m:
            continue
        name = m.group(2)
        fields = {}
        size = None
        stack = []  # (depth, is anonymous record) of the enclosing nested records
        for ln in lines[1:]:
            sm = re.search(r"\[sizeof=(\d+)", ln)
            if sm:
                size = int(sm.group(1))
                break
            fm = FIELD.match(ln)
            if not fm:
                continue
            depth = (len(fm.group(2)) - 1) // 2
            decl = fm.group(3)
            while stack and stack[-1][0] >= depth:
                stack.pop()
            if depth < 1:
                continue
            visible = all(anon for _, anon in stack)  # members of named nested records are not fields of this type
            rec = re.match(r"^(struct|union) (\S+)", decl)
            if rec and not re.search(r"\w\s*$", decl[len(rec.group(0)):].strip() or ""):
                pass
            if re.match(r"^(struct|union) [^ ]*\((anonymous|unnamed)", decl):
                stack.append((depth, True))
                continue
            if rec and depth >= 1:
                # a named nested record: its own name is a field here, its members are not
                stack.append((depth, False))
            if not visible:
                continue
            ident = re.findall(r"[A-Za-z_]\w*", re.sub(r"\(.*", "", decl.split("(*")[-1]) if "(*" in decl else decl)
            if not ident:
                continue
            fields.setdefault(ident[-1], int(fm.group(1)))
        out[name] = {"size": size, "fields": fields}
    return out


# ---- 2. N64 offsets from the comments ----------------------------------------------------------------------------
STRUCT_OPEN = re.compile(r"(?:typedef\s+)?(struct|union)\s*(\w*)\s*\{")
COMMENT_FIELD = re.compile(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*([^;]*?);")


def field_name(decl):
    decl = re.sub(r"\[[^\]]*\]", "", decl)
    m = re.search(r"\(\s*\*\s*(\w+)\s*\)", decl)  # function pointer
    if m:
        return m.group(1)
    decl = decl.split(":")[0]
    ids = re.findall(r"[A-Za-z_]\w*", decl)
    return ids[-1] if ids else None


def strip_named_nested(body):
    """Removes the bodies of nested struct/union members that have a name (their comments hold offsets relative to
    the inner type); anonymous ones stay, since their members are accessed as fields of the outer type."""
    out, i = [], 0
    while i < len(body):
        j = body.find("{", i)
        if j < 0:
            out.append(body[i:])
            break
        depth, k = 1, j + 1
        while k < len(body) and depth:
            depth += {"{": 1, "}": -1}.get(body[k], 0)
            k += 1
        anonymous = re.match(r"\s*;", body[k:]) is not None
        out.append(body[i:j])
        out.append(strip_named_nested(body[j + 1:k - 1]) + " " if anonymous else "{} ")
        i = k
    return "".join(out)


def top_level_fields(body):
    fields = {}
    for fm in COMMENT_FIELD.finditer(strip_named_nested(body)):
        fname = field_name(fm.group(2).split("{")[-1].split("}")[-1])
        if fname and fname not in fields:
            fields[fname] = int(fm.group(1), 16)
    return fields


def parse_comment_offsets():
    """{type name: {field: n64 offset}} for every struct/union definition whose fields carry offset comments."""
    result = {}
    for base in ("game/src", "game/include"):
        for dp, _, files in os.walk(os.path.join(ROOT, base)):
            for fn in files:
                if not fn.endswith((".h", ".c")):
                    continue
                text = open(os.path.join(dp, fn), errors="replace").read()
                for m in STRUCT_OPEN.finditer(text):
                    depth, i = 1, m.end()
                    while i < len(text) and depth:
                        depth += {"{": 1, "}": -1}.get(text[i], 0)
                        i += 1
                    body = text[m.end():i - 1]
                    tail = re.match(r"\s*(\w+)\s*;", text[i:])
                    names = [n for n in (m.group(2), tail.group(1) if tail else None) if n]
                    fields = top_level_fields(body)
                    if len(fields) < 2:
                        continue
                    for n in names:
                        result.setdefault(n, fields)
    return result


# ---- 3. conversions between struct pointer types -----------------------------------------------------------------
CAST = re.compile(r"(CStyleCastExpr|ImplicitCastExpr) 0x[0-9a-f]+ <([^>]*)> '([^']*)'(?::'([^']*)')? <BitCast>")
TYPED = re.compile(r"'([^']*)'(?::'([^']*)')?")
LOC = re.compile(r"<(/[^:>]+|[\w./-]+\.[ch]):(\d+):\d+")


def rec_of(t):
    """'struct Foo *' -> 'Foo' (single-level pointers to records only)."""
    m = re.match(r"^(?:const |volatile )*(?:struct |union )?(\w+) \*$", t.strip())
    return m.group(1) if m else None


def parse_casts(text, src):
    pairs = []
    lines = text.split("\n")
    cur_file, cur_line = src, 0
    for idx, ln in enumerate(lines):
        for lm in LOC.finditer(ln):
            cur_file, cur_line = lm.group(1), int(lm.group(2))
        lm = re.search(r"<line:(\d+):\d+", ln)
        if lm:
            cur_line = int(lm.group(1))
        m = CAST.search(ln)
        if not m:
            continue
        to_t = m.group(4) or m.group(3)
        # the operand is the next line, one level deeper
        if idx + 1 >= len(lines):
            continue
        tm = None
        for tm in TYPED.finditer(lines[idx + 1]):
            pass
        first = TYPED.search(lines[idx + 1].split(">", 1)[-1]) if ">" in lines[idx + 1] else None
        if not first:
            continue
        from_t = first.group(2) or first.group(1)
        a, b = rec_of(from_t), rec_of(to_t)
        if a and b and a != b and not cur_file.startswith(("/Applications", "/Library", "/usr")):
            pairs.append((a, b, "%s:%d" % (os.path.relpath(cur_file, ROOT) if cur_file.startswith("/") else cur_file,
                                           cur_line), m.group(1) == "ImplicitCastExpr"))
    return pairs


def analyse(src):
    lay = parse_layouts(clang(["-Xclang", "-fdump-record-layouts"], src))
    n64 = parse_layouts(clang(["-Xclang", "-fdump-record-layouts"], src, mips=True))
    casts = parse_casts(clang(["-Xclang", "-ast-dump"], src), src)
    return lay, n64, casts


def main():
    args = sys.argv[1:]
    jobs = 8
    if args[:1] == ["-j"]:
        jobs, args = int(args[1]), args[2:]
    srcs = args or sorted(os.path.relpath(os.path.join(dp, f), ROOT)
                          for base in ("game/src",) for dp, _, fs in os.walk(os.path.join(ROOT, base))
                          for f in fs if f.endswith(".c"))
    os.makedirs(OUT, exist_ok=True)
    layouts, n64lay, sites = {}, {}, collections.defaultdict(list)
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for lay, n64l, casts in ex.map(analyse, srcs):
            for k, v in lay.items():
                layouts.setdefault(k, v)
            for k, v in n64l.items():
                n64lay.setdefault(k, v)
            for a, b, where, implicit in casts:
                key = tuple(sorted((a, b)))
                sites[key].append(("implicit " if implicit else "cast ") + where)
    comments = parse_comment_offsets()
    n64 = {k: v["fields"] for k, v in n64lay.items()}

    report, clean, unknown = [], [], []
    known = set(layouts) | set(n64)
    for (a, b), where in sorted(sites.items(), key=lambda kv: -len(kv[1])):
        if a not in known or b not in known:
            continue  # conversions to/from void, scalars, char buffers: no layout to compare
        if a not in layouts or b not in layouts or a not in n64 or b not in n64:
            unknown.append((a, b, where))
            continue
        ha, hb = layouts[a]["fields"], layouts[b]["fields"]
        by_off_a = collections.defaultdict(list)
        for f, o in n64[a].items():
            if f in ha:
                by_off_a[o].append(f)
        bad = []
        for fb, ob in sorted(n64[b].items(), key=lambda kv: kv[1]):
            if fb not in hb or ob not in by_off_a:
                continue
            for fa in by_off_a[ob]:
                if ha[fa] != hb[fb]:
                    bad.append("N64 0x%X: %s.%s @host %d  vs  %s.%s @host %d" % (ob, a, fa, ha[fa], b, fb, hb[fb]))
        (report if bad else clean).append((a, b, where, bad))

    with open(os.path.join(OUT, "report.txt"), "w") as f:
        for a, b, where, bad in report:
            f.write("== %s <-> %s  (%d conversion sites)\n" % (a, b, len(where)))
            for w in sorted(set(where))[:8]:
                f.write("   at %s\n" % w)
            for line in bad:
                f.write("   " + line + "\n")
            f.write("\n")
    typos = []
    for name, fields in sorted(comments.items()):
        if name not in n64:
            continue
        for fname, off in fields.items():
            real = n64[name].get(fname)
            if real is not None and real != off:
                typos.append("%s.%s: comment says 0x%X, N64 layout is 0x%X" % (name, fname, off, real))
    with open(os.path.join(OUT, "comment_errors.txt"), "w") as f:
        f.write("\n".join(typos) + "\n")
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("sources: %d, struct types: %d host / %d N64 layouts; offset comments that disagree with the N64 "
                "layout: %d (comment_errors.txt)\n" % (len(srcs), len(layouts), len(n64), len(typos)))
        f.write("struct pointer pairs converted: %d; mismatching: %d; consistent: %d; not checkable: %d\n"
                % (len(sites), len(report), len(clean), len(unknown)))
        f.write("\nnot checkable (no layout for one side):\n")
        for a, b, where in unknown:
            f.write("   %s <-> %s  (%d sites, e.g. %s)\n" % (a, b, len(where), where[0]))
    print(open(os.path.join(OUT, "summary.txt")).read().split("\n\n")[0])
    print("report: build/layout/report.txt, comment errors: build/layout/comment_errors.txt")


if __name__ == "__main__":
    main()
