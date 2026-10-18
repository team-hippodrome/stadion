#!/usr/bin/env python3 -I
"""Rewrites game/src/geo_layout.c so command fields are decoded from the word slots the layout arrays use.
Each original 32-bit word of a command is one uintptr_t slot holding the word's numeric value (so the first byte
of the original BE word is its most significant byte, whatever the host endianness)."""
import re, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
hdr = (root / "build/geo_layout.h.orig").read_text()
src_path = root / "game/src/geo_layout.c"
src = src_path.read_text()
structs = {}
for m in re.finditer(r"typedef struct (unk_D_800ABE00_cmd\w+) \{\n(.*?)\n\} \1; // size = (0x[0-9A-Fa-f]+)", hdr, re.S):
    name, body, size = m.group(1), m.group(2), int(m.group(3), 16)
    fields = {}
    for fm in re.finditer(r"/\* 0x([0-9A-Fa-f]+) \*/ ([^;]+);", body):
        off = int(fm.group(1), 16); decl = fm.group(2).strip()
        mm = re.match(r"(.+?)\s+(\w+)(\[[^\]]*\])?$", decl)
        typ, nm = mm.group(1).strip(), mm.group(2)
        if nm == "unk_08" and off == 7: off = 8
        fields[nm] = (off, typ)
    structs[name] = (fields, size)
def access(off, typ):
    if typ == "Vec3s":
        return f"(&(Vec3s){{ GL_S16({off:#x}), GL_S16({off+2:#x}), GL_S16({off+4:#x}) }})"
    if "*" in typ or typ.endswith("Callback"):
        return f"(({typ})GL_PTR({off:#x}))"
    return {"u8": f"GL_U8({off:#x})", "s8": f"((s8)GL_U8({off:#x}))", "u16": f"((u16)GL_S16({off:#x}))",
            "s16": f"GL_S16({off:#x})", "u32": f"GL_U32({off:#x})", "s32": f"((s32)GL_U32({off:#x}))"}[typ]
out = []
# process function by function
parts = re.split(r"(?m)^(?=void geo_layout_cmd_|void register_graph_node|GraphNode\* process_geo_layout)", src)
for part in parts:
    cm = re.search(r"(unk_D_800ABE00_cmd\w+)\* cmd = \(unk_D_800ABE00_cmd\w+\*\)gGeoLayoutCommand;", part)
    if cm:
        fields, size = structs[cm.group(1)]
        part = re.sub(r"cmd->(\w+)", lambda m: access(fields[m.group(1)][0], fields[m.group(1)][1]) if m.group(1) in fields else m.group(0), part)
        # '&cmd->unk' where the access already yields a pointer (Vec3s compound literal)
        part = part.replace("&(&(Vec3s)", "(&(Vec3s)")
    part = re.sub(r"sizeof\((unk_D_800ABE00_cmd\w+)\)", lambda m: f"GL_CMDSIZE({structs[m.group(1)][1]:#x})", part)
    out.append(part)
new = "".join(out)
new = re.sub(r"\s*unk_D_800ABE00_cmd\w+\* cmd = \(unk_D_800ABE00_cmd\w+\*\)gGeoLayoutCommand;", "", new)
src_path.write_text(new)
print("done; remaining struct refs:", len(re.findall(r"unk_D_800ABE00_cmd", new)))
