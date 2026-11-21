// Translation of raw N64 display lists (as found in ROM-loaded assets) into host Gfx.
//
// Assets keep their original format: 8-byte big-endian F3DEX2 commands, 32-bit addresses (segment-relative
// 0x0Nxxxxxx or fragment link addresses 0x8Nxxxxxx), big-endian Vtx/Mtx. Fast3D consumes 16-byte host Gfx with
// real pointers. Other ports convert at asset-extraction time (Torch / ZAPD for the O2R format); here the same
// conversion happens the first time a list is drawn, and results are cached until the segment or fragment maps
// change (which is when raw pointers can start meaning something else).
#include <ultra64.h>
#include <port_compat.h>

#include <cstdint>
#include "port_platform.h"
#include "interp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "StadionPort.h"

extern "C" {
struct PortMemoryMap {
    uintptr_t vaddr;
    size_t size;
};
extern PortMemoryMap gSegments[16];
extern PortMemoryMap gFragments[240];
uintptr_t Memmap_GetSegmentVaddr(uintptr_t mask);
uintptr_t Memmap_GetFragmentVaddr(void* addr);
}

namespace {

uintptr_t gListSegs[16]; // see TranslateList: segment bases set inside the lists being translated
std::mutex gLock;
std::unordered_map<const void*, Gfx*> gCache;
std::unordered_map<const void*, void*> gDescCache; // Port_ConvertDesc
// Translated lists, vertices and matrices. When the cache is dropped the arena is retired, not freed: lists built
// from it may still be queued for the renderer, so it is freed once a few frames have been rendered since
// (Port_GfxFrameDone). Without this every flush kept a full copy of the scene (gigabytes within minutes in battle).
std::deque<std::vector<uint8_t>> gArena;
size_t gArenaBytes = 0;
uint64_t gFramesDone = 0;
std::deque<std::pair<uint64_t, std::deque<std::vector<uint8_t>>>> gRetired; // frame count when retired, arena
constexpr uint64_t kRetireFrames = 4;           // rendered frames before a retired arena is freed
constexpr size_t kArenaLimit = 32u << 20;       // vertices and matrices are translated on every use: retire when larger

void RetireArena() {
    gCache.clear();
    gDescCache.clear();
    if (gArena.empty()) return;
    gRetired.emplace_back(gFramesDone, std::move(gArena));
    gArena.clear();
    gArenaBytes = 0;
}

inline uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint16_t Be16(const uint8_t* p) {
    return (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
}

// debug switches, read once: getenv scans the whole environment, and these are checked per command and per matrix
bool ValidateDL() {
    static const bool v = getenv("PORT_VALIDATE_DL") != nullptr;
    return v;
}
bool MtxLog() {
    static const bool v = getenv("PORT_MTXLOG") != nullptr;
    return v;
}

bool InRaw(const void* p) {
    uintptr_t a = (uintptr_t)p;
    if (Port_VIsRaw(p)) return true;
    // segment 0 is the identity map of RDRAM (0x80000000..): game-owned memory, not ROM-format data
    for (int i = 1; i < 16; i++) {
        if (gSegments[i].vaddr < 0x100000000ull || a < gSegments[i].vaddr) continue; // vaddr may hold a non-host (N64) value
        size_t size = Port_PoolBlockSize((void*)gSegments[i].vaddr);
        if (size == 0) size = gSegments[i].size;
        if (size > gSegments[i].size) size = gSegments[i].size;
        if (a < gSegments[i].vaddr + size) {
            static int shown = 0;
            if (ValidateDL() && shown++ < 6) fprintf(stderr, "[raw] %p in segment %d base=%p size=0x%zx (map size 0x%zx)\n", p, i, (void*)gSegments[i].vaddr, size, gSegments[i].size);
            return true;
        }
    }
    for (int i = 0; i < 240; i++) {
        if (gFragments[i].vaddr != 0 && a >= gFragments[i].vaddr && a < gFragments[i].vaddr + gFragments[i].size) {
            static int shownF = 0;
            if (ValidateDL() && shownF++ < 6) fprintf(stderr, "[raw] %p in fragment slot %d base=%p size=0x%zx\n", p, i, (void*)gFragments[i].vaddr, gFragments[i].size);
            return true;
        }
    }
    return false;
}

// Resolves an address found in raw data: N64 segment/fragment addresses become host pointers. Returns nullptr when
// the address names a segment that is only known to the GPU at run time (e.g. 0x0E dynamic vertex buffers); the
// caller then keeps the numeric value and Fast3D resolves it through its own segment table.
const void* ResolveRaw0(uint32_t addr, bool* resolved);
const void* ResolveRaw(uint32_t addr, bool* resolved) {
    const void* r = ResolveRaw0(addr, resolved);
    if (*resolved && (uintptr_t)r < 0x100000000ull) { // not a host pointer: treat as unresolved
        static int shown = 0;
        if (shown++ < 5) fprintf(stderr, "[resolve] 0x%x -> %p rejected\n", addr, r);
        *resolved = false;
        return nullptr;
    }
    return r;
}
const void* ResolveRaw0(uint32_t addr, bool* resolved) {
    *resolved = false;
    if (addr == 0) return nullptr;
    if ((addr >> 28) == 0) {
        if (((addr >> 24) & 0xF) == 0) return nullptr;
        uintptr_t base = gListSegs[(addr >> 24) & 0xF];
        if (base == 0) base = gSegments[(addr >> 24) & 0xF].vaddr;
        if (base < 0x100000000ull) return nullptr; // not a host pointer (unset or N64 address)
        *resolved = true;
        return (const void*)(base + (addr & 0xFFFFFF));
    }
    if (void* v = Port_VResolve(addr)) {
        *resolved = true;
        return v;
    }
    if (addr >= 0x81000000 && addr < 0x90000000) {
        uintptr_t r = Memmap_GetFragmentVaddr((void*)(uintptr_t)addr);
        if (r != addr) {
            *resolved = true;
            return (const void*)r;
        }
    }
    return nullptr;
}

uint8_t* Alloc(size_t n) {
    gArena.emplace_back(n + 16);
    gArenaBytes += n + 16;
    uint8_t* p = gArena.back().data();
    return (uint8_t*)(((uintptr_t)p + 15) & ~(uintptr_t)15);
}

// Segment bases set by G_MOVEWORD(G_MW_SEGMENT) inside the lists being translated. Raw model lists point their own
// data segment at a table in the blob and then address it as 0x06xxxxxx; the game's global segment table does not
// know about that, so translation tracks it (inherited by nested lists, as on the RSP).
Gfx* TranslateList(const uint8_t* raw);

// Vertex arrays: 16 bytes each, big-endian s16 fields.
Vtx* TranslateVtx(const uint8_t* raw, int n) {
    Vtx* out = (Vtx*)Alloc((size_t)n * sizeof(Vtx));
    for (int i = 0; i < n; i++) {
        const uint8_t* s = raw + i * 16;
        int16_t* d = (int16_t*)&out[i];
        for (int k = 0; k < 6; k++) d[k] = (int16_t)Be16(s + k * 2);   // ob[3], flag, tc[2]
        memcpy((uint8_t*)&out[i] + 12, s + 12, 4);                     // color/normal bytes
    }
    return out;
}

// Fixed-point matrix: 8 words of packed integer parts then 8 words of packed fractions. Fast3D reads them as 32-bit
// words (each holding two 16-bit fields), so the swap is per word, not per halfword.
void* TranslateMtx(const uint8_t* raw) {
    uint32_t* out = (uint32_t*)Alloc(64);
    for (int i = 0; i < 16; i++) out[i] = Be32(raw + i * 4);
    if (MtxLog()) fprintf(stderr, "[mtxtr] raw=%p -> %p\n", (const void*)raw, (void*)out);
    return out;
}

Gfx* TranslateList(const uint8_t* raw) {
    auto it = gCache.find(raw);
    if (it != gCache.end()) return it->second;

    std::vector<Gfx> out;
    out.reserve(64);
    const uint8_t* p = raw;
    bool ended = false;
    for (int count = 0; count < (1 << 16) && !ended; count++, p += 8) {
        uint32_t w0 = Be32(p), w1 = Be32(p + 4);
        uint8_t op = w0 >> 24;
        Gfx g;
        memset(&g, 0, sizeof g);
        g.words.w0 = w0;
        g.words.w1 = w1;
        bool resolved;
        switch (op) {
            case 0xDF: // G_ENDDL
                ended = true;
                break;
            case 0xDE: { // G_DL
                if (w1 == 0) { // branch to NULL: nothing to run
                    if (((w0 >> 16) & 0xFF) == 1) { g.words.w0 = 0xDF000000u; ended = true; }
                    else { g.words.w0 = 0; g.words.w1 = 0; }
                    break;
                }
                const void* t = ResolveRaw(w1, &resolved);
                if (resolved && InRaw(t)) {
                    g.words.w1 = (uintptr_t)TranslateList((const uint8_t*)t);
                } else if (resolved) {
                    g.words.w1 = (uintptr_t)t;
                }
                if (((w0 >> 16) & 0xFF) == 1) ended = true; // branch, no return
                break;
            }
            case 0xE1: { // G_RDPHALF_1: the branch-on-z target when followed by G_BRANCH_Z, otherwise texrect data
                if (p[8] == 0x04) {
                    const void* t = ResolveRaw(w1, &resolved);
                    if (resolved && InRaw(t)) g.words.w1 = (uintptr_t)TranslateList((const uint8_t*)t);
                    else if (resolved) g.words.w1 = (uintptr_t)t;
                }
                break;
            }
            case 0x01: { // G_VTX
                int n = (w0 >> 12) & 0xFF;
                const void* t = ResolveRaw(w1, &resolved);
                if (resolved && InRaw(t)) g.words.w1 = (uintptr_t)TranslateVtx((const uint8_t*)t, n);
                else if (resolved) g.words.w1 = (uintptr_t)t;
                break;
            }
            case 0xDA: { // G_MTX
                const void* t = ResolveRaw(w1, &resolved);
                if (resolved && InRaw(t)) g.words.w1 = (uintptr_t)TranslateMtx((const uint8_t*)t);
                else if (resolved) g.words.w1 = (uintptr_t)t;
                break;
            }
            case 0xDC: { // G_MOVEMEM: viewport has s16 fields, lights are bytes
                const void* t = ResolveRaw(w1, &resolved);
                if (resolved) {
                    if (InRaw(t) && (w0 & 0xFF) == 8) { // G_MV_VIEWPORT
                        int16_t* vp = (int16_t*)Alloc(16);
                        for (int k = 0; k < 8; k++) vp[k] = (int16_t)Be16((const uint8_t*)t + k * 2);
                        g.words.w1 = (uintptr_t)vp;
                    } else {
                        g.words.w1 = (uintptr_t)t;
                    }
                }
                break;
            }
            case 0xDB: { // G_MOVEWORD: segment base registers
                if (((w0 >> 16) & 0xFF) == 6) { // G_MW_SEGMENT
                    int seg = (w0 & 0xFFFF) / 4;
                    const void* t = ResolveRaw(w1, &resolved);
                    if (seg < 16) {
                        if (resolved) {
                            gListSegs[seg] = (uintptr_t)t;
                            g.words.w1 = (uintptr_t)t;
                        } else if (w1 >= 0x100000000ull || w1 == 0) {
                            gListSegs[seg] = (uintptr_t)w1;
                        }
                    }
                }
                break;
            }
            case 0xFD: case 0xFE: case 0xFF: { // G_SETTIMG / ZIMG / CIMG
                const void* t = ResolveRaw(w1, &resolved);
                if (resolved) g.words.w1 = (uintptr_t)t;
                break;
            }
            default:
                break;
        }
        if (ValidateDL() && op > 0x08 && op < 0xD7) {
            static int shown = 0;
            if (shown++ < 4) {
                fprintf(stderr, "[xlat] invalid opcode %02x at raw %p (cmd #%d) list start %p:", op, (const void*)p, count, (const void*)raw);
                for (int k = 0; k < 48; k++) fprintf(stderr, "%s%02x", (k & 3) ? "" : " ", raw[k]);
                fputc('\n', stderr);
            }
        }
        out.push_back(g);
    }
    if (!ended) {
        Gfx e; memset(&e, 0, sizeof e);
        e.words.w0 = 0xDF000000u;
        out.push_back(e);
    }
    Gfx* dst = (Gfx*)Alloc(out.size() * sizeof(Gfx));
    memcpy(dst, out.data(), out.size() * sizeof(Gfx));
    gCache[raw] = dst;
    return dst;
}

} // namespace

extern "C" {

// Called wherever game code writes a display-list pointer (G_DL / branch).
void* Port_GfxPrepareDL(uintptr_t dl) {
    if ((dl >> 32) == 0xFFFFFFFFull) dl &= 0xFFFFFFFFull; // an s32 address sign-extended into 64 bits
    if (dl == 0) {
        static int shown = 0;
        if (ValidateDL() && shown++ < 3) {
            void* fr[10];
            int n = Port_Backtrace(fr, 10);
            fprintf(stderr, "@null-dl slide=0x%lx:", (unsigned long)Port_ImageSlide());
            for (int i = 1; i < n; i++) fprintf(stderr, " %p", fr[i]);
            fputc('\n', stderr);
        }
        return nullptr;
    }
    std::lock_guard<std::mutex> g(gLock);
    bool resolved;
    const void* p = (const void*)dl;
    if (dl < 0x100000000ull) {
        p = ResolveRaw((uint32_t)dl, &resolved);
        if (!resolved) return (void*)dl; // runtime segment: leave for Fast3D
    }
    if (InRaw(p)) {
        memset(gListSegs, 0, sizeof gListSegs);
        if (ValidateDL()) {
            uint8_t op0 = *(const uint8_t*)p;
            static int shownT = 0;
            if (op0 > 0x08 && op0 < 0xD7 && shownT++ < 3) {
                void* fr[10];
                int n = Port_Backtrace(fr, 10);
                fprintf(stderr, "@bad-dl raw=%p link=0x%lx slide=0x%lx:", p, (unsigned long)dl, (unsigned long)Port_ImageSlide());
                for (int i = 1; i < n; i++) fprintf(stderr, " %p", fr[i]);
                fputc('\n', stderr);
            }
        }
        return TranslateList((const uint8_t*)p);
    }
    return (void*)p;
}

void* Port_GfxPrepareVtx(void* v, int n) {
    if (((uintptr_t)v >> 32) == 0xFFFFFFFFull) v = (void*)((uintptr_t)v & 0xFFFFFFFFull);
    if (v == nullptr) return nullptr;
    std::lock_guard<std::mutex> g(gLock);
    uintptr_t a = (uintptr_t)v;
    bool resolved;
    const void* p = v;
    if (a < 0x100000000ull) {
        p = ResolveRaw((uint32_t)a, &resolved);
        if (!resolved) return v;
    }
    if (InRaw(p)) return TranslateVtx((const uint8_t*)p, n);
    return (void*)p;
}

void* Port_GfxPrepareMtx(void* m) {
    if (((uintptr_t)m >> 32) == 0xFFFFFFFFull) m = (void*)((uintptr_t)m & 0xFFFFFFFFull);
    if (m == nullptr) return nullptr;
    void* site = __builtin_return_address(0); // the gSPMatrix in game code (frame interpolation ids, interp.cpp)
    std::lock_guard<std::mutex> g(gLock);
    uintptr_t a = (uintptr_t)m;
    bool resolved;
    const void* p = m;
    if (a < 0x100000000ull) {
        p = ResolveRaw((uint32_t)a, &resolved);
        if (!resolved) return m;
    }
    if (InRaw(p)) return TranslateMtx((const uint8_t*)p); // ROM data: the same every frame
    Port_InterpTagMtx(p, site);
    return (void*)p;
}

// Texture / palette descriptors in model data: {u8 fmt, u8 size, s16 count, 4 bytes, u32 data address, ...}. The host
// copy keeps the first 8 bytes (s16 swapped) and widens the address at +8 to a pointer-sized field holding the
// 32-bit link address (Fast3D and the display-list wrappers resolve link addresses themselves).
void* Port_ConvertDesc(void* link) {
    if (link == nullptr) return nullptr;
    std::lock_guard<std::mutex> g(gLock);
    uintptr_t a = (uintptr_t)link;
    bool resolved;
    const void* p = link;
    if (a < 0x100000000ull) {
        p = ResolveRaw((uint32_t)a, &resolved);
        if (!resolved) return link;
    }
    if (!InRaw(p)) return (void*)p;
    // PORT: dropped with the other translations (Port_GfxFlushCache): a blob reloaded at the same address holds other
    // pointers (the Gallery's 2nd and later photos drew their stage textures from the previous photo's slot)
    auto it = gDescCache.find(p);
    if (it != gDescCache.end()) return it->second;
    const uint8_t* s = (const uint8_t*)p;
    if (ValidateDL()) {
        static int shownD = 0;
        if (shownD++ < 8) {
            fprintf(stderr, "[desc] link=%p raw=%p:", link, p);
            for (int k = 0; k < 24; k++) fprintf(stderr, "%s%02x", (k & 3) ? "" : " ", s[k]);
            fputc('\n', stderr);
        }
    }
    uint8_t* d = Alloc(16);
    memset(d, 0, 16);
    d[0] = s[0];
    d[1] = s[1];
    *(int16_t*)(d + 2) = (int16_t)Be16(s + 2);
    memcpy(d + 4, s + 4, 4);
    *(uintptr_t*)(d + 8) = Be32(s + 8);
    gDescCache[p] = d;
    return d;
}

// Element idx of an array of texture descriptors. In ROM data the elements are 12 bytes apart; the host struct is 16.
void* Port_DescAt(void* base, int idx, size_t hostStride) {
    if (base == nullptr) return nullptr;
    std::unique_lock<std::mutex> g(gLock);
    uintptr_t a = (uintptr_t)base;
    bool resolved;
    const void* p = base;
    if (a < 0x100000000ull) {
        p = ResolveRaw((uint32_t)a, &resolved);
        if (!resolved) return base;
    }
    bool raw = InRaw(p);
    g.unlock();
    if (!raw) return (uint8_t*)p + (size_t)idx * hostStride;
    return Port_ConvertDesc((void*)((const uint8_t*)p + (size_t)idx * 12));
}

int Port_IsRawData(const void* p) {
    return InRaw(p) ? 1 : 0;
}

void Port_GfxFlushCache(void) {
    Port_InterpMapsChanged();
    std::lock_guard<std::mutex> g(gLock);
    RetireArena();
}

// A frame was rendered (or only tracked): frees arenas retired long enough ago that no queued list uses them.
void Port_GfxFrameDone(void) {
    std::lock_guard<std::mutex> g(gLock);
    gFramesDone++;
    while (!gRetired.empty() && gRetired.front().first + kRetireFrames <= gFramesDone) gRetired.pop_front();
    if (gArenaBytes > kArenaLimit) RetireArena();
}

} // extern "C"
