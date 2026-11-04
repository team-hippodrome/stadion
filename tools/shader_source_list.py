#!/usr/bin/env python3
"""Derive shader warm-up candidates from named static N64 display lists.

Run against a checkout prepared by tools/setup.sh. Uses the SDK's command
macros and the port's combiner normalizer. Unresolved calls invalidate inherited
state. Dynamic draw lists and asset lists outside the C sources are not scanned.
The optional Vulkan manifest assumes single-sample framebuffers and explores
inherited depth state. It is intended for verification before shipping.
The output is a candidate manifest, separate from the shipped replay list.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


ARRAY = re.compile(r"\bGfx\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{")
MACRO = re.compile(r"(gs\w+)\s*\(")
COMMENTS = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
STATE = {
    "gsDPSetCombineMode", "gsDPSetCombineLERP", "gsDPSetCycleType",
    "gsDPSetRenderMode", "gsDPSetAlphaCompare", "gsDPSetDepthSource",
    "gsDPSetOtherMode", "gsSPSetOtherMode",
}
DRAW = {"gsSP1Triangle", "gsSP2Triangles", "gsSP1Quadrangle"}


def balanced(text, start, left, right):
    depth = 1
    for i in range(start, len(text)):
        depth += (text[i] == left) - (text[i] == right)
        if depth == 0:
            return i
    raise ValueError("unbalanced source fragment")


def read_lists(game):
    lists = {}
    duplicates = set()
    skipped = set()
    for path in sorted((game / "src").rglob("*.c")):
        source = COMMENTS.sub("", path.read_text(errors="replace"))
        for match in ARRAY.finditer(source):
            name = match[1]
            if name in lists:
                duplicates.add(name)
            end = balanced(source, match.end(), "{", "}")
            body = source[match.end():end]
            commands = []
            pos = 0
            while macro := MACRO.search(body, pos):
                if body[pos:macro.start()].strip(" ,\t\r\n"):
                    skipped.add(name)
                    break
                pos = balanced(body, macro.end(), "(", ")") + 1
                commands.append((macro[1], body[macro.end():pos - 1].strip()))
            if body[pos:].strip(" ,\t\r\n"):
                skipped.add(name)
            lists[name] = commands
    for name in duplicates | skipped:
        del lists[name]
    return lists, duplicates | skipped


def excerpt(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def helper_source(source, macros):
    # Compile the renderer's normalizer with the SDK's macro encodings so the
    # audit does not maintain a second copy of shader key generation.
    fast = source / "libultraship"
    header = (fast / "include/fast/interpreter.h").read_text()
    gbi = (fast / "include/fast/lus_gbi.h").read_text()
    interpreter = (fast / "src/fast/interpreter.cpp").read_text()
    enums = excerpt(header, "enum {\n    SHADER_0,", "#endif")
    structs = (excerpt(header, "struct ColorCombinerKey {", "#define SHADER_MAX_TEXTURES")
               + excerpt(header, "struct ColorCombiner {", "struct RenderingState {"))
    shader_id_shift = excerpt(header, "constexpr size_t SHADER_ID_SHIFT", "\n")
    extensions = "\n".join(re.findall(r"^#define G_CCMUX_(?:KEY_\w+|CONVERT_\w+) \d+", gbi, re.M))
    normalizer = excerpt(interpreter, "void Interpreter::GenerateCC(", "\n}\n") + "\n}\n"
    mode_helpers = excerpt(interpreter, "static inline uint32_t color_comb(uint32_t a", "// Sign-extend")
    mode_helpers = mode_helpers[mode_helpers.rindex("static inline uint32_t color_comb"):]
    mode = excerpt(interpreter, "    mRdp->combine_mode = rgb | (alpha << 16)", "\n}").strip()
    decode = excerpt(interpreter, "    gfx->GfxDpSetCombineMode(\n        color_comb(C0(", "\n    return false;")
    blend = excerpt(interpreter, "    bool use_alpha = ((mRdp->other_mode_l", "    static const bool sFloorLog")
    flags = excerpt(interpreter, "    bool alpha_threshold = (mRdp->other_mode_l", "    if (!mShaderStack.empty())")
    flags = (blend + flags).replace("mRdp->other_mode_l", "low").replace("mRdp->other_mode_h", "high")
    flags = flags.replace("mRdp->grayscale", "false")
    macro_rows = ",\n".join(f"    {macro}" for macro in macros)
    return f"""
#include <cstdint>
#include <compare>
#define _SHIFTL(v, s, w) ((uint32_t(v) & (UINT32_MAX >> (32 - (w)))) << (s))
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define _LANGUAGE_C_PLUS_PLUS
#define F3DEX_GBI_2
#include <PR/gbi.h>
{enums}#endif
{structs}
{shader_id_shift}
{extensions}
{normalizer.replace("Interpreter::GenerateCC", "GenerateCC")}
{mode_helpers}
static uint64_t combine_mode(uint32_t rgb, uint32_t alpha, uint32_t rgb_cyc2, uint32_t alpha_cyc2) {{
    {mode.replace("mRdp->combine_mode =", "return")}
}}
#define C0(shift, bits) ((w0 >> (shift)) & ((1u << (bits)) - 1))
#define C1(shift, bits) ((w1 >> (shift)) & ((1u << (bits)) - 1))
static uint64_t decode(uint32_t w0, uint32_t w1) {{
{decode.replace("gfx->GfxDpSetCombineMode", "return combine_mode")}
}}
static const Gfx commands[] = {{
{macro_rows}
}};
int main(int argc, char**) {{
    if (argc == 1) {{
        for (const auto& cmd : commands)
            std::printf("%08x %08x\\n", (unsigned)cmd.words.w0, (unsigned)cmd.words.w1);
        return 0;
    }}
    unsigned w0, w1, low, high;
    while (std::scanf("%x %x %x %x", &w0, &w1, &low, &high) == 4) {{
        uint64_t cc_options = 0;
{flags}
        cc_options |= UINT64_MAX << SHADER_ID_SHIFT;
        ColorCombiner comb{{}};
        GenerateCC(&comb, {{decode(w0, w1), cc_options, 0}});
        unsigned clamps = (comb.usedTextures[0] ? 3 : 0) | (comb.usedTextures[1] ? 12 : 0);
        for (unsigned mask = 0; mask < 16; ++mask) {{
            if (mask & ~clamps) continue;
            const auto id0 = static_cast<unsigned long long>(comb.shader_id0);
            const auto id1 = static_cast<unsigned long long>(comb.shader_id1 | mask * SHADER_OPT(TEXEL0_CLAMP_S));
            const unsigned state = 1 | ((low & Z_UPD) ? 0x40 : 0) | ((low & ZMODE_DEC) == ZMODE_DEC ? 0x80 : 0);
            const bool canTest = (low & Z_CMP) != 0;
            const bool mustTest = canTest && (low & G_ZS_PRIM);
            for (unsigned depth : {{0u, 0x10u}}) {{
                if (!mustTest) std::printf("%016llx %016llx %x\\n", id0, id1, state | depth);
                if (canTest) std::printf("%016llx %016llx %x\\n", id0, id1, state | depth | 0x20);
            }}
        }}
    }}
}}
"""


def walk(lists, encoded):
    states = set()
    unresolved = set()

    def visit(name, state, stack):
        if name not in lists or name in stack or len(stack) >= 64:
            unresolved.add(name)
            return (None, 0, 0, 0, 0)
        combine, low, low_mask, high, high_mask = state
        for macro, args in lists[name]:
            if macro in ("gsSPDisplayList", "gsSPBranchList"):
                state = visit(args, (combine, low, low_mask, high, high_mask), stack | {name})
                combine, low, low_mask, high, high_mask = state
                if macro == "gsSPBranchList":
                    break
            elif macro == "gsSPEndDisplayList":
                break
            elif macro.startswith(("gsSPBranch", "gsSPCull", "gsSPLoadUcode")):
                unresolved.add(f"{name}: {macro}")
                return (None, 0, 0, 0, 0)
            elif macro in STATE:
                w0, w1 = encoded[f"{macro}({args})"]
                opcode = w0 >> 24
                if opcode == 0xFC:
                    combine = (w0, w1)
                elif opcode in (0xE2, 0xE3):
                    length = (w0 & 255) + 1
                    shift = 32 - ((w0 >> 8) & 255) - length
                    if shift < 0:
                        raise ValueError(f"invalid other-mode command: {macro}({args})")
                    mask = ((1 << length) - 1) << shift
                    if opcode == 0xE2:
                        low = (low & ~mask) | (w1 & mask)
                        low_mask |= mask
                    else:
                        high = (high & ~mask) | (w1 & mask)
                        high_mask |= mask
                elif opcode == 0xEF:
                    high, high_mask, low, low_mask = w0 & 0xFFFFFF, 0xFFFFFF, w1, 0xFFFFFFFF
            elif macro in DRAW:
                # Require the combiner, render mode and cycle type. The low
                # three bits (alpha compare and depth source) may be inherited.
                if (combine is None or (low_mask & 0xFFFFFFF8) != 0xFFFFFFF8
                        or (high_mask & 0x300000) != 0x300000):
                    continue
                for inherited in (0, 1, 3, 4, 5, 7):
                    if (inherited & low_mask & 7) == (low & 7):
                        states.add((*combine, low | inherited, high))
        return combine, low, low_mask, high, high_mask

    for name in lists:
        visit(name, (None, 0, 0, 0, 0), set())
    return states, unresolved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vulkan-output", type=Path)
    args = parser.parse_args()
    lists, skipped = read_lists(args.source / "game")
    if not lists:
        parser.error("no static display lists found; run tools/setup.sh in the source checkout")
    macros = sorted({f"{macro}({value})" for commands in lists.values()
                     for macro, value in commands if macro in STATE})
    with tempfile.TemporaryDirectory() as tmp:
        cpp, exe = Path(tmp) / "keys.cpp", Path(tmp) / "keys"
        cpp.write_text(helper_source(args.source, macros))
        include = args.source / "game/lib/ultralib/include"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-I", include, cpp, "-o", exe], check=True)
        rows = subprocess.check_output([exe], text=True).splitlines()
        encoded = dict(zip(macros, (tuple(int(v, 16) for v in row.split()) for row in rows), strict=True))
        states, unresolved = walk(lists, encoded)
        raw = "".join(" ".join(f"{v:x}" for v in state) + "\n" for state in sorted(states))
        pipelines = set(subprocess.check_output([exe, "keys"], text=True, input=raw).splitlines())
        keys = {row.rsplit(" ", 1)[0] for row in pipelines}
    shipped = {line for line in (args.source / "src/port/shader_warmup.txt").read_text().splitlines()
               if line and not line.startswith("#")}
    args.output.write_text("# Static display-list shader candidates\n" + "".join(f"{key}\n" for key in sorted(keys)))
    if args.vulkan_output:
        args.vulkan_output.write_text("".join(f"{row}\n" for row in sorted(pipelines)))
        print(f"{len(pipelines)} Vulkan pipeline candidates (1x MSAA)")
    print(f"{len(lists)} static lists; {len(states)} draw states; {len(skipped)} skipped lists; "
          f"{len(unresolved)} unresolved branches or targets")
    print(f"{len(keys)} candidates; {len(keys & shipped)} already shipped; {len(keys - shipped)} new candidates")


if __name__ == "__main__":
    main()
