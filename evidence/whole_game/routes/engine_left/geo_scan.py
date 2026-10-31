#!/usr/bin/env python3 -I
"""Finds every geo layout the shipped game carries and counts the commands and node types they use.

Usage: geo_scan.py <US ROM (.z64)> <game/src dir> [--json out.json] [--show 0x26,...]

Two independent sources:
  rom:  every 4-aligned offset of the raw ROM and of every PERS-SZP / Yay0 blob in it (decompressed), parsed as a
        packed geo layout (big-endian words, command byte first). This sees layouts wherever they live (code
        fragments, data fragments, model archives), with no entry point needed.
  src:  every word array in the decomp's C sources (fragments' and the main program's layouts in slot form: one
        array element per original word, a symbol is a pointer slot), parsed the same way.
A candidate is a command stream that only uses table commands (byte < 0x27) with the fixed words of the commands
that have no operands (01/04/05/06 000000), never closes more nodes than it opened, opens at least one node and
ends where a layout ends (01 end at depth 0, 04 return at depth 0, 02 jump). Command sizes are geo_layout.c's
GL_CMDSIZE values; 0x12 (geo_layout_cmd_noop) does not advance the command pointer, so a layout reaching it would
spin forever: the parse stops there and flags the candidate.
"""
import json, re, struct, sys, collections, pathlib

NAMES = ["branch_and_link", "end", "jump", "branch", "return", "open_node", "close_node", "attach_node",
         "set_callback", "create_container", "create_reference", "create_camera", "create_type3", "create_type4",
         "create_ortho", "create_perspective", "create_background", "create_clear_depth", "noop", "create_fog",
         "create_light", "skip", "create_ambient_light", "create_shadow_context", "create_shadow",
         "create_cull_distance", "create_switch_case", "create_translate_rotate", "create_translate",
         "create_animated_part", "create_display_list_part", "create_model_part", "create_display_list_matrix",
         "create_scale", "create_display_list", "create_shadow_texture", "create_anchor", "set_node_flag4",
         "create_group"]
SIZE = [8, 4, 8, 8, 4, 4, 4, 8, 0xc, 4, 8, 0x18, 4, 4, 4, 4, 4, 4, 0, 8, 0xc, 0xc, 4, 0x14, 8, 8, 4, 0x10,
        0x10, 0x1c, 8, 0x18, 0x14, 0x10, 8, 0x10, 4, 4, 0x14]
# node type each command creates (geo_node.c GeoNode_Init calls); 0x07/0x0A attach or reference an existing node
NODE = {0x09: 0, 0x0A: 1, 0x0B: 2, 0x0C: 3, 0x0D: 4, 0x0E: 5, 0x0F: 6, 0x10: 7, 0x11: 8, 0x13: 0xA, 0x14: 0xB,
        0x16: 0xD, 0x17: 0xE, 0x18: 0xF, 0x19: 0x10, 0x1A: 0x11, 0x1B: 0x12, 0x1C: 0x13, 0x1D: 0x14, 0x1E: 0x15,
        0x1F: 0x16, 0x20: 0x17, 0x21: 0x18, 0x22: 0x19, 0x23: 0x1A, 0x24: 0x1B, 0x26: 0x1C}
FIXED = {0x01, 0x04, 0x05, 0x06}
POINTER = {0x00, 0x02, 0x03, 0x07, 0x0A}  # the command dereferences or jumps to word 1


def plausible(w):
    """a slot symbol, or a link address: KSEG0 (0x80-0x8F) or a segment (0x01-0x0F), 4-aligned, not 0"""
    return w is None or (w != 0 and w % 4 == 0 and ((w >> 24) in range(0x80, 0x90) or (w >> 24) in range(1, 0x10)))


def parse(words, start):
    """words: list of ints (None for a symbol slot); -> (end index, [(cmd, depth, word index)], flag) or None"""
    i, depth, ops, opened = start, 0, [], 0
    while i < len(words) and len(ops) < 8192:
        w = words[i]
        if w is None:
            return None
        c = w >> 24
        if c > 0x26 or (c in FIXED and w & 0xFFFFFF):
            return None
        if c == 0x12:
            ops.append((c, depth, i))
            return (i + 1, ops, "noop") if opened else None
        ops.append((c, depth, i))
        if c == 0x05:
            depth += 1
            opened += 1
        elif c == 0x06:
            depth -= 1
            if depth < 0:
                return None
        n = SIZE[c] // 4
        if i + n > len(words):
            return None
        if c in POINTER and not plausible(words[i + 1]):
            return None
        if c in (0x01, 0x04) or c == 0x02:
            if depth == 0 and opened and len(ops) >= 3:
                return i + n, ops, None
            return None
        i += n
    return None


def scan(words, label, out):
    """maximal candidates; a candidate's leading depth-0 commands before its root (the last node it creates at depth 0
    before the first open_node) are split off as "prefix": words that parse as commands but precede the layout"""
    i = 0
    while i < len(words):
        r = parse(words, i)
        if r:
            end, ops, flag = r
            first_open = next(o[2] for o in ops if o[0] == 0x05)
            for j in range(first_open - 1, i - 1, -1):  # the root: the latest word before the first open_node
                w = words[j]                            # that starts a node command parsing to the same end
                if w is not None and ((w >> 24) in NODE or (w >> 24) == 0x07):
                    r2 = parse(words, j)
                    if r2 and r2[0] == end:
                        break
            else:
                j = i
            if j != i:
                prefix = [o for o in ops if o[2] < j]
                end, ops, flag = parse(words, j)
            else:
                prefix = []
            out.append({"where": label, "start": j, "end": end, "ops": ops, "prefix": prefix, "flag": flag})
            i = end
        else:
            i += 1


def be_words(buf):
    n = len(buf) // 4
    return list(struct.unpack(">%dI" % n, buf[:n * 4]))


def yay0(d, o=0):
    be = lambda b, k: struct.unpack(">I", b[k:k + 4])[0]
    size, lo, co = be(d, o + 4), be(d, o + 8), be(d, o + 12)
    if size > 0x400000:
        raise ValueError("size")
    out = bytearray(); mp = o + 16; bits = 0; nb = 0
    while len(out) < size:
        if nb == 0:
            bits = be(d, mp); mp += 4; nb = 32
        if bits & 0x80000000:
            out.append(d[o + co]); co += 1
        else:
            v = struct.unpack(">H", d[o + lo:o + lo + 2])[0]; lo += 2
            dist = (v & 0xFFF) + 1; k = v >> 12
            if k == 0:
                k = d[o + co] + 18; co += 1
            else:
                k += 2
            for _ in range(k):
                out.append(out[-dist])
        bits = (bits << 1) & 0xFFFFFFFF; nb -= 1
    return bytes(out[:size])


def rom_sources(rom):
    yield "rom", rom
    done = set()
    for m in re.finditer(b"PERS-SZP", rom):
        o = m.start()
        hs = struct.unpack(">I", rom[o + 8:o + 12])[0]
        try:
            yield "PERS-SZP@%#x" % o, yay0(rom, o + hs)
            done.add(o + hs)
        except Exception as e:
            print("skip PERS-SZP@%#x: %s" % (o, e), file=sys.stderr)
    for m in re.finditer(b"Yay0", rom):
        if m.start() in done:
            continue
        try:
            yield "Yay0@%#x" % m.start(), yay0(rom, m.start())
        except Exception as e:
            print("skip Yay0@%#x: %s" % (m.start(), e), file=sys.stderr)


ARRAY = re.compile(r"(?:static\s+)?(?:const\s+)?(?:uintptr_t|u32|s32|unsigned int)\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", re.S)


def src_sources(src_dir):
    for path in sorted(pathlib.Path(src_dir).rglob("*.c")):
        text = path.read_text(errors="replace")
        text = re.sub(r"//[^\n]*", "", text)
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        for m in ARRAY.finditer(text):
            words = []
            for tok in m.group(2).split(","):
                tok = tok.strip()
                if not tok:
                    continue
                try:
                    words.append(int(tok, 0) & 0xFFFFFFFF)
                except ValueError:
                    words.append(None)
            yield "%s:%s" % (path.relative_to(src_dir), m.group(1)), words


def main():
    rom_path, src_dir = sys.argv[1], sys.argv[2]
    show = set()
    out_json = None
    a = sys.argv[3:]
    while a:
        if a[0] == "--json":
            out_json = a[1]; a = a[2:]
        elif a[0] == "--show":
            show = {int(x, 0) for x in a[1].split(",")}; a = a[2:]
        else:
            sys.exit("unknown " + a[0])
    rom = open(rom_path, "rb").read()
    result = {}
    for kind, sources in (("rom", ((l, be_words(b)) for l, b in rom_sources(rom))), ("src", src_sources(src_dir))):
        cands = []
        nbuf = 0
        for label, words in sources:
            nbuf += 1
            scan(words, label, cands)
        cmds = collections.Counter()
        nested3 = []
        for c in cands:
            for op, depth, _ in c["ops"]:
                cmds[op] += 1
            first = [op for op, d, _ in c["ops"] if op in NODE or op in (0x07,)]
            for k, (op, depth, idx) in enumerate(c["ops"]):
                if op == 0x0C and (depth > 0 or k > 0):
                    nested3.append((c["where"], c["start"], k, depth))
        layouts = collections.Counter(op for c in cands for op in {o for o, _, _ in c["ops"]})
        result[kind] = {"buffers": nbuf, "candidates": len(cands),
                        "commands": {"%02X %s" % (k, NAMES[k]): [cmds.get(k, 0), layouts.get(k, 0)] for k in range(0x27)},
                        "type3_not_first": nested3[:50], "type3_not_first_count": len(nested3),
                        "flagged": [(c["where"], c["start"], c["flag"]) for c in cands if c["flag"]][:50]}
        print("== %s: %d buffers, %d candidate layouts" % (kind, nbuf, len(cands)))
        for k in range(0x27):
            print("  %02X %-28s commands %7d  layouts %6d" % (k, NAMES[k], cmds.get(k, 0), layouts.get(k, 0)))
        print("  0x0C not the first command:", len(nested3), nested3[:10])
        print("  flagged:", result[kind]["flagged"][:10])
        pre = collections.Counter(o for c in cands for o, _, _ in c["prefix"])
        print("  prefix words (before a root):", {"%02X" % k: v for k, v in sorted(pre.items())})
        result[kind]["prefix_commands"] = {"%02X" % k: v for k, v in sorted(pre.items())}
        for c in cands:
            hit = {o for o, _, _ in c["ops"]} & show
            if hit:
                print("  show %s start=%d end=%d cmds=%s" % (c["where"], c["start"], c["end"],
                      " ".join("%02X" % o for o, _, _ in c["ops"][:40])))
    if out_json:
        json.dump(result, open(out_json, "w"), indent=1)


if __name__ == "__main__":
    main()
