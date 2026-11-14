#include "port_platform.h"
#include <stdlib.h>
// Stand-ins that let the executable link. Items marked TODO(port) have no real implementation yet;
// the rest are small host reimplementations of N64 SDK pieces or of hand-written assembly.
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

// ---- libc-ish ------------------------------------------------------------------------------------

void _bcopy(void* src, void* dst, s32 n) { memmove(dst, src, (size_t)n); }
void __bcopy(void* src, void* dst, s32 n) { memmove(dst, src, (size_t)n); }

float __sinf(float x) { return sinf(x); }
float __cosf(float x) { return cosf(x); }
u64 __ull_rem(u64 a, u64 b) { return a % b; }
float __ull_to_f(u64 a) { return (float)a; }

// SDK xprintf entry point: formats into a scratch buffer and hands it to the output callback in one chunk.
typedef char* PrintfOut(char*, const char*, size_t);
int _Printf(PrintfOut* prout, char* arg, const char* fmt, va_list args) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof buf, fmt, args);
    if (n < 0) {
        return n;
    }
    if (n >= (int)sizeof buf) {
        n = (int)sizeof buf - 1;
    }
    prout(arg, buf, (size_t)n);
    return n;
}

// ---- Yay0 (hand-written MIPS in the original: src/yay0.s) -----------------------------------------
// Header: "Yay0", u32 decompressed size, u32 link-table offset, u32 chunk offset (big-endian).
static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

void Yay0_Decompress(void* srcv, void* dstv) {
    const u8* src = (const u8*)srcv;
    u8* dst = (u8*)dstv;
    u32 size = be32(src + 4);
    const u8* links = src + be32(src + 8);
    const u8* chunks = src + be32(src + 12);
    const u8* mask = src + 16;
    u32 out = 0, bits = 0, mbits = 0;
    while (out < size) {
        if (mbits == 0) {
            bits = be32(mask);
            mask += 4;
            mbits = 32;
        }
        if (bits & 0x80000000u) {
            dst[out++] = *chunks++;
        } else {
            u32 link = ((u32)links[0] << 8) | links[1];
            links += 2;
            u32 dist = link & 0xFFF;
            u32 len = link >> 12;
            if (len == 0) {
                len = (u32)(*chunks++) + 18;
            } else {
                len += 2;
            }
            const u8* from = dst + out - (dist + 1);
            while (len-- && out < size) {
                dst[out++] = *from++;
            }
        }
        bits <<= 1;
        mbits--;
    }
}

// The audio code's own Yay0 decoder (hand-written, 517A0.s): same format.
void func_80050BA0(void* src, void* dst) { Yay0_Decompress(src, dst); }

// ---- TODO(port): hand-written assembly with no host implementation yet ------------------------------
// GB Tower emulator core (src/fragments/gb_tower_emulator/gb_tower_emulator_code_asm.s).
// (GbEmu_InitContext, GbCpu_Execute, GbSgb_RenderBorder: native core in gb_core.c)
// (libgsm speech decoder kernels, src/libgsm.s: game/src/libgsm/libgsm.c)
void set_watch_lohi(s32 a) { (void)a; }

// ---- TODO(port): libultra internals referenced by libleo / controller-pak code ----------------------
u32 __OSGlobalIntMask = 0;
void* __osRunQueue = 0;
void __osEnqueueThread(void* q, void* t) { (void)q; (void)t; }
void* __osPopThread(void* q) { (void)q; return 0; }
void __osPiGetAccess(void) {}
void __osPiRelAccess(void) {}
void __osSetHWIntrRoutine(s32 i, void* fn, void* sp) { (void)i; (void)fn; (void)sp; }
s32 __osContRamRead(void* mq, int ch, unsigned short addr, unsigned char* buf) { (void)mq; (void)ch; (void)addr; (void)buf; return 1; }
s32 __osContRamWrite(void* mq, int ch, unsigned short addr, unsigned char* buf, int force) { (void)mq; (void)ch; (void)addr; (void)buf; (void)force; return 1; }
s32 osEPiRawStartDma(void* h, s32 dir, u32 cart, void* dram, u32 size) { (void)h; (void)dir; (void)cart; (void)dram; (void)size; return 0; }
u32 osSetIntMask(u32 m) { (void)m; return 1; }
extern void port_osViRepeatLine(u8 on); // os_devices.cpp
void osViRepeatLine(u8 on) { port_osViRepeatLine(on); }
void __LeoBootGame3(void* entry) { (void)entry; }
u8 leoBootID[32];
u8 leoDiskStack[0x400];

// ---- TODO(port): RSP microcode blobs and ROM-resident data the original linked in ---------------------
long long int rspbootTextStart[1], aspMainTextStart[1], aspMainDataStart[1];
long long int njpgdspMainTextStart[1], njpgdspMainDataStart[1];
unsigned char _binary_assets_us_F3DEX2_bin_start[1];

// ---- TODO(port): audio synthesizer pieces (the audio microcode is not emulated yet) -------------------
void alSynNew(void* drvr, void* c) { (void)drvr; (void)c; }
void alSynDelete(void* drvr) { (void)drvr; }
float _doModFunc(void* d, s32 count) { (void)d; (void)count; return 0.0f; }
void _init_lpfilter(void* lp) { (void)lp; }

// ---- TODO(port): main-segment data that was never converted to C (lives in the ROM's data section) --------
// reset.c mirrors these as members of a struct; the standalone symbols keep the link happy.
s32 gSoftResetLineNum, gSoftResetRed, gSoftResetGreen, gSoftResetBlue;
unsigned char gGbMemoryMap[0x10000];

// ---- Host replacement for RDRAM: the arena the game's main pool is carved from ---------------------------
unsigned char gPortHeap[96 * 1024 * 1024] __attribute__((aligned(16)));

// ---- Hardware registers (SP/DP/MI/VI/AI/PI/SI): plain memory, zero until written ----------------------------
static unsigned char sHwPage[0x1000000] __attribute__((aligned(16)));
void* port_hw_reg(unsigned long addr) { return &sHwPage[addr & 0xFFFFFC]; }

// 64-bit helpers the MIPS compiler emitted calls to (SDK ll.c).
int64_t __ll_mul(int64_t a, int64_t b) { return a * b; }
u64 __ull_div(u64 a, u64 b) { return b ? a / b : 0; }

unsigned long long Port_ViTick(void);
void Port_TraceState(int state) {
    static int last = -1;
    if (state != last && getenv("PORT_TRACE_STATE")) fprintf(stderr, "[state] %d @frame %llu\n", state, Port_ViTick());
    last = state;
}

void Port_LogF2L(float mf[4][4], void* m) {
    static int on = -1, n = 0;
    if (on < 0) on = getenv("PORT_MTXLOG") != NULL;
    if (!on || n++ > 3000) return;
    fprintf(stderr, "[f2l] %p: r0=%g %g %g %g r1=%g %g %g %g t=%g %g %g %g\n", m, mf[0][0], mf[0][1], mf[0][2], mf[0][3], mf[1][0], mf[1][1], mf[1][2], mf[1][3], mf[3][0], mf[3][1], mf[3][2], mf[3][3]);
}

void Port_LogAlloc(void* p, int n, void* caller) {
    static int on = -1, cnt = 0;
    if (on < 0) on = getenv("PORT_ALLOCLOG") != NULL ? (getenv("PORT_ALLOCLOG")[0] == 'a' ? 2 : 1) : 0;
    if (!on || (on == 1 && n != 0x40) || cnt++ > (on == 2 ? 2000000 : 400)) return;
    if (on == 2) { // all mode: allocations whose low 20 address bits fall in PORT_ALLOCLOG_LO..HI (hex)
        static unsigned long lo = 0, hi = 0;
        if (hi == 0) {
            lo = getenv("PORT_ALLOCLOG_LO") ? strtoul(getenv("PORT_ALLOCLOG_LO"), NULL, 16) : 0;
            hi = getenv("PORT_ALLOCLOG_HI") ? strtoul(getenv("PORT_ALLOCLOG_HI"), NULL, 16) : 0xFFFFF;
        }
        unsigned long a = (unsigned long)(uintptr_t)p & 0xFFFFF;
        if (a + (unsigned long)n <= lo || a > hi) return;
        fprintf(stderr, "[alloc] %p size=0x%x caller=%p slide=0x%lx\n", p, n, caller, (unsigned long)Port_ImageSlide());
        return;
    }
    fprintf(stderr, "[alloc] %p caller=%p slide=0x%lx\n", p, caller, (unsigned long)Port_ImageSlide());
}

void Port_TraceJpeg(const char* what, int a, int b, int c) {
    static int on = -1;
    if (on < 0) on = getenv("PORT_TRACE_JPEG") != NULL;
    if (on) fprintf(stderr, "[jpeg] %s %d %d %d\n", what, a, b, c);
}

#ifdef _WIN32
// BSD byte-string functions (declared in src/port/sdk/port_compat.h), not in the Windows C runtime
void bcopy(const void* src, void* dst, int n) {
    memmove(dst, src, (size_t)n);
}
int bcmp(const void* a, const void* b, int n) {
    return memcmp(a, b, (size_t)n);
}
void bzero(void* p, int n) {
    memset(p, 0, (size_t)n);
}
#endif

