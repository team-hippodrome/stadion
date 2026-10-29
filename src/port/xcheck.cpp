// Battle cross-check against the original game in ares (tools/battle_xcheck.py, tools/battle_sweep.py, tools/ares_ref).
// Off unless one of these is set; the ares driver has the same switches (ARES_REF_XCHECK*) and writes the same trace:
//   PORT_XCHECK=<file>            the battle trace: one line per forced random value
//                                 ("R <function> <call> <arg> <value> caller=<function>") and per battle event
//                                 ("E <n> <event> ..." with the raw bytes of both Pokemon's ActivePokemonState, the
//                                 BattleScene, gBattleDamage and the message queues)
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
//                                                                             returns at once): the sound effects alone
//   PORT_XCHECK_COM=1             the player's side needs no input in battle: its move time limit runs out at the
//                                 second query of each decision, so the game picks its first usable move and its first
//                                 healthy replacement (BattleScene_IsActionSelectionTimerExpired); the opponent is the
//                                 computer as usual
//   PORT_XCHECK_ANIM=<dir>        the frames of each move animation, from its start to the next battle event, one per
//                                 game frame, as <dir>/m<k>_<move>/f_<n>.ppm (n: battle game frames since the start; at
//                                 most PORT_XCHECK_ANIM_FRAMES game frames, default 300); every frame is drawn meanwhile
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
#include <dlfcn.h>
#include <sys/stat.h>

extern "C" unsigned long long Port_ViTick(void);
extern "C" void Port_GuRandomSeed(unsigned int seed); // game/lib/ultralib/src/gu/random.c
extern "C" int gPortXcheckTraceGuRandom;              // (below)

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

struct MonLine { int side, slot; std::vector<std::pair<const MonField*, unsigned>> set; }; // slot -1: all
struct Rule { int fn; std::string caller; std::vector<uint32_t> values; size_t next = 0; };

struct State {
    bool read = false, force = false, com = false;
    uint32_t seed = 0;
    FILE* out = nullptr;
    std::vector<MonLine> mons;
    std::vector<Rule> rules;
    int stopTurns = 0, turns = 0;
    bool noMusic = false;
    std::map<uint32_t, uint32_t> calls; // per stream (see StreamKey)
    unsigned events = 0;
    std::string animDir, animCur;
    int animFrames = 300, animCount = 0;
    unsigned long long gameFrame = 0;               // battle game frames so far
    unsigned long long animStart = 0, animLast = 0; // game frame of the animation's start, of the last saved frame
    bool animOn = false;
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
    g.com = getenv("PORT_XCHECK_COM") != nullptr;
    gPortXcheckTraceGuRandom = getenv("PORT_XCHECK_GURANDOM") != nullptr;
    if (const char* a = getenv("PORT_XCHECK_ANIM")) g.animDir = a;
    if (const char* n = getenv("PORT_XCHECK_ANIM_FRAMES")) g.animFrames = atoi(n);
}

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
    Dl_info info;
    if (ra && dladdr(ra, &info) && info.dli_sname) return info.dli_sname[0] == '_' ? info.dli_sname + 1 : info.dli_sname;
    return "?";
}

void EndCapture() {
    g.animOn = false;
}
} // namespace

extern "C" {

int Port_XcheckEnabled(void) {
    Init();
    return g.out != nullptr || g.force || g.com || !g.animDir.empty() || !g.mons.empty();
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
        fprintf(g.out, "R %s %u %d %u frame=%llu gf=%llu caller=%s\n", kRandomNames[fn], k, arg, r, Port_ViTick(),
                g.gameFrame, from);
        fflush(g.out);
    }
    *out = (int)r;
    return 1;
}

// a case's party fields for this BattleMon (side 0: player, 1: opponent; the TeamRoster's pokemonPicked and the
// roster info copy alike), in the port's byte order; game side: Port_XcheckApplyCaseParties (battle_engine_359F90.c)
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
// Port_XcheckBattleEvent (battle_engine_359F90.c)
void Port_XcheckWriteEvent(const char* name, const void* state0, const void* state1, const void* scene, unsigned damage,
                           const void* messageQueues) {
    Init();
    EndCapture(); // a move animation is captured up to the next event
    if (strcmp(name, "turn") == 0 && g.stopTurns > 0 && g.turns++ >= g.stopTurns) {
        if (g.out) fprintf(g.out, "S turns=%d frame=%llu gf=%llu\n", g.stopTurns, Port_ViTick(), g.gameFrame);
        if (g.out) fflush(g.out);
        fprintf(stderr, "[xcheck] %d turns done, stopping at frame %llu\n", g.stopTurns, Port_ViTick());
        _exit(0);
    }
    if (!g.out) return;
    fprintf(g.out, "E %u %s frame=%llu gf=%llu st0=%s st1=%s scene=%s dmg=%u mq=%s\n", g.events++, name, Port_ViTick(),
            g.gameFrame,
            Hex(state0, 0xC8).c_str(), Hex(state1, 0xC8).c_str(), Hex(scene, 0xDC).c_str(), damage,
            messageQueues ? Hex(messageQueues, 0x480).c_str() : "");
    fflush(g.out);
}

// a move animation starts (BattleScene_EnterRow3StartMoveAnim / Row19): the attacker's side and move
void Port_XcheckMoveAnim(int side, int move) {
    Init();
    if (g.out) {
        fprintf(g.out, "A %d move=%d side=%d frame=%llu gf=%llu\n", g.animCount, move, side, Port_ViTick(), g.gameFrame);
        fflush(g.out);
    }
    // guRandom (MathUtil_Random16: the effects' particles) from the same state on both sides, as the ares driver does
    if (g.force) Port_GuRandomSeed(174823885);
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

// guRandom's callers while an animation is captured ("G <frame> <caller>"; the particles draw from it, so a different
// number of calls before them changes the effect): PORT_XCHECK_GURANDOM=1
int gPortXcheckTraceGuRandom = 0;

void Port_XcheckGuRandomCalled(void* caller) {
    if (g.animOn && g.out) fprintf(g.out, "G %llu %s\n", g.gameFrame - g.animStart, CallerName(caller));
}

// os_devices.cpp: draw this frame even when running fast (an animation is being captured)
int Port_XcheckWantDraw(void) {
    return g.animOn;
}

// battle_engine_main.c: BattleScene_UpdateFrame runs (once per battle game frame)
void Port_XcheckGameFrame(void) {
    g.gameFrame++;
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
