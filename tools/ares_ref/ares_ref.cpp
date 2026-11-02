// ares_ref — headless ares (N64 core + paraLLEl-RDP) driver that produces reference frames for the port.
//
// Boot logic (paks, CIC detection, power-on) follows michiiik/N64Recomp ares-bridge/src/ares_core_glue.cpp.
// Adds what the bridge does not have yet: scripted controller input, VI frame capture and save states.
//
// Usage: ARES_REF_FIRMWARE=<ares>/mia/Firmware/Nintendo\ 64 ares_ref <rom.z64> <script> [--load state.bin]
// Script lines (frames are emulated VI frames, counted from boot or from the loaded state):
//   hold <from> <to> <BUTTONS>   hold buttons (A, B, Z, START, L, R, UP, DOWN, LEFT, RIGHT, CU, CD, CL, CR; joined
//                                with +) for frames from..to inclusive
//   tap <frame> <BUTTONS>         hold for 2 frames
//   shot <frame> <file.ppm>       save the picture shown at that frame
//   every <n> <dir>               save a picture every n frames as <dir>/f_<frame>.ppm (for finding a moment)
//   save <frame> <state.bin>      save the emulator state
//   dump <frame> <file>           write RDRAM (8 MB, big-endian byte order) to a file
//   fill <from> <to> <addr> <bytes> <u16 hex>   before each of those frames, fill RDRAM [addr, addr+bytes) with a
//                                halfword (experiments: what does the RDP read from there?)
//   end <frame>                   stop
// Environment: ARES_REF_FLASH / ARES_REF_FLASH_OUT (Stadium FlashRAM in / out), ARES_REF_GBROM / ARES_REF_GBSAV /
// ARES_REF_GBSAV_OUT / ARES_REF_GBBOARD (Transfer Pak cartridge), ARES_REF_WAV=<file> (the N64 audio output (AI),
// resampled to 48 kHz, as raw signed 16-bit stereo little-endian PCM; <file>.frames gets the u32 sample-frame count
// written so far at the end of every VI frame, for lining it up with pictures).
// Battle cross-check (tools/battle_xcheck.py; needs ares with ares-host-hooks.patch, see build.sh):
//   ARES_REF_XCHECK=<file>          write the battle trace: forced random values and the battle state at turn starts and
//                                   around each action (Battle_ResolveMonAction), the same records the port writes with
//                                   PORT_XCHECK=<file> (src/port/xcheck.cpp)
//   ARES_REF_XCHECK_RNG=<seed>      replace the battle's random functions (Battle_Random, ...) with a seeded stream
//   ARES_REF_XCHECK_CASE=<file>     a case (parties, forced values per caller, turns): the port's PORT_XCHECK_CASE format
//   ARES_REF_XCHECK_SYMS=<file>     battle engine function starts ("<link address hex> <name>" lines, from the decomp
//                                   map) to name the caller of each random call, as the port does with dladdr
//   ARES_REF_XCHECK_COM=1           no input needed in battle: the player's move time limit runs out at the second query
//                                   of each decision (first usable move, first healthy replacement), as PORT_XCHECK_COM
//   ARES_REF_XCHECK_ANIM=<dir>      save each move animation's frames, from its start to the next battle event, one per
//                                   game frame, as <dir>/m<k>_<move>/f_<n>.ppm (n: battle game frames since the start;
//                                   at most ARES_REF_XCHECK_ANIM_FRAMES game frames, default 300)
// Trace lines carry gf=<n>: battle game frames (BattleScene_UpdateFrame calls), as the port's.
#include <ares/ares.hpp>
#include <n64/n64.hpp>
#include <nall/main.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

using namespace ares;

namespace {

// ---- boot helpers (from ares-bridge) ----
uint64_t ipl2checksum(uint32_t seed, const uint8_t* rom) {
    auto rotl = [](uint32_t v, uint32_t s) -> uint32_t { return (v << s) | (v >> ((-s) & 31)); };
    auto rotr = [](uint32_t v, uint32_t s) -> uint32_t { return (v >> s) | (v << ((-s) & 31)); };
    auto csum = [](uint32_t a0, uint32_t a1, uint32_t a2) -> uint32_t {
        if (a1 == 0) a1 = a2;
        uint64_t prod = (uint64_t)a0 * a1;
        uint32_t diff = (uint32_t)(prod >> 32) - (uint32_t)prod;
        return diff ? diff : a0;
    };
    size_t cursor = 0;
    auto readm = [&]() -> uint32_t {
        uint32_t v = rom[cursor] << 24 | rom[cursor + 1] << 16 | rom[cursor + 2] << 8 | rom[cursor + 3];
        cursor += 4;
        return v;
    };
    uint32_t init = 0x6c078965 * (seed & 0xff) + 1;
    uint32_t data = readm();
    init ^= data;
    uint32_t state[16];
    for (auto& s : state) s = init;
    uint32_t dataNext = data, dataLast, loop = 0;
    while (true) {
        loop++;
        dataLast = data;
        data = dataNext;
        state[0] += csum(1007 - loop, data, loop);
        state[1] = csum(state[1], data, loop);
        state[2] ^= data;
        state[3] += csum(data + 5, 0x6c078965, loop);
        state[9] = (dataLast < data) ? csum(state[9], data, loop) : state[9] + data;
        state[4] += rotr(data, dataLast & 0x1f);
        state[7] = csum(state[7], rotl(data, dataLast & 0x1f), loop);
        state[6] = (data < state[6]) ? (state[3] + state[6]) ^ (data + loop) : (state[4] + data) ^ state[6];
        state[5] += rotl(data, dataLast >> 27);
        state[8] = csum(state[8], rotr(data, dataLast >> 27), loop);
        if (loop == 1008) break;
        dataNext = readm();
        state[15] = csum(csum(state[15], rotl(data, dataLast >> 27), loop), rotl(dataNext, data >> 27), loop);
        state[14] = csum(csum(state[14], rotr(data, dataLast & 0x1f), loop), rotr(dataNext, data & 0x1f), loop);
        state[13] += rotr(data, data & 0x1f) + rotr(dataNext, dataNext & 0x1f);
        state[10] = csum(state[10] + data, dataNext, loop);
        state[11] = csum(state[11] ^ data, dataNext, loop);
        state[12] += state[8] ^ data;
    }
    uint32_t buf[4];
    for (auto& b : buf) b = state[0];
    for (loop = 0; loop < 16; loop++) {
        uint32_t d = state[loop];
        uint32_t tmp = buf[0] + rotr(d, d & 0x1f);
        buf[0] = tmp;
        buf[1] = d < tmp ? buf[1] + d : csum(buf[1], d, loop);
        uint32_t tb = (d & 2) >> 1, t2 = d & 1;
        buf[2] = tb == t2 ? buf[2] + d : csum(buf[2], d, loop);
        buf[3] = t2 == 1 ? buf[3] ^ d : csum(buf[3], d, loop);
    }
    uint64_t checksum = (uint64_t)csum(buf[0], buf[1], 16) << 32;
    checksum |= buf[3] ^ buf[2];
    return checksum & 0xffffffffffffull;
}

std::string detectCic(const uint8_t* ipl3) {
    switch (ipl2checksum(0x3f, ipl3)) {
        case 0x45cc73ee317aull: return "CIC-NUS-6101";
        case 0x44160ec5d9afull: return "CIC-NUS-7102";
        case 0xa536c0f1d859ull: return "CIC-NUS-6102";
    }
    if (ipl2checksum(0x78, ipl3) == 0x586fd4709867ull) return "CIC-NUS-6103";
    if (ipl2checksum(0x91, ipl3) == 0x8618a45bc2d3ull) return "CIC-NUS-6105";
    if (ipl2checksum(0x85, ipl3) == 0x2bbad4e6eb74ull) return "CIC-NUS-6106";
    return "CIC-NUS-6102";
}

// ---- input / video ----
std::atomic<uint32_t> gButtons{ 0 };
enum : uint32_t { bA = 1, bB = 2, bZ = 4, bSTART = 8, bL = 16, bR = 32, bUP = 64, bDOWN = 128, bLEFT = 256,
                  bRIGHT = 512, bCU = 1024, bCD = 2048, bCL = 4096, bCR = 8192 };
const std::map<std::string, uint32_t> kButtonNames = {
    { "A", bA }, { "B", bB }, { "Z", bZ }, { "START", bSTART }, { "L", bL }, { "R", bR }, { "UP", bUP },
    { "DOWN", bDOWN }, { "LEFT", bLEFT }, { "RIGHT", bRIGHT }, { "CU", bCU }, { "CD", bCD }, { "CL", bCL },
    { "CR", bCR } };
const std::map<std::string, uint32_t> kNodeNames = {
    { "A", bA }, { "B", bB }, { "Z", bZ }, { "Start", bSTART }, { "L", bL }, { "R", bR }, { "Up", bUP },
    { "Down", bDOWN }, { "Left", bLEFT }, { "Right", bRIGHT }, { "C-Up", bCU }, { "C-Down", bCD },
    { "C-Left", bCL }, { "C-Right", bCR } };

std::mutex gVideoMutex;
std::vector<uint32_t> gPicture;
uint32_t gPicW = 0, gPicH = 0;
std::atomic<uint64_t> gPictures{ 0 };

struct RefPlatform : ares::Platform {
    std::shared_ptr<vfs::directory> systemPak, cartridgePak, gbPak;
    auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
        if (!node) return {};
        string name = node->name();
        if (name == "Nintendo 64") return systemPak;
        if (name == "Game Boy Cartridge" || name == "Game Boy Color Cartridge") return gbPak;
        if (name.endsWith("Cartridge")) return cartridgePak;
        return {};
    }
    auto status(string_view message) -> void override { fprintf(stderr, "[ares] %s\n", string{ message }.data()); }
    auto video(Node::Video::Screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
        std::lock_guard<std::mutex> lock(gVideoMutex);
        gPicture.resize((size_t)width * height);
        for (u32 y = 0; y < height; y++) {
            memcpy(&gPicture[(size_t)y * width], (const uint8_t*)data + (size_t)y * pitch, width * 4);
        }
        gPicW = width;
        gPicH = height;
        gPictures++;
    }
    FILE* wav = nullptr;
    uint64_t wavFrames = 0;
    auto audio(Node::Audio::Stream stream) -> void override {
        // the N64 has one stream (AI); drain it whenever samples are pending
        while (stream->pending()) {
            f64 s[2] = { 0, 0 };
            stream->read(s);
            if (!wav) continue;
            int16_t out[2];
            for (int c = 0; c < 2; c++) {
                f64 v = s[c] * 32767.0;
                out[c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            fwrite(out, 2, 2, wav);
            wavFrames++;
        }
    }
    auto input(Node::Input::Input node) -> void override {
        // only controller port 1's gamepad is driven
        if (auto button = node->cast<Node::Input::Button>()) {
            auto parent = node->parent().lock();
            auto port = parent ? parent->parent().lock() : nullptr;
            if (!port || string{ port->name() } != "Controller Port 1") return;
            auto it = kNodeNames.find(std::string{ string{ node->name() }.data() });
            button->setValue(it != kNodeNames.end() && (gButtons.load() & it->second));
        } else if (auto axis = node->cast<Node::Input::Axis>()) {
            axis->setValue(0);
        }
    }
} gPlatform;

bool savePicture(const std::string& path) {
    std::lock_guard<std::mutex> lock(gVideoMutex);
    if (gPicture.empty()) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", gPicW, gPicH);
    for (uint32_t p : gPicture) {
        uint8_t rgb[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

uint32_t parseButtons(const std::string& s) {
    uint32_t v = 0;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '+')) {
        auto it = kButtonNames.find(tok);
        if (it == kButtonNames.end()) {
            fprintf(stderr, "unknown button '%s'\n", tok.c_str());
            exit(2);
        }
        v |= it->second;
    }
    return v;
}


// ---- battle cross-check (ARES_REF_XCHECK*) ----
// The battle engine is fragment 51 (link address 0x84300000); the game relocates it to wherever the pool put it and
// keeps that address in gFragments[51] (memmap.c), so hooks move with it. Addresses: the decomp's
// build/pokestadium-us.map and linker_scripts/us/symbol_addrs.txt.
namespace xcheck {
constexpr u32 kFragmentTable = 0x800A58F0; // gFragments[240] {vaddr, size}
constexpr u32 kFragmentId = 51, kLinkBase = 0x84300000;
constexpr u32 kActivePokemon = 0x84390010;  // sActivePokemon[2]
constexpr u32 kMessageQueues = 0x843901A0;  // gBattleMessageQueues
constexpr u32 kBattleScene = 0x84390240;    // sBattleScene (first word: BattleScene*)
constexpr u32 kBattleDamage = 0x843C4DC4;   // gBattleDamage (u16)
constexpr u32 kSetFragmentMap = 0x800020B0; // Memmap_SetFragmentMap(id, vaddr, size): main code, not relocated
constexpr u32 kGuRandom = 0x8005D1B0;       // guRandom (main code): its callers are traced with ARES_REF_XCHECK_GURANDOM
constexpr u32 kGuRandomSeed = 0x8007A950;   // guRandom's xseed (lui 0x8008 / lw -0x56B0 in the ROM's guRandom)
constexpr u32 kParticleMathId = 4, kParticleMathLink = 0x81400000; // particle_math fragment
constexpr u32 kParticleFrameCounter = 0x8140E6B8; // gParticleFrameCounter (link address)
constexpr u32 kGeoFrameCounter = 0x8006F084; // D_8006F084 (u16, main code): GeoRender_AdvanceFrameCounter's scene frame counter
constexpr u32 kModelTexCounter = 0x800AF7AE; // D_800AF7AE (s16, main code): one per Pokemon model drawn (model texture frames)
constexpr u32 kPlayCommand = 0x800367A0;     // Audio_PlayCommand (main code): traced with ARES_REF_XCHECK_SFX
constexpr u32 kPlaySfxById = 0x80048B90;     // Audio_PlaySoundEffectById (main code): the same
constexpr u32 kOsGetTime = 0x8005F150;       // osGetTime (main code): forced for the announcer's callers (below)
constexpr u32 kOsTimeStream = 0x40;          // its stream (Port_XcheckOsGetTime)
constexpr u32 kPlaySoundEffect = 0x80039024; // Audio_PlaySoundEffect (main code, every sound effect): the same
constexpr u32 kQueueSequence = 0x80041C70;   // Audio_QueueSequenceId (main code, the announcer's lines): the same
constexpr u32 kStartBattleMusic = 0x8004153C; // Audio_StartBattleMusicSequence (main code): skipped for "nomusic"
enum Fn : int { BattleRandom, BattleRandomAccuracy, AiRandomBelow, AnimRandomRange, AnimRandomWarmup, kRandomFns,
                AiSeed = kRandomFns, FragmentMap, BattleMusic, GuRandom, GameFrame, StartTurn, ResolveEnter, TimerExpired, MoveAnim, MoveAnimMid, SixSlotTrailUpdateSlot, ResolveExit, Probe, PlayCommand, PlaySfxById, QueueSequence, OsGetTime, PlaySoundEffect };
// names shared with the port's trace (src/port/xcheck.cpp)
const char* const kRandomNames[kRandomFns] = { "Battle_Random", "Battle_RandomAccuracy", "BattleAI_RandomBelowInclusive",
                                               "BattleAnim_RandomRange", "BattleAnim_RandomIntWithWarmup" };
struct Hooked { u32 link; Fn fn; };
const Hooked kHooked[] = {
    { 0x8436F6D8, BattleRandom }, { 0x8436F788, BattleRandomAccuracy }, { 0x843767E4, AiRandomBelow },
    { 0x8430C414, AnimRandomRange }, { 0x8430C384, AnimRandomWarmup }, { 0x843768D8, AiSeed },
    { 0x84320108, StartTurn }, { 0x8437345C, ResolveEnter }, { 0x8430D124, TimerExpired },
    { 0x84302A78, MoveAnim },    // BattleScene_EnterRow3StartMoveAnim
    { 0x843033E0, MoveAnimMid }, // BattleScene_EnterRow19StartMoveAnimMidFrame
    { 0x84300E88, GameFrame },   // BattleScene_UpdateFrame: once per battle game frame
    { 0x8436DAC4, SixSlotTrailUpdateSlot }, // BattleAnim_SixSlotTrail_UpdateSlot: reads a stack word it never writes
};

bool gOn = false, gForce = false, gCom = false, gStop = false;
u32 gSeed = 0;
FILE* gOut = nullptr;
// a case (src/port/xcheck.cpp has the format): BattleMon fields to write, forced values, turns to play
struct MonField { const char* name; u32 offset, size; };
const MonField kMonFields[] = { { "species", 0x00, 1 }, { "hp", 0x02, 2 },   { "status", 0x05, 1 }, { "type1", 0x06, 1 },
                                { "type2", 0x07, 1 },   { "m1", 0x09, 1 },   { "m2", 0x0A, 1 },     { "m3", 0x0B, 1 },
                                { "m4", 0x0C, 1 },      { "p1", 0x20, 1 },   { "p2", 0x21, 1 },     { "p3", 0x22, 1 },
                                { "p4", 0x23, 1 },      { "level", 0x24, 1 }, { "maxhp", 0x26, 2 }, { "atk", 0x28, 2 },
                                { "def", 0x2A, 2 },     { "spd", 0x2C, 2 },  { "spc", 0x2E, 2 } };
struct MonLine { int side, slot; std::vector<std::pair<const MonField*, u32>> set; };
struct Rule { int fn; std::string caller; std::vector<u32> values; size_t next = 0; };
std::vector<MonLine> gMons;
std::vector<Rule> gRules;
int gStopTurns = 0, gTurns = 0;
bool gNoMusic = false, gTraceGuRandom = false, gTraceSfx = false;
u32 gOsTimeCalls = 0; // the OsGetTime hook's index (Port_XcheckOsGetTime)
// ARES_REF_XCHECK_PROBE=<link>:<sp offset>,...: at those battle engine function entries, log the stack word at
// sp+offset, or register offset-1000 ("P <battle game frame> <link> <value>"; e.g. a variable the function reads
// before it writes it)
std::vector<std::pair<u32, s32>> gProbes;
std::vector<std::pair<u32, std::string>> gSyms; // battle engine function starts (link addresses), sorted
constexpr u32 kSessionContext = 0x800AE540;    // gSessionContext: battleSides at 0x1194 (0x28 each)
std::map<u32, u32> gCalls; // per stream: the logic functions one each, the animation ones one per argument
u32 gBase = 0;                 // gFragments[51].vaddr the hooks were placed for
std::map<u32, Fn> gAt;         // runtime address -> function
u64 gFrame = 0;
u64 gGameFrame = 0, gSavedGameFrame = ~0ull; // battle game frames so far; the last one captured
std::string gAnimDir;
int gAnimFrames = 300, gAnimLeft = 0, gAnimCount = 0;
std::string gAnimCur;
u64 gAnimStart = 0;
u32 gEvents = 0;

auto& ram() { return Nintendo64::rdram.ram; }
u32 phys(u32 vaddr) { return vaddr & 0x1FFFFFFF; }
// a word as the CPU sees it: from its data cache when the line is there (the newest writes sit in dirty lines), else
// from RDRAM
u32 rd32(u32 vaddr) {
    u32 p = phys(vaddr) & ~3u;
    if (vaddr >= 0x80000000u && vaddr < 0xA0000000u) { // cached segment (KSEG0)
        auto& line = Nintendo64::cpu.dcache.line(vaddr);
        if (line.valid() && (line.tagKey & ~1u) == (p & ~0xFFFu)) return line.words[p >> 2 & 3];
    }
    if (p + 4 > ram().size) return 0;
    return ram().Nintendo64::Memory::Writable::read<Nintendo64::Word>(p);
}
u8 rd8(u32 vaddr) { return (u8)(rd32(vaddr) >> ((3 - (vaddr & 3)) * 8)); }
// a byte as the CPU would store it: into its data cache line when the line is there, and into RDRAM
void wr8(u32 vaddr, u8 v) {
    u32 p = phys(vaddr) & ~3u, sh = (3 - (vaddr & 3)) * 8;
    if (p + 4 > ram().size) return;
    if (vaddr >= 0x80000000u && vaddr < 0xA0000000u) {
        auto& line = Nintendo64::cpu.dcache.line(vaddr);
        if (line.valid() && (line.tagKey & ~1u) == (p & ~0xFFFu)) {
            u32& w = line.words[p >> 2 & 3];
            w = (w & ~(0xFFu << sh)) | ((u32)v << sh);
        }
    }
    u32 w = ram().Nintendo64::Memory::Writable::read<Nintendo64::Word>(p);
    w = (w & ~(0xFFu << sh)) | ((u32)v << sh);
    ram().Nintendo64::Memory::Writable::write<Nintendo64::Word>(p, w);
}
std::string hexBytes(u32 vaddr, u32 n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (u32 i = 0; i < n; i++) {
        u8 b = rd8(vaddr + i);
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}
u32 rt(u32 link) { return gBase + (link - kLinkBase); } // link address -> runtime address

// the forced value stream: splitmix64 of (seed, function, call index), identical in src/port/xcheck.cpp
u32 streamValue(u32 seed, u32 fn, u32 k) {
    u64 z = ((u64)seed << 32) ^ ((u64)fn << 24) ^ k;
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (u32)(z ^ (z >> 31));
}
// the stream a call draws from: the animation functions are polled per frame (e.g. a 1-in-50 idle animation check),
// so their streams are kept per argument and a frame of timing difference cannot shift the effects' draws
u32 streamKey(int fn, s32 arg) { return fn >= AnimRandomRange ? (u32)fn << 16 | ((u32)arg & 0xFFFF) : (u32)fn << 16; }

// the function a return address lies in (from ARES_REF_XCHECK_SYMS): in a loaded fragment its link address is
// ra - base + 0x80000000 + (id + 0x10) << 20 (FRAGMENT_ID in the decomp's macros.h), main code is linked as it runs
std::string callerName(u32 ra) {
    u32 link = ra;
    for (u32 id = 0; id < 240; id++) {
        u32 base = rd32(kFragmentTable + id * 8), size = rd32(kFragmentTable + id * 8 + 4);
        if (base >= 0x80000000u && ra >= base && ra < base + size) {
            link = ra - base + (0x80000000u | ((id + 0x10) << 20));
            break;
        }
    }
    auto it = std::upper_bound(gSyms.begin(), gSyms.end(), std::make_pair(link, std::string{ "\x7f" }));
    return it == gSyms.begin() ? "?" : std::prev(it)->second;
}

u32 nextValue(int fn, s32 arg, const std::string& from, u32& k) {
    u32 key = streamKey(fn, arg);
    k = gCalls[key]++;
    u32 v = streamValue(gSeed, key, k);
    Rule* rule = nullptr; // a rule for this caller wins over one for every caller
    for (Rule& r : gRules) {
        if (r.fn == fn && (r.caller == from || (r.caller.empty() && !rule))) rule = &r;
    }
    if (rule) {
        v = rule->values[rule->next];
        rule->next = (rule->next + 1) % rule->values.size();
    }
    return v;
}

// the case's party fields, written when the battle engine is mapped (as Port_XcheckApplyCaseParties): both sides'
// picked Pokemon (TeamRoster 0x1C, 0x54 each) and their roster info copies (TeamRoster.rosterInfo 0x214 -> 0x28)
void applyParties() {
    for (int side = 0; side < 2; side++) {
        u32 roster = rd32(kSessionContext + 0x1194 + side * 0x28 + 0x08);
        if (!roster) continue;
        u32 info = rd32(roster + 0x214);
        for (int i = 0; i < 6; i++) {
            for (u32 mon : { roster + 0x1C + i * 0x54, info ? info + 0x28 + i * 0x54 : 0u }) {
                if (!mon) continue;
                for (auto& m : gMons) {
                    if (m.side != side || (m.slot >= 0 && m.slot != i)) continue;
                    for (auto& [f, v] : m.set) {
                        if (f->size == 1) {
                            wr8(mon + f->offset, (u8)v);
                        } else { // big-endian u16
                            wr8(mon + f->offset, (u8)(v >> 8));
                            wr8(mon + f->offset + 1, (u8)v);
                        }
                    }
                }
            }
        }
    }
    fprintf(stderr, "[xcheck] case parties written\n");
}

void endCapture() { gAnimLeft = 0; }

void event(const char* name) {
    u32 scene = rd32(rt(kBattleScene));
    u32 ap0 = rd32(rt(kActivePokemon)), ap1 = rd32(rt(kActivePokemon) + 4);
    u32 mq = rd32(rt(kMessageQueues));
    if (gStop) return; // (the rest of the stopping frame)
    endCapture(); // a move animation is captured up to the next event
    if (!strcmp(name, "turn") && gStopTurns > 0 && gTurns++ >= gStopTurns) {
        if (gOut) fprintf(gOut, "S turns=%d frame=%llu gf=%llu\n", gStopTurns, (unsigned long long)gFrame,
                          (unsigned long long)gGameFrame);
        if (gOut) fflush(gOut);
        gStop = true;
        return;
    }
    if (!gOut || !ap0 || !ap1 || !scene) return;
    // ActivePokemon.state (0x654): ActivePokemonState up to its Controller* (0xC8); BattleScene up to its CameraNode*;
    // the message queues' byte arrays (0x48..0x4C8)
    fprintf(gOut, "E %u %s frame=%llu gf=%llu st0=%s st1=%s scene=%s dmg=%u mq=%s\n", gEvents++, name,
            (unsigned long long)gFrame, (unsigned long long)gGameFrame,
            hexBytes(ap0 + 0x654, 0xC8).c_str(), hexBytes(ap1 + 0x654, 0xC8).c_str(), hexBytes(scene, 0xDC).c_str(),
            (unsigned)(rd32(rt(kBattleDamage)) >> 16), mq ? hexBytes(mq + 0x48, 0x480).c_str() : "");
    fflush(gOut);
}

void place(u32 base);

bool returnWith(u64 value) {
    auto& cpu = Nintendo64::cpu;
    cpu.ipu.r[2].u64 = value;
    cpu.pipeline.setPc(cpu.ipu.r[31].u64);
    return true;
}

bool hook(u32 address) {
    auto it = gAt.find(address);
    if (it == gAt.end()) return false;
    auto& cpu = Nintendo64::cpu;
    Fn fn = it->second;
    if (fn < kRandomFns) {
        if (!gForce) return false;
        s32 a = 0;
        if (fn == AiRandomBelow || fn == AnimRandomRange) a = (s32)cpu.ipu.r[4].u32;
        if (fn == AnimRandomWarmup) a = (s32)cpu.fpu.r[12].f32;
        std::string from = callerName((u32)cpu.ipu.r[31].u64);
        u32 k, v = nextValue(fn, a, from, k), out = 0;
        switch (fn) {
        case BattleRandom: case BattleRandomAccuracy: out = v & 0xFF; break;
        case AiRandomBelow: out = a <= 0 ? 0 : v % (u32)(a + 1); break;
        case AnimRandomRange: out = a <= 0 ? 0 : (u16)(v % (u32)a); break;
        case AnimRandomWarmup: out = a <= 0 ? 0 : v % (u32)a; break;
        default: break;
        }
        if (gOut) {
            fprintf(gOut, "R %s %u %d %u frame=%llu gf=%llu caller=%s\n", kRandomNames[fn], k, (int)a, out,
                    (unsigned long long)gFrame, (unsigned long long)gGameFrame, from.c_str());
            fflush(gOut);
        }
        return returnWith((u64)(s64)(s32)out);
    }
    switch (fn) {
    case FragmentMap: // the battle engine was just loaded (and is about to be relocated): hook it before it runs
        if (cpu.ipu.r[4].u32 == kFragmentId && cpu.ipu.r[5].u32 != gBase) place(cpu.ipu.r[5].u32);
        return false;
    case AiSeed: return gForce ? returnWith(0) : false;
    case BattleMusic: return returnWith(0); // (void) the battle music is not started
    case GuRandom:
        if (gAnimLeft > 0 && gOut) fprintf(gOut, "G %llu %s\n", (unsigned long long)(gGameFrame - gAnimStart),
                                           callerName((u32)cpu.ipu.r[31].u64).c_str());
        return false;
    case PlaySoundEffect: // a0, a1 banks, a2 id, a3 priority, volume scale on the stack (5th word)
        if (gOut && gTraceSfx) fprintf(gOut, "U %llu 3 %x %d %d\n", (unsigned long long)gGameFrame, cpu.ipu.r[6].u32,
                                       (s32)cpu.ipu.r[7].u32, (s32)rd32((u32)cpu.ipu.r[29].u64 + 20));
        return false;
    case OsGetTime: { // the announcer's line choices (as Port_XcheckOsGetTime): the stream value of the call's index
        // (from the battle's first game frame and each animation start) instead of CPU time, for the functions that use it to choose (3D140_2.c and one pitch
        // variation in audio_commands_category2.c); every other caller gets the real time
        static const u32 kChoosers[][2] = { // [start, end) in main code (decomp symbol_addrs, US)
            { 0x8003DB84, 0x8003EB40 }, { 0x8003EB40, 0x8003F1AC }, { 0x8003F1AC, 0x8003F4C0 }, // func_8003DB84 .. F1AC
            { 0x8003F4C0, 0x8003F624 },                                                         // func_8003F4C0
            { 0x80041EF4, 0x800420C0 },                                    // Audio_SelectSoundAvoidingRecent
            { 0x8004E304, 0x8004E810 } };                                  // Audio_PlayCategory12SoundCommand
        u32 ra = (u32)cpu.ipu.r[31].u64;
        bool chooser = false;
        for (auto& r : kChoosers) chooser |= ra >= r[0] && ra < r[1];
        if (!chooser) return false;
        u32 v = streamValue(gSeed, kOsTimeStream, gOsTimeCalls++);
        cpu.ipu.r[3].u64 = (u64)(s64)(s32)v; // OSTime (u64) comes back in v0 (high word) and v1 (low word)
        return returnWith(0);
    }
    case PlayCommand: case PlaySfxById: case QueueSequence: // "U <battle game frame> <kind> <args>", as Port_XcheckSfx
        if (gOut && gTraceSfx) fprintf(gOut, "U %llu %d %x %d %d\n", (unsigned long long)gGameFrame, fn == PlayCommand ? 0 : fn == PlaySfxById ? 1 : 2,
                          cpu.ipu.r[4].u32, fn == PlayCommand ? (s32)cpu.ipu.r[5].u32 : 0,
                          fn == PlayCommand ? (s32)cpu.ipu.r[6].u32 : 0);
        if (fn == QueueSequence && gNoMusic) return returnWith(0); // (void) no announcer with "nomusic" (as the port)
        return false;
    case SixSlotTrailUpdateSlot: // its spA8 (lw s7,168(sp) after addiu sp,-176) is never set: the N64 reads what an
        // earlier call left there; with forced randomness 0 on both sides, as the port initializes it
        if (gForce)
            for (int i = 0; i < 4; i++) wr8((u32)cpu.ipu.r[29].u64 - 8 + i, 0);
        return false;
    case Probe:
        for (auto& [link, off] : gProbes)
            if (rt(link) == address && gOut)
                fprintf(gOut, "P %llu %08X %d\n", (unsigned long long)gGameFrame, link,
                        off >= 1000 ? (s32)cpu.ipu.r[off - 1000].u32 : (s32)rd32((u32)cpu.ipu.r[29].u64 + off));
        return false;
    case StartTurn: event("turn"); return false;
    case ResolveEnter: {
        event("resolve");
        u32 ra = (u32)cpu.ipu.r[31].u64;
        if (!gAt.count(ra)) { // the call site's return address: the action's result, once it is resolved
            gAt[ra] = ResolveExit;
            auto& v = cpu.hostHooks.addresses;
            v.insert(std::upper_bound(v.begin(), v.end(), ra), ra);
            cpu.recompiler.reset();
        }
        return false;
    }
    case ResolveExit: event("resolved"); return false;
    case GameFrame: // "V <battle game frame> <VI frame>" (as Port_XcheckGameFrame): the N64 can take 3 VI frames for one in
        // busy effects, and the sound check maps each side's sound onto battle game frames with these
        gGameFrame++;
        if (gGameFrame == 1) gOsTimeCalls = 0;
        if (gOut) fprintf(gOut, "V %llu %llu\n", (unsigned long long)gGameFrame, (unsigned long long)gFrame);
        return false;
    case TimerExpired: { // BattleScene_IsActionSelectionTimerExpired: "not yet", then "expired" (as the port)
        if (!gCom) return false;
        static bool asked = false;
        asked = !asked;
        return returnWith(asked ? 0 : 1);
    }
    case MoveAnim: case MoveAnimMid: {
        u32 pm = rd32(kFragmentTable + kParticleMathId * 8); // gParticleFrameCounter, in the relocated particle_math
        u32 pfcAddr = pm ? pm + (kParticleFrameCounter - kParticleMathLink) : 0, pfc = pfcAddr ? rd32(pfcAddr) : 0;
        u32 gfc = (u32)rd8(kGeoFrameCounter) << 8 | rd8(kGeoFrameCounter + 1);
        s16 mtc = (s16)((u32)rd8(kModelTexCounter) << 8 | rd8(kModelTexCounter + 1));
        if (gForce) { // guRandom (the effects' particles) from the same state on both sides (Port_GuRandomSeed)
            for (int i = 0; i < 4; i++) wr8(kGuRandomSeed + i, (u8)(174823885u >> (24 - 8 * i)));
            // and the effects' frame counter (as Port_XcheckMoveAnim)
            if (pfcAddr) for (int i = 0; i < 4; i++) wr8(pfcAddr + i, 0);
            gOsTimeCalls = 0; // (OsGetTime)
            // and the model texture and scene frame counters (as Port_XcheckMoveAnim)
            for (u32 a : {kModelTexCounter, kGeoFrameCounter}) wr8(a, 0), wr8(a + 1, 0);
            // and the animation streams from their start (as Port_XcheckMoveAnim)
            for (auto it = gCalls.begin(); it != gCalls.end();) it = (it->first >> 16) >= AnimRandomRange ? gCalls.erase(it) : std::next(it);
        }
        u32 ap = (u32)cpu.ipu.r[4].u64; // ActivePokemon* of the attacker
        u8 move = ap ? rd8(ap + 0x654 + 0x38 + 0x5A) : 0;
        if (gOut) {
            fprintf(gOut, "A %d move=%u side=%d frame=%llu gf=%llu pfc=%u gfc=%u mtc=%d\n", gAnimCount, move,
                    ap == rd32(rt(kActivePokemon)) ? 0 : 1, (unsigned long long)gFrame, (unsigned long long)gGameFrame, pfc, gfc,
                    (int)mtc);
            fflush(gOut);
        }
        if (!gAnimDir.empty()) {
            char name[64];
            snprintf(name, sizeof(name), "/m%02d_%03u", gAnimCount, move);
            gAnimCur = gAnimDir + name;
#ifdef _WIN32
            _mkdir(gAnimCur.c_str());
#else
            mkdir(gAnimCur.c_str(), 0755);
#endif
            gAnimLeft = gAnimFrames;
            gAnimStart = gGameFrame;
            gSavedGameFrame = ~0ull;
        }
        gAnimCount++;
        return false;
    }
    default: return false;
    }
}

// the battle engine's load address from gFragments[51], if the fragment is really there (its header has the
// "FRAGMENT" magic at +8; right after a state load the table can read as garbage)
u32 battleEngineBase() {
    u32 base = rd32(kFragmentTable + kFragmentId * 8);
    if (base < 0x80000000u || base >= 0x80800000u || rd32(base + 8) != 0x46524147u) return 0;
    return base;
}

void place(u32 base) {
    auto& cpu = Nintendo64::cpu;
    bool loaded = base && base != gBase;
    gBase = base;
    gAt.clear();
    cpu.hostHooks.addresses.clear();
    gAt[kSetFragmentMap] = FragmentMap;
    if (gNoMusic) gAt[kStartBattleMusic] = BattleMusic;
    if (gTraceGuRandom) gAt[kGuRandom] = GuRandom;
    if (gForce) gAt[kOsGetTime] = OsGetTime;
    if (gTraceSfx) gAt[kPlayCommand] = PlayCommand, gAt[kPlaySfxById] = PlaySfxById, gAt[kPlaySoundEffect] = PlaySoundEffect;
    if (gTraceSfx || gNoMusic) gAt[kQueueSequence] = QueueSequence; // (traced, and silent with "nomusic")
    if (base) {
        for (auto& h : kHooked) gAt[rt(h.link)] = h.fn;
        for (auto& pr : gProbes) gAt.emplace(rt(pr.first), Probe); // (not over the hooks above)
    }
    for (auto& [a, f] : gAt) cpu.hostHooks.addresses.push_back(a); // (sorted: std::map order)
    cpu.recompiler.reset();
    fprintf(stderr, "[xcheck] battle engine at %08x (%zu hooks)\n", base, gAt.size());
    if (gOut) fprintf(gOut, "F base=%08x frame=%llu\n", base, (unsigned long long)gFrame);
    // the case's parties, before the engine copies them into the battle (its init runs frames after the load)
    if (loaded && !gMons.empty()) applyParties();
}

void readCase(const char* path) {
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
                auto eq = kv.find('=');
                for (const MonField& f : kMonFields) {
                    if (kv.substr(0, eq) == f.name) m.set.push_back({ &f, (u32)strtoul(kv.c_str() + eq + 1, nullptr, 0) });
                }
            }
            gMons.push_back(m);
        } else if (cmd == "rule") {
            Rule r;
            std::string target, values, v;
            ss >> target >> values;
            auto at = target.find('@');
            std::string fn = target.substr(0, at);
            r.caller = at == std::string::npos ? "" : target.substr(at + 1);
            r.fn = -1;
            for (int i = 0; i < kRandomFns; i++) if (fn == kRandomNames[i]) r.fn = i;
            std::stringstream vs(values);
            while (std::getline(vs, v, ',')) r.values.push_back((u32)strtoul(v.c_str(), nullptr, 0));
            if (r.fn >= 0 && !r.values.empty()) gRules.push_back(r);
        } else if (cmd == "stop") {
            ss >> gStopTurns;
        } else if (cmd == "nomusic") {
            gNoMusic = true;
        }
    }
}

void init() {
    const char* out = getenv("ARES_REF_XCHECK");
    const char* rng = getenv("ARES_REF_XCHECK_RNG");
    gCom = getenv("ARES_REF_XCHECK_COM") != nullptr;
    gTraceGuRandom = getenv("ARES_REF_XCHECK_GURANDOM") != nullptr;
    gTraceSfx = getenv("ARES_REF_XCHECK_SFX") != nullptr;
    if (const char* pr = getenv("ARES_REF_XCHECK_PROBE")) {
        for (const char* q = pr; *q;) {
            char* e;
            u32 link = (u32)strtoul(q, &e, 16);
            s32 off = *e == ':' ? (s32)strtol(e + 1, &e, 0) : 0;
            gProbes.push_back({ link, off });
            if (*e != ',') break;
            q = e + 1;
        }
    }
    if (const char* a = getenv("ARES_REF_XCHECK_ANIM")) gAnimDir = a;
    if (const char* n = getenv("ARES_REF_XCHECK_ANIM_FRAMES")) gAnimFrames = atoi(n);
    const char* cs = getenv("ARES_REF_XCHECK_CASE");
    gOn = out || rng || gCom || !gAnimDir.empty() || cs;
    if (!gOn) return;
    if (out) gOut = fopen(out, "w");
    if (rng) {
        gForce = true;
        gSeed = (u32)strtoul(rng, nullptr, 0);
    }
    if (cs) readCase(cs);
    if (const char* sy = getenv("ARES_REF_XCHECK_SYMS")) {
        std::ifstream in(sy);
        std::string line, a, n;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            if (line.empty() || line[0] == '#' || !(ls >> a >> n)) continue;
            gSyms.push_back({ (u32)std::stoul(a, nullptr, 16), n });
        }
        std::sort(gSyms.begin(), gSyms.end());
    }
    Nintendo64::cpu.hostHooks.callback = hook;
    place(battleEngineBase());
    fprintf(stderr, "[xcheck] trace %s, rng %s, com %d\n", out ? out : "-", rng ? rng : "natural", gCom);
}

// once per frame: follow the battle engine fragment (loaded / moved / released)
void frame(u64 n) {
    if (!gOn) return;
    gFrame = n;
    u32 base = battleEngineBase();
    if (base != gBase) place(base);
}

// the picture of the current move animation, once per game frame (the first VI frame that ends in a new one)
void afterFrame() {
    if (gAnimLeft <= 0 || gGameFrame == gSavedGameFrame) return;
    gSavedGameFrame = gGameFrame;
    char name[64];
    snprintf(name, sizeof(name), "/f_%03llu.ppm", (unsigned long long)(gGameFrame - gAnimStart));
    gAnimLeft--;
    // 160x120, box-averaged (as the port writes them)
    std::lock_guard<std::mutex> lock(gVideoMutex);
    if (gPicture.empty()) return;
    const u32 ow = 160, oh = 120;
    std::vector<uint8_t> out(ow * oh * 3);
    for (u32 y = 0; y < oh; y++) {
        u32 ya = y * gPicH / oh, yb = std::max(ya + 1, (y + 1) * gPicH / oh);
        for (u32 x = 0; x < ow; x++) {
            u32 xa = x * gPicW / ow, xb = std::max(xa + 1, (x + 1) * gPicW / ow), sum[3] = { 0, 0, 0 }, n = 0;
            for (u32 yy = ya; yy < yb; yy++) {
                for (u32 xx = xa; xx < xb; xx++) {
                    u32 p = gPicture[(size_t)yy * gPicW + xx];
                    sum[0] += (p >> 16) & 255, sum[1] += (p >> 8) & 255, sum[2] += p & 255;
                    n++;
                }
            }
            for (int c = 0; c < 3; c++) out[(y * ow + x) * 3 + c] = (uint8_t)(sum[c] / n);
        }
    }
    if (FILE* f = fopen((gAnimCur + name).c_str(), "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", ow, oh);
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
    }
}
} // namespace xcheck

struct Hold { uint64_t from, to; uint32_t buttons; };
struct Fill { uint64_t from, to; uint32_t addr, bytes; uint16_t value; };
struct Action { std::string kind, arg; uint64_t n; };

} // namespace

static int realMain(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: ares_ref <rom.z64> <script> [--load state.bin]\n");
        return 2;
    }
    std::string loadState, firmwareDir;
    for (int i = 3; i + 1 < argc; i++) {
        if (std::string{ argv[i] } == "--load") loadState = argv[i + 1];
    }
    // (an environment variable: nall rewrites path arguments)
    if (const char* fw = getenv("ARES_REF_FIRMWARE")) firmwareDir = fw;

    std::vector<Hold> holds;
    std::vector<Fill> fills;
    std::multimap<uint64_t, Action> actions;
    uint64_t endFrame = 0, everyN = 0;
    std::string everyDir;
    {
        std::ifstream in(argv[2]);
        std::string line;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::string cmd;
            if (!(ss >> cmd) || cmd[0] == '#') continue;
            if (cmd == "hold") { Hold h; std::string b; ss >> h.from >> h.to >> b; h.buttons = parseButtons(b); holds.push_back(h); }
            else if (cmd == "tap") { Hold h; std::string b; ss >> h.from >> b; h.to = h.from + 1; h.buttons = parseButtons(b); holds.push_back(h); }
            else if (cmd == "shot" || cmd == "save" || cmd == "dump") { Action a; a.kind = cmd; ss >> a.n >> a.arg; actions.insert({ a.n, a }); }
            else if (cmd == "fill") { Fill f; std::string a, v; ss >> f.from >> f.to >> a >> f.bytes >> v; f.addr = std::stoul(a, nullptr, 16) & 0x7fffff; f.value = (uint16_t)std::stoul(v, nullptr, 16); fills.push_back(f); }
            else if (cmd == "every") { ss >> everyN >> everyDir; }
            else if (cmd == "end") { ss >> endFrame; }
            else { fprintf(stderr, "bad script line: %s\n", line.c_str()); return 2; }
        }
    }

    std::ifstream romIn(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(romIn)), std::istreambuf_iterator<char>());
    if (rom.size() < 0x1000 || rom[0] != 0x80) {
        fprintf(stderr, "need a big-endian .z64 ROM\n");
        return 2;
    }

    ares::platform = &gPlatform;
    gPlatform.systemPak = std::make_shared<vfs::directory>();
    // PIF boot firmware (ares ships it in mia/Firmware/Nintendo 64); without it the game never starts
    for (const char* name : { "pif.ntsc.rom", "pif.pal.rom", "pif.sm5.rom" }) {
        std::ifstream fin(firmwareDir + "/" + name, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            fprintf(stderr, "missing firmware %s/%s (set ARES_REF_FIRMWARE=<ares>/mia/Firmware/Nintendo 64)\n", firmwareDir.c_str(), name);
            return 2;
        }
        gPlatform.systemPak->append(name, bytes);
    }
    auto cart = std::make_shared<vfs::directory>();
    std::string title((const char*)&rom[0x20], 20);
    while (!title.empty() && (title.back() == ' ' || title.back() == 0)) title.pop_back();
    cart->setAttribute("title", string{ title.c_str() });
    cart->setAttribute("region", string{ "NTSC" });
    cart->setAttribute("cic", string{ detectCic(&rom[0x40]).c_str() });
    cart->append("program.rom", rom);
    // ARES_REF_FLASH=<file>: the cartridge's 128 KiB FlashRAM (N64 byte order, as tools/save_editor writes it)
    if (const char* fl = getenv("ARES_REF_FLASH")) {
        std::ifstream fin(fl, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        if (bytes.size() != 128 * 1024) {
            fprintf(stderr, "[ref] flash image %s must be 131072 bytes (got %zu)\n", fl, bytes.size());
            return 2;
        }
        cart->append("save.flash", bytes);
        fprintf(stderr, "[ref] flash <- %s\n", fl);
    }
    gPlatform.cartridgePak = cart;

    Nintendo64::option("Enable GPU acceleration", "true");
    Nintendo64::option("Deterministic Entropy", "true");
    Nintendo64::option("Expansion Pak", "true");
    Nintendo64::option("Quality", "SD");
    Nintendo64::option("Recompiler", "true");
    Node::System root;
    if (!Nintendo64::load(root, "[Nintendo] Nintendo 64 (NTSC)")) {
        fprintf(stderr, "Nintendo64::load failed\n");
        return 1;
    }
    if (auto port = root->find<Node::Port>("Cartridge Slot")) { port->allocate(); port->connect(); }
    if (auto port = root->find<Node::Port>("Controller Port 1")) { port->allocate("Gamepad"); port->connect(); }
    // ARES_REF_GBROM=<file.gb> (+ ARES_REF_GBSAV=<file.sav>, ARES_REF_GBBOARD=MBC5): a Transfer Pak with that cartridge
    // in controller 1 (needs ares built with the Game Boy core: ARES_CORES="n64;gb")
    if (const char* gbrom = getenv("ARES_REF_GBROM")) {
        std::ifstream gin(gbrom, std::ios::binary);
        std::vector<uint8_t> gb((std::istreambuf_iterator<char>(gin)), std::istreambuf_iterator<char>());
        gPlatform.gbPak = std::make_shared<vfs::directory>();
        std::string gtitle(gb.size() > 0x144 ? (const char*)&gb[0x134] : "", gb.size() > 0x144 ? 15 : 0);
        gPlatform.gbPak->setAttribute("title", string{ gtitle.c_str() });
        const char* board = getenv("ARES_REF_GBBOARD");
        gPlatform.gbPak->setAttribute("board", string{ board ? board : "MBC5" });
        gPlatform.gbPak->append("program.rom", gb);
        std::vector<uint8_t> sram(32 * 1024, 0xFF);
        if (const char* gbsav = getenv("ARES_REF_GBSAV")) {
            std::ifstream sin(gbsav, std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(sin)), std::istreambuf_iterator<char>());
            if (!d.empty()) sram = d;
        }
        gPlatform.gbPak->append("save.ram", sram);
        auto pak = root->find<Node::Port>("Controller Port 1/Gamepad/Pak");
        if (pak) {
            pak->allocate("Transfer Pak");
            pak->connect();
            if (auto slot = root->find<Node::Port>("Controller Port 1/Gamepad/Pak/Transfer Pak/Cartridge Slot")) {
                slot->allocate();
                slot->connect();
                fprintf(stderr, "[ref] Transfer Pak with %s\n", gbrom);
            } else {
                fprintf(stderr, "[ref] no Game Boy cartridge slot (ares built without the Game Boy core?)\n");
            }
        }
    }
    Nintendo64::system.power(false);
    FILE* wavFramesFile = nullptr;
    if (const char* wavPath = getenv("ARES_REF_WAV")) {
        gPlatform.wav = fopen(wavPath, "wb");
        wavFramesFile = fopen((std::string{ wavPath } + ".frames").c_str(), "wb");
        for (auto& stream : root->find<Node::Audio::Stream>()) stream->setResamplerFrequency(48000.0);
        fprintf(stderr, "[ref] audio -> %s (48000 Hz s16 stereo)\n", wavPath);
    }

    if (!loadState.empty()) {
        std::ifstream sin(loadState, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(sin)), std::istreambuf_iterator<char>());
        serializer s(data.data(), (u32)data.size());
        if (!Nintendo64::system.unserialize(s)) {
            fprintf(stderr, "could not load state %s\n", loadState.c_str());
            return 1;
        }
        fprintf(stderr, "[ref] loaded state %s\n", loadState.c_str());
    }

    xcheck::init();
    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t frame = 1; !endFrame || frame <= endFrame; frame++) {
        uint32_t b = 0;
        for (auto& h : holds) {
            if (frame >= h.from && frame <= h.to) b |= h.buttons;
        }
        gButtons = b;
        for (auto& f : fills) {
            if (frame < f.from || frame > f.to) continue;
            auto& ram = Nintendo64::rdram.ram;
            for (u32 i = 0; i + 2 <= f.bytes; i += 2) {
                u32 a = (f.addr + i) & ~3u;
                u32 w = ram.Nintendo64::Memory::Writable::read<Nintendo64::Word>(a);
                if (((f.addr + i) & 2) == 0) w = (w & 0xffff) | ((u32)f.value << 16);
                else w = (w & 0xffff0000) | f.value;
                ram.Nintendo64::Memory::Writable::write<Nintendo64::Word>(a, w);
            }
        }
        uint64_t before = gPictures.load();
        xcheck::frame(frame);
        Nintendo64::system.run();
        if (wavFramesFile) {
            uint32_t n = (uint32_t)gPlatform.wavFrames;
            fwrite(&n, 4, 1, wavFramesFile);
        }
        bool wantShot = actions.count(frame) || (everyN && frame % everyN == 0) || xcheck::gAnimLeft > 0;
        if (getenv("REF_DEBUG") && frame % 30 == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); fprintf(stderr, "[ref] frame %llu pictures %llu\n", (unsigned long long)frame, (unsigned long long)gPictures.load()); }
        if (wantShot) { // the screen thread delivers the picture shortly after the frame
            for (int i = 0; i < 200 && gPictures.load() == before; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        xcheck::afterFrame();
        if (xcheck::gStop) {
            fprintf(stderr, "[xcheck] %d turns done, stopping at frame %llu\n", xcheck::gStopTurns, (unsigned long long)frame);
            endFrame = frame;
            break;
        }
        if (everyN && frame % everyN == 0) {
            char name[64];
            snprintf(name, sizeof(name), "/f_%06llu.ppm", (unsigned long long)frame);
            savePicture(everyDir + name);
        }
        auto range = actions.equal_range(frame);
        for (auto it = range.first; it != range.second; ++it) {
            const Action& a = it->second;
            if (a.kind == "shot") {
                fprintf(stderr, "[ref] frame %llu -> %s (%s)\n", (unsigned long long)frame, a.arg.c_str(), savePicture(a.arg) ? "ok" : "no picture");
            } else if (a.kind == "dump") {
                auto& ram = Nintendo64::rdram.ram;
                std::vector<uint8_t> buf(ram.size);
                for (u32 i = 0; i + 4 <= ram.size; i += 4) {
                    u32 w = ram.Nintendo64::Memory::Writable::read<Nintendo64::Word>(i);
                    buf[i] = w >> 24; buf[i + 1] = w >> 16; buf[i + 2] = w >> 8; buf[i + 3] = w;
                }
                std::ofstream out(a.arg, std::ios::binary);
                out.write((const char*)buf.data(), buf.size());
                fprintf(stderr, "[ref] frame %llu RDRAM -> %s\n", (unsigned long long)frame, a.arg.c_str());
            } else if (a.kind == "save") {
                serializer s = Nintendo64::system.serialize();
                std::ofstream out(a.arg, std::ios::binary);
                out.write((const char*)s.data(), s.size());
                fprintf(stderr, "[ref] frame %llu state -> %s (%u bytes)\n", (unsigned long long)frame, a.arg.c_str(), (unsigned)s.size());
            }
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "[ref] %llu frames in %.1f s, %llu pictures\n", (unsigned long long)endFrame, secs, (unsigned long long)gPictures.load());
    // ARES_REF_GBSAV_OUT=<file>: the Transfer Pak cartridge's battery RAM as the game left it
    if (const char* gbout = getenv("ARES_REF_GBSAV_OUT"); gbout && gPlatform.gbPak) {
        Nintendo64::controllerPort1.save();
        if (auto fp = gPlatform.gbPak->read("save.ram")) {
            std::vector<uint8_t> bytes(fp->size());
            fp->seek(0);
            fp->read({ bytes.data(), bytes.size() });
            std::ofstream(gbout, std::ios::binary).write((const char*)bytes.data(), bytes.size());
            fprintf(stderr, "[ref] gb save -> %s (%zu bytes)\n", gbout, bytes.size());
        }
    }
    if (gPlatform.wav) fclose(gPlatform.wav);
    if (wavFramesFile) fclose(wavFramesFile);
    // ARES_REF_FLASH_OUT=<file>: the cartridge FlashRAM as the game left it (N64 byte order, like ARES_REF_FLASH)
    if (const char* flOut = getenv("ARES_REF_FLASH_OUT")) {
        Nintendo64::cartridge.save();
        if (auto fp = cart->read("save.flash")) {
            std::vector<uint8_t> bytes(fp->size());
            fp->seek(0);
            fp->read({ bytes.data(), bytes.size() });
            std::ofstream(flOut, std::ios::binary).write((const char*)bytes.data(), bytes.size());
            fprintf(stderr, "[ref] flash -> %s (%zu bytes)\n", flOut, bytes.size());
        } else {
            fprintf(stderr, "[ref] no flash to write (ARES_REF_FLASH_OUT needs ARES_REF_FLASH)\n");
        }
    }
    Nintendo64::system.unload();
    return 0;
}

// nall provides the process entry point and calls nall::main with the arguments (without the program path).
auto nall::main(nall::Arguments arguments) -> void {
    std::vector<std::string> args{ "ares_ref" };
    for (auto& a : arguments) args.push_back(a.data());
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    exit(realMain((int)argv.size(), argv.data()));
}
