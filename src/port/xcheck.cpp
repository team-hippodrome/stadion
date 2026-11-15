// Battle cross-check against the original game in ares (tools/battle_xcheck.py, tools/battle_sweep.py, tools/ares_ref).
// Off unless one of these is set; the ares driver has the same switches (ARES_REF_XCHECK*) and writes the same trace:
//   PORT_XCHECK=<file>            the battle trace: one line per forced random value
//                                 ("R <function> <call> <arg> <value> caller=<function>") and per battle event
//                                 ("E <n> <event> ..." with the raw bytes of both Pokemon's ActivePokemonState, the
//                                 BattleScene, sBattleDamage and the message queues)
//   PORT_XCHECK_VI_TIMING=<file>  observed "game-frame retrace" pairs; comparison-only mixer-clock pacing
//   PORT_XCHECK_RNG=<seed>        the battle's random functions (Battle_Random, Battle_RandomAccuracy,
//                                 BattleAI_RandomBelowInclusive, BattleAnim_RandomRange, BattleAnim_RandomIntWithWarmup)
//                                 return a seeded stream instead of their osGetCount-timed generators, so the port and the
//                                 emulator, whose CPU timing differs, draw the same numbers
//   PORT_XCHECK_CASE=<file>       a case (tools/battle_sweep.py writes them), lines:
//                                   mon <side> <slot|*> <field>=<value> ...   the parties, written when the battle engine
//                                                                             is mapped (fields: see kMonFields)
//                                   rule <function>[@<caller>] <v>[,<v>...]   forced values for that function (called
//                                                                             from that function), used in turn
//                                   stop <turns>                              end the run when that many turns are done
//                                   nomusic                                   no battle music (Audio_StartBattleMusicSequence
//                                                                             returns at once) and no announcer
//                                                                             (Audio_QueueSequenceId): the sound effects alone
//   PORT_XCHECK_COM=1             the player's side needs no input in battle: its move time limit runs out at the
//                                 second query of each decision, so the game picks its first usable move and its first
//                                 healthy replacement (BattleScene_IsActionSelectionTimerExpired); the opponent is the
//                                 computer as usual
//   PORT_XCHECK_ANIM=<dir>        the frames of each move animation, from its start to the next battle event, one per
//                                 game frame, as <dir>/m<k>_<move>/f_<n>.ppm (n: battle game frames since the start; at
//                                 most PORT_XCHECK_ANIM_FRAMES game frames, default 300); every frame is drawn meanwhile
//   PORT_BATTLE=<file>            battle boot (below): skip the menus and start the battle the case file describes
// Every trace line carries gf=<n>: battle game frames (BattleScene_UpdateFrame calls). The N64 slows down when busy
// (2-3 VI frames per game frame in particle-heavy effects) while the port always takes 2, so durations and frame pairs
// are compared in game frames.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include "StadionPort.h"
#if defined(_WIN32)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
struct PortHostSymbol { void* address; const char* name; };
extern "C" const PortHostSymbol gPortHostSymbols[];
extern "C" const unsigned gPortHostSymbolCount;
#else
#include <dlfcn.h>
#endif

extern "C" void Port_XcheckWaitRetraces(uint64_t retraces);
extern "C" unsigned long long osGetTime(void);         // libultra (port_os)
extern "C" int gPortXcheckTraceGuRandom;              // (below)
extern "C" unsigned int sParticleFrameCounter;        // particle_math: free-running effect frame counter
extern "C" unsigned short D_8006F084;                 // geo_render: the scene frame counter (GeoRender_AdvanceFrameCounter)
extern "C" short D_800AF7AE;                          // battle_hud: the model texture counter (one per Pokemon model drawn)

namespace {
// the order is the function index in the value stream (tools/ares_ref/ares_ref.cpp kRandomNames)
const char* const kRandomNames[] = { "Battle_Random", "Battle_RandomAccuracy", "BattleAI_RandomBelowInclusive",
                                     "BattleAnim_RandomRange", "BattleAnim_RandomIntWithWarmup" };
constexpr int kRandomFns = 5;

// BattleMon fields a case can set (game/src/game_state.h): name, offset, size
struct MonField { const char* name; int offset, size; };
const MonField kMonFields[] = { { "species", 0x00, 1 }, { "hp", 0x02, 2 },   { "status", 0x05, 1 }, { "type1", 0x06, 1 },
                                { "type2", 0x07, 1 },   { "m1", 0x09, 1 },   { "m2", 0x0A, 1 },     { "m3", 0x0B, 1 },
                                { "m4", 0x0C, 1 },      { "p1", 0x20, 1 },   { "p2", 0x21, 1 },     { "p3", 0x22, 1 },
                                { "p4", 0x23, 1 },      { "level", 0x24, 1 }, { "maxhp", 0x26, 2 }, { "atk", 0x28, 2 },
                                { "def", 0x2A, 2 },     { "spd", 0x2C, 2 },  { "spc", 0x2E, 2 } };

struct MonLine {
    int side, slot; // slot -1: all
    std::vector<std::pair<const MonField*, unsigned>> set;
    std::string nick; // battle boot: the nickname (ASCII)
};
// battle boot: a side's trainer and its scripted decisions
struct BootAction { char kind; int arg; }; // 'm' move slot 0-3, 's' party index
struct BootSide { std::string name; bool scripted = false; std::vector<BootAction> acts; size_t next = 0; };
struct Rule { int fn; std::string caller; std::vector<uint32_t> values; size_t next = 0; };

struct State {
    bool read = false, force = false, com = false;
    uint32_t seed = 0;
    FILE* out = nullptr;
    std::vector<MonLine> mons;
    std::vector<Rule> rules;
    int stopTurns = 0, turns = 0;
    bool noMusic = false, traceSfx = false;
    uint32_t osTimeCalls = 0; // Port_XcheckOsGetTime's index
    std::map<uint32_t, uint32_t> calls; // per stream (see StreamKey)
    unsigned events = 0;
    std::string animDir, animCur;
    int animFrames = 300, animCount = 0;
    std::vector<uint64_t> referenceVi{ 0 };
    uint64_t referencePortStart = 0;
    std::vector<int> loadWait; // PORT_XCHECK_LOAD_WAIT: per animation, game frames left to report "still loading"
    unsigned long long gameFrame = 0;               // battle game frames so far
    unsigned long long animStart = 0, animLast = 0; // game frame of the animation's start, of the last saved frame
    bool animOn = false;
    // ---- battle boot (PORT_BATTLE=<case file> / --battle): see the section at the end of this file ----
    bool boot = false;
    int bootArena = -1, bootMode = 9, bootRule = 6, bootThink = 20, bootAi = 0;
    bool bootIntro = true;
    BootSide bootSides[2];
    std::string dumpDir, dumpRes;
    int dumpEvery = 1, dumpW = 0, dumpH = 0, maxFrames = 0;
    bool dumpStarted = false, dumpDone = false;
    unsigned long long dumpStart = 0, dumpLast = ~0ull;
} g;

void ReadCase(const char* path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd) || cmd[0] == '#') continue;
        if (cmd == "mon") {
            MonLine m;
            std::string slot, kv;
            ss >> m.side >> slot;
            m.slot = slot == "*" ? -1 : atoi(slot.c_str());
            while (ss >> kv) {
                size_t eq = kv.find('=');
                if (eq != std::string::npos && kv.compare(0, eq, "nick") == 0) {
                    m.nick = kv.substr(eq + 1);
                    continue;
                }
                for (const MonField& f : kMonFields) {
                    if (kv.compare(0, eq, f.name) == 0 && strlen(f.name) == eq) {
                        m.set.push_back({ &f, (unsigned)strtoul(kv.c_str() + eq + 1, nullptr, 0) });
                    }
                }
            }
            g.mons.push_back(m);
        } else if (cmd == "rule") {
            Rule r;
            std::string target, values;
            ss >> target >> values;
            size_t at = target.find('@');
            std::string fn = target.substr(0, at);
            r.caller = at == std::string::npos ? "" : target.substr(at + 1);
            r.fn = -1;
            for (int i = 0; i < kRandomFns; i++) {
                if (fn == kRandomNames[i]) r.fn = i;
            }
            std::stringstream vs(values);
            std::string v;
            while (std::getline(vs, v, ',')) r.values.push_back((uint32_t)strtoul(v.c_str(), nullptr, 0));
            if (r.fn >= 0 && !r.values.empty()) g.rules.push_back(r);
        } else if (cmd == "arena") {
            ss >> g.bootArena;
        } else if (cmd == "seed") {
            uint32_t v = 0;
            ss >> v;
            g.force = true;
            g.seed = v;
        } else if (cmd == "intro") {
            int v = 1;
            ss >> v;
            g.bootIntro = v != 0;
        } else if (cmd == "think") {
            ss >> g.bootThink;
        } else if (cmd == "mode") {
            ss >> g.bootMode;
            if (!(ss >> g.bootRule)) g.bootRule = 6;
        } else if (cmd == "ai") {
            ss >> g.bootAi;
        } else if (cmd == "trainer") {
            int side = 0;
            std::string name;
            ss >> side >> name;
            if (side >= 0 && side < 2) g.bootSides[side].name = name;
        } else if (cmd == "act") {
            int side = 0;
            std::string tok;
            ss >> side;
            while (side >= 0 && side < 2 && ss >> tok) {
                // "m0".."m3" a move slot, "s0".."s5" a switch to that party slot ("0".."3" alone: a move slot)
                char kind = (tok[0] == 's' || tok[0] == 'm') ? tok[0] : 'm';
                const char* num = (tok[0] == 's' || tok[0] == 'm') ? tok.c_str() + 1 : tok.c_str();
                g.bootSides[side].acts.push_back({ kind, atoi(num) });
                g.bootSides[side].scripted = true;
            }
        } else if (cmd == "dump") {
            ss >> g.dumpDir;
        } else if (cmd == "dump_every") {
            ss >> g.dumpEvery;
        } else if (cmd == "dump_res") {
            ss >> g.dumpRes;
        } else if (cmd == "frames") {
            ss >> g.maxFrames;
        } else if (cmd == "stop") {
            ss >> g.stopTurns;
        } else if (cmd == "nomusic") {
            g.noMusic = true;
        }
    }
}

void Init() {
    if (g.read) return;
    g.read = true;
    if (const char* p = getenv("PORT_XCHECK")) g.out = fopen(p, "w");
    if (const char* r = getenv("PORT_XCHECK_RNG")) {
        g.force = true;
        g.seed = (uint32_t)strtoul(r, nullptr, 0);
    }
    if (const char* c = getenv("PORT_XCHECK_CASE")) ReadCase(c);
    if (const char* b = getenv("PORT_BATTLE")) {
        if (*b) {
            g.boot = true;
            ReadCase(b);
            if (!g.force) g.force = true, g.seed = 1; // a battle boot is always deterministic
            // the environment overrides the case file
            if (const char* v = getenv("PORT_BATTLE_FRAMES")) g.dumpDir = v;
            if (const char* v = getenv("PORT_BATTLE_EVERY")) g.dumpEvery = atoi(v);
            if (const char* v = getenv("PORT_BATTLE_RES")) g.dumpRes = v;
            if (const char* v = getenv("PORT_BATTLE_MAX_FRAMES")) g.maxFrames = atoi(v);
            if (const char* v = getenv("PORT_BATTLE_INTRO")) g.bootIntro = atoi(v) != 0;
            if (g.dumpEvery < 1) g.dumpEvery = 1;
            if (!g.dumpRes.empty() && sscanf(g.dumpRes.c_str(), "%dx%d", &g.dumpW, &g.dumpH) != 2) g.dumpW = g.dumpH = 0;
            if (!g.dumpDir.empty()) mkdir(g.dumpDir.c_str(), 0755);
        }
    }
    g.com = getenv("PORT_XCHECK_COM") != nullptr;
    gPortXcheckTraceGuRandom = getenv("PORT_XCHECK_GURANDOM") != nullptr;
    g.traceSfx = getenv("PORT_XCHECK_SFX") != nullptr;
    if (const char* a = getenv("PORT_XCHECK_ANIM")) g.animDir = a;
    if (const char* n = getenv("PORT_XCHECK_ANIM_FRAMES")) g.animFrames = atoi(n);
    if (const char* p = getenv("PORT_XCHECK_VI_TIMING")) {
        std::ifstream file(p);
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream row(line);
            int64_t frame, retrace;
            std::string trailing;
            if (!(row >> frame >> retrace) || (row >> trailing) || frame <= 0 || retrace < 0 ||
                (uint64_t)frame != g.referenceVi.size() || frame > 1000000 ||
                (frame > 1 && (uint64_t)retrace < g.referenceVi.back())) {
                fprintf(stderr, "[xcheck] invalid reference VI schedule: %s\n", p);
                _exit(2);
            }
            g.referenceVi.push_back(retrace);
        }
        if (!file.eof() || g.referenceVi.size() < 2) {
            fprintf(stderr, "[xcheck] cannot read reference VI schedule: %s\n", p);
            _exit(2);
        }
    }
    if (const char* w = getenv("PORT_XCHECK_LOAD_WAIT")) {
        std::stringstream ws(w);
        std::string v;
        while (std::getline(ws, v, ',')) g.loadWait.push_back(atoi(v.c_str()));
    }
}

constexpr uint32_t kOsTimeStream = 0x40; // the announcer's osGetTime() (Port_XcheckOsGetTime), as the ares driver's
// splitmix64 of (seed, stream, call index): the same stream as the ares driver's streamValue
uint32_t StreamValue(uint32_t seed, uint32_t key, uint32_t k) {
    uint64_t z = ((uint64_t)seed << 32) ^ ((uint64_t)key << 24) ^ k;
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)(z ^ (z >> 31));
}

// the stream a call draws from: one per logic function; the animation functions (3, 4) are polled per frame, so
// theirs are kept per argument (a frame of timing difference cannot shift the effects' draws); as the ares driver
uint32_t StreamKey(int fn, int arg) {
    return fn >= 3 ? (uint32_t)fn << 16 | ((uint32_t)arg & 0xFFFF) : (uint32_t)fn << 16;
}

std::string Hex(const void* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

// the game function a return address lies in (the game's functions are exported: dladdr names them)
const char* CallerName(void* ra) {
#if defined(_WIN32)
    static const std::vector<PortHostSymbol> symbols = [] {
        std::vector<PortHostSymbol> rows(gPortHostSymbols, gPortHostSymbols + gPortHostSymbolCount);
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return reinterpret_cast<uintptr_t>(a.address) < reinterpret_cast<uintptr_t>(b.address);
        });
        return rows;
    }();
    if (ra) {
        auto found = std::upper_bound(symbols.begin(), symbols.end(), reinterpret_cast<uintptr_t>(ra),
            [](uintptr_t address, const auto& row) { return address < reinterpret_cast<uintptr_t>(row.address); });
        if (found != symbols.begin()) return (--found)->name;
    }
#else
    Dl_info info;
    if (ra && dladdr(ra, &info) && info.dli_sname) return info.dli_sname[0] == '_' ? info.dli_sname + 1 : info.dli_sname;
#endif
    return "?";
}

void EndCapture() {
    g.animOn = false;
}
} // namespace

extern "C" {

int Port_XcheckEnabled(void) {
    Init();
    return g.out != nullptr || g.force || g.com || !g.animDir.empty() || !g.mons.empty() || g.boot;
}

// the move presentation waits for the move's effect assets (battle_engine_309ED0.c): instant in the port, while the N64
// can spend game frames more on the DMA, and every state that runs on through that wait (Moltres' simulated flame
// mesh, its flame particles, model idle motions) reaches the animation that many frames further along.
// PORT_XCHECK_LOAD_WAIT=<n0>,<n1>,...: before animation k the port reports "still loading" n_k more game frames
// (tools/battle_sweep.py sets them from the ares trace: its A lines' gf minus the port's)
int Port_XcheckLoadWait(void) {
    Init();
    if ((size_t)g.animCount >= g.loadWait.size() || g.loadWait[g.animCount] <= 0) return 0;
    g.loadWait[g.animCount]--;
    return 1;
}

int Port_XcheckForcingRandom(void) {
    Init();
    return g.force;
}

int Port_XcheckCom(void) {
    Init();
    return g.com;
}

// fn: index in kRandomNames; arg: the function's range argument (as the hooked function computes its result from it);
// caller: the hooked function's return address
int Port_XcheckRandom(int fn, int arg, int* out, void* caller) {
    Init();
    if (!g.force || fn < 0 || fn >= kRandomFns) return 0;
    const char* from = CallerName(caller);
    uint32_t key = StreamKey(fn, arg);
    uint32_t k = g.calls[key]++;
    uint32_t v = StreamValue(g.seed, key, k);
    Rule* rule = nullptr; // a rule for this caller wins over one for every caller
    for (Rule& r : g.rules) {
        if (r.fn == fn && (r.caller == from || (r.caller.empty() && !rule))) rule = &r;
    }
    if (rule) {
        v = rule->values[rule->next];
        rule->next = (rule->next + 1) % rule->values.size();
    }
    uint32_t r = 0;
    switch (fn) {
        case 0: case 1: r = v & 0xFF; break;                                    // Battle_Random(Accuracy): u8
        case 2: r = arg <= 0 ? 0 : v % (uint32_t)(arg + 1); break;              // 0..arg
        case 3: r = arg <= 0 ? 0 : (uint16_t)(v % (uint32_t)arg); break;        // 0..arg-1
        case 4: r = arg <= 0 ? 0 : v % (uint32_t)arg; break;                    // 0..arg-1
    }
    if (g.out) {
        fprintf(g.out, "R %s %u %d %u frame=%llu gf=%llu caller=%s\n", kRandomNames[fn], k, arg, r, (unsigned long long)Port_ViTick(),
                g.gameFrame, from);
        fflush(g.out);
    }
    *out = (int)r;
    return 1;
}

// a case's party fields for this BattleMon (side 0: player, 1: opponent; the TeamRoster's pokemonPicked and the
// roster info copy alike), in the port's byte order; game side: Port_XcheckApplyCaseParties (battle_engine_mechanics.c)
void Port_XcheckPatchMon(int side, int slot, void* battleMon) {
    Init();
    uint8_t* b = (uint8_t*)battleMon;
    for (const MonLine& m : g.mons) {
        if (m.side != side || (m.slot >= 0 && m.slot != slot)) continue;
        for (auto& [f, v] : m.set) {
            if (f->size == 1) {
                b[f->offset] = (uint8_t)v;
            } else {
                uint16_t h = (uint16_t)v;
                memcpy(b + f->offset, &h, 2);
            }
        }
    }
}

int Port_XcheckNoMusic(void) {
    Init();
    return g.noMusic;
}

int Port_XcheckHasCase(void) {
    Init();
    return !g.mons.empty();
}

// the battle state at an event, as raw bytes in the port's (little-endian) layout; game side:
// Port_XcheckBattleEvent (battle_engine_mechanics.c)
void Port_XcheckWriteEvent(const char* name, const void* state0, const void* state1, const void* scene, unsigned damage,
                           const void* messageQueues) {
    Init();
    EndCapture(); // a move animation is captured up to the next event
    if (strcmp(name, "turn") == 0 && g.stopTurns > 0 && g.turns++ >= g.stopTurns) {
        if (g.out) fprintf(g.out, "S turns=%d frame=%llu gf=%llu\n", g.stopTurns, (unsigned long long)Port_ViTick(), g.gameFrame);
        if (g.out) fflush(g.out);
        fprintf(stderr, "[xcheck] %d turns done, stopping at frame %llu\n", g.stopTurns, (unsigned long long)Port_ViTick());
        _exit(0);
    }
    if (!g.out) return;
    fprintf(g.out, "E %u %s frame=%llu gf=%llu st0=%s st1=%s scene=%s dmg=%u mq=%s\n", g.events++, name, (unsigned long long)Port_ViTick(),
            g.gameFrame,
            Hex(state0, 0xC8).c_str(), Hex(state1, 0xC8).c_str(), Hex(scene, 0xDC).c_str(), damage,
            messageQueues ? Hex(messageQueues, 0x480).c_str() : "");
    fflush(g.out);
}

// a move animation starts (BattleScene_EnterRow3StartMoveAnim / Row19): the attacker's side and move
void Port_XcheckMoveAnim(int side, int move) {
    Init();
    if (g.out) {
        fprintf(g.out, "A %d move=%d side=%d frame=%llu gf=%llu pfc=%u gfc=%u mtc=%d\n", g.animCount, move, side,
                (unsigned long long)Port_ViTick(), g.gameFrame, sParticleFrameCounter, (unsigned)D_8006F084, (int)D_800AF7AE);
        fflush(g.out);
    }
    // guRandom (MathUtil_Random16: the effects' particles) from the same state on both sides, as the ares driver does
    if (g.force) Port_GuRandomSeed(174823885);
    // and the effects' frame counter (sParticleFrameMod8 etc. pick texture frames, e.g. Thundershock's bolts). It counts
    // battle game frames, and the N64 can spend a frame more before an animation (its effect assets load by DMA), which
    // put the bolts one texture frame out of phase after Dragon Rage. The ares driver does the same.
    if (g.force) sParticleFrameCounter = 0;
    // and the two other texture-frame counters, for the same reason. D_800AF7AE goes up for every Pokemon model drawn
    // and is only cleared when a model is set up; animated model parts take their texture frame and scroll from it
    // (DisplayList_InitAnimatedFrameSegment: Rapidash's mane, Moltres's wings). D_8006F084 counts game frames since boot,
    // so each side's menus leave it at another value; effects pick frames with it (D_8006F084 & 7 and the
    // BattleAnim_Build*DisplayList colour cycles). The ares driver does the same.
    if (g.force) D_800AF7AE = 0, D_8006F084 = 0;
    g.osTimeCalls = 0; // (Port_XcheckOsGetTime)
    // and the animation streams (idle checks every frame: blinks, idle motions) from their start, so they line up
    // within the animation although the two games reach it after different numbers of frames
    for (auto it = g.calls.begin(); it != g.calls.end();) {
        it = (it->first >> 16) >= 3 ? g.calls.erase(it) : std::next(it);
    }
    if (!g.animDir.empty()) {
        char name[64];
        snprintf(name, sizeof name, "/m%02d_%03d", g.animCount, move);
        g.animCur = g.animDir + name;
        mkdir(g.animDir.c_str(), 0755);
        mkdir(g.animCur.c_str(), 0755);
        g.animStart = g.gameFrame;
        g.animLast = ~0ull;
        g.animOn = true;
    }
    g.animCount++;
}

int Port_XcheckAnimActive(void) {
    Init();
    return !g.animDir.empty();
}

// ---- battle boot ----------------------------------------------------------------------------------------------
// PORT_BATTLE=<case file> (or `pokestadium <rom> --battle <case file>`) starts the battle engine straight away
// (State_PortBattle, game/src/game_state.c) with the parties, arena and random seed of the case file, no menus and no
// replay. The case file is the xcheck case format (above) plus:
//   arena <id>                  stadium_models file index: 0 Free Battle, 1-3 Cups 97-99, 4 Petit, 5 Pika, 6 Prime,
//                               7-16 Gym Leader Castle, 17 Vs. Mewtwo (default: the mode's arena)
//   seed <n>                    the random seed (same as PORT_XCHECK_RNG; default 1)
//   intro <0|1>                 0: the send-out intro ("Go! PIKACHU!", about 330 game frames) runs without frames being
//                               dumped; frame 0 is the first frame after it (default 1)
//   think <n>                   game frames a scripted side's menu is shown before its choice (default 20)
//   mode <n> [<rule>]           session mode / rule category (default 9 6 = Battle Now!)
//   trainer <side> <NAME>       the trainer name (ASCII, no spaces)
//   act <side> m0 s2 m1 ...     scripted decisions of a side, in order: m<slot> uses a move slot 0-3, s<slot> switches to
//                               that party slot. A side with `act` lines is controlled like a player (menus shown);
//                               a side without is the computer (BattleScene_CollectCpuAction)
//   mon <side> <slot> species=<dex> level=.. nick=NAME hp=.. maxhp=.. status=.. m1..m4=<move> p1..p4=<PP> atk= def= spd= spc=
//                               a mon line with species= makes that party slot (up to 6); `*` lines apply to every slot
//   dump <dir>, dump_every <n>, dump_res <WxH>, frames <n>
//                               every n-th battle game frame as <dir>/f_<n>.ppm, scaled to WxH, ending after <n> frames
// Environment overrides: PORT_BATTLE_FRAMES, PORT_BATTLE_EVERY, PORT_BATTLE_RES, PORT_BATTLE_MAX_FRAMES, PORT_BATTLE_INTRO.
// The existing options (PORT_XCHECK=<trace>, PORT_XCHECK_ANIM, PORT_CAMLOG, PORT_PARTLOG, PORT_SCREENSHOT_DIR ...) work as before.
int Port_BattleBootActive(void) {
    Init();
    return g.boot;
}

static const MonLine* FindMonField(int side, int slot, const char* name, unsigned* value) {
    const MonLine* found = nullptr;
    for (const MonLine& m : g.mons) {
        if (m.side != side || (m.slot >= 0 && m.slot != slot)) continue;
        for (auto& [f, v] : m.set) {
            if (strcmp(f->name, name) == 0) {
                *value = v;
                found = &m;
            }
        }
    }
    return found;
}

// the party slot's species and level (0 species: no such slot); nick receives the nickname or "" (12 bytes)
int Port_BattleBootMon(int side, int slot, int* species, int* level, char* nick) {
    unsigned v = 0;
    *species = *level = 0;
    nick[0] = 0;
    // a slot exists only through a line naming it (a `*` line alone does not make six Pokemon)
    bool named = false;
    for (const MonLine& m : g.mons) {
        if (m.side != side || m.slot != slot) continue;
        for (auto& [f, val] : m.set) named |= strcmp(f->name, "species") == 0;
    }
    if (!named || !FindMonField(side, slot, "species", &v)) return 0;
    *species = (int)v;
    *level = FindMonField(side, slot, "level", &v) ? (int)v : 50;
    for (const MonLine& m : g.mons) {
        if (m.side == side && (m.slot < 0 || m.slot == slot) && !m.nick.empty()) snprintf(nick, 12, "%s", m.nick.c_str());
    }
    return 1;
}

int Port_BattleBootHasField(int side, int slot, const char* name) {
    unsigned v;
    return FindMonField(side, slot, name, &v) != nullptr;
}

const char* Port_BattleBootTrainer(int side) {
    return g.bootSides[side & 1].name.empty() ? (side ? "COM" : "1P") : g.bootSides[side & 1].name.c_str();
}

int Port_BattleBootScripted(int side) { return g.bootSides[side & 1].scripted; }
int Port_BattleBootArena(void) { return g.boot ? g.bootArena : -1; }
int Port_BattleBootMode(int* rule, int* ai) {
    *rule = g.bootRule;
    *ai = g.bootAi;
    return g.bootMode;
}
int Port_BattleBootThink(void) { return g.bootThink; }

// the side's next scripted decision: 0 none left, 1 move slot, 2 switch to a party slot; consumed when `consume`
int Port_BattleBootNextAction(int side, int consume, int* arg) {
    BootSide& b = g.bootSides[side & 1];
    if (b.next >= b.acts.size()) return 0;
    *arg = b.acts[b.next].arg;
    int kind = b.acts[b.next].kind == 's' ? 2 : 1;
    if (consume) b.next++;
    return kind;
}

// BattleScene_Update, every frame: the scene phase (4 the send-out intro, 1 the camera shots of the turn's start, 2 the
// decisions and moves). Without the intro, the frame dump starts when the send-out intro is over (phase 4 ends)
void Port_BattleBootPhase(int phase) {
    if (!g.boot || g.dumpStarted) return;
    if (g.bootIntro || phase == 1 || phase == 2) {
        g.dumpStarted = true;
        g.dumpStart = g.gameFrame;
        if (g.out) fprintf(g.out, "B start gf=%llu intro=%d\n", g.gameFrame, g.bootIntro);
    }
}

// the battle engine returned (State_PortBattle): the run is over
void Port_BattleBootFinished(void) {
    if (g.out) {
        fprintf(g.out, "S end gf=%llu\n", g.gameFrame);
        fflush(g.out);
    }
    fprintf(stderr, "[battle] battle over after %llu game frames\n", g.gameFrame);
    fflush(stderr);
    _exit(0);
}

// Engine.cpp, once per presented frame: where to write the battle boot frame (null: none due); *w, *h the size asked for
// (0: as drawn). The frame number is the battle game frame since the dump started.
const char* Port_BattleFramePath(int* w, int* h) {
    static std::string path;
    if (!g.boot || g.dumpDir.empty() || !g.dumpStarted || g.dumpDone) return nullptr;
    unsigned long long n = g.gameFrame - g.dumpStart;
    if (n % g.dumpEvery != 0 || g.gameFrame == g.dumpLast) return nullptr;
    if (g.maxFrames > 0 && n >= (unsigned long long)g.maxFrames) return nullptr;
    g.dumpLast = g.gameFrame;
    char name[64];
    snprintf(name, sizeof name, "/f_%05llu.ppm", n);
    path = g.dumpDir + name;
    *w = g.dumpW;
    *h = g.dumpH;
    return path.c_str();
}

// after the frame was written: the last due frame of `frames` ends the run
void Port_BattleFrameWritten(void) {
    unsigned long long n = g.gameFrame - g.dumpStart;
    if (g.maxFrames > 0 && n + g.dumpEvery >= (unsigned long long)g.maxFrames) {
        g.dumpDone = true;
        if (g.out) {
            fprintf(g.out, "S frames=%d gf=%llu\n", g.maxFrames, g.gameFrame);
            fflush(g.out);
        }
        fprintf(stderr, "[battle] %d frames dumped, stopping\n", g.maxFrames);
        fflush(stderr);
        _exit(0);
    }
}

// PORT_CAMLOG / PORT_PARTLOG (game/src/geo_render.c, particle_math_269820.c): the battle game frame, and the frame
// since the current move animation started
unsigned long long Port_CamlogGameFrameNo(void) { return g.gameFrame; }
unsigned long long Port_CamlogAnimFrame(void) { return g.gameFrame - g.animStart; }

// guRandom's callers while an animation is captured ("G <frame> <caller>"; the particles draw from it, so a different
// number of calls before them changes the effect): PORT_XCHECK_GURANDOM=1
int gPortXcheckTraceGuRandom = 0;

void Port_XcheckGuRandomCalled(void* caller) {
    if (g.animOn && g.out) fprintf(g.out, "G %llu %s\n", g.gameFrame - g.animStart, CallerName(caller));
}

// os_devices.cpp: draw this frame even when running fast (an animation is being captured)
int Port_XcheckWantDraw(void) {
    return g.animOn || (g.boot && !g.dumpDir.empty() && g.dumpStarted && !g.dumpDone &&
                        (g.gameFrame - g.dumpStart) % g.dumpEvery == 0);
}

// a sound command (PORT_XCHECK_SFX=1): "U <battle game frame> <kind> <args>", kind 0 Audio_PlayCommand(a, b, c),
// 1 Audio_PlaySoundEffectById(a), 2 Audio_QueueSequenceId(a) (the announcer's lines), 3 Audio_PlaySoundEffect(.., id a, priority b, volume c) (every
// sound effect started); the ares driver writes the same lines (ARES_REF_XCHECK_SFX), so the order and game
// frame of a move's sounds can be compared where the recorded sound differs
void Port_XcheckSfx(int kind, unsigned a, int b, int c) {
    Init();
    if (g.out && g.traceSfx) fprintf(g.out, "U %llu %d %x %d %d\n", g.gameFrame, kind, a, b, c);
}

// osGetTime() in the announcer's code (3D140_2.c: which line to say, Audio_SelectSoundAvoidingRecent's shuffle;
// audio_commands_category2.c: a sound's pitch variation). It is CPU timing, so the two games chose different lines.
// With PORT_XCHECK_RNG both sides return the stream value of the call's index instead, counted from the battle's first
// game frame and from each animation start (as the random streams); the ares driver does the same for these callers.
// (The index, not the game frame: the announcer's next line waits for the last one to play out, in sound time.)
unsigned long long Port_XcheckOsGetTime(void) {
    Init();
    if (!g.force) return osGetTime();
    return StreamValue(g.seed, kOsTimeStream, g.osTimeCalls++);
}

// battle_engine_main.c: BattleScene_UpdateFrame runs (once per battle game frame)
void Port_XcheckGameFrame(void) {
    g.gameFrame++;
    if (g.gameFrame == 1) {
        g.osTimeCalls = 0;
        g.referencePortStart = Port_ViTick();
    }
    if (g.gameFrame < g.referenceVi.size()) {
        uint64_t target = g.referencePortStart + g.referenceVi[g.gameFrame] - g.referenceVi[1];
        uint64_t now = Port_ViTick();
        if (target > now) Port_XcheckWaitRetraces(target - now);
    }
    // "V <battle game frame> <VI frame>", as the ares driver. The runner can replay its retrace gaps so sound
    // commands run on the observed N64 timeline while the mixer retains its original sample clock.
    if (g.out) fprintf(g.out, "V %llu %llu\n", g.gameFrame, (unsigned long long)Port_ViTick());
}

// Engine.cpp, once per presented frame: where to write this frame (null: not captured); the first presented frame of
// each game frame, named by game frames since the animation started (as the ares driver's)
const char* Port_XcheckFramePath(void) {
    static std::string path;
    if (!g.animOn) return nullptr;
    unsigned long long now = g.gameFrame, n = now - g.animStart;
    if (n >= (unsigned long long)g.animFrames) {
        EndCapture();
        return nullptr;
    }
    if (now == g.animLast) return nullptr;
    g.animLast = now;
    char name[64];
    snprintf(name, sizeof name, "/f_%03llu.ppm", n);
    path = g.animCur + name;
    return path.c_str();
}

} // extern "C"
