// Native Game Boy / Super Game Boy core for the GB Tower.
//
// On the N64 the GB Tower runs a hand-written MIPS emulator (asm/us/fragments/gb_tower_emulator/
// gb_tower_emulator_code_asm.s: CPU, memory, LCD, Super Game Boy) and a custom RSP microcode that turns the Game Boy
// picture into the frame buffer. Neither can run on the host, so this file provides a C core behind the same entry
// points (GbEmu_InitContext, GbCpu_Execute, GbSgb_RenderBorder) plus the frame composition (Port_GbPresent).
// The surrounding GB Tower code (decompiled C) still loads the cartridge through the Transfer Pak, writes the save RAM
// back, draws the loading screen, the in-game menu and the status bar.
//
// The emulated machine is a Super Game Boy (like the N64 emulator): DMG CPU and LCD, SGB packets (palettes, attribute
// maps, border, multiplayer detection). Cartridges with the Game Boy Color flag (header 0x143 bit 7, e.g. Pokémon
// Yellow) run as a Game Boy Color instead, as on the N64 (the GB Tower picks its colour microcode by that flag): double
// speed, VRAM/WRAM banks, HDMA, colour palettes and tile attributes; the tower then frames the picture with its own
// border image. Sound: APU register writes go to the GB Tower's own sound emulation (GbApu_QueueRegisterWrite), which mixes and outputs it.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "StadionPort.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int32_t s32;

// ---- the GB Tower's emulator context (game/include/global.h: unk_D_8122B2C0), fields this core uses ----
// Field offsets, provided by the game side (gb_tower_emulator_7F9A0.c) where the struct type is known
// (Port_GbEmuOffsets, StadionPort.h).

enum { OFF_ROM, OFF_SRAM, OFF_SRAM_SIZE, OFF_PAD, OFF_DIRTY, OFF_DELAY, OFF_VARIANT, OFF_STADIUM_DATA, OFF_BORDER_SHOWN };
#define EMU_FIELD(emu, k) ((u8*)(emu) + Port_GbEmuOffsets[k])

// ---- machine state ----
static struct {
    // CPU
    u8 a, f, b, c, d, e, h, l;
    u16 sp, pc;
    int ime, imeDelay, halted;
    // memory
    const u8* rom;
    u32 romSize;
    u8* sram;
    u32 sramSize;
    u8 vram[0x4000], wram[0x8000], oam[0xA0], hram[0x7F], io[0x80], ie;
    int mbc;
    u32 romBank, ramBank;
    int ramEnable, mbc1Mode;
    // timer / serial
    u32 divCounter, timaCounter, serialCounter;
    // LCD
    u32 dot;
    int windowLine;
    u8 shades[144][160];  // 0-3 after BGP/OBP
    u8 bgIndex[160];      // raw BG colour index of the current line (sprite priority)
    // joypad / SGB
    u16 pad;
    int sgbPlayers, sgbPlayer;
    u8 lastP1;
    int pktState, pktBits, pktCount, pktTotal;
    u8 pkt[7 * 16];
    u16 sgbPal[4][4];       // active palettes (BGR555)
    u16 sgbSysPal[512][4];  // from PAL_TRN
    u8 sgbAttr[18][20];     // palette per 8x8 cell
    u8 sgbAttrFiles[45][90];
    int sgbMask;
    u8 sgbBorderTiles[256 * 32];
    u8 sgbBorderMap[32 * 28 * 2];
    u16 sgbBorderPal[4][16];
    int sgbHasBorder, sgbBorderChanged;
    u8 frozen[144][160];
    // Game Boy Color
    int cgb, doubleSpeed, vbk, svbk;
    u8 bgPal[64], objPal[64], bcps, ocps;
    u16 hdmaSrc, hdmaDst;
    int hdmaLeft; // HBlank DMA: 16-byte blocks still to copy (0: idle)
    u8 bgAttr[160];
    u16 color[144][160]; // BGR555 per pixel
    u16 shown[144][160]; // the picture handed to the presentation (kept while the screen is masked)
    int shownLcdOn;
    u32 frameCycles; // CPU cycles since the frame started (time stamp for the tower's sound mixer)
    u32 soundBase;   // CPU cycles of the earlier Game Boy frames in this GbCpu_Execute call (fast-forward)
    u32 speed;       // Game Boy frames per call: 1, or 2 / 4 in the Doduo / Dodrio Tower fast-forward
    int trnCmd, trnFrames;
    u8 trnArg;
    // MASK_EN as the N64 emulator keeps it: maskEn is the last value sent (context 0x5402), maskShown the one in effect
    // (0x5401: copied at each frame end, 4 frames late after a cancel, unmaskDelay = 0x5DD2). While maskShown is set
    // the emulator does not hand the frame to its RSP task, so the last picture stays (Game Boy Color too).
    // The "border shown" flag (context 0x5404): a CHR_TRN/PCT_TRN packet, then the mask back to 0.
    u8 maskEn, maskShown, unmaskDelay, borderTrn;
    int frameDone;
    int initialised;
} G;

// ---- helpers ----
#define FZ 0x80
#define FN 0x40
#define FH 0x20
#define FC 0x10

static void SgbWriteP1(u8 v);
static void LcdStep(u32 cycles);
static void TimerStep(u32 cycles);

static void RequestInterrupt(int bit) {
    G.io[0x0F] |= (u8)(1 << bit);
}

static void MarkSramDirty(void* emu, u32 offset) {
    u32 page = offset >> 8;
    u8* delay = EMU_FIELD(emu, OFF_DELAY);
    // 0xFE: a page the tower does not write back (for Pokémon carts the scratch RAM below 0x500, where the tower
    // also keeps its cartridge probe block)
    if (page < 0x80 && delay[page] != 0xFE) {
        u8* dirty = EMU_FIELD(emu, OFF_DIRTY);
        dirty[page] = 0;  // written: the GB Tower code copies the page back to the cartridge
        delay[page] = 30; // ... once no further write came for this many frames
    }
}

static void* gEmu;

// ---- memory ----
static u8 ReadIo(u16 a);
static void WriteIo(u16 a, u8 v);

static u8 Read(u16 a) {
    if (a < 0x4000) {
        if (G.mbc == 1 && G.mbc1Mode) {
            return G.rom[(((G.romBank >> 5) & 3) * 0x80000 + a) % G.romSize];
        }
        return G.rom[a % G.romSize];
    }
    if (a < 0x8000) {
        return G.rom[((size_t)G.romBank * 0x4000 + (a - 0x4000)) % G.romSize];
    }
    if (a < 0xA000) {
        return G.vram[G.vbk * 0x2000 + (a - 0x8000)];
    }
    if (a < 0xC000) {
        if (!G.ramEnable || G.sramSize == 0) {
            return 0xFF;
        }
        return G.sram[((size_t)G.ramBank * 0x2000 + (a - 0xA000)) % G.sramSize];
    }
    if (a < 0xFE00) {
        u16 w = (u16)((a - 0xC000) & 0x1FFF);
        return G.wram[w < 0x1000 ? w : (G.svbk ? G.svbk : 1) * 0x1000 + (w - 0x1000)];
    }
    if (a < 0xFEA0) {
        return G.oam[a - 0xFE00];
    }
    if (a < 0xFF00) {
        return 0xFF;
    }
    if (a < 0xFF80) {
        return ReadIo(a);
    }
    if (a < 0xFFFF) {
        return G.hram[a - 0xFF80];
    }
    return G.ie;
}

static void Write(u16 a, u8 v) {
    if (a < 0x8000) {
        if (a < 0x2000) {
            G.ramEnable = (v & 0x0F) == 0x0A;
        } else if (a < 0x4000) {
            if (G.mbc == 5) {
                if (a < 0x3000) {
                    G.romBank = (G.romBank & 0x100) | v;
                } else {
                    G.romBank = (G.romBank & 0xFF) | ((u32)(v & 1) << 8);
                }
            } else if (G.mbc == 3) {
                G.romBank = (v & 0x7F) ? (v & 0x7F) : 1;
            } else if (G.mbc == 1) {
                G.romBank = (G.romBank & 0x60) | ((v & 0x1F) ? (v & 0x1F) : 1);
            }
        } else if (a < 0x6000) {
            if (G.mbc == 5) {
                G.ramBank = v & 0x0F;
            } else if (G.mbc == 3) {
                G.ramBank = v & 0x03;
            } else if (G.mbc == 1) {
                G.romBank = (G.romBank & 0x1F) | ((u32)(v & 3) << 5);
                G.ramBank = v & 3;
            }
        } else if (G.mbc == 1) {
            G.mbc1Mode = v & 1;
        }
        return;
    }
    if (a < 0xA000) {
        G.vram[G.vbk * 0x2000 + (a - 0x8000)] = v;
    } else if (a < 0xC000) {
        if (G.ramEnable && G.sramSize != 0) {
            u32 off = (u32)(((size_t)((G.mbc == 1 && !G.mbc1Mode) ? 0 : G.ramBank) * 0x2000 + (a - 0xA000)) % G.sramSize);
            if (G.sram[off] != v) {
                G.sram[off] = v;
                MarkSramDirty(gEmu, off);
            }
        }
    } else if (a < 0xFE00) {
        u16 w = (u16)((a - 0xC000) & 0x1FFF);
        G.wram[w < 0x1000 ? w : (G.svbk ? G.svbk : 1) * 0x1000 + (w - 0x1000)] = v;
    } else if (a < 0xFEA0) {
        G.oam[a - 0xFE00] = v;
    } else if (a < 0xFF00) {
    } else if (a < 0xFF80) {
        WriteIo(a, v);
    } else if (a < 0xFFFF) {
        G.hram[a - 0xFF80] = v;
    } else {
        G.ie = v;
    }
}

// The GB Tower's sound emulation (gb_tower_emulator_86CB0.c): register writes are queued with their time in machine
// cycles within the frame and applied while mixing; NR52 reflects its channel state.
extern void GbApu_QueueRegisterWrite(u16 reg, u8 value, u16 time);
extern u8 GbMem_ReadIoRegister(u16 reg);

static u8 ReadIo(u16 a) {
    u8 r = (u8)(a & 0x7F);
    switch (r) {
        case 0x00: { // P1
            u8 sel = G.io[0x00] & 0x30;
            u8 low = 0x0F;
            if ((sel & 0x30) == 0x30) {
                // nothing selected: with SGB multiplayer the low bits give the current controller id
                low = G.sgbPlayers > 1 ? (u8)(0x0F - G.sgbPlayer) : 0x0F;
            } else {
                u16 p = G.sgbPlayer == 0 ? G.pad : 0;
                if (!(sel & 0x10)) { // directions
                    if (p & 0x0100) low &= ~0x01; // right
                    if (p & 0x0200) low &= ~0x02; // left
                    if (p & 0x0800) low &= ~0x04; // up
                    if (p & 0x0400) low &= ~0x08; // down
                }
                if (!(sel & 0x20)) { // buttons
                    if (p & 0x8000) low &= ~0x01; // A
                    if (p & 0x4000) low &= ~0x02; // B
                    if (p & 0x2000) low &= ~0x04; // select
                    if (p & 0x1000) low &= ~0x08; // start
                }
            }
            return (u8)(0xC0 | sel | low);
        }
        case 0x02:
            return G.io[0x02] | 0x7E;
        case 0x04:
            return (u8)(G.divCounter >> 8);
        case 0x07:
            return G.io[0x07] | 0xF8;
        case 0x26:
            return GbMem_ReadIoRegister(0xFF26) | 0x70;
        case 0x0F:
            return G.io[0x0F] | 0xE0;
        case 0x41:
            return G.io[0x41] | 0x80;
        default:
            if (r >= 0x10 && r < 0x40) {
                static const u8 mask[0x30] = {
                    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
                    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                };
                return G.io[r] | (r < 0x30 ? mask[r - 0x10] : 0);
            }
            if (G.cgb) {
                switch (r) {
                    case 0x4D: return (u8)(0x7E | (G.doubleSpeed ? 0x80 : 0) | (G.io[0x4D] & 1));
                    case 0x4F: return (u8)(0xFE | G.vbk);
                    case 0x55: return G.hdmaLeft ? (u8)(G.hdmaLeft - 1) : 0xFF;
                    case 0x68: return (u8)(G.bcps | 0x40);
                    case 0x69: return G.bgPal[G.bcps & 0x3F];
                    case 0x6A: return (u8)(G.ocps | 0x40);
                    case 0x6B: return G.objPal[G.ocps & 0x3F];
                    case 0x70: return (u8)(0xF8 | G.svbk);
                }
            }
            if (r >= 0x4C) {
                return 0xFF;
            }
            return G.io[r];
    }
}

static void WriteIo(u16 a, u8 v) {
    u8 r = (u8)(a & 0x7F);
    switch (r) {
        case 0x00:
            G.io[0x00] = v & 0x30;
            SgbWriteP1(v & 0x30);
            return;
        case 0x02:
            G.io[0x02] = v;
            G.serialCounter = 0;
            return;
        case 0x04:
            G.divCounter = 0;
            return;
        case 0x0F:
            G.io[0x0F] = v & 0x1F;
            return;
        case 0x41:
            G.io[0x41] = (G.io[0x41] & 0x07) | (v & 0x78);
            return;
        case 0x44:
            return; // LY is read-only
        case 0x46: { // OAM DMA (instant)
            u16 src = (u16)(v << 8);
            for (int i = 0; i < 0xA0; i++) {
                G.oam[i] = Read((u16)(src + i));
            }
            G.io[0x46] = v;
            return;
        }
        case 0x4D: G.io[0x4D] = v & 1; return; // KEY1: speed switch armed, done by STOP
        case 0x4F: if (G.cgb) G.vbk = v & 1; return;
        case 0x51: G.hdmaSrc = (u16)((G.hdmaSrc & 0x00F0) | (v << 8)); return;
        case 0x52: G.hdmaSrc = (u16)((G.hdmaSrc & 0xFF00) | (v & 0xF0)); return;
        case 0x53: G.hdmaDst = (u16)((G.hdmaDst & 0x00F0) | ((v & 0x1F) << 8)); return;
        case 0x54: G.hdmaDst = (u16)((G.hdmaDst & 0x1F00) | (v & 0xF0)); return;
        case 0x55:
            if (!G.cgb) return;
            if (G.hdmaLeft && !(v & 0x80)) { // stop a running HBlank DMA
                G.hdmaLeft = 0;
                return;
            }
            if (v & 0x80) {
                G.hdmaLeft = (v & 0x7F) + 1;
            } else { // general purpose DMA: all at once
                for (int i = 0; i < ((v & 0x7F) + 1) * 16; i++) {
                    G.vram[G.vbk * 0x2000 + ((G.hdmaDst + i) & 0x1FFF)] = Read((u16)(G.hdmaSrc + i));
                }
                G.hdmaSrc = (u16)(G.hdmaSrc + ((v & 0x7F) + 1) * 16);
                G.hdmaDst = (u16)(G.hdmaDst + ((v & 0x7F) + 1) * 16);
            }
            return;
        case 0x68: G.bcps = v & 0xBF; return;
        case 0x69:
            // as the N64 emulator: with a Pokémon cartridge (variant set) BG palette writes are ignored while MASK_EN is
            // on. Yellow converts its palettes through a null pointer during its border transfer (colour 0x00FF from
            // ROM 0x0000, a red-orange screen); these writes never reach the tower's screen.
            if (*EMU_FIELD(gEmu, OFF_VARIANT) != 0 && G.maskEn) return;
            G.bgPal[G.bcps & 0x3F] = v;
            if (G.bcps & 0x80) G.bcps = (u8)(0x80 | ((G.bcps + 1) & 0x3F));
            return;
        case 0x6A: G.ocps = v & 0xBF; return;
        case 0x6B:
            G.objPal[G.ocps & 0x3F] = v;
            if (G.ocps & 0x80) G.ocps = (u8)(0x80 | ((G.ocps + 1) & 0x3F));
            return;
        case 0x70: if (G.cgb) G.svbk = v & 7; return;
        case 0x40:
            if ((G.io[0x40] & 0x80) && !(v & 0x80)) { // LCD off
                G.io[0x44] = 0;
                G.dot = 0;
                G.io[0x41] &= ~3;
            }
            G.io[0x40] = v;
            return;
        default:
            G.io[r] = v;
            if (r >= 0x10 && r < 0x40) {
                // as the N64 emulator (0x8120B04C): the time runs over all the Game Boy frames of this call and is
                // divided by the speed, so the tower's mixer plays them in one buffer (the music speeds up too)
                GbApu_QueueRegisterWrite((u16)(0xFF00 | r), v, (u16)(((G.soundBase + G.frameCycles) >> 2) / (G.speed ? G.speed : 1)));
            }
            return;
    }
}

// ---- Super Game Boy ----
static u16 Rd16(const u8* p) {
    return (u16)(p[0] | (p[1] << 8));
}

static void SgbSetAttrBlockAll(int pal) {
    for (int y = 0; y < 18; y++) {
        for (int x = 0; x < 20; x++) {
            G.sgbAttr[y][x] = (u8)pal;
        }
    }
}

// The 4 KB a *_TRN command transfers: the tiles the BG map shows (20x13 cells, in order), as the SGB samples the
// picture of the next frame
static void SgbVramTransfer(u8* out, int size) {
    u8 lcdc = G.io[0x40];
    u16 map = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    int n = 0;
    for (int ty = 0; ty < 18 && n < size; ty++) {
        for (int tx = 0; tx < 20 && n < size; tx++) {
            u8 t = G.vram[map + ty * 32 + tx];
            u16 base = (lcdc & 0x10) ? (u16)(t * 16) : (u16)(0x1000 + (s8)t * 16);
            for (int k = 0; k < 16 && n < size; k++) {
                out[n++] = G.vram[(base + k) & 0x1FFF];
            }
        }
    }
}

static void SgbSetPal(int p0, int p1, const u8* d) {
    u16 c0 = Rd16(d + 1);
    for (int p = 0; p < 4; p++) {
        G.sgbPal[p][0] = c0;
    }
    for (int i = 0; i < 3; i++) {
        G.sgbPal[p0][1 + i] = Rd16(d + 3 + i * 2);
        G.sgbPal[p1][1 + i] = Rd16(d + 9 + i * 2);
    }
}

static void SgbAttrBlk(const u8* d) {
    int sets = d[1];
    const u8* s = d + 2;
    for (int i = 0; i < sets && i < 18; i++, s += 6) {
        int ctrl = s[0];
        int pin = s[1] & 3, pline = (s[1] >> 2) & 3, pout = (s[1] >> 4) & 3;
        int x1 = s[2] & 31, y1 = s[3] & 31, x2 = s[4] & 31, y2 = s[5] & 31;
        if ((ctrl & 7) == 1) {
            pline = pin; // only "inside" given: the border gets it too
        }
        if ((ctrl & 7) == 4) {
            pline = pout;
        }
        for (int y = 0; y < 18; y++) {
            for (int x = 0; x < 20; x++) {
                int inside = x > x1 && x < x2 && y > y1 && y < y2;
                int onLine = !inside && x >= x1 && x <= x2 && y >= y1 && y <= y2;
                if (inside && (ctrl & 1)) {
                    G.sgbAttr[y][x] = (u8)pin;
                } else if (onLine && (ctrl & 2 || (ctrl & 7) == 1 || (ctrl & 7) == 4)) {
                    G.sgbAttr[y][x] = (u8)pline;
                } else if (!inside && !onLine && (ctrl & 4)) {
                    G.sgbAttr[y][x] = (u8)pout;
                }
            }
        }
    }
}

static void SgbAttrLin(const u8* d) {
    int n = d[1];
    for (int i = 0; i < n && i < 110; i++) {
        u8 b = d[2 + i];
        int line = b & 31, pal = (b >> 5) & 3;
        if (b & 0x80) { // horizontal
            if (line < 18)
                for (int x = 0; x < 20; x++) G.sgbAttr[line][x] = (u8)pal;
        } else if (line < 20) {
            for (int y = 0; y < 18; y++) G.sgbAttr[y][line] = (u8)pal;
        }
    }
}

static void SgbAttrDiv(const u8* d) {
    int pr = d[1] & 3, pb = (d[1] >> 2) & 3, pl = (d[1] >> 4) & 3;
    int horiz = d[1] & 0x40;
    int pos = d[2] & 31;
    for (int y = 0; y < 18; y++) {
        for (int x = 0; x < 20; x++) {
            int c = horiz ? y : x;
            G.sgbAttr[y][x] = (u8)(c < pos ? pl : c == pos ? pb : pr);
        }
    }
}

static void SgbAttrChr(const u8* d) {
    int x = d[1] % 20, y = d[2] % 18;
    int n = d[3] | (d[4] << 8);
    int vertical = d[5] & 1;
    for (int i = 0; i < n && i < 360; i++) {
        u8 b = d[6 + i / 4];
        G.sgbAttr[y][x] = (u8)((b >> (6 - 2 * (i % 4))) & 3);
        if (vertical) {
            if (++y >= 18) { y = 0; if (++x >= 20) x = 0; }
        } else if (++x >= 20) {
            x = 0;
            if (++y >= 18) y = 0;
        }
    }
}

static void SgbAttrSet(int file) {
    if (file >= 45) {
        return;
    }
    const u8* f = G.sgbAttrFiles[file];
    for (int i = 0; i < 360; i++) {
        G.sgbAttr[i / 20][i % 20] = (u8)((f[i / 4] >> (6 - 2 * (i % 4))) & 3);
    }
}

// The *_TRN commands take their data from the picture the SGB samples after the packet: run them at the end of the
// next frame (games with a Game Boy Color path, like Yellow, fill VRAM only after sending the packet).
static void SgbCommand(void);

static void SgbTransfer(int cmd, u8 arg) {
    static u8 trn[4096];
    // Pokémon Yellow (the tower's cartridge variant 4) in Game Boy Color mode sends its border packets with the wrong
    // source (its CGB palette update clobbers HL first), so the tower takes the border from its own copy of Yellow's
    // (US) border: 128 2bpp tiles, then the tile map and palettes, at the start of its UI asset (unk_5C58).
    const u8* stadium = *(const u8**)EMU_FIELD(gEmu, OFF_STADIUM_DATA);
    if (G.cgb && *EMU_FIELD(gEmu, OFF_VARIANT) == 4 && stadium != NULL && (cmd == 0x13 || cmd == 0x14)) {
        memset(trn, 0, sizeof(trn));
        if (cmd == 0x13) {
            for (int t = 0; t < 128; t++) memcpy(trn + t * 32, stadium + t * 16, 16);
            memcpy(G.sgbBorderTiles + (arg & 1) * 4096, trn, 4096);
            G.sgbBorderChanged = 1;
            return;
        }
        memcpy(trn, stadium + 0x800, 0x900);
        memcpy(G.sgbBorderMap, trn, sizeof(G.sgbBorderMap));
        for (int p = 0; p < 4; p++)
            for (int c = 0; c < 16; c++) G.sgbBorderPal[p][c] = Rd16(trn + 0x800 + p * 32 + c * 2);
        G.sgbHasBorder = 1;
        G.sgbBorderChanged = 1;
        return;
    }
    switch (cmd) {
        case 0x0B: // PAL_TRN
            SgbVramTransfer(trn, 4096);
            for (int i = 0; i < 512; i++)
                for (int c = 0; c < 4; c++) G.sgbSysPal[i][c] = Rd16(trn + i * 8 + c * 2);
            break;
        case 0x13: // CHR_TRN
            SgbVramTransfer(trn, 4096);
            memcpy(G.sgbBorderTiles + (arg & 1) * 4096, trn, 4096);
            G.sgbBorderChanged = 1;
            break;
        case 0x14: // PCT_TRN
            SgbVramTransfer(trn, 4096);
            memcpy(G.sgbBorderMap, trn, sizeof(G.sgbBorderMap));
            for (int p = 0; p < 4; p++)
                for (int c = 0; c < 16; c++) G.sgbBorderPal[p][c] = Rd16(trn + 0x800 + p * 32 + c * 2);
            G.sgbHasBorder = 1;
            G.sgbBorderChanged = 1;
            break;
        case 0x15: // ATTR_TRN
            SgbVramTransfer(trn, 45 * 90);
            memcpy(G.sgbAttrFiles, trn, sizeof(G.sgbAttrFiles));
            break;
    }
}

static void SgbCommand(void) {
    const u8* d = G.pkt;
    int cmd = d[0] >> 3;
    switch (cmd) {
        case 0x00: SgbSetPal(0, 1, d); break;
        case 0x01: SgbSetPal(2, 3, d); break;
        case 0x02: SgbSetPal(0, 3, d); break;
        case 0x03: SgbSetPal(1, 2, d); break;
        case 0x04: SgbAttrBlk(d); break;
        case 0x05: SgbAttrLin(d); break;
        case 0x06: SgbAttrDiv(d); break;
        case 0x07: SgbAttrChr(d); break;
        case 0x0A: { // PAL_SET
            for (int p = 0; p < 4; p++) {
                int idx = (d[1 + p * 2] | (d[2 + p * 2] << 8)) & 511;
                memcpy(G.sgbPal[p], G.sgbSysPal[idx], sizeof(G.sgbPal[p]));
            }
            for (int p = 1; p < 4; p++) {
                G.sgbPal[p][0] = G.sgbPal[0][0];
            }
            if (d[9] & 0x80) {
                SgbAttrSet(d[9] & 0x3F);
            }
            if (d[9] & 0x40) {
                G.sgbMask = 0;
                G.maskEn = 0;
                G.unmaskDelay = 4;
            }
            break;
        }
        case 0x0B: case 0x13: case 0x14: case 0x15:
            if (cmd == 0x13 || cmd == 0x14) G.borderTrn = 1;
            G.trnCmd = cmd;
            G.trnArg = d[1];
            G.trnFrames = 2; // this frame's end, then the next one
            break;
        case 0x11: // MLT_REQ
            G.sgbPlayers = (d[1] & 3) == 3 ? 4 : (d[1] & 3) + 1;
            if (G.sgbPlayers == 3) G.sgbPlayers = 2;
            G.sgbPlayer = 0;
            break;
        case 0x16: // ATTR_SET
            SgbAttrSet(d[1] & 0x3F);
            if (d[1] & 0x40) {
                G.sgbMask = 0;
                G.maskEn = 0;
                G.unmaskDelay = 4;
            }
            break;
        case 0x17: // MASK_EN
            G.sgbMask = d[1] & 3;
            G.maskEn = d[1] & 3;
            G.unmaskDelay = G.maskEn ? 0 : 4;
            if (G.sgbMask == 1) memcpy(G.frozen, G.shades, sizeof(G.frozen));
            break;
        default:
            break; // sound, data transfer, jump, icon, ...: not needed
    }
}

static void SgbWriteP1(u8 v) {
    u8 old = G.lastP1;
    G.lastP1 = v;
    // multiplayer: the controller id advances when P15 goes from low to high
    if (G.sgbPlayers > 1 && !(old & 0x20) && (v & 0x20) && G.pktState == 0) {
        G.sgbPlayer = (G.sgbPlayer + 1) % G.sgbPlayers;
    }
    if (v == 0x00) { // reset pulse: a packet starts
        G.pktState = 1;
        G.pktBits = 0;
        if (G.pktCount == 0) {
            memset(G.pkt, 0, sizeof(G.pkt));
        }
        return;
    }
    if (G.pktState == 0 || v == 0x30 || old != 0x30) {
        return;
    }
    int bit = (v == 0x10) ? 1 : 0; // P15 low: 1, P14 low: 0
    if (G.pktBits < 128) {
        int byte = G.pktCount * 16 + G.pktBits / 8;
        if (byte < (int)sizeof(G.pkt) && bit) {
            G.pkt[byte] |= (u8)(1 << (G.pktBits & 7));
        }
        G.pktBits++;
        return;
    }
    // stop bit: the packet is complete
    G.pktState = 0;
    if (G.pktCount == 0) {
        G.pktTotal = G.pkt[0] & 7;
        if (G.pktTotal == 0) G.pktTotal = 1;
    }
    if (++G.pktCount >= G.pktTotal) {
        SgbCommand();
        G.pktCount = 0;
    }
}

// ---- LCD ----
static void RenderLine(int ly) {
    u8 lcdc = G.io[0x40];
    u8* out = G.shades[ly];
    u8 bgp = G.io[0x47];
    if (!(lcdc & 0x01)) {
        memset(out, bgp & 3, 160);
        memset(G.bgIndex, 0, 160);
    } else {
        u8 scy = G.io[0x42], scx = G.io[0x43];
        u16 map = (lcdc & 0x08) ? 0x1C00 : 0x1800;
        int y = (ly + scy) & 255;
        for (int x = 0; x < 160; x++) {
            int bx = (x + scx) & 255;
            u8 t = G.vram[map + (y / 8) * 32 + bx / 8];
            u16 base = (lcdc & 0x10) ? (u16)(t * 16) : (u16)(0x1000 + (s8)t * 16);
            u8 lo = G.vram[base + (y & 7) * 2], hi = G.vram[base + (y & 7) * 2 + 1];
            int bit = 7 - (bx & 7);
            int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            G.bgIndex[x] = (u8)ci;
            out[x] = (bgp >> (ci * 2)) & 3;
        }
        // window
        u8 wy = G.io[0x4A], wx = G.io[0x4B];
        if ((lcdc & 0x20) && ly >= wy && wx <= 166) {
            u16 wmap = (lcdc & 0x40) ? 0x1C00 : 0x1800;
            int wl = G.windowLine;
            int drawn = 0;
            for (int x = 0; x < 160; x++) {
                int wxp = x - (wx - 7);
                if (wxp < 0) continue;
                drawn = 1;
                u8 t = G.vram[wmap + (wl / 8) * 32 + wxp / 8];
                u16 base = (lcdc & 0x10) ? (u16)(t * 16) : (u16)(0x1000 + (s8)t * 16);
                u8 lo = G.vram[base + (wl & 7) * 2], hi = G.vram[base + (wl & 7) * 2 + 1];
                int bit = 7 - (wxp & 7);
                int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                G.bgIndex[x] = (u8)ci;
                out[x] = (bgp >> (ci * 2)) & 3;
            }
            if (drawn) G.windowLine++;
        }
    }
    // sprites
    if (lcdc & 0x02) {
        int h = (lcdc & 0x04) ? 16 : 8;
        int idx[10], n = 0;
        for (int i = 0; i < 40 && n < 10; i++) {
            int sy = G.oam[i * 4] - 16;
            if (ly >= sy && ly < sy + h) idx[n++] = i;
        }
        // DMG priority: smaller X first, then OAM order: draw in reverse priority
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++)
                if (G.oam[idx[b] * 4 + 1] < G.oam[idx[a] * 4 + 1] ||
                    (G.oam[idx[b] * 4 + 1] == G.oam[idx[a] * 4 + 1] && idx[b] < idx[a])) {
                    int t = idx[a]; idx[a] = idx[b]; idx[b] = t;
                }
        for (int k = n - 1; k >= 0; k--) {
            const u8* s = &G.oam[idx[k] * 4];
            int sy = s[0] - 16, sx = s[1] - 8;
            u8 tile = s[2], attr = s[3];
            int row = ly - sy;
            if (attr & 0x40) row = h - 1 - row;
            if (h == 16) tile &= 0xFE;
            u16 base = (u16)(tile * 16 + row * 2);
            u8 lo = G.vram[base & 0x1FFF], hi = G.vram[(base + 1) & 0x1FFF];
            u8 pal = (attr & 0x10) ? G.io[0x49] : G.io[0x48];
            for (int px = 0; px < 8; px++) {
                int x = sx + px;
                if (x < 0 || x >= 160) continue;
                int bit = (attr & 0x20) ? px : 7 - px;
                int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (ci == 0) continue;
                if ((attr & 0x80) && G.bgIndex[x] != 0) continue;
                out[x] = (pal >> (ci * 2)) & 3;
            }
        }
    }
}

// Game Boy Color line: tile attributes from VRAM bank 1 (palette, bank, flips, BG priority), colour palettes, sprites
// by OAM order. LCDC bit 0 only takes the BG/window's priority away.
static u16 PalColor(const u8* pal, int p, int ci) {
    return (u16)((pal[p * 8 + ci * 2] | (pal[p * 8 + ci * 2 + 1] << 8)) & 0x7FFF);
}

static void BgPixelCgb(u16 map, int tx, int ty, int px, int py, int x, u16* out) {
    u16 m = (u16)(map + ty * 32 + tx);
    u8 t = G.vram[m], attr = G.vram[0x2000 + m];
    u8 lcdc = G.io[0x40];
    u16 base = (lcdc & 0x10) ? (u16)(t * 16) : (u16)(0x1000 + (s8)t * 16);
    if (attr & 0x08) base = (u16)(base + 0x2000);
    if (attr & 0x40) py = 7 - py;
    u8 lo = G.vram[base + py * 2], hi = G.vram[base + py * 2 + 1];
    int bit = (attr & 0x20) ? px : 7 - px;
    int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
    G.bgIndex[x] = (u8)ci;
    G.bgAttr[x] = attr;
    out[x] = PalColor(G.bgPal, attr & 7, ci);
}

static void RenderLineCgb(int ly) {
    u8 lcdc = G.io[0x40];
    u16* out = G.color[ly];
    u8 scy = G.io[0x42], scx = G.io[0x43];
    u16 map = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    int y = (ly + scy) & 255;
    for (int x = 0; x < 160; x++) {
        int bx = (x + scx) & 255;
        BgPixelCgb(map, bx / 8, y / 8, bx & 7, y & 7, x, out);
    }
    u8 wy = G.io[0x4A], wx = G.io[0x4B];
    if ((lcdc & 0x20) && ly >= wy && wx <= 166) {
        u16 wmap = (lcdc & 0x40) ? 0x1C00 : 0x1800;
        int wl = G.windowLine, drawn = 0;
        for (int x = 0; x < 160; x++) {
            int wxp = x - (wx - 7);
            if (wxp < 0) continue;
            drawn = 1;
            BgPixelCgb(wmap, wxp / 8, wl / 8, wxp & 7, wl & 7, x, out);
        }
        if (drawn) G.windowLine++;
    }
    if (lcdc & 0x02) {
        int h = (lcdc & 0x04) ? 16 : 8;
        int idx[10], n = 0;
        for (int i = 0; i < 40 && n < 10; i++) {
            int sy = G.oam[i * 4] - 16;
            if (ly >= sy && ly < sy + h) idx[n++] = i;
        }
        u8 done[160] = { 0 };
        for (int k = 0; k < n; k++) { // lower OAM index first; the first opaque sprite pixel wins
            const u8* sp = &G.oam[idx[k] * 4];
            int sy = sp[0] - 16, sx = sp[1] - 8;
            u8 tile = sp[2], attr = sp[3];
            int row = ly - sy;
            if (attr & 0x40) row = h - 1 - row;
            if (h == 16) tile &= 0xFE;
            u16 base = (u16)(((attr & 0x08) ? 0x2000 : 0) + tile * 16 + row * 2);
            u8 lo = G.vram[base], hi = G.vram[base + 1];
            for (int px = 0; px < 8; px++) {
                int x = sx + px;
                if (x < 0 || x >= 160 || done[x]) continue;
                int bit = (attr & 0x20) ? px : 7 - px;
                int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (ci == 0) continue;
                done[x] = 1;
                if ((lcdc & 0x01) && G.bgIndex[x] != 0 && ((G.bgAttr[x] & 0x80) || (attr & 0x80))) continue;
                out[x] = PalColor(G.objPal, attr & 7, ci);
            }
        }
    }
}

static void HBlankDma(void) {
    if (!G.hdmaLeft) return;
    for (int i = 0; i < 16; i++) {
        G.vram[G.vbk * 0x2000 + ((G.hdmaDst + i) & 0x1FFF)] = Read((u16)(G.hdmaSrc + i));
    }
    G.hdmaSrc = (u16)(G.hdmaSrc + 16);
    G.hdmaDst = (u16)(G.hdmaDst + 16);
    G.hdmaLeft--;
}

static void SetMode(int mode) {
    u8 stat = G.io[0x41];
    G.io[0x41] = (u8)((stat & ~3) | mode);
    if ((mode == 0 && (stat & 0x08)) || (mode == 1 && (stat & 0x10)) || (mode == 2 && (stat & 0x20))) {
        RequestInterrupt(1);
    }
}

static void CheckLyc(void) {
    if (G.io[0x44] == G.io[0x45]) {
        if (!(G.io[0x41] & 0x04)) {
            G.io[0x41] |= 0x04;
            if (G.io[0x41] & 0x40) RequestInterrupt(1);
        }
    } else {
        G.io[0x41] &= ~0x04;
    }
}

static void LcdStep(u32 cycles) {
    if (!(G.io[0x40] & 0x80)) {
        // LCD off: frames still pass for the presentation
        G.dot += cycles;
        if (G.dot >= 70224) {
            G.dot -= 70224;
            G.frameDone = 1;
        }
        return;
    }
    while (cycles > 0) {
        u32 step = cycles > 4 ? 4 : cycles;
        cycles -= step;
        u32 lineDot = G.dot % 456;
        int ly = G.io[0x44];
        u32 before = lineDot;
        G.dot += step;
        lineDot = G.dot % 456;
        if (ly < 144) {
            if (before < 80 && lineDot >= 80) {
                SetMode(3);
                if (G.cgb) RenderLineCgb(ly);
                else RenderLine(ly);
            } else if (before < 252 && lineDot >= 252) {
                SetMode(0);
                if (G.cgb) HBlankDma();
            }
        }
        if (G.dot >= 456) {
            G.dot -= 456;
            ly++;
            if (ly == 144) {
                SetMode(1);
                RequestInterrupt(0);
                G.frameDone = 1;
            } else if (ly > 153) {
                ly = 0;
                G.windowLine = 0;
            }
            G.io[0x44] = (u8)ly;
            if (ly < 144) SetMode(2);
            CheckLyc();
        }
    }
}

static void TimerStep(u32 cycles) {
    u32 oldDiv = G.divCounter;
    G.divCounter = (G.divCounter + cycles) & 0xFFFF;
    (void)oldDiv;
    if (G.io[0x07] & 4) {
        static const u32 period[4] = { 1024, 16, 64, 256 };
        G.timaCounter += cycles;
        u32 p = period[G.io[0x07] & 3];
        while (G.timaCounter >= p) {
            G.timaCounter -= p;
            if (++G.io[0x05] == 0) {
                G.io[0x05] = G.io[0x06];
                RequestInterrupt(2);
            }
        }
    }
    // serial with the internal clock and no partner: 8 bits shift in 0xFF
    if ((G.io[0x02] & 0x81) == 0x81) {
        G.serialCounter += cycles;
        if (G.serialCounter >= 4096) {
            G.io[0x01] = 0xFF;
            G.io[0x02] &= 0x7F;
            RequestInterrupt(3);
        }
    }
}

// ---- CPU ----
#define BC ((u16)((G.b << 8) | G.c))
#define DE ((u16)((G.d << 8) | G.e))
#define HL ((u16)((G.h << 8) | G.l))
#define SET_BC(v) (G.b = (u8)((v) >> 8), G.c = (u8)(v))
#define SET_DE(v) (G.d = (u8)((v) >> 8), G.e = (u8)(v))
#define SET_HL(v) (G.h = (u8)((v) >> 8), G.l = (u8)(v))

static u8 Fetch(void) {
    return Read(G.pc++);
}
static u16 Fetch16(void) {
    u16 lo = Fetch();
    return (u16)(lo | (Fetch() << 8));
}
static void Push(u16 v) {
    Write(--G.sp, (u8)(v >> 8));
    Write(--G.sp, (u8)v);
}
static u16 Pop(void) {
    u16 lo = Read(G.sp++);
    return (u16)(lo | (Read(G.sp++) << 8));
}

static u8* Reg8(int i) {
    switch (i) {
        case 0: return &G.b;
        case 1: return &G.c;
        case 2: return &G.d;
        case 3: return &G.e;
        case 4: return &G.h;
        case 5: return &G.l;
        default: return &G.a;
    }
}
static u8 GetR(int i) {
    return i == 6 ? Read(HL) : *Reg8(i);
}
static void SetR(int i, u8 v) {
    if (i == 6) Write(HL, v);
    else *Reg8(i) = v;
}

static void Alu(int op, u8 v) {
    u8 a = G.a;
    int carry = (G.f & FC) ? 1 : 0;
    int r;
    switch (op) {
        case 0: // ADD
            r = a + v;
            G.f = (u8)(((r & 0xFF) ? 0 : FZ) | (((a & 15) + (v & 15)) > 15 ? FH : 0) | (r > 255 ? FC : 0));
            G.a = (u8)r;
            break;
        case 1: // ADC
            r = a + v + carry;
            G.f = (u8)(((r & 0xFF) ? 0 : FZ) | (((a & 15) + (v & 15) + carry) > 15 ? FH : 0) | (r > 255 ? FC : 0));
            G.a = (u8)r;
            break;
        case 2: // SUB
            r = a - v;
            G.f = (u8)(((r & 0xFF) ? 0 : FZ) | FN | ((a & 15) < (v & 15) ? FH : 0) | (r < 0 ? FC : 0));
            G.a = (u8)r;
            break;
        case 3: // SBC
            r = a - v - carry;
            G.f = (u8)(((r & 0xFF) ? 0 : FZ) | FN | (((a & 15) - (v & 15) - carry) < 0 ? FH : 0) | (r < 0 ? FC : 0));
            G.a = (u8)r;
            break;
        case 4: G.a &= v; G.f = (u8)((G.a ? 0 : FZ) | FH); break;
        case 5: G.a ^= v; G.f = (u8)(G.a ? 0 : FZ); break;
        case 6: G.a |= v; G.f = (u8)(G.a ? 0 : FZ); break;
        default: // CP
            r = a - v;
            G.f = (u8)(((r & 0xFF) ? 0 : FZ) | FN | ((a & 15) < (v & 15) ? FH : 0) | (r < 0 ? FC : 0));
            break;
    }
}

static int Cond(int cc) {
    switch (cc) {
        case 0: return !(G.f & FZ);
        case 1: return (G.f & FZ) != 0;
        case 2: return !(G.f & FC);
        default: return (G.f & FC) != 0;
    }
}

static u32 ExecCB(void) {
    u8 op = Fetch();
    int r = op & 7, b = (op >> 3) & 7;
    u8 v = GetR(r);
    u32 cyc = r == 6 ? 16 : 8;
    switch (op >> 6) {
        case 0: {
            u8 res = v;
            int c = 0;
            switch (b) {
                case 0: c = v >> 7; res = (u8)((v << 1) | c); break;               // RLC
                case 1: c = v & 1; res = (u8)((v >> 1) | (c << 7)); break;         // RRC
                case 2: c = v >> 7; res = (u8)((v << 1) | ((G.f & FC) ? 1 : 0)); break; // RL
                case 3: c = v & 1; res = (u8)((v >> 1) | ((G.f & FC) ? 0x80 : 0)); break; // RR
                case 4: c = v >> 7; res = (u8)(v << 1); break;                     // SLA
                case 5: c = v & 1; res = (u8)((v >> 1) | (v & 0x80)); break;       // SRA
                case 6: c = 0; res = (u8)((v >> 4) | (v << 4)); break;             // SWAP
                default: c = v & 1; res = (u8)(v >> 1); break;                     // SRL
            }
            G.f = (u8)((res ? 0 : FZ) | (c ? FC : 0));
            SetR(r, res);
            break;
        }
        case 1: // BIT
            G.f = (u8)((G.f & FC) | FH | ((v >> b) & 1 ? 0 : FZ));
            cyc = r == 6 ? 12 : 8;
            break;
        case 2: SetR(r, (u8)(v & ~(1 << b))); break; // RES
        default: SetR(r, (u8)(v | (1 << b))); break; // SET
    }
    return cyc;
}

static u32 Step(void) {
    // interrupts
    u8 pending = G.ie & G.io[0x0F] & 0x1F;
    if (pending) {
        G.halted = 0;
        if (G.ime) {
            for (int i = 0; i < 5; i++) {
                if (pending & (1 << i)) {
                    G.ime = 0;
                    G.io[0x0F] &= (u8)~(1 << i);
                    Push(G.pc);
                    G.pc = (u16)(0x40 + i * 8);
                    return 20;
                }
            }
        }
    }
    if (G.halted) {
        return 4;
    }
    if (G.imeDelay) {
        G.imeDelay = 0;
        G.ime = 1;
    }
    u8 op = Fetch();
    switch (op) {
        case 0x00: return 4;
        case 0x01: { u16 v = Fetch16(); SET_BC(v); return 12; }
        case 0x02: Write(BC, G.a); return 8;
        case 0x03: SET_BC(BC + 1); return 8;
        case 0x07: { int c = G.a >> 7; G.a = (u8)((G.a << 1) | c); G.f = c ? FC : 0; return 4; }
        case 0x08: { u16 a = Fetch16(); Write(a, (u8)G.sp); Write((u16)(a + 1), (u8)(G.sp >> 8)); return 20; }
        case 0x09: case 0x19: case 0x29: case 0x39: {
            u16 v = op == 0x09 ? BC : op == 0x19 ? DE : op == 0x29 ? HL : G.sp;
            u32 r = (u32)HL + v;
            G.f = (u8)((G.f & FZ) | (((HL & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0));
            SET_HL(r);
            return 8;
        }
        case 0x0A: G.a = Read(BC); return 8;
        case 0x0B: SET_BC(BC - 1); return 8;
        case 0x0F: { int c = G.a & 1; G.a = (u8)((G.a >> 1) | (c << 7)); G.f = c ? FC : 0; return 4; }
        case 0x10: // STOP: on the Game Boy Color with KEY1 armed, the speed switch
            Fetch();
            if (G.cgb && (G.io[0x4D] & 1)) {
                G.doubleSpeed ^= 1;
                G.io[0x4D] = 0;
            }
            return 4;
        case 0x11: { u16 v = Fetch16(); SET_DE(v); return 12; }
        case 0x12: Write(DE, G.a); return 8;
        case 0x13: SET_DE(DE + 1); return 8;
        case 0x17: { int c = G.a >> 7; G.a = (u8)((G.a << 1) | ((G.f & FC) ? 1 : 0)); G.f = c ? FC : 0; return 4; }
        case 0x18: { s8 e = (s8)Fetch(); G.pc = (u16)(G.pc + e); return 12; }
        case 0x1A: G.a = Read(DE); return 8;
        case 0x1B: SET_DE(DE - 1); return 8;
        case 0x1F: { int c = G.a & 1; G.a = (u8)((G.a >> 1) | ((G.f & FC) ? 0x80 : 0)); G.f = c ? FC : 0; return 4; }
        case 0x20: case 0x28: case 0x30: case 0x38: {
            s8 e = (s8)Fetch();
            if (Cond((op >> 3) & 3)) { G.pc = (u16)(G.pc + e); return 12; }
            return 8;
        }
        case 0x21: { u16 v = Fetch16(); SET_HL(v); return 12; }
        case 0x22: Write(HL, G.a); SET_HL(HL + 1); return 8;
        case 0x23: SET_HL(HL + 1); return 8;
        case 0x27: { // DAA
            int a = G.a;
            if (!(G.f & FN)) {
                if ((G.f & FC) || a > 0x99) { a += 0x60; G.f |= FC; }
                if ((G.f & FH) || (a & 0x0F) > 0x09) a += 0x06;
            } else {
                if (G.f & FC) a -= 0x60;
                if (G.f & FH) a -= 0x06;
            }
            G.a = (u8)a;
            G.f = (u8)((G.f & (FN | FC)) | (G.a ? 0 : FZ));
            return 4;
        }
        case 0x2A: G.a = Read(HL); SET_HL(HL + 1); return 8;
        case 0x2B: SET_HL(HL - 1); return 8;
        case 0x2F: G.a = (u8)~G.a; G.f |= FN | FH; return 4;
        case 0x31: G.sp = Fetch16(); return 12;
        case 0x32: Write(HL, G.a); SET_HL(HL - 1); return 8;
        case 0x33: G.sp++; return 8;
        case 0x37: G.f = (u8)((G.f & FZ) | FC); return 4;
        case 0x3A: G.a = Read(HL); SET_HL(HL - 1); return 8;
        case 0x3B: G.sp--; return 8;
        case 0x3F: G.f = (u8)((G.f & FZ) | ((G.f & FC) ? 0 : FC)); return 4;
        case 0x76: G.halted = 1; return 4;
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
            if (Cond((op >> 3) & 3)) { G.pc = Pop(); return 20; }
            return 8;
        case 0xC1: { u16 v = Pop(); SET_BC(v); return 12; }
        case 0xD1: { u16 v = Pop(); SET_DE(v); return 12; }
        case 0xE1: { u16 v = Pop(); SET_HL(v); return 12; }
        case 0xF1: { u16 v = Pop(); G.a = (u8)(v >> 8); G.f = (u8)(v & 0xF0); return 12; }
        case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
            u16 a = Fetch16();
            if (Cond((op >> 3) & 3)) { G.pc = a; return 16; }
            return 12;
        }
        case 0xC3: G.pc = Fetch16(); return 16;
        case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
            u16 a = Fetch16();
            if (Cond((op >> 3) & 3)) { Push(G.pc); G.pc = a; return 24; }
            return 12;
        }
        case 0xC5: Push(BC); return 16;
        case 0xD5: Push(DE); return 16;
        case 0xE5: Push(HL); return 16;
        case 0xF5: Push((u16)((G.a << 8) | G.f)); return 16;
        case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            Alu((op >> 3) & 7, Fetch());
            return 8;
        case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            Push(G.pc);
            G.pc = (u16)(op & 0x38);
            return 16;
        case 0xC9: G.pc = Pop(); return 16;
        case 0xCB: return ExecCB();
        case 0xCD: { u16 a = Fetch16(); Push(G.pc); G.pc = a; return 24; }
        case 0xD9: G.pc = Pop(); G.ime = 1; return 16;
        case 0xE0: Write((u16)(0xFF00 + Fetch()), G.a); return 12;
        case 0xE2: Write((u16)(0xFF00 + G.c), G.a); return 8;
        case 0xE8: {
            s8 e = (s8)Fetch();
            G.f = (u8)((((G.sp & 15) + (e & 15)) > 15 ? FH : 0) | (((G.sp & 255) + (u8)e) > 255 ? FC : 0));
            G.sp = (u16)(G.sp + e);
            return 16;
        }
        case 0xE9: G.pc = HL; return 4;
        case 0xEA: Write(Fetch16(), G.a); return 16;
        case 0xF0: G.a = Read((u16)(0xFF00 + Fetch())); return 12;
        case 0xF2: G.a = Read((u16)(0xFF00 + G.c)); return 8;
        case 0xF3: G.ime = 0; G.imeDelay = 0; return 4;
        case 0xF8: {
            s8 e = (s8)Fetch();
            G.f = (u8)((((G.sp & 15) + (e & 15)) > 15 ? FH : 0) | (((G.sp & 255) + (u8)e) > 255 ? FC : 0));
            SET_HL(G.sp + e);
            return 12;
        }
        case 0xF9: G.sp = HL; return 8;
        case 0xFA: G.a = Read(Fetch16()); return 16;
        case 0xFB: G.imeDelay = 1; return 4;
        default:
            break;
    }
    if (op >= 0x40 && op < 0x80) { // LD r,r'
        SetR((op >> 3) & 7, GetR(op & 7));
        return ((op & 7) == 6 || ((op >> 3) & 7) == 6) ? 8 : 4;
    }
    if (op >= 0x80 && op < 0xC0) {
        Alu((op >> 3) & 7, GetR(op & 7));
        return (op & 7) == 6 ? 8 : 4;
    }
    switch (op & 0xC7) {
        case 0x04: { // INC r
            int r = (op >> 3) & 7;
            u8 v = GetR(r);
            u8 res = (u8)(v + 1);
            SetR(r, res);
            G.f = (u8)((G.f & FC) | (res ? 0 : FZ) | ((v & 15) == 15 ? FH : 0));
            return r == 6 ? 12 : 4;
        }
        case 0x05: { // DEC r
            int r = (op >> 3) & 7;
            u8 v = GetR(r);
            u8 res = (u8)(v - 1);
            SetR(r, res);
            G.f = (u8)((G.f & FC) | FN | (res ? 0 : FZ) | ((v & 15) == 0 ? FH : 0));
            return r == 6 ? 12 : 4;
        }
        case 0x06: { // LD r,n
            int r = (op >> 3) & 7;
            SetR(r, Fetch());
            return r == 6 ? 12 : 8;
        }
    }
    // undefined opcodes (0xD3, 0xDB, ...): the CPU locks up; treat as NOP
    return 4;
}

// ---- entry points used by the GB Tower code ----
static void Reset(void* emu) {
    memset(&G, 0, sizeof(G));
    gEmu = emu;
    *EMU_FIELD(emu, OFF_BORDER_SHOWN) = 0;
    G.rom = *(u8**)EMU_FIELD(emu, OFF_ROM);
    G.sram = EMU_FIELD(emu, OFF_SRAM);
    G.sramSize = *(u32*)EMU_FIELD(emu, OFF_SRAM_SIZE);
    static const u32 romSizes[9] = { 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000 };
    u8 rs = G.rom[0x148];
    G.romSize = rs < 9 ? romSizes[rs] : 0x100000;
    if (G.romSize > 0x100000) G.romSize = 0x100000; // the GB Tower loads at most 1 MB
    switch (G.rom[0x147]) {
        case 0x01: case 0x02: case 0x03: G.mbc = 1; break;
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: G.mbc = 3; break;
        case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: G.mbc = 5; break;
        default: G.mbc = 0; break;
    }
    G.romBank = 1;
    G.cgb = (G.rom[0x143] & 0x80) != 0 && !getenv("PORT_GB_NO_CGB");
    if (G.cgb) { // Game Boy Color after its boot ROM (A = 0x11 tells the game); palettes start white
        G.a = 0x11; G.f = 0x80; G.b = 0x00; G.c = 0x00; G.d = 0xFF; G.e = 0x56; G.h = 0x00; G.l = 0x0D;
        for (int i = 0; i < 64; i += 8) { // as the N64 emulator: colours 0-1 white, 2-3 black in every palette
            memcpy(&G.bgPal[i], "\xFF\x7F\xFF\x7F", 4);
            memcpy(&G.objPal[i], "\xFF\x7F\xFF\x7F", 4);
        }
        G.svbk = 1;
    } else { // Super Game Boy after its boot ROM
        G.a = 0x01; G.f = 0x00; G.b = 0x00; G.c = 0x14; G.d = 0x00; G.e = 0x00; G.h = 0xC0; G.l = 0x60;
    }
    G.sp = 0xFFFE;
    G.pc = 0x0100;
    G.io[0x00] = 0x30;
    G.io[0x05] = 0; G.io[0x06] = 0; G.io[0x07] = 0;
    G.io[0x10] = 0x80; G.io[0x11] = 0xBF; G.io[0x12] = 0xF3; G.io[0x14] = 0xBF; G.io[0x16] = 0x3F;
    G.io[0x19] = 0xBF; G.io[0x1A] = 0x7F; G.io[0x1B] = 0xFF; G.io[0x1C] = 0x9F; G.io[0x1E] = 0xBF;
    G.io[0x20] = 0xFF; G.io[0x23] = 0xBF; G.io[0x24] = 0x77; G.io[0x25] = 0xF3; G.io[0x26] = 0xF0;
    G.io[0x40] = 0x91; G.io[0x47] = 0xFC; G.io[0x48] = 0xFF; G.io[0x49] = 0xFF;
    G.io[0x41] = 0x85;
    G.lastP1 = 0x30;
    G.sgbPlayers = 1;
    // the SGB's default palette 0 (shades of green-grey) until the game sends its own
    static const u16 defPal[4] = { 0x67BF, 0x265B, 0x10B5, 0x2866 };
    for (int p = 0; p < 4; p++) memcpy(G.sgbPal[p], defPal, sizeof(defPal));
    G.initialised = 1;
}

void GbEmu_InitContext(void* emu) {
    Reset(emu);
}

void GbSgb_RenderBorder(void* emu, void* tables) {
    (void)emu;
    (void)tables;
}

// The end of one Game Boy frame (VBlank).
static void FrameEnd(void* emu) {
    if (G.trnFrames && --G.trnFrames == 0) {
        SgbTransfer(G.trnCmd, G.trnArg);
    }
    // the frame goes to the presentation unless the screen is masked (the N64 emulator skips its VBlank hand-over to the
    // RSP task while context 0x5401 is set, 0x8120B0C4): games mask the screen while they load their border through
    // VRAM, so the transfer's tiles are never shown
    if (!G.maskShown) {
        memcpy(G.shown, G.color, sizeof(G.shown));
        G.shownLcdOn = (G.io[0x40] & 0x80) != 0;
    }
    // as the N64 emulator's frame end (gb_tower_emulator_code_asm.s, 0x8120A010): the mask in effect follows MASK_EN,
    // after a cancel 4 frames late. When it changed to 0 after a border transfer, raise context byte 0x5404; the GB
    // Tower then starts its frame switching (slide from the Game Boy frame to the game's own border, Z cycles the
    // Doduo/Dodrio layouts: gb_tower_emulator_7F9A0.c, sSgbBorderReady)
    if (G.unmaskDelay) {
        G.unmaskDelay--;
    } else if (G.maskEn != G.maskShown) {
        G.maskShown = G.maskEn;
        if (G.maskEn == 0 && G.borderTrn) {
            *EMU_FIELD(emu, OFF_BORDER_SHOWN) = 1;
            G.borderTrn = 0;
        }
    }
}

// One N64 frame. The GB Tower code calls it once per N64 frame with a budget of 154 lines (one Game Boy frame) times
// its speed, emu->unk_548E: 1, or 2 / 4 after C-Right in the Doduo / Dodrio Tower (gb_tower_emulator_7F9A0.c). The N64
// emulator keeps the budget at context 0x53DE and runs that many lines, so the Game Boy game runs 2x / 4x; running a
// single frame regardless left the fast-forward without effect.
int GbCpu_Execute(void* emu, int cycles) {
    if (!G.initialised || gEmu != emu) {
        Reset(emu);
    }
    u32 speed = (u32)cycles / 0x9A;
    G.speed = speed < 1 ? 1 : speed > 4 ? 4 : speed;
    G.pad = *(u16*)EMU_FIELD(emu, OFF_PAD);
    // save RAM write-back: count down the pages written last time (see MarkSramDirty)
    u8* dirty = EMU_FIELD(emu, OFF_DIRTY);
    u8* delay = EMU_FIELD(emu, OFF_DELAY);
    for (int i = 0; i < 0x80; i++) {
        if (dirty[i] == 0 && delay[i] != 0 && delay[i] < 0xFE) delay[i]--;
    }
    G.soundBase = 0;
    for (u32 frame = 0; frame < G.speed; frame++) {
        G.frameDone = 0;
        G.frameCycles = 0;
        for (u32 guard = 0; !G.frameDone && guard < 200000; guard++) {
            u32 c = Step();
            G.frameCycles += c;
            TimerStep(c);
            LcdStep(G.doubleSpeed ? c / 2 : c); // the LCD keeps its pace in double speed
        }
        G.soundBase += G.frameCycles;
        FrameEnd(emu);
    }
    return 0;
}

// ---- frame composition (the job of the GB Tower's RSP microcode) ----
static u16 Rgba5551(u16 bgr) {
    u16 r = bgr & 31, g = (bgr >> 5) & 31, b = (bgr >> 10) & 31;
    return (u16)((r << 11) | (g << 6) | (b << 1) | 1);
}

static void PutPx(u16* fb, int x, int y, u16 c) {
    fb[y * 320 + x] = (u16)((c >> 8) | (c << 8)); // frame buffers hold N64 byte order
}

// Draws the Super Game Boy picture (256x224: border, Game Boy screen at 48,40) into a GB Tower frame buffer (320 wide)
// at (ox, oy), then the GB Tower's status strip (320x16, RGBA5551 in N64 byte order, alpha bit = drawn) at (0, sy).
// brightness (0..0x40, the task's word 0x5C bits 16-23: emu->unk_53FD) scales the Game Boy screen: the tower fades it
// to black behind its in-game menu (C-Up) and in again afterwards; ignoring it left the menu text over the picture.
// frameBrightness (bits 24-31: emu->unk_53FC) scales the frame around it while the tower switches frames (Z).
// frameImage is the tower's frame for the current layout (the Game Boy console, or the Doduo / Dodrio Stadium art
// unlocked by Cup wins); NULL is layout 0, the game's own Super Game Boy border (Yellow's Pikachu border).
// Without a border from the game (Game Boy Color games, plain Game Boy games) the tower's own frame image is the
// border: 256 RGBA5551 palette entries (N64 byte order) followed by 256x224 colour indices.
void Port_GbPresent(void* frameBuffer, const void* statusStrip, const void* frameImage, int ox, int oy, int sy,
                    int brightness, int frameBrightness) {
    u16* fb = (u16*)frameBuffer;
    if (!G.initialised) {
        return;
    }
    u16 backdrop = Rgba5551(G.sgbPal[0][0]);
    for (int y = 0; y < 224; y++) {
        for (int x = 0; x < 256; x++) {
            u16 c = backdrop;
            int gx = x - 48, gy = y - 40;
            int inGb = gx >= 0 && gx < 160 && gy >= 0 && gy < 144;
            int borderDrawn = 0;
            if (frameImage != NULL) {
                const u8* img = (const u8*)frameImage;
                u8 ci = img[512 + y * 256 + x];
                u16 be = (u16)((img[ci * 2] << 8) | img[ci * 2 + 1]);
                if (!inGb) {
                    c = be;
                    borderDrawn = 1;
                }
            } else if (G.sgbHasBorder) {
                const u8* m = &G.sgbBorderMap[((y / 8) * 32 + x / 8) * 2];
                u16 entry = (u16)(m[0] | (m[1] << 8));
                int tile = entry & 0xFF, pal = ((entry >> 10) & 7) - 4, hf = entry & 0x4000, vf = entry & 0x8000;
                int px = hf ? 7 - (x & 7) : (x & 7), py = vf ? 7 - (y & 7) : (y & 7);
                const u8* t = &G.sgbBorderTiles[tile * 32];
                int bit = 7 - px;
                int ci = ((t[py * 2] >> bit) & 1) | (((t[py * 2 + 1] >> bit) & 1) << 1) | (((t[16 + py * 2] >> bit) & 1) << 2) |
                         (((t[17 + py * 2] >> bit) & 1) << 3);
                if (ci != 0 && pal >= 0 && pal < 4) {
                    c = Rgba5551(G.sgbBorderPal[pal][ci]);
                    borderDrawn = 1;
                }
            }
            if (inGb && !borderDrawn && G.cgb) {
                c = G.shownLcdOn ? Rgba5551(G.shown[gy][gx]) : 0xFFFF;
            } else if (inGb && !borderDrawn) {
                if (G.sgbMask == 2) {
                    c = 0x0001;
                } else if (G.sgbMask == 3) {
                    c = backdrop;
                } else {
                    u8 shade = G.sgbMask == 1 ? G.frozen[gy][gx] : G.shades[gy][gx];
                    if (!(G.io[0x40] & 0x80)) shade = 0;
                    c = Rgba5551(G.sgbPal[G.sgbAttr[gy / 8][gx / 8] & 3][shade]);
                }
            }
            int fade = (inGb && !borderDrawn) ? brightness : frameBrightness;
            if (fade < 0x40) {
                int k = fade < 0 ? 0 : fade;
                c = (u16)(((((c >> 11) & 31) * k / 0x40) << 11) | ((((c >> 6) & 31) * k / 0x40) << 6) |
                          ((((c >> 1) & 31) * k / 0x40) << 1) | (c & 1));
            }
            int fx = ox + x, fy = oy + y;
            if (fx >= 0 && fx < 320 && fy >= 0 && fy < 288) {
                PutPx(fb, fx, fy, c);
            }
        }
    }
    if (statusStrip != NULL) {
        const u16* s = (const u16*)statusStrip;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 320; x++) {
                u16 v = s[y * 320 + x];
                if (v & 0xFFFE) { // drawn pixels (the strip is cleared to 0; the text has no alpha bit)
                    fb[(sy + y) * 320 + x] = v;
                }
            }
        }
    }
}
