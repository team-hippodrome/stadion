// Battle cross-check against the original game in ares (tools/battle_xcheck.py, tools/ares_ref). Off unless one of
// these is set; the ares driver has the same switches (ARES_REF_XCHECK*) and writes the same trace:
//   PORT_XCHECK=<file>            the battle trace: one line per forced random value ("R <function> <call> <arg> <value>")
//                                 and per battle event ("E <n> <event> ..." with the raw bytes of both Pokemon's
//                                 ActivePokemonState, the BattleScene, gBattleDamage and the message queues)
//   PORT_XCHECK_RNG=<seed>[:<queue file>]  the battle's random functions (Battle_Random, Battle_RandomAccuracy,
//                                 BattleAI_RandomBelowInclusive, BattleAnim_RandomRange, BattleAnim_RandomIntWithWarmup)
//                                 return a seeded stream per function instead of their osGetCount-timed generators,
//                                 after the queued values (lines "<function> <value>"); so the port and the emulator,
//                                 whose CPU timing differs, draw the same numbers
//   PORT_XCHECK_COM=1             the player's side needs no input in battle: its move time limit runs out at the
//                                 second query of each decision, so the game picks its first usable move and its first
//                                 healthy replacement (BattleScene_IsActionSelectionTimerExpired); the opponent is the
//                                 computer as usual
//   PORT_XCHECK_ANIM=<dir>        every frame of each move animation from its start, as <dir>/m<k>_<move>/f_<n>.ppm
//                                 (at most PORT_XCHECK_ANIM_FRAMES, default 150)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>

extern "C" unsigned long long Port_ViTick(void);

namespace {
// the order is the function index in the value stream (tools/ares_ref/ares_ref.cpp kRandomNames)
const char* const kRandomNames[] = { "Battle_Random", "Battle_RandomAccuracy", "BattleAI_RandomBelowInclusive",
                                     "BattleAnim_RandomRange", "BattleAnim_RandomIntWithWarmup" };
constexpr int kRandomFns = 5;

struct State {
    bool read = false, force = false, com = false;
    uint32_t seed = 0;
    FILE* out = nullptr;
    std::map<int, std::vector<uint32_t>> queue;
    std::map<uint32_t, uint32_t> calls; // per stream (see StreamKey)
    unsigned events = 0;
    std::string animDir, animCur;
    int animFrames = 150, animCount = 0;
    unsigned long long animStart = 0, animLast = 0; // game frame of the animation's start, of the last saved frame
    bool animOn = false;
} g;

void Init() {
    if (g.read) return;
    g.read = true;
    if (const char* p = getenv("PORT_XCHECK")) g.out = fopen(p, "w");
    if (const char* r = getenv("PORT_XCHECK_RNG")) {
        g.force = true;
        std::string spec = r;
        size_t colon = spec.find(':');
        g.seed = (uint32_t)strtoul(spec.substr(0, colon).c_str(), nullptr, 0);
        if (colon != std::string::npos) {
            std::ifstream q(spec.substr(colon + 1));
            std::string name;
            uint32_t v;
            while (q >> name >> v) {
                for (int i = 0; i < kRandomFns; i++) {
                    if (name == kRandomNames[i]) g.queue[i].push_back(v);
                }
            }
        }
    }
    g.com = getenv("PORT_XCHECK_COM") != nullptr;
    if (const char* a = getenv("PORT_XCHECK_ANIM")) g.animDir = a;
    if (const char* n = getenv("PORT_XCHECK_ANIM_FRAMES")) g.animFrames = atoi(n);
}

// splitmix64 of (seed, function, call index): the same stream as the ares driver's streamValue
uint32_t StreamValue(uint32_t seed, uint32_t fn, uint32_t k) {
    uint64_t z = ((uint64_t)seed << 32) ^ ((uint64_t)fn << 24) ^ k;
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
} // namespace

extern "C" {

int Port_XcheckEnabled(void) {
    Init();
    return g.out != nullptr || g.force || g.com || !g.animDir.empty();
}

int Port_XcheckForcingRandom(void) {
    Init();
    return g.force;
}

int Port_XcheckCom(void) {
    Init();
    return g.com;
}

// fn: index in kRandomNames; arg: the function's range argument (as the hooked function computes its result from it)
int Port_XcheckRandom(int fn, int arg, int* out) {
    Init();
    if (!g.force || fn < 0 || fn >= kRandomFns) return 0;
    uint32_t key = StreamKey(fn, arg);
    uint32_t k = g.calls[key]++;
    uint32_t v;
    auto& q = g.queue[fn];
    if (!q.empty()) {
        v = q.front();
        q.erase(q.begin());
    } else {
        v = StreamValue(g.seed, key, k);
    }
    uint32_t r = 0;
    switch (fn) {
        case 0: case 1: r = v & 0xFF; break;                                    // Battle_Random(Accuracy): u8
        case 2: r = arg <= 0 ? 0 : v % (uint32_t)(arg + 1); break;              // 0..arg
        case 3: r = arg <= 0 ? 0 : (uint16_t)(v % (uint32_t)arg); break;        // 0..arg-1
        case 4: r = arg <= 0 ? 0 : v % (uint32_t)arg; break;                    // 0..arg-1
    }
    if (g.out) {
        fprintf(g.out, "R %s %u %d %u frame=%llu\n", kRandomNames[fn], k, arg, r, Port_ViTick());
        fflush(g.out);
    }
    *out = (int)r;
    return 1;
}

// the battle state at an event, as raw bytes in the port's (little-endian) layout; game side:
// Port_XcheckBattleEvent (battle_engine_359F90.c)
void Port_XcheckWriteEvent(const char* name, const void* state0, const void* state1, const void* scene, unsigned damage,
                           const void* messageQueues) {
    Init();
    if (!g.out) return;
    fprintf(g.out, "E %u %s frame=%llu st0=%s st1=%s scene=%s dmg=%u mq=%s\n", g.events++, name, Port_ViTick(),
            Hex(state0, 0xC8).c_str(), Hex(state1, 0xC8).c_str(), Hex(scene, 0xDC).c_str(), damage,
            messageQueues ? Hex(messageQueues, 0x480).c_str() : "");
    fflush(g.out);
}

// a move animation starts (BattleScene_EnterRow3StartMoveAnim / Row19): the attacker's side and move
void Port_XcheckMoveAnim(int side, int move) {
    Init();
    if (g.out) {
        fprintf(g.out, "A %d move=%d side=%d frame=%llu\n", g.animCount, move, side, Port_ViTick());
        fflush(g.out);
    }
    if (!g.animDir.empty()) {
        char name[64];
        snprintf(name, sizeof name, "/m%02d_%03d", g.animCount, move);
        g.animCur = g.animDir + name;
        mkdir(g.animDir.c_str(), 0755);
        mkdir(g.animCur.c_str(), 0755);
        g.animStart = Port_ViTick();
        g.animLast = ~0ull;
        g.animOn = true;
    }
    g.animCount++;
}

// Engine.cpp, once per presented frame: where to write this frame (empty: not captured)
int Port_XcheckAnimActive(void) {
    Init();
    return !g.animDir.empty();
}

// named by game frame since the animation started (as the ares driver's), once per game frame
const char* Port_XcheckFramePath(void) {
    static std::string path;
    if (!g.animOn) return nullptr;
    unsigned long long now = Port_ViTick(), n = now - g.animStart;
    if (n >= (unsigned long long)g.animFrames) {
        g.animOn = false;
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
