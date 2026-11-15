// Function-evidence instrumentation (tools/evidence/, cmake/evidence.cmake): only in builds configured with
// -DCMAKE_PROJECT_INCLUDE=cmake/evidence.cmake, which compile the battle engine with -finstrument-functions and add
// this file. Off unless PORT_EVID=<file> is set:
//   PORT_EVID=<file>          aggregate call data, rewritten every PORT_EVID_DUMP_EVERY frames (300) and at exit:
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
//   PORT_EVID_SET=<fn>[#<n>][@exit]:<addr>:<size>[|]=<value>;...  ("|=": OR the value in)   at entry of function <fn> (image
//                             offset; only its n-th entry, counted from 1, with #n; with @exit when that call returns), write <value> (size 1, 2 or 4, host byte order) at
//                             <addr>: an image offset followed by steps "+<off>" (add) and "*" (load the pointer
//                             there), e.g. the offset of sBattleUser, "*", "+0x5A" for sBattleUser->usedMove.
//                             tools/evidence/run.py --set translates symbol names. A forced condition: the game
//                             state is no longer one the game produced itself, and records say so
//   PORT_EVID_CALL=<fn>[#<n>][@exit]:<target>:<ret>:<args>;...  at entry of <fn> (n-th entry; with @exit when that call
//                             returns), call <target> (image offset)
//                             directly: for code nothing in the shipped game calls. <ret> v, i (integer) or f
//                             (float); <args>: up to 6 of i<int> (integer), f<float>, p<off> (pointer into a 256-byte
//                             scratch buffer, filled from PORT_EVID_CALL_BUF=<hex> before each call); up to 6 integer
//                             and 4 float arguments in any order (arm64 registers per kind). With
//                             PORT_EVID_CALL_FORK=1 each call runs in a forked child (crash- and hang-proof). Logged to the
//                             sequence file: "K <frame> <target> ret=<value> buf0=<hex> buf1=<hex>"
//   LLVM_PROFILE_FILE=<file>  with PORT_EVID_COVERAGE (cmake/evidence.cmake): the branch/line counters, written with
//                             the call data (the port ends runs with _exit, so the profile runtime's own exit
//                             hook does not run)
// Addresses are written as offsets from the executable's Mach-O header (nm address - 0x100000000); ctx is
// "<phase>/<attacker side>/<user species>:<used move>/<target species>/<damage>" from the battle globals.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/wait.h>
#endif
#include "fragments/battle_engine/battle_engine.h"
#include "../StadionPort.h"

#define NOINST __attribute__((no_instrument_function))

static NOINST void Hex(FILE* f, const char* key, const void* p, size_t n);
int __llvm_profile_write_file(void) __attribute__((weak));
void __llvm_profile_set_filename(const char*) __attribute__((weak));

enum { kPathDepth = 10, kStackMax = 512, kSiteSlots = 1 << 18, kPathSlots = 1 << 18, kProbe = 512, kWatchMax = 64 };

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
static uint64_t sLastDump, sCalls, sDumpEvery = 300;
static uintptr_t sWatch[kWatchMax];
static int sWatchN;
static uintptr_t sInjectAt;
static int sInjectMoves[4], sInjectPP[4] = { -1, -1, -1, -1 };
static uint8_t sInjectSeen[256];

enum { kSetMax = 32, kCallMax = 2048, kStepMax = 8 };
typedef struct {
    uintptr_t fn, base;
    int nth, seen, size, nsteps, orIn, atExit, armedDepth;
    long steps[kStepMax]; // >= 0: add; -1: load pointer
    uint32_t value;
} SetRec;
static SetRec sSets[kSetMax];
static int sSetN;

typedef struct {
    uintptr_t at, target;
    int nth, seen, nargs, atExit, armedDepth;
    char ret, kind[6];
    double val[6];
} CallRec;
static CallRec sCallRecs[kCallMax];
static _Thread_local int tDepth; // instrumented call depth of this thread
static int sCallFork;
static int sCallN;
static uint8_t sCallBuf[256], sCallBufInit[256];
static size_t sCallBufLen;

static NOINST void ParseCalls(const char* p) {
    while (*p && sCallN < kCallMax) {
        CallRec* r = &sCallRecs[sCallN];
        char* end;
        memset(r, 0, sizeof *r);
        r->at = (uintptr_t)strtoull(p, &end, 0);
        p = end;
        if (*p == '#') {
            r->nth = (int)strtol(p + 1, &end, 0);
            p = end;
        }
        if (strncmp(p, "@exit", 5) == 0) {
            r->atExit = 1;
            p += 5;
        }
        if (*p++ != ':') return;
        r->target = (uintptr_t)strtoull(p, &end, 0);
        p = end;
        if (*p++ != ':') return;
        r->ret = *p++;
        if (*p++ != ':') return;
        while (*p && *p != ';' && r->nargs < 6) {
            r->kind[r->nargs] = *p++;
            r->val[r->nargs++] = strtod(p, &end);
            p = end;
            if (*p == ',') p++;
        }
        sCallN++;
        if (*p == ';') p++;
    }
}

// arm64 passes integer and float arguments in separate registers (x0..x7, s0..s7) in order within each kind, so one
// call shape with six integers and four floats fits any prototype with at most that many of each
typedef intptr_t (*FnMix)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, float, float, float, float);
typedef float (*FnMixF)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, float, float, float, float);

// exit: as ApplySets (an @exit record is armed at the entry it counts and runs when that call returns)
static NOINST void DoCalls(uintptr_t fn, int exit) {
    for (int i = 0; i < sCallN; i++) {
        CallRec* r = &sCallRecs[i];
        if (r->at != fn) continue;
        if (!exit) {
            r->seen++;
            if (r->nth && r->seen != r->nth) continue;
            if (r->atExit) {
                r->armedDepth = tDepth;
                continue;
            }
        } else if (!r->atExit || r->armedDepth != tDepth + 1) {
            continue;
        } else {
            r->armedDepth = 0;
        }
        memcpy(sCallBuf, sCallBufInit, sizeof sCallBuf);
        intptr_t a[6] = { 0 };
        float f[4] = { 0 };
        int floats = 0;
        int ints = 0;
        for (int k = 0; k < r->nargs; k++) {
            if (r->kind[k] == 'f') {
                if (floats < 4) f[floats++] = (float)r->val[k];
            } else if (ints < 6) {
                a[ints++] = r->kind[k] == 'p' ? (intptr_t)(sCallBuf + (intptr_t)r->val[k]) : (intptr_t)r->val[k];
            }
        }
        void* t = (void*)(sBase + r->target);
        char ret[64] = "-";
#if defined(_WIN32)
        if (sCallFork || floats) {
            if (sSeq) fprintf(sSeq, "K %llu %lx unsupported=windows_fork_or_float_arguments\n",
                              (unsigned long long)Port_ViTick(), (unsigned long)r->target);
            continue;
        }
#else
        pid_t child = 0;
        if (sCallFork) {
            // PORT_EVID_CALL_FORK: the call runs in a forked child (2 s alarm), so a crash or a hang costs only that
            // call; the child writes its coverage counters to <LLVM_PROFILE_FILE>.k<i> and its result line
            if (sSeq) fflush(sSeq);
            child = fork();
            if (child > 0) {
                int st = 0;
                waitpid(child, &st, 0);
                if (sSeq && !(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
                    fprintf(sSeq, "K %llu %lx failed=%s%d\n", (unsigned long long)Port_ViTick(), (unsigned long)r->target,
                            WIFSIGNALED(st) ? "signal" : "exit", WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
                    fflush(sSeq);
                }
                continue;
            }
            alarm(2);
        }
#endif
        if (r->ret == 'f') {
            snprintf(ret, sizeof ret, "%g", (double)((FnMixF)t)(a[0], a[1], a[2], a[3], a[4], a[5], f[0], f[1], f[2], f[3]));
        } else {
            intptr_t v = ((FnMix)t)(a[0], a[1], a[2], a[3], a[4], a[5], f[0], f[1], f[2], f[3]);
            if (r->ret == 'i') snprintf(ret, sizeof ret, "%lld", (long long)v);
        }
        if (sSeq) {
            fprintf(sSeq, "K %llu %lx ret=%s", (unsigned long long)Port_ViTick(), (unsigned long)r->target, ret);
            Hex(sSeq, "buf0", sCallBufInit, sCallBufLen);
            Hex(sSeq, "buf1", sCallBuf, sCallBufLen);
            fputc('\n', sSeq);
        }
#if !defined(_WIN32)
        if (sCallFork && child == 0) {
            char path[1100];
            if (sSeq) fflush(sSeq);
            if (__llvm_profile_set_filename && getenv("LLVM_PROFILE_FILE")) {
                snprintf(path, sizeof path, "%s.k%d", getenv("LLVM_PROFILE_FILE"), i);
                __llvm_profile_set_filename(path);
                __llvm_profile_write_file();
            }
            _exit(0);
        }
#endif
    }
}

static NOINST void ParseSets(const char* p) {
    while (*p && sSetN < kSetMax) {
        SetRec* r = &sSets[sSetN];
        char* end;
        memset(r, 0, sizeof *r);
        r->fn = (uintptr_t)strtoull(p, &end, 0);
        p = end;
        if (*p == '#') {
            r->nth = (int)strtol(p + 1, &end, 0);
            p = end;
        }
        if (strncmp(p, "@exit", 5) == 0) {
            r->atExit = 1;
            p += 5;
        }
        if (*p++ != ':') return;
        r->base = (uintptr_t)strtoull(p, &end, 0);
        p = end;
        while (*p == '+' || *p == '*') {
            if (r->nsteps >= kStepMax) return;
            if (*p == '*') {
                r->steps[r->nsteps++] = -1;
                p++;
            } else {
                r->steps[r->nsteps++] = strtol(p + 1, &end, 0);
                p = end;
            }
        }
        if (*p++ != ':') return;
        r->size = (int)strtol(p, &end, 0);
        p = end;
        if (*p == '|') {
            r->orIn = 1;
            p++;
        }
        if (*p++ != '=') return;
        r->value = (uint32_t)strtoul(p, &end, 0);
        p = end;
        sSetN++;
        if (*p == ';') p++;
    }
}
static _Thread_local uintptr_t tStack[kStackMax];
static _Thread_local int tBusy;

// per function, the number of sequence lines written (hash of the function offset)
static uint16_t sSeqCount[1 << 16];

static NOINST void Dump(void);

// PORT_EVID_BACKGROUND=1: the game runs as a macOS accessory app (no Dock icon, does not take the focus from the app
// in use), set on the main thread; with PORT_HIDDEN_WINDOW=1 (Engine.cpp) a run has nothing on screen. SDL's own
// SDL_MAC_BACKGROUND_APP hint only applies when SDL creates the application object, which the port does earlier
#if defined(__APPLE__)
static NOINST void Background(void* unused) {
    (void)unused;
    id app = ((id(*)(Class, SEL))objc_msgSend)(objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (app) ((void (*)(id, SEL, long))objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 1);
}

#endif

static NOINST void Init(void) {
    sInit = 1;
    sOutPath = getenv("PORT_EVID");
    if (sOutPath == NULL) return;
#if defined(__APPLE__)
    if (getenv("PORT_EVID_BACKGROUND")) dispatch_async_f(dispatch_get_main_queue(), NULL, Background);
    sBase = (uintptr_t)&_mh_execute_header;
#elif defined(_WIN32)
    sBase = (uintptr_t)GetModuleHandleA(NULL);
#else
    extern char __executable_start[];
    sBase = (uintptr_t)__executable_start;
#endif
    sSites = calloc(kSiteSlots, sizeof(SiteRec));
    sPaths = calloc(kPathSlots, sizeof(PathRec));
    if (getenv("PORT_EVID_SEQ")) sSeq = fopen(getenv("PORT_EVID_SEQ"), "w");
    if (getenv("PORT_EVID_SEQ_MAX")) sSeqMax = atoi(getenv("PORT_EVID_SEQ_MAX"));
    if (getenv("PORT_EVID_DUMP_EVERY")) sDumpEvery = strtoull(getenv("PORT_EVID_DUMP_EVERY"), NULL, 0);
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
    if (getenv("PORT_EVID_SET")) ParseSets(getenv("PORT_EVID_SET"));
    if (getenv("PORT_EVID_CALL")) ParseCalls(getenv("PORT_EVID_CALL"));
    sCallFork = getenv("PORT_EVID_CALL_FORK") != NULL;
    if (getenv("PORT_EVID_CALL_BUF")) {
        const char* h = getenv("PORT_EVID_CALL_BUF");
        for (sCallBufLen = 0; h[0] && h[1] && sCallBufLen < sizeof sCallBufInit; h += 2) {
            unsigned v;
            sscanf(h, "%2x", &v);
            sCallBufInit[sCallBufLen++] = (uint8_t)v;
        }
    }
    atexit(Dump);
    sOn = 1;
}

static NOINST void Ctx(char* out, size_t n) {
    BattleScene* sc = sBattleScene.unk_00;
    snprintf(out, n, "%d/%d/%d:%d/%d/%u", sc ? sc->unk_1C : -1, sc ? (int)sc->unk_2C : -1,
             sBattleUser ? sBattleUser->id : -1, sBattleUser ? sBattleUser->usedMove : -1,
             sBattleTarget ? sBattleTarget->id : -1, (unsigned)sBattleDamage);
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
    Hex(sSeq, "user", sBattleUser, 0x64);
    Hex(sSeq, "target", sBattleTarget, 0x64);
    Hex(sSeq, "st0", sActivePokemon[0] ? (void*)&sActivePokemon[0]->state : NULL, 0xC8);
    Hex(sSeq, "st1", sActivePokemon[1] ? (void*)&sActivePokemon[1]->state : NULL, 0xC8);
    Hex(sSeq, "scene", sBattleScene.unk_00, 0xDC);
    fprintf(sSeq, " dmg=%u\n", (unsigned)sBattleDamage);
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

// exit: 0 at entry (counts the call; an @exit record is armed with the call's depth), 1 at a return (applies an armed
// @exit record when the call at that depth returns)
static NOINST void ApplySets(uintptr_t fn, int exit) {
    for (int i = 0; i < sSetN; i++) {
        SetRec* r = &sSets[i];
        if (r->fn != fn) continue;
        if (!exit) {
            r->seen++;
            if (r->nth && r->seen != r->nth) continue;
            if (r->atExit) {
                r->armedDepth = tDepth;
                continue;
            }
        } else if (!r->atExit || r->armedDepth != tDepth + 1) {
            continue;
        } else {
            r->armedDepth = 0;
        }
        uintptr_t a = sBase + r->base;
        for (int k = 0; k < r->nsteps; k++) {
            if (r->steps[k] < 0) {
                a = *(uintptr_t*)a;
                if (a == 0) break;
            } else {
                a += (uintptr_t)r->steps[k];
            }
        }
        if (a == 0) continue;
        if (r->size == 1) *(uint8_t*)a = (uint8_t)(r->value | (r->orIn ? *(uint8_t*)a : 0));
        else if (r->size == 2) *(uint16_t*)a = (uint16_t)(r->value | (r->orIn ? *(uint16_t*)a : 0));
        else *(uint32_t*)a = r->value | (r->orIn ? *(uint32_t*)a : 0);
        if (sSeq) fprintf(sSeq, "S %llu %lx set %d=%u\n", (unsigned long long)Port_ViTick(), (unsigned long)fn, i,
                          r->value);
    }
}

// A crash would lose what ran since the last periodic dump (often the very code that crashed), so the first dump
// also hooks SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT: dump once more, then hand the signal to the handler installed
// before (the port's crash report). Hooked at the first dump rather than at start-up, when the port's handler is
// in place. Not async-signal-safe (stdio, the profile writer); acceptable for a run that is ending anyway.
#if !defined(_WIN32)
static const int kCrashSigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
static struct sigaction sPrevAct[5];
static volatile sig_atomic_t sCrashing;
static int sCrashHooked;

static NOINST void CrashDump(int sig, siginfo_t* info, void* uc) {
    int k = 0;
    while (k < 5 && kCrashSigs[k] != sig) k++;
    if (!sCrashing) {
        sCrashing = 1;
        tBusy = 1;
        Dump();
    }
    if (k < 5) {
        sigaction(sig, &sPrevAct[k], NULL);
        if (sPrevAct[k].sa_flags & SA_SIGINFO) {
            if (sPrevAct[k].sa_sigaction) sPrevAct[k].sa_sigaction(sig, info, uc);
            return;
        }
        if (sPrevAct[k].sa_handler != SIG_DFL && sPrevAct[k].sa_handler != SIG_IGN) {
            sPrevAct[k].sa_handler(sig);
            return;
        }
    }
    raise(sig); // the default action, with the previous disposition restored
}

static NOINST void HookCrashes(void) {
    sCrashHooked = 1;
    for (int k = 0; k < 5; k++) {
        struct sigaction a;
        memset(&a, 0, sizeof a);
        a.sa_sigaction = CrashDump;
        a.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&a.sa_mask);
        sigaction(kCrashSigs[k], &a, &sPrevAct[k]);
    }
}

#else
static volatile sig_atomic_t sCrashing;
static int sCrashHooked;
static LONG CALLBACK NOINST EvidenceException(EXCEPTION_POINTERS* exception) {
    DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (!sCrashing && (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_FLT_DIVIDE_BY_ZERO)) {
        int wasBusy = tBusy;
        sCrashing = 1;
        tBusy = 1;
        Dump();
        tBusy = wasBusy;
        sCrashing = 0; // a later handler may recover this first-chance exception
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static NOINST void HookCrashes(void) {
    sCrashHooked = 1;
    AddVectoredExceptionHandler(1, EvidenceException);
}
#endif

static NOINST void Dump(void) {
    if (!sOn) return;
    if (!sCrashHooked && !sCrashing) HookCrashes();
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
#if defined(_WIN32)
    MoveFileExA(tmp, sOutPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    rename(tmp, sOutPath);
#endif
    if (sSeq) fflush(sSeq);
    if (__llvm_profile_write_file && getenv("LLVM_PROFILE_FILE")) __llvm_profile_write_file();
}

// Terminal probes stop the game threads; flush evidence before ending without host GUI destructors.
void NOINST Port_EvidenceExit(int status) {
    Dump();
    fflush(NULL);
    _exit(status);
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
    for (uint32_t i = (uint32_t)h & (kSiteSlots - 1), k = 0; k < kProbe; k++, i = (i + 1) & (kSiteSlots - 1)) {
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
    for (uint32_t i = (uint32_t)ph & (kPathSlots - 1), k = 0; k < kProbe; k++, i = (i + 1) & (kPathSlots - 1)) {
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
    if (sSetN) ApplySets(fn, 0);
    if (frame >= sLastDump + sDumpEvery) { // (runs end with _exit: the last PORT_EVID_DUMP_EVERY frames are lost)
        sLastDump = frame;
        Dump();
    }
    tBusy = 0;
    // direct calls after the busy flag is down, so the called code is recorded like any other
    if (sCallN) DoCalls(fn, 0);
}

void NOINST __cyg_profile_func_exit(void* this_fn, void* call_site) {
    if (tDepth > 0) tDepth--;
    if (!sOn || tBusy) return;
    uintptr_t fn = (uintptr_t)this_fn - sBase;
    if (sSetN) {
        tBusy = 1;
        ApplySets(fn, 1);
        tBusy = 0;
    }
    if (sCallN) DoCalls(fn, 1);
    if (sWatchN == 0) return;
    if (IsWatched(fn)) {
        tBusy = 1;
        Watch("exit", fn, (uintptr_t)call_site - sBase);
        tBusy = 0;
    }
}
