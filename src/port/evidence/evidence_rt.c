// Function-evidence instrumentation (tools/evidence/, cmake/evidence.cmake): only in builds configured with
// -DCMAKE_PROJECT_INCLUDE=cmake/evidence.cmake, which compile the battle engine with -finstrument-functions and add
// this file. Off unless PORT_EVID=<file> is set:
//   PORT_EVID=<file>          aggregate call data, rewritten every ~20 s of game time and at exit:
//                             "F <fn> <parent> <site> <count> <first frame> <ctx>"   per (function, instrumented
//                                 caller on the shadow stack or 0, call site: the return address, which may be in
//                                 code outside the instrumented files; for a function the compiler inlined, the
//                                 caller's return address: use the parent then)
//                             "P <count> <first frame> <fn> <parent> ... "  per instrumented call path (shadow stack,
//                                                                          innermost first, at most kPathDepth)
//   PORT_EVID_SEQ=<file>      the order of calls: "C <frame> <depth> <fn> <site> <ctx>", at most PORT_EVID_SEQ_MAX
//                             (default 40) lines per function, so per-frame functions do not flood it
//   PORT_EVID_WATCH=<off,...> functions (image offsets, from nm) whose entry and exit also record the battle state:
//                             "W <enter|exit> <frame> <fn> <site> user=<hex> target=<hex> st0=<hex> st1=<hex>
//                              scene=<hex> dmg=<n>" (BattleMonRuntime 0x64 bytes, ActivePokemonState 0xC8, BattleScene
//                             0xDC; the same layouts tools/battle_xcheck.py decodes)
//   PORT_EVID_MOVES=<m1,m2,m3,m4>[:<p1,p2,p3,p4>] with PORT_EVID_INJECT_AT=<off>: on each entry of that function
//                             (e.g. BattleScene_HandlePlayerActionInput) the player's active Pokemon gets these moves
//                             (BattleMonRuntime 0x1F..0x22) and, the first time each species is seen, these PP; the
//                             game's time-limit fallback then uses the first move with PP left. Port-only: the
//                             battle is no longer the one ares plays, unless ares gets the same change
// Addresses are written as offsets from the executable's Mach-O header (nm address - 0x100000000); ctx is
// "<phase>/<attacker side>/<user species>:<used move>/<target species>/<damage>" from the battle globals.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include "fragments/battle_engine/battle_engine.h"

#define NOINST __attribute__((no_instrument_function))

uint64_t Port_ViTick(void);

enum { kPathDepth = 10, kStackMax = 512, kSiteSlots = 1 << 15, kPathSlots = 1 << 16, kWatchMax = 64 };

typedef struct {
    uintptr_t fn, parent, site;
    uint64_t count, frame;
    char ctx[48];
} SiteRec;

typedef struct {
    uint64_t hash, count, frame;
    uintptr_t path[kPathDepth];
    int n;
} PathRec;

static int sInit, sOn;
static FILE *sSeq;
static const char* sOutPath;
static uintptr_t sBase;
static SiteRec* sSites;
static PathRec* sPaths;
static int sSeqMax = 40;
static uint64_t sLastDump, sCalls;
static uintptr_t sWatch[kWatchMax];
static int sWatchN;
static uintptr_t sInjectAt;
static int sInjectMoves[4], sInjectPP[4] = { -1, -1, -1, -1 };
static uint8_t sInjectSeen[256];
static _Thread_local uintptr_t tStack[kStackMax];
static _Thread_local int tDepth;
static _Thread_local int tBusy;

// per function, the number of sequence lines written (hash of the function offset)
static uint16_t sSeqCount[1 << 16];

static NOINST void Dump(void);

static NOINST void Init(void) {
    sInit = 1;
    sOutPath = getenv("PORT_EVID");
    if (sOutPath == NULL) return;
    sBase = (uintptr_t)&_mh_execute_header;
    sSites = calloc(kSiteSlots, sizeof(SiteRec));
    sPaths = calloc(kPathSlots, sizeof(PathRec));
    if (getenv("PORT_EVID_SEQ")) sSeq = fopen(getenv("PORT_EVID_SEQ"), "w");
    if (getenv("PORT_EVID_SEQ_MAX")) sSeqMax = atoi(getenv("PORT_EVID_SEQ_MAX"));
    if (getenv("PORT_EVID_WATCH")) {
        const char* p = getenv("PORT_EVID_WATCH");
        while (*p && sWatchN < kWatchMax) {
            char* end;
            unsigned long long v = strtoull(p, &end, 0);
            if (end == p) break;
            sWatch[sWatchN++] = (uintptr_t)v;
            p = *end == ',' ? end + 1 : end;
        }
    }
    if (getenv("PORT_EVID_MOVES") && getenv("PORT_EVID_INJECT_AT")) {
        sInjectAt = (uintptr_t)strtoull(getenv("PORT_EVID_INJECT_AT"), NULL, 0);
        sscanf(getenv("PORT_EVID_MOVES"), "%i,%i,%i,%i:%i,%i,%i,%i", &sInjectMoves[0], &sInjectMoves[1],
               &sInjectMoves[2], &sInjectMoves[3], &sInjectPP[0], &sInjectPP[1], &sInjectPP[2], &sInjectPP[3]);
    }
    atexit(Dump);
    sOn = 1;
}

static NOINST void Ctx(char* out, size_t n) {
    BattleScene* sc = sBattleScene.unk_00;
    snprintf(out, n, "%d/%d/%d:%d/%d/%u", sc ? sc->unk_1C : -1, sc ? (int)sc->unk_2C : -1,
             gBattleUser ? gBattleUser->id : -1, gBattleUser ? gBattleUser->usedMove : -1,
             gBattleTarget ? gBattleTarget->id : -1, (unsigned)gBattleDamage);
}

static NOINST void Hex(FILE* f, const char* key, const void* p, size_t n) {
    static const char d[] = "0123456789abcdef";
    const uint8_t* b = p;
    fprintf(f, " %s=", key);
    if (p == NULL) return;
    for (size_t i = 0; i < n; i++) {
        fputc(d[b[i] >> 4], f);
        fputc(d[b[i] & 15], f);
    }
}

static NOINST void Watch(const char* what, uintptr_t fn, uintptr_t site) {
    if (sSeq == NULL) return;
    fprintf(sSeq, "W %s %llu %lx %lx", what, (unsigned long long)Port_ViTick(), (unsigned long)fn,
            (unsigned long)site);
    Hex(sSeq, "user", gBattleUser, 0x64);
    Hex(sSeq, "target", gBattleTarget, 0x64);
    Hex(sSeq, "st0", sActivePokemon[0] ? (void*)&sActivePokemon[0]->state : NULL, 0xC8);
    Hex(sSeq, "st1", sActivePokemon[1] ? (void*)&sActivePokemon[1]->state : NULL, 0xC8);
    Hex(sSeq, "scene", sBattleScene.unk_00, 0xDC);
    fprintf(sSeq, " dmg=%u\n", (unsigned)gBattleDamage);
}

static NOINST int IsWatched(uintptr_t fn) {
    for (int i = 0; i < sWatchN; i++) {
        if (sWatch[i] == fn) return 1;
    }
    return 0;
}

static NOINST void Inject(void) {
    if (sActivePokemon[0] == NULL) return;
    uint8_t* rt = (uint8_t*)&sActivePokemon[0]->state.runtime; // pointer-free: the N64 layout
    uint8_t species = rt[0x0B];
    for (int i = 0; i < 4; i++) rt[0x1F + i] = (uint8_t)sInjectMoves[i];
    if (!sInjectSeen[species]) {
        sInjectSeen[species] = 1;
        for (int i = 0; i < 4; i++) {
            if (sInjectPP[i] >= 0) rt[0x32 + i] = (uint8_t)((rt[0x32 + i] & 0xC0) | (sInjectPP[i] & 0x3F));
        }
        if (sSeq) fprintf(sSeq, "I %llu species=%d moves=%d,%d,%d,%d\n", (unsigned long long)Port_ViTick(), species,
                          rt[0x1F], rt[0x20], rt[0x21], rt[0x22]);
    }
}

static NOINST void Dump(void) {
    if (!sOn) return;
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", sOutPath);
    FILE* f = fopen(tmp, "w");
    if (f == NULL) return;
    fprintf(f, "# evidence v1 calls=%llu frame=%llu\n", (unsigned long long)sCalls, (unsigned long long)Port_ViTick());
    for (int i = 0; i < kSiteSlots; i++) {
        SiteRec* s = &sSites[i];
        if (s->count) {
            fprintf(f, "F %lx %lx %lx %llu %llu %s\n", (unsigned long)s->fn, (unsigned long)s->parent,
                    (unsigned long)s->site,
                    (unsigned long long)s->count, (unsigned long long)s->frame, s->ctx);
        }
    }
    for (int i = 0; i < kPathSlots; i++) {
        PathRec* p = &sPaths[i];
        if (p->count) {
            fprintf(f, "P %llu %llu", (unsigned long long)p->count, (unsigned long long)p->frame);
            for (int k = 0; k < p->n; k++) fprintf(f, " %lx", (unsigned long)p->path[k]);
            fputc('\n', f);
        }
    }
    fclose(f);
    rename(tmp, sOutPath);
    if (sSeq) fflush(sSeq);
}

static NOINST uint64_t Mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0xBF58476D1CE4E5B9ull;
}

void NOINST __cyg_profile_func_enter(void* this_fn, void* call_site) {
    if (!sInit) Init();
    if (tDepth < kStackMax) tStack[tDepth] = (uintptr_t)this_fn;
    tDepth++;
    if (!sOn || tBusy) return;
    tBusy = 1;
    uintptr_t fn = (uintptr_t)this_fn - sBase, site = (uintptr_t)call_site - sBase;
    uintptr_t parent = tDepth >= 2 && tDepth - 2 < kStackMax ? tStack[tDepth - 2] - sBase : 0;
    uint64_t frame = Port_ViTick();
    sCalls++;

    // per (function, instrumented caller, call site)
    uint64_t h = Mix(Mix(Mix(0, fn), parent), site);
    for (uint32_t i = (uint32_t)h & (kSiteSlots - 1), k = 0; k < kSiteSlots; k++, i = (i + 1) & (kSiteSlots - 1)) {
        SiteRec* s = &sSites[i];
        if (s->count == 0) {
            s->fn = fn;
            s->parent = parent;
            s->site = site;
            s->frame = frame;
            Ctx(s->ctx, sizeof s->ctx);
        } else if (s->fn != fn || s->parent != parent || s->site != site) {
            continue;
        }
        s->count++;
        break;
    }

    // per call path through the instrumented functions (innermost first)
    int n = tDepth < kPathDepth ? tDepth : kPathDepth;
    int top = (tDepth < kStackMax ? tDepth : kStackMax) - 1;
    uint64_t ph = 0;
    for (int k = 0; k < n && top - k >= 0; k++) ph = Mix(ph, tStack[top - k]);
    ph |= 1;
    for (uint32_t i = (uint32_t)ph & (kPathSlots - 1), k = 0; k < kPathSlots; k++, i = (i + 1) & (kPathSlots - 1)) {
        PathRec* p = &sPaths[i];
        if (p->count == 0) {
            p->hash = ph;
            p->frame = frame;
            p->n = 0;
            for (int j = 0; j < n && top - j >= 0; j++) p->path[p->n++] = tStack[top - j] - sBase;
        } else if (p->hash != ph) {
            continue;
        }
        p->count++;
        break;
    }

    if (sSeq) {
        uint16_t* c = &sSeqCount[(uint32_t)Mix(0, fn) & 0xFFFF];
        if (*c < sSeqMax) {
            char ctx[48];
            Ctx(ctx, sizeof ctx);
            fprintf(sSeq, "C %llu %d %lx %lx %s\n", (unsigned long long)frame, tDepth, (unsigned long)fn,
                    (unsigned long)site, ctx);
            (*c)++;
        }
        if (sWatchN && IsWatched(fn)) Watch("enter", fn, site);
    }
    if (sInjectAt && fn == sInjectAt) Inject();
    if (frame >= sLastDump + 1200) {
        sLastDump = frame;
        Dump();
    }
    tBusy = 0;
}

void NOINST __cyg_profile_func_exit(void* this_fn, void* call_site) {
    if (tDepth > 0) tDepth--;
    if (!sOn || tBusy || sWatchN == 0) return;
    uintptr_t fn = (uintptr_t)this_fn - sBase;
    if (IsWatched(fn)) {
        tBusy = 1;
        Watch("exit", fn, (uintptr_t)call_site - sBase);
        tBusy = 0;
    }
}
