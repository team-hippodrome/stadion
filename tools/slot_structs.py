#!/usr/bin/env python3 -I
"""Rewrites the geo layout command structs for the 'one 8-byte slot per original 32-bit word' encoding that the
widened layout arrays use (the same scheme the SM64 PC port uses via CMD_PROCESS_OFFSET). Field offsets come from
the /* 0xNN */ comments. Vec3s fields span word boundaries, so they become three separate s16 fields."""
import re, sys, pathlib
p = pathlib.Path(sys.argv[1])
s = p.read_text()
SZ = {"u8": 1, "s8": 1, "char": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "f32": 4}
def slot(off): return (off & 3) | ((off & ~3) << 1)
def conv(m):
    name, body, tail = m.group(1), m.group(2), m.group(3)
    size = int(re.search(r"size = (0x[0-9A-Fa-f]+)", tail).group(1), 16)
    fields = []
    for fm in re.finditer(r"/\* 0x([0-9A-Fa-f]+) \*/ ([^;]+);", body):
        off = int(fm.group(1), 16); decl = fm.group(2).strip()
        fields.append((off, decl))
    out, cur, n = [], 0, 0
    def pad(to):
        nonlocal cur, n
        if to > cur:
            out.append(f"    char slotpad{n}[{to - cur}];"); n += 1; cur = to
    for off, decl in fields:
        mm = re.match(r"(.+?)\s+(\w+)(\[[^\]]*\])?$", decl)
        typ, nm, arr = mm.group(1).strip(), mm.group(2), mm.group(3)
        if typ == "char" and nm.startswith("pad"):
            continue
        if nm == "unk_08" and off == 7:
            off = 8   # typo in the original comment
        if typ == "Vec3s":
            for i, ax in enumerate("xyz"):
                no = slot(off + 2 * i); pad(no)
                out.append(f"    /* {off+2*i:#04x} -> {no:#04x} */ s16 {nm}_{ax};"); cur = no + 2
            continue
        no = slot(off); pad(no)
        sz = 8 if "*" in typ or typ.endswith("Callback") else SZ.get(typ, 4)
        out.append(f"    /* {off:#04x} -> {no:#04x} */ {decl};"); cur = no + sz
    newsize = ((size + 3) & ~3) * 2
    pad(newsize)
    return f"typedef struct {name} {{\n" + "\n".join(out) + f"\n}} {name}; // slot layout, size = {newsize:#x} (was {size:#x})"
s2 = re.sub(r"typedef struct (unk_D_800ABE00_cmd\w+) \{\n(.*?)\n\} \1;([^\n]*)", conv, s, flags=re.S)
p.write_text(s2)
print("converted", len(re.findall("slot layout", s2)))
