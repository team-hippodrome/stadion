// Segment addresses (images inside UI archives) that a European ROM keeps somewhere else than US Rev 2: the tables
// tools/gen_seg_syms.py writes into src/port/generated/seg_syms.c, used by Port_SegSym (rom_version.cpp).
#pragma once

#include "rom_layouts.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortSegSym {
    uint32_t rev2;  // the value US Rev 2 links the name to
    uint32_t value; // the value in this ROM's build
} PortSegSym;

typedef struct PortSegSymTable {
    PortRomVersion version;
    const PortSegSym* syms; // sorted by rev2
    uint32_t count;
} PortSegSymTable;

extern const PortSegSymTable gPortSegSymTables[];
extern const unsigned gPortSegSymTableCount;

#ifdef __cplusplus
}
#endif
