#!/usr/bin/env python3 -I
"""Web build: writes the absolute symbols of src/port/generated/abs_syms.S (the N64 segment addresses and ROM offsets
the game links against) as a WebAssembly object file, since wasm-ld has neither absolute symbols from assembly nor
--defsym.

Usage: gen_wasm_abs_syms.py <abs_syms.S> <out.o> <global base>

A wasm data symbol is an offset into a data segment of its object file; the symbols here carry the absolute flag
(which only lifts the linker's bounds check) and an offset that wraps around 2^64 from the segment's address to the
wanted value. That needs the segment's address: it is the first byte of the program's static data, at the global base
(-sGLOBAL_BASE), provided this object is the first one on the link line with read-only data (CMakeLists.txt puts it
there). main.cpp checks the result at startup with the extra symbol port_abs_check."""
import re
import sys

CHECK_NAME, CHECK_VALUE = "port_abs_check", 0x12345670


def uleb(v):
    out = bytearray()
    while True:
        b, v = v & 0x7F, v >> 7
        out.append(b | 0x80 if v else b)
        if not v:
            return bytes(out)


def name(s):
    s = s.encode()
    return uleb(len(s)) + s


def section(sid, payload):
    return bytes([sid]) + uleb(len(payload)) + payload


def main():
    src, out, base = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    syms = re.findall(r"^\.set SYM\((\w+)\), (0x[0-9A-Fa-f]+)", open(src).read(), re.M)
    syms.append((CHECK_NAME, hex(CHECK_VALUE)))
    WASM_SYMBOL_TYPE_DATA, WASM_SYMBOL_ABSOLUTE = 1, 0x200
    symtab = bytearray(uleb(len(syms)))
    for n, v in syms:
        offset = (int(v, 16) - base) & 0xFFFFFFFFFFFFFFFF
        symtab += bytes([WASM_SYMBOL_TYPE_DATA]) + uleb(WASM_SYMBOL_ABSOLUTE) + name(n) + uleb(0) + uleb(offset) + uleb(0)
    # one byte of read-only data: the segment the symbols are relative to
    seginfo = uleb(1) + name(".rodata.port_abs_anchor") + uleb(0) + uleb(0)
    linking = uleb(2) + bytes([5]) + uleb(len(seginfo)) + seginfo + bytes([8]) + uleb(len(symtab)) + symtab
    memory_import = uleb(1) + name("env") + name("__linear_memory") + bytes([2, 0x04]) + uleb(0)  # memory64
    data = uleb(1) + bytes([0, 0x42, 0, 0x0B]) + uleb(1) + b"\0"  # active segment at i64.const 0
    obj = (b"\0asm" + bytes([1, 0, 0, 0]) + section(2, memory_import) + section(12, uleb(1)) + section(11, data)
           + section(0, name("linking") + linking))
    with open(out, "wb") as f:
        f.write(obj)


main()
