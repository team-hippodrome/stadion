#include <unordered_set>
#include "port_platform.h"
// Host stand-ins for the N64 hardware the game talks to through libultra: cartridge ROM (PI DMA),
// the VI retrace interrupt, RSP/RDP task completion, timers and the save/peripheral devices.
#include <ultra64.h>
#include <port_compat.h>

#include "Engine.h"
#include "interp.h"
#include "pacing.h"

#include <atomic>
#include <csignal>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unistd.h>

extern "C" void port_PostEvent(OSEvent event);
extern "C" void port_PostMesgFromIrq(OSMesgQueue* mq, OSMesg msg);

// ---- Globals the SDK exposes ---------------------------------------------------------------------

extern "C" {
s32 osTvType = 1; // NTSC
s32 osResetType = 0;
u32 osMemSize = 0x800000;
OSViMode osViModeTable[1];
OSViMode osViModeNtscLpn1;
OSViMode osViModeMpalLpn1;
OSPiHandle* __osPiTable = nullptr;
OSIoMesg __osFlashMsg;
OSMesgQueue __osFlashMessageQ;
OSPiHandle __osFlashHandler;
OSMesg __osFlashMsgBuf[1];
s32 __osFlashVersion;
u32 __osFlashID[4];
}

// ---- Cartridge ROM -------------------------------------------------------------------------------

namespace {
std::vector<uint8_t> gRom;
OSPiHandle gCartHandle, gDriveHandle;
} // namespace

bool Port::LoadRom(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    gRom.resize((size_t)n);
    size_t got = fread(gRom.data(), 1, (size_t)n, f);
    fclose(f);
    return got == (size_t)n;
}

const uint8_t* Port::RomData() {
    return gRom.data();
}

size_t Port::RomSize() {
    return gRom.size();
}

extern "C" {

OSPiHandle* port_osCartRomInit(void) {
    gCartHandle.type = DEVICE_TYPE_CART;
    gCartHandle.baseAddress = 0xB0000000;
    gCartHandle.domain = PI_DOMAIN1;
    return &gCartHandle;
}

OSPiHandle* osDriveRomInit(void) {
    gDriveHandle.type = DEVICE_TYPE_BULK;
    gDriveHandle.baseAddress = 0xA6000000;
    gDriveHandle.domain = PI_DOMAIN1;
    return &gDriveHandle;
}


s32 osEPiLinkHandle(OSPiHandle* h) {
    h->next = __osPiTable;
    __osPiTable = h;
    return 0;
}

// ---- Cartridge save memory: flash (the game's main save) and SRAM ------------------------------------
// The flash chip is emulated at the command level the game's flash.c drives it with: command writes through
// osEPiWriteIo, status/ID/array reads through osEPiReadIo and osEPiStartDma. Contents persist in a file.
namespace {
constexpr size_t kFlashSize = 0x20000;   // 1 Mbit MX29L1100
constexpr size_t kFlashPage = 128;
constexpr size_t kSramSize = 0x8000;
std::vector<uint8_t> gFlash(kFlashSize, 0xFF);
std::vector<uint8_t> gSram(kSramSize, 0);
uint8_t gFlashWriteBuf[kFlashPage];
enum class FlashMode { Array, Status, Id };
FlashMode gFlashMode = FlashMode::Array;
bool gFlashWriteArmed = false;
u32 gFlashStatus = 0;
size_t gFlashEraseFirstPage = 0;
bool gFlashEraseChip = false;
bool gFlashLoaded = false;

std::string FlashPath() {
    const char* p = getenv("PORT_FLASH_FILE");
    return p ? p : "pokestadium.flash";
}

void FlashLoad() {
    if (gFlashLoaded) return;
    gFlashLoaded = true;
    if (FILE* f = fopen(FlashPath().c_str(), "rb")) {
        size_t n = fread(gFlash.data(), 1, gFlash.size(), f);
        (void)n;
        fclose(f);
    }
}

void FlashSave() {
    if (FILE* f = fopen(FlashPath().c_str(), "wb")) {
        fwrite(gFlash.data(), 1, gFlash.size(), f);
        fclose(f);
    }
}

bool IsFlash(OSPiHandle* h) { return h->type == DEVICE_TYPE_FLASH; }
} // namespace

s32 osEPiWriteIo(OSPiHandle* h, u32 devAddr, u32 data) {
    if (!IsFlash(h)) return 0;
    FlashLoad();
    if ((devAddr & 0x0FFFFFFF) < 0x10000) {
        return 0; // data-window writes are not used; the game writes pages through DMA
    }
    static const bool sTrace = getenv("PORT_TRACE_FLASH") != nullptr;
    if (sTrace) fprintf(stderr, "[flash] cmd dev=0x%x data=0x%08x\n", (unsigned)devAddr, (unsigned)data);
    switch (data >> 24) {
        case 0xD2: gFlashMode = FlashMode::Status; break;
        case 0xE1: gFlashMode = FlashMode::Id; break;
        case 0xF0: gFlashMode = FlashMode::Array; break;
        case 0x3C: gFlashEraseChip = true; gFlashEraseFirstPage = 0; break;
        case 0x4B: gFlashEraseChip = false; gFlashEraseFirstPage = data & 0xFFFF; break;
        case 0x78: {
            if (gFlashEraseChip) {
                std::fill(gFlash.begin(), gFlash.end(), 0xFF);
            } else {
                size_t off = gFlashEraseFirstPage * kFlashPage;
                if (off + 128 * kFlashPage <= gFlash.size()) {
                    std::fill(gFlash.begin() + off, gFlash.begin() + off + 128 * kFlashPage, 0xFF);
                }
            }
            gFlashStatus = 0x08;
            FlashSave();
            break;
        }
        case 0xB4: gFlashWriteArmed = true; break;
        case 0xA5: {
            size_t off = (data & 0xFFFF) * kFlashPage;
            if (off + kFlashPage <= gFlash.size()) {
                for (size_t i = 0; i < kFlashPage; i++) gFlash[off + i] &= gFlashWriteBuf[i];
            }
            gFlashStatus = 0x04;
            FlashSave();
            break;
        }
    }
    return 0;
}

s32 osEPiReadIo(OSPiHandle* h, u32 devAddr, u32* data) {
    *data = 0;
    if (IsFlash(h) && gFlashMode == FlashMode::Status) {
        *data = gFlashStatus;
    }
    static const bool sTrace = getenv("PORT_TRACE_FLASH") != nullptr;
    if (sTrace && IsFlash(h)) fprintf(stderr, "[flash] status read -> 0x%x (mode %d)\n", (unsigned)*data, (int)gFlashMode);
    return 0;
}

// Cartridge ROM, flash and SRAM transfers; the completion message goes back to the requester's queue as it
// would from the PI interrupt handler.
s32 port_osEPiStartDma(OSPiHandle* h, OSIoMesg* mb, s32 direction) {
    if ((uintptr_t)mb->dramAddr < 0x100000000ull) { // PORT: a truncated 32-bit buffer pointer somewhere in the game
        static int shown = 0;
        if (shown++ < 4) fprintf(stderr, "[dma-bad] dram=%p dev=0x%x size=0x%x caller=%p slide=0x%lx\n", mb->dramAddr, (unsigned)mb->devAddr, (unsigned)mb->size, __builtin_return_address(0), (unsigned long)Port_ImageSlide());
        port_PostMesgFromIrq(mb->hdr.retQueue, (OSMesg)mb);
        return 0;
    }
    static const bool trace = getenv("PORT_TRACE") != nullptr;
    if (trace) {
        fprintf(stderr, "[dma] dir=%d type=%d dev=0x%x dram=%p size=0x%zx\n", direction, h->type, mb->devAddr, mb->dramAddr, (size_t)mb->size);
    }
    if (IsFlash(h)) {
        FlashLoad();
        static const bool sTraceDma = getenv("PORT_TRACE_FLASH") != nullptr;
        if (sTraceDma) fprintf(stderr, "[flash] dma dir=%d dev=0x%x size=0x%zx mode=%d\n", direction, (unsigned)mb->devAddr, (size_t)mb->size, (int)gFlashMode);
        if (direction == OS_WRITE) {
            memcpy(gFlashWriteBuf, mb->dramAddr, std::min<size_t>(mb->size, kFlashPage));
        } else if (gFlashMode == FlashMode::Id) {
            u32 id[2] = { 0x11118001, 0x00C2001E }; // MX29L1100 type / maker words
            memcpy(mb->dramAddr, id, std::min<size_t>(mb->size, sizeof id));
        } else {
            size_t off = (size_t)mb->devAddr * 2; // devAddr counts halfwords for this chip
            if (off < gFlash.size()) {
                memcpy(mb->dramAddr, gFlash.data() + off, std::min<size_t>(mb->size, gFlash.size() - off));
            }
        }
    } else if (h->type == DEVICE_TYPE_CART && direction == OS_READ) {
        size_t off = mb->devAddr & 0x0FFFFFFF;
        if (off < gRom.size()) {
            size_t n = std::min<size_t>(mb->size, gRom.size() - off);
            memcpy(mb->dramAddr, gRom.data() + off, n);
        }
    } else if (h->type == DEVICE_TYPE_SRAM) {
        size_t off = mb->devAddr & 0x7FFF;
        size_t n = std::min<size_t>(mb->size, gSram.size() - off);
        if (direction == OS_READ) memcpy(mb->dramAddr, gSram.data() + off, n);
        else memcpy(gSram.data() + off, mb->dramAddr, n);
    }
    port_PostMesgFromIrq(mb->hdr.retQueue, (OSMesg)mb);
    return 0;
}

// Big-endian read of the loaded ROM image (replaces direct loads from cartridge address space).
u32 port_rom_read_u32(u32 off) {
    if ((size_t)off + 4 > gRom.size()) {
        return 0;
    }
    return ((u32)gRom[off] << 24) | ((u32)gRom[off + 1] << 16) | ((u32)gRom[off + 2] << 8) | gRom[off + 3];
}

uintptr_t osVirtualToPhysical(void* p) {
    return (uintptr_t)p;
}

u32 osGetMemSize(void) {
    return osMemSize;
}

void osInitialize(void) {}
s32 osAfterPreNMI(void) { return 0; }
u32 __osDisableInt(void) { return 1; }
void __osRestoreInt(u32) {}
void __osSetSR(u32) {}
void __osSetCause(u32) {}
void __osSetCompare(u32) {}
void __osSetCount(u32) {}
u32 __osSetFpcCsr(u32) { return 0; }
s32 __osAiDeviceBusy(void) { return 0; }
void osDpSetStatus(u32) {}
u32 osDpGetCounters(u32* buf) {
    memset(buf, 0, 16);
    return 0;
}

// ---- Peripherals not emulated yet ----------------------------------------------------------------
// Controller Pak / Transfer Pak report "no pak"; the real implementations will map onto libultraship.
// osPfsIsPlug and the osGbpak* calls: transfer_pak.cpp (Transfer Pak with a Game Boy cartridge, PORT_GB_ROM).

// ---- VI: frame buffers drawn by the CPU ----------------------------------------------------------
// Most frames are rendered by the RDP (Fast3D renders them straight to the window). Some screens (the GB Tower) are
// drawn by the CPU into a frame buffer in RAM and shown with osViSwapBuffer: when no display list rendered a frame
// buffer since the previous swap, the swapped buffer is presented from memory (PumpRspTasks).
extern "C" uint64_t Port_GpuDrawStamp(const void* address);
static std::mutex gSwapLock;
static void* gViPending = nullptr;
static std::unordered_map<const void*, uint64_t> gViStampAtLastSwap; // per frame buffer
static bool gViBlack = false;
static float gViXScale = 1.0f;

static std::condition_variable gSwapDone;
// PORT: every address the game has shown with osViSwapBuffer; the renderer uses it to tell full-screen-sized
// pictures drawn for use as textures (Gallery enlarge: a 640x480 photo) from frame buffers
static std::mutex gViFbLock;
static std::unordered_set<uintptr_t> gViFbs;
extern "C" int Port_ViFrameBufferState(const void* addr) { // -1: none shown yet, 1: shown, 0: never shown
    std::lock_guard<std::mutex> g(gViFbLock);
    if (gViFbs.size() < 2) return -1;
    return gViFbs.count((uintptr_t)addr) ? 1 : 0;
}
extern "C" bool Port_Lockstep();
void port_osViSwapBuffer(void* fb) {
    {
        std::lock_guard<std::mutex> v(gViFbLock);
        gViFbs.insert((uintptr_t)fb);
    }
    std::unique_lock<std::mutex> g(gSwapLock);
    gViPending = fb;
    // In lockstep mode the swap completes when the main thread has looked at it (as a graphics task does), so each
    // CPU-drawn frame is shown and frame numbers stay deterministic.
    if (Port_Lockstep()) {
        gSwapDone.wait_for(g, std::chrono::milliseconds(200), [] { return gViPending == nullptr; });
    }
}
// osViRepeatLine(1): the VI shows the frame buffer's first line on every line (the game's blank screen between scenes
// is a one-line 640-pixel buffer in this mode).
static bool gViRepeatLine = false;
extern "C" void port_osViRepeatLine(u8 on) {
    std::lock_guard<std::mutex> g(gSwapLock);
    gViRepeatLine = on != 0;
}
// osViSetMode rewrites the VI registers, including the Y scale that repeat-line mode zeroes (the GB Tower sets its own
// mode after the scene change's blank screen).
extern "C" void port_osViSetMode(void* mode) {
    (void)mode;
    std::lock_guard<std::mutex> g(gSwapLock);
    gViRepeatLine = false;
}
void port_osViBlack(u8 black) {
    std::lock_guard<std::mutex> g(gSwapLock);
    gViBlack = black != 0;
}
void port_osViSetXScale(f32 scale) {
    std::lock_guard<std::mutex> g(gSwapLock);
    gViXScale = (scale > 0.1f && scale <= 1.0f) ? scale : 1.0f;
}

// The libultraship implementation, reached through its real symbol name (the macro in port_compat.h renames the
// game's calls to the wrapper below).
#if defined(__APPLE__)
extern "C" int32_t LUS_osContStartReadData(OSMesgQueue* mq) __asm__("_osContStartReadData");
#else
extern "C" int32_t LUS_osContStartReadData(OSMesgQueue* mq) __asm__("osContStartReadData");
#endif
extern "C" void port_WakeQueue(OSMesgQueue* mq);

extern "C" s32 port_osContStartReadData(OSMesgQueue* mq) {
    // libultraship reads the pads synchronously in osContGetReadData and never posts the SI completion the game
    // waits for, so post it here.
    int32_t r = LUS_osContStartReadData(mq);
    port_osSendMesg(mq, nullptr, OS_MESG_NOBLOCK);
    return r;
}

// Debug aid: PORT_SPEED=<n> runs the game n times faster (VI retrace and OS timers shortened), to get through slow
// scenes quickly; PORT_SPEED=max runs as fast as the host allows (lockstep mode only). Vsync is turned off for
// speeds above 1 by Engine.cpp.
double Port_Speed() {
    static double speed = [] {
        const char* e = getenv("PORT_SPEED");
        if (e == nullptr) return 1.0;
        if (strcmp(e, "max") == 0 || atof(e) <= 0.0) return 1e6;
        return std::max(1.0, atof(e));
    }();
    return speed;
}

} // extern "C"

// Only every n-th frame is drawn when running fast (PORT_RENDER_EVERY overrides); the game cannot tell.
static uint64_t Port_RenderEvery() {
    static uint64_t n = getenv("PORT_RENDER_EVERY") ? (uint64_t)std::max(1, atoi(getenv("PORT_RENDER_EVERY")))
                                                   : (uint64_t)std::min(10.0, Port_Speed());
    return n;
}

// ---- Lockstep clock -------------------------------------------------------------------------------
// By default time is virtual: the VI retrace and OS timers fire only when every game thread is blocked, and
// osGetTime/osGetCount report that virtual time. Game code then always sees the same sequence of events for the
// same input, so a run can be recorded (pad state per frame) and replayed to the exact same situation, at any
// speed. PORT_REALTIME=1 switches back to the free-running wall-clock threads.
namespace {
constexpr uint64_t kViCycles = 46875000 / 60; // CPU count cycles per NTSC frame
std::mutex gClockLock;
uint64_t gVTime = 0;      // virtual CPU count of the current event
uint64_t gCountBump = 0;  // advances osGetCount between events, deterministically
std::atomic<uint64_t> gTick{ 0 }; // VI retraces so far
struct VTimer {
    uint64_t due, seq;
    OSMesgQueue* mq;
    OSMesg msg;
};
std::vector<VTimer> gVTimers;
uint64_t gTimerSeq = 0;
std::atomic<uint64_t> gPictureChangedTick{ 0 }; // last game frame whose presented picture differed from the one before
} // namespace

// Freeze detection by picture comparison (PORT_FREEZE_CHECK=1, on by default for replays and screenshot runs): the
// window thread hashes every presented frame and reports changes here; the clock reports a freeze when the picture
// has not changed for PORT_FREEZE_FRAMES game frames (default 300 = 5 s), whether frames are still drawn or not.
extern "C" bool Port_FreezeCheckEnabled() {
    static bool on = getenv("PORT_FREEZE_CHECK") ? atoi(getenv("PORT_FREEZE_CHECK")) != 0
                                                 : (getenv("PORT_REPLAY") != nullptr || getenv("PORT_SCREENSHOT_DIR") != nullptr);
    return on;
}

// A picture counts as progress only if it was not seen in the last PORT_LOOP_WINDOW game frames (default 600 = 10 s):
// a static screen and an animation that keeps cycling through the same pictures (a hang that still animates, or a
// screen waiting for input) both stop producing new pictures.
namespace {
std::mutex gPicLock;
std::unordered_map<uint64_t, uint64_t> gPicLastSeen; // picture hash -> last game frame it was presented
size_t gPicLoopSize = 0;                             // distinct pictures seen since the last new one
}

extern "C" void Port_NotePictureHash(uint64_t hash) {
    static const uint64_t window = getenv("PORT_LOOP_WINDOW") ? strtoull(getenv("PORT_LOOP_WINDOW"), nullptr, 10) : 600;
    std::lock_guard<std::mutex> g(gPicLock);
    uint64_t tick = gTick.load();
    auto it = gPicLastSeen.find(hash);
    bool novel = it == gPicLastSeen.end() || tick - it->second > window;
    gPicLastSeen[hash] = tick;
    if (novel) {
        static const bool trace = getenv("PORT_TRACE_PICTURES") != nullptr;
        if (trace) fprintf(stderr, "[pic] frame %llu new picture %016llx (known %zu)\n", (unsigned long long)tick,
                           (unsigned long long)hash, gPicLastSeen.size());
        gPictureChangedTick = tick;
        gPicLoopSize = 0;
    } else if (it->second < gPictureChangedTick.load()) {
        gPicLoopSize++; // first repeat since the last new picture
    }
    if (gPicLastSeen.size() > 20000) { // forget pictures older than the window
        for (auto e = gPicLastSeen.begin(); e != gPicLastSeen.end();) {
            e = (tick - e->second > window) ? gPicLastSeen.erase(e) : std::next(e);
        }
    }
}

static size_t Port_PictureLoopSize() {
    std::lock_guard<std::mutex> g(gPicLock);
    return gPicLoopSize;
}

extern "C" int port_ThreadsIdle(void);
extern "C" void port_DumpThreads(void);

extern "C" bool Port_Lockstep() {
    static bool on = getenv("PORT_REALTIME") == nullptr;
    return on;
}

extern "C" uint64_t Port_ViTick() {
    return gTick.load();
}

#undef osGetTime
#undef osGetCount
extern "C" OSTime port_osGetTime(void) {
    if (!Port_Lockstep()) {
        return osGetTime();
    }
    // Each read advances time a little, like running code does; busy-waits on the clock therefore still finish.
    std::lock_guard<std::mutex> g(gClockLock);
    gCountBump += 1000;
    return gVTime + gCountBump;
}

extern "C" u32 port_osGetCount(void) {
    if (!Port_Lockstep()) {
        return osGetCount();
    }
    return (u32)port_osGetTime();
}

extern "C" {

// ---- Timers ---------------------------------------------------------------------------------------

s32 port_osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    if (Port_Lockstep()) {
        std::lock_guard<std::mutex> g(gClockLock);
        // a timer never fires in zero time (code ran in between), so timer loops still move the clock forward
        gVTimers.push_back({ gVTime + gCountBump + std::max<OSTime>(countdown, 2000), ++gTimerSeq, mq, msg });
        static bool trace = getenv("PORT_TRACE_TIMERS") != nullptr;
        if (trace) fprintf(stderr, "[timer] frame %llu countdown %llu\n", (unsigned long long)gTick.load(), (unsigned long long)countdown);
        return 0;
    }
    // 46.875 MHz CPU count rate -> microseconds
    auto us = std::chrono::microseconds((long long)(((countdown * 64) / 3000) / Port_Speed()));
    std::thread([us, interval, mq, msg]() {
        std::this_thread::sleep_for(us);
        port_PostMesgFromIrq(mq, msg);
    }).detach();
    return 0;
}

} // extern "C"

// ---- The game's sound engine: on, or off as before it was ported -----------------------------------------------
// Running the sound engine changes the game's timing (it waits for fades and sounds to end, as on the N64), so input
// recordings made while it was not ported only replay correctly without it. Recordings now carry a "# game audio"
// line; replaying one without it turns the engine off. PORT_GAME_AUDIO=0/1 decides explicitly.
extern "C" int Port_GameAudio(void) {
    static const int on = [] {
        if (const char* e = getenv("PORT_GAME_AUDIO")) {
            return atoi(e) != 0 ? 1 : 0;
        }
        int r = 1;
        if (const char* rp = getenv("PORT_REPLAY")) {
            r = 0;
            if (FILE* f = fopen(rp, "r")) {
                char line[256];
                while (fgets(line, sizeof line, f)) {
                    if (strncmp(line, "# game audio", 12) == 0) {
                        r = 1;
                        break;
                    }
                }
                fclose(f);
            }
        }
        if (r == 0) {
            fprintf(stderr, "[audio] game sound off (%s)\n",
                    getenv("PORT_GAME_AUDIO") ? "PORT_GAME_AUDIO=0" : "the replayed recording predates it");
        }
        return r;
    }();
    return on;
}

// ---- Controller input: live, recorded, replayed -----------------------------------------------------
// The window thread publishes the live pad state (PublishPads). In lockstep mode the clock latches one state per
// frame (what the game reads during that frame), records it to PORT_RECORD (default last_input.rec) and, with
// PORT_REPLAY=<file>, takes it from a recording instead until the recording runs out; live input applies after that.
// A recording line is "<frame> <button stick_x stick_y> x4" and is written only when the state changes.
namespace {
std::mutex gPadLock;
OSContPad gPadSnapshot[MAXCONTROLLERS];
OSContPad gPadLatched[MAXCONTROLLERS];

struct PadFrame {
    uint64_t tick;
    OSContPad pads[MAXCONTROLLERS];
};
std::vector<PadFrame> gReplay;
size_t gReplayIdx = 0;
uint64_t gReplayEnd = 0;
OSContPad gReplayCur[MAXCONTROLLERS];
FILE* gRecord = nullptr;
OSContPad gRecordLast[MAXCONTROLLERS];
bool gRecordAny = false;

void InputInit() {
    const char* rp = getenv("PORT_REPLAY");
    if (rp != nullptr) {
        if (FILE* f = fopen(rp, "r")) {
            char line[256];
            while (fgets(line, sizeof line, f)) {
                if (line[0] == '#') {
                    unsigned long long end;
                    if (sscanf(line, "# end %llu", &end) == 1) gReplayEnd = end;
                    continue;
                }
                PadFrame pf = {};
                unsigned long long t;
                int b[4], x[4], y[4];
                if (sscanf(line, "%llu %d %d %d %d %d %d %d %d %d %d %d %d", &t, &b[0], &x[0], &y[0], &b[1], &x[1],
                           &y[1], &b[2], &x[2], &y[2], &b[3], &x[3], &y[3]) != 13) {
                    continue;
                }
                pf.tick = t;
                for (int i = 0; i < 4; i++) {
                    pf.pads[i].button = (u16)b[i];
                    pf.pads[i].stick_x = (s8)x[i];
                    pf.pads[i].stick_y = (s8)y[i];
                }
                gReplay.push_back(pf);
            }
            fclose(f);
            if (!gReplay.empty()) gReplayEnd = std::max(gReplayEnd, gReplay.back().tick);
            fprintf(stderr, "[input] replaying %zu input changes from %s (until frame %llu)\n", gReplay.size(), rp,
                    (unsigned long long)gReplayEnd);
        } else {
            fprintf(stderr, "[input] cannot read replay file %s\n", rp);
        }
    }
    // (default: in the app's data folder; a started app's working directory may not be writable)
    static const std::string defaultRecord = Port::AppPath("last_input.rec");
    static const std::string continuedRecord = Port::AppPath("last_input_continued.rec");
    const char* wp = getenv("PORT_RECORD") ? getenv("PORT_RECORD") : defaultRecord.c_str();
    if (rp != nullptr && strcmp(rp, wp) == 0) {
        wp = continuedRecord.c_str(); // never overwrite the recording being replayed
    }
    if (*wp != '\0' && (gRecord = fopen(wp, "w")) != nullptr) {
        fprintf(gRecord, "# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4\n");
        if (Port_GameAudio()) {
            fprintf(gRecord, "# game audio\n"); // made with the sound engine running (see Port_GameAudio)
        }
        fflush(gRecord);
    }
}

} // namespace

// Records the frame the run reached, so a replay knows how long the recording lasted (also called on crash).
extern "C" void Port_InputFinish() {
    if (gRecord != nullptr) {
        fprintf(gRecord, "# end %llu\n", (unsigned long long)gTick.load());
        fflush(gRecord);
    }
}

namespace {
void LatchPads(uint64_t tick) {
    OSContPad p[MAXCONTROLLERS];
    {
        std::lock_guard<std::mutex> g(gPadLock);
        memcpy(p, gPadSnapshot, sizeof p);
    }
    // Debug aid: PORT_AUTOSTART=<period in ms> taps Start (A as well with PORT_AUTOA) every period after 8 s.
    static const char* autoStart = getenv("PORT_AUTOSTART");
    if (autoStart != nullptr) {
        uint64_t ms = tick * 1000 / 60;
        uint64_t period = atol(autoStart) > 0 ? (uint64_t)atol(autoStart) : 5000;
        static const bool autoA = getenv("PORT_AUTOA") != nullptr;
        if (ms > 8000 && (ms % period) < 150) p[0].button |= autoA ? (CONT_A | CONT_START) : CONT_START;
    }
    if (tick <= gReplayEnd && (!gReplay.empty() || gReplayEnd != 0)) {
        while (gReplayIdx < gReplay.size() && gReplay[gReplayIdx].tick <= tick) {
            memcpy(gReplayCur, gReplay[gReplayIdx].pads, sizeof gReplayCur);
            gReplayIdx++;
        }
        memcpy(p, gReplayCur, sizeof p);
        if (tick == gReplayEnd) fprintf(stderr, "[input] replay finished at frame %llu, live input from here\n", (unsigned long long)tick);
    }
    if (gRecord != nullptr) {
        bool changed = !gRecordAny;
        for (int i = 0; i < MAXCONTROLLERS; i++) {
            changed |= p[i].button != gRecordLast[i].button || p[i].stick_x != gRecordLast[i].stick_x ||
                       p[i].stick_y != gRecordLast[i].stick_y;
        }
        if (changed) {
            fprintf(gRecord, "%llu", (unsigned long long)tick);
            for (int i = 0; i < 4; i++) fprintf(gRecord, " %d %d %d", p[i].button, p[i].stick_x, p[i].stick_y);
            fputc('\n', gRecord);
            fflush(gRecord);
            memcpy(gRecordLast, p, sizeof p);
            gRecordAny = true;
        }
        if (tick % 600 == 0) Port_InputFinish();
    }
    std::lock_guard<std::mutex> g(gPadLock);
    memcpy(gPadLatched, p, sizeof p);
}
} // namespace

void Port::PublishPads(const void* pads) {
    std::lock_guard<std::mutex> g(gPadLock);
    memcpy(gPadSnapshot, pads, sizeof(gPadSnapshot));
}

extern "C" void port_osContGetReadData(OSContPad* pad) {
    std::lock_guard<std::mutex> g(gPadLock);
    memcpy(pad, Port_Lockstep() ? gPadLatched : gPadSnapshot, sizeof(gPadSnapshot));
}

// ---- VI retrace -----------------------------------------------------------------------------------

namespace {
std::mutex gViLock;
OSMesgQueue* gViQueue = nullptr;
OSMesg gViMsg = nullptr;
u32 gViEvery = 1;
std::atomic<bool> gViRunning{false};
std::atomic<bool> gStop{false};
std::atomic<uint64_t> gStopAt{ 0 }; // PORT_STOP_AT=<frame>: draw and capture that frame, then exit
std::mutex gTaskLock;
std::condition_variable gTaskDoneCv;
uint64_t gTasksSubmitted = 0, gTasksProcessed = 0;
std::condition_variable gTaskQueuedCv; // the window loop waits on it (Port::WaitForRspTasks)
std::deque<OSTask*> gTasks;
OSTask* gLoaded = nullptr;

bool TasksDrained() {
    std::lock_guard<std::mutex> g(gTaskLock);
    return gTasks.empty() && gTasksProcessed == gTasksSubmitted;
}

// When the next VI retrace is due in wall-clock time (PORT_SPEED times real time). At normal speed retraces follow the
// refresh of the display the window is on, when it runs at a whole multiple of 60 Hz (within 2%: 59.94, 60, 120, 240
// Hz): every n-th vblank (n = refresh / 60), shortly after it. The game's frame is then drawn early in the refresh and
// every drawn frame stays on screen for the same number of refreshes. A free-running 16.67 ms clock beat against the
// display instead: retraces drifted through the refresh and were delayed by the window thread, frames drawn late in a
// refresh missed it, and a 30 fps scene alternated 1 and 3 refreshes per frame (PORT_TRACE_PACING=1 shows it).
// PORT_DISPLAY_PACING=0 turns it off (the free-running clock); PORT_PACING_LEAD_US sets how long after the vblank a
// retrace comes (default 500).
std::atomic<bool> gDisplayPaced{ false };
std::atomic<int64_t> gRetraceVblank{ 0 }; // the vblank the last paced retrace followed (steady clock), 0: not paced
std::atomic<int64_t> gRetracePeriod{ 0 };

class RetraceSchedule {
  public:
    void Wait() {
        const double periodUs = 16667.0 / Port_Speed();
        if (periodUs <= 1.0) return; // PORT_SPEED=max
        int64_t now = Port_SteadyNs();
        bool wasPaced = mPaced;
        int64_t target = Port_Speed() == 1.0 ? DisplayTarget(now) : 0;
        if (target == 0) { // the free-running clock
            if (wasPaced) mNext = std::chrono::steady_clock::now();
            mNext += std::chrono::microseconds((long long)periodUs);
            auto wallNow = std::chrono::steady_clock::now();
            if (mNext < wallNow - std::chrono::milliseconds(100)) mNext = wallNow; // do not race to catch up after a stall
            std::this_thread::sleep_until(mNext);
            return;
        }
        if (target > now) std::this_thread::sleep_for(std::chrono::nanoseconds(target - now));
    }

  private:
    // the steady-clock time of the next retrace on the display's refresh grid; 0 when the display does not set it
    int64_t DisplayTarget(int64_t now) {
        static const bool sOn = !(getenv("PORT_DISPLAY_PACING") && atoi(getenv("PORT_DISPLAY_PACING")) == 0);
        static const int64_t sLeadNs = getenv("PORT_PACING_LEAD_US") ? atoll(getenv("PORT_PACING_LEAD_US")) * 1000 : 500000;
        constexpr double kRetraceNs = 1e9 / 60;
        int64_t refresh = 0, vblank = 0;
        int n = 0;
        if (sOn && Port_DisplayVblank(&refresh, &vblank) && refresh > 1000000) {
            n = (int)std::lround(kRetraceNs / refresh);
            if (n < 1 || std::fabs(n * refresh - kRetraceNs) > 0.02 * kRetraceNs) n = 0;
        }
        if (n != mRefreshes || std::llabs(refresh - mRefreshNs) > mRefreshNs / 500) { // (the estimate settles slowly)
            if (Port_PacingTrace()) {
                if (n > 0) {
                    fprintf(stderr, "[pacing] retraces follow the display: %.3f Hz, every %d refresh(es), %.1f ms after the vblank\n",
                            1e9 / refresh, n, sLeadNs / 1e6);
                } else {
                    fprintf(stderr, "[pacing] retraces on the free-running clock (display: %s)\n",
                            refresh > 0 ? (std::to_string(1e9 / refresh) + " Hz, not a multiple of 60 Hz").c_str()
                                        : "refresh not known");
                }
            }
            mRefreshes = n;
            mRefreshNs = refresh;
        }
        mPaced = n > 0;
        gDisplayPaced = mPaced;
        if (!mPaced) gRetraceVblank = 0;
        if (!mPaced) {
            mTarget = 0;
            return 0;
        }
        int64_t base = vblank + sLeadNs;
        double k;
        if (mTarget == 0 || mTarget + n * refresh < now - 100000000) {
            // the first retrace, or one after a stall: the next grid point (no racing to catch up)
            k = std::ceil((double)(now - base) / refresh);
        } else {
            // n refreshes after the previous one, snapped to the grid (it follows the display's measured clock); one
            // that is already late (the game was busy) comes at once, and the next is back on the grid
            k = std::floor((double)(mTarget + n * refresh - base) / refresh + 0.5);
        }
        mTarget = base + (int64_t)k * refresh;
        gRetraceVblank = mTarget - sLeadNs;
        gRetracePeriod = refresh;
        return mTarget;
    }

    std::chrono::steady_clock::time_point mNext = std::chrono::steady_clock::now();
    int64_t mTarget = 0;
    int64_t mRefreshNs = 0;
    int mRefreshes = 0;
    bool mPaced = false;
};

// The lockstep clock: waits until the game is idle, then delivers the next event in virtual time (an OS timer or
// the next VI retrace, throttled to PORT_SPEED times real time).
void LockstepClock() {
    const double periodUs = 16667.0 / Port_Speed();
    RetraceSchedule schedule;
    if (Port_Speed() == 1.0) Port_PreciseTimingThread();
    for (;;) {
        auto waitStart = std::chrono::steady_clock::now();
        bool reported = false;
        // drained, idle, drained: the two are read under different locks; a completion posted after the first check
        // leaves a runnable thread, and a task submitted after the idle check shows in the second one
        while (!(TasksDrained() && port_ThreadsIdle() && TasksDrained())) {
            if (gStop) return;
            std::this_thread::sleep_for(std::chrono::microseconds(20));
            // (after 150 ms with PORT_TRACE_PACING=1, to see what a hitch waits for)
            static const auto reportAfter = getenv("PORT_TRACE_PACING") ? std::chrono::milliseconds(150)
                                                                         : std::chrono::milliseconds(3000);
            if (!reported && std::chrono::steady_clock::now() - waitStart > reportAfter) {
                reported = true; // a thread that never blocks stops the clock: say which one
                fprintf(stderr, "[clock] frame %llu: game not idle for %lld ms (tasks drained: %d)\n",
                        (unsigned long long)gTick.load(), (long long)reportAfter.count(), (int)TasksDrained());
                port_DumpThreads();
            }
        }
        if (gStop) return;
        uint64_t tick = gTick.load();
        // (a stop frame does not freeze the clock: the run ends at the first frame drawn at or after it, and the game
        // may not draw on every retrace)
        uint64_t nextVi = (tick + 1) * kViCycles;
        VTimer due{};
        bool haveTimer = false;
        {
            std::lock_guard<std::mutex> g(gClockLock);
            auto best = gVTimers.end();
            for (auto it = gVTimers.begin(); it != gVTimers.end(); ++it) {
                if (it->due <= nextVi && (best == gVTimers.end() || it->due < best->due ||
                                          (it->due == best->due && it->seq < best->seq))) {
                    best = it;
                }
            }
            if (best != gVTimers.end()) {
                due = *best;
                gVTimers.erase(best);
                haveTimer = true;
                gVTime = std::max(gVTime + gCountBump, due.due);
                gCountBump = 0;
            }
        }
        if (haveTimer) {
            port_PostMesgFromIrq(due.mq, due.msg);
            continue;
        }
        // PORT_TRACE_PACING=1: retraces that came late in wall-clock time (a visible hitch), with how long the clock
        // waited for the game to go idle
        static const bool tracePacing = getenv("PORT_TRACE_PACING") != nullptr;
        if (tracePacing) {
            static auto lastTick = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            double gapMs = std::chrono::duration<double, std::milli>(now - lastTick).count();
            double busyMs = std::chrono::duration<double, std::milli>(now - waitStart).count();
            if (gapMs > periodUs / 1000.0 * 1.5 + 2) {
                fprintf(stderr, "[pace] frame %llu: %.1f ms since the last retrace (game busy %.1f ms)\n",
                        (unsigned long long)tick, gapMs, busyMs);
            }
            lastTick = now;
        }
        schedule.Wait();
        Port_PacingNote(PORT_PACE_RETRACE, Port_SteadyNs());
        {
            std::lock_guard<std::mutex> g(gClockLock);
            gVTime = std::max(nextVi, gVTime + gCountBump);
            gCountBump = 0;
        }
        gTick = ++tick;
        LatchPads(tick);
        if (Port_FreezeCheckEnabled()) { // freeze report: the picture has not changed for PORT_FREEZE_FRAMES game frames
            static const uint64_t limit = getenv("PORT_FREEZE_FRAMES") ? strtoull(getenv("PORT_FREEZE_FRAMES"), nullptr, 10) : 300;
            static uint64_t reportedFor = UINT64_MAX, nextReport = 0;
            uint64_t changed = gPictureChangedTick.load();
            if (reportedFor != changed) {
                reportedFor = changed;
                nextReport = changed + limit;
            }
            if (tick > nextReport) { // reported again at 2x, 4x, ... the limit, to compare where the threads are
                nextReport = changed + (tick - changed) * 2;
                size_t loop = Port_PictureLoopSize();
                if (loop > 1) {
                    fprintf(stderr, "[freeze] frame %llu: no new picture since frame %llu (looping over %zu pictures); game threads:\n",
                            (unsigned long long)tick, (unsigned long long)changed, loop);
                } else {
                    fprintf(stderr, "[freeze] frame %llu: picture unchanged since frame %llu; game threads:\n",
                            (unsigned long long)tick, (unsigned long long)changed);
                }
                port_DumpThreads();
                {
                    std::lock_guard<std::mutex> g(gViLock);
                    if (gViQueue) fprintf(stderr, "  VI queue %p: %d/%d, every %u\n", (void*)gViQueue, (int)gViQueue->validCount, (int)gViQueue->msgCount, gViEvery);
                }
                if (getenv("PORT_EXIT_ON_FREEZE") && tick - changed > limit * 3) {
                    Port_InputFinish();
                    fflush(stderr);
                    _exit(3);
                }
            }
        }
        OSMesgQueue* q;
        OSMesg m;
        u32 every;
        {
            std::lock_guard<std::mutex> g(gViLock);
            q = gViQueue;
            m = gViMsg;
            every = gViEvery;
        }
        if (q && (tick % every) == 0) {
            port_PostMesgFromIrq(q, m);
        }
    }
}
} // namespace

// whether retraces follow the display's refresh (Engine.cpp then keeps libultraship's frame limiter out of the way)
extern "C" bool Port_DisplayPaced() {
    return gDisplayPaced.load();
}

// the vblank the latest retrace followed and the refresh period, when retraces follow the display (else false)
extern "C" bool Port_LastRetraceVblank(int64_t* vblankNs, int64_t* periodNs) {
    *vblankNs = gRetraceVblank.load();
    *periodNs = gRetracePeriod.load();
    return *vblankNs != 0 && *periodNs > 0;
}

extern "C" {

void port_osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    std::lock_guard<std::mutex> g(gViLock);
    gViQueue = mq;
    gViMsg = msg;
    gViEvery = retraceCount ? retraceCount : 1;
}

// SIGUSR1: capture the next drawn frame and end the run (tools/replay.sh UNTIL=<pattern> sends it as soon as the
// condition being tested shows up in the log, so runs do not continue past it).
[[maybe_unused]] static void Port_StopSoonSignal(int) {
    gStopAt = gTick.load() + 1;
}

void port_osCreateViManager(OSPri) {
    if (gViRunning.exchange(true)) {
        return;
    }
#ifdef SIGUSR1 // not on Windows
    signal(SIGUSR1, Port_StopSoonSignal);
#endif
    if (Port_Lockstep()) {
        InputInit();
        if (getenv("PORT_STOP_AT")) gStopAt = strtoull(getenv("PORT_STOP_AT"), nullptr, 10);
        std::thread(LockstepClock).detach();
        return;
    }
    std::thread([]() {
        u32 tick = 0;
        RetraceSchedule schedule;
        if (Port_Speed() == 1.0) Port_PreciseTimingThread();
        while (!gStop) {
            schedule.Wait();
            Port_PacingNote(PORT_PACE_RETRACE, Port_SteadyNs());
            OSMesgQueue* q;
            OSMesg m;
            u32 every;
            {
                std::lock_guard<std::mutex> g(gViLock);
                q = gViQueue;
                m = gViMsg;
                every = gViEvery;
            }
            gTick = ++tick;
            if (q && (tick % every) == 0) {
                port_PostMesgFromIrq(q, m);
            }
        }
    }).detach();
}

} // extern "C"

// ---- RSP / RDP tasks --------------------------------------------------------------------------------

extern "C" {

void osSpTaskLoad(OSTask* t) { gLoaded = t; }

void osSpTaskStartGo(OSTask* t) {
    static const bool trace = getenv("PORT_TRACE") != nullptr;
    static const bool asyncTasks = getenv("PORT_ASYNC_TASKS") != nullptr;
    if (trace) fprintf(stderr, "[sp] task type=%u flags=0x%x data=%p\n", (t ? t : gLoaded)->t.type, (t ? t : gLoaded)->t.flags, (void*)(t ? t : gLoaded)->t.data_ptr);
    std::unique_lock<std::mutex> g(gTaskLock);
    gTasks.push_back(t ? t : gLoaded);
    uint64_t mine = ++gTasksSubmitted;
    gTaskQueuedCv.notify_one();
    // The RSP/RDP of the original finish before the CPU can free or reuse the task's data (scenes unload display-list
    // data right after submitting the last frame). Rendering here happens later on the window thread, so the submitting
    // thread keeps the scheduling token and waits for it. In realtime mode a timeout keeps boot (before the window
    // loop runs) alive; in lockstep mode the wait is unbounded so the game never runs ahead of the RSP.
    if (Port_Lockstep()) {
        while (!gStop && !gTaskDoneCv.wait_for(g, std::chrono::milliseconds(250), [&] { return gTasksProcessed >= mine; })) {
        }
    } else if (!asyncTasks) {
        gTaskDoneCv.wait_for(g, std::chrono::milliseconds(250), [&] { return gTasksProcessed >= mine; });
    }
}

s32 osSpTaskYielded(OSTask*) { return 0; }
void osSpTaskYield(void) {}

} // extern "C"

extern "C" void Port_RunJpegTask(void* data);
extern "C" void Port_RunAudioTask(const void* data, uint32_t size);
extern "C" void Port_RequestCapture(uint64_t frame);

// PORT_STOP_AT / PORT_SHOT_AT=f1,f2,...: whether the frame drawn now is to be saved (and the run ended at the stop frame)
static void CheckCapture(bool* stopFrame, bool* shotFrame) {
    *stopFrame = gStopAt != 0 && gTick.load() >= gStopAt;
    static std::vector<uint64_t> sShots = [] {
        std::vector<uint64_t> v;
        if (const char* e = getenv("PORT_SHOT_AT")) {
            for (const char* p = e; *p;) {
                char* end;
                uint64_t f = strtoull(p, &end, 10);
                if (end == p) break;
                v.push_back(f);
                p = (*end == ',') ? end + 1 : end;
            }
            std::sort(v.begin(), v.end());
        }
        return v;
    }();
    static size_t sShotNext = 0;
    *shotFrame = sShotNext < sShots.size() && gTick.load() >= sShots[sShotNext];
    if (*shotFrame) {
        while (sShotNext < sShots.size() && gTick.load() >= sShots[sShotNext]) sShotNext++;
    }
    if (*stopFrame || *shotFrame) {
        Port_RequestCapture(gTick.load());
    }
}

// A CPU-drawn frame buffer (RGBA5551, N64 byte order, 320 wide): drawn to the screen with a copy-mode display list.
static void PresentCpuFrame(void* fb, bool black, float xscale, bool repeatLine) {
    static Gfx dl[2048];
    static uint16_t zero[320 * 6];
    static uint16_t line[320 * 6];
    const int width = 320, rows = 240, strip = 6;
    int srcW = (int)(width * xscale + 0.5f);
    Gfx* g = dl;
    gDPPipeSync(g++);
    gDPSetColorImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, fb);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, width, rows);
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTexturePersp(g++, G_TP_NONE);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gDPSetTextureLUT(g++, G_TT_NONE);
    for (int y = 0; y < rows; y += strip) {
        const uint16_t* src = black ? zero : (const uint16_t*)fb + y * width;
        if (!black && repeatLine) { // every line is the buffer's first line
            for (int r = 0; r < strip; r++) memcpy(line + r * width, fb, width * sizeof(uint16_t));
            src = line;
        }
        gDPLoadTextureTile(g++, src, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, strip, 0, 0, width - 1, strip - 1, 0,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                           G_TX_NOLOD);
        // osViSetXScale < 1: the VI shows the first width*scale columns stretched over the screen
        gSPTextureRectangle(g++, 0, y << 2, width << 2, (y + strip) << 2, G_TX_RENDERTILE, 0, 0,
                            (int)(1024.0f * srcW / width), 1 << 10);
    }
    gDPFullSync(g++);
    gSPEndDisplayList(g++);
    Port::CancelInterpolation();
    Port::RunFrame(dl);
}

// Runs on the thread that owns the window: executes queued RSP tasks and signals completion.
static void TryPresentCpuFrame() {
    void* fb = nullptr;
    bool black;
    bool repeatLine;
    float xscale;
    {
        std::lock_guard<std::mutex> g(gSwapLock);
        std::lock_guard<std::mutex> t(gTaskLock);
        // only when no graphics task is waiting (its frame may be the one being swapped)
        bool gfxQueued = false;
        for (OSTask* q : gTasks) {
            gfxQueued |= q->t.type == M_GFXTASK;
        }
        if (gViPending != nullptr && !gfxQueued) {
            fb = gViPending;
            gViPending = nullptr;
            gSwapDone.notify_all();
        }
        black = gViBlack;
        repeatLine = gViRepeatLine;
        xscale = gViXScale;
    }
    if (fb != nullptr) {
        uint64_t stamp = Port_GpuDrawStamp(fb);
        auto last = gViStampAtLastSwap.find(fb);
        // the RDP did not render this frame buffer since it was last shown: a CPU-drawn frame
        static const bool sTraceVi = getenv("PORT_TRACE_VI") != nullptr;
        if (sTraceVi) fprintf(stderr, "[vi] swap %p stamp %llu last %lld frame %llu\n", fb, (unsigned long long)stamp,
                              last != gViStampAtLastSwap.end() ? (long long)last->second : -1LL, (unsigned long long)gTick.load());
        if (stamp == 0 || (last != gViStampAtLastSwap.end() && last->second == stamp)) {
            bool stopFrame, shotFrame;
            CheckCapture(&stopFrame, &shotFrame);
            if (sTraceVi) fprintf(stderr, "[vi] CPU present %p black %d frame %llu\n", fb, (int)black, (unsigned long long)gTick.load());
            if (sTraceVi) { // non-zero 16-bit pixels of the first 16 rows (320 wide) and in the whole buffer
                const uint16_t* p = (const uint16_t*)fb;
                int nz = 0, nzTop = 0;
                for (int i = 0; i < 320 * 240; i++) if (p[i]) { nz++; if (i < 320 * 16) nzTop++; }
                fprintf(stderr, "[vi]   nonzero %d (top16 rows %d); first:", nz, nzTop);
                for (int i = 0, k = 0; i < 320 * 240 && k < 12; i++) if (p[i]) { fprintf(stderr, " %d:%04x", i, p[i]); k++; }
                fputc('\n', stderr);
            }
            PresentCpuFrame(fb, black, xscale, repeatLine);
            if (stopFrame) {
                fprintf(stderr, "[stop] reached frame %llu\n", (unsigned long long)gTick.load());
                Port_InputFinish();
                fflush(stderr);
                _exit(0);
            }
        }
        gViStampAtLastSwap[fb] = Port_GpuDrawStamp(fb);
    }
}

void Port::PumpRspTasks() {
    TryPresentCpuFrame();
    for (;;) {
        OSTask* t = nullptr;
        {
            std::lock_guard<std::mutex> g(gTaskLock);
            if (gTasks.empty()) {
                break;
            }
            t = gTasks.front();
            gTasks.pop_front();
        }
        static const bool sTracePacing = getenv("PORT_TRACE_PACING") != nullptr;
        auto taskStart = std::chrono::steady_clock::now();
        if (t->t.type == M_GFXTASK) {
            static uint64_t gfxCount = 0;
            bool stopFrame, shotFrame;
            CheckCapture(&stopFrame, &shotFrame);
            if ((gfxCount++ % Port_RenderEvery()) == 0 || stopFrame || shotFrame) {
                Port::RunGameFrame(t->t.data_ptr, stopFrame || shotFrame); // (interp.cpp: in-between frames)
            } else if (Port::TrackFrame(t->t.data_ptr)) { // not drawn, but its state changes must still happen
                Port::CancelInterpolation();
                Port::RunFrame(t->t.data_ptr);                  // renders to a texture: draw it after all
            }
            if (stopFrame) {
                fprintf(stderr, "[stop] reached frame %llu\n", (unsigned long long)gTick.load());
                Port_InputFinish();
                fflush(stderr);
                _exit(0);
            }
            port_PostEvent(OS_EVENT_DP);
        }
        if (t->t.type == 4) { // M_NJPEGTASK: JPEG decode microcode, emulated on the host
            Port_RunJpegTask(t->t.data_ptr);
        }
        if (t->t.type == M_AUDTASK) { // audio microcode, emulated on the host (audio_hle.cpp)
            Port_RunAudioTask(t->t.data_ptr, (uint32_t)t->t.data_size);
        }
        if (sTracePacing) { // PORT_TRACE_PACING=1: RSP tasks that took long on the window thread
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - taskStart).count();
            // per retrace and task type: many short tasks add up too (a scene's JPEG backgrounds)
            static uint64_t sumTick = 0;
            static double sumMs[8] = {};
            static int sumCount[8] = {};
            uint64_t tick = gTick.load();
            if (tick != sumTick) {
                for (int k = 0; k < 8; k++) {
                    if (sumMs[k] > 30 || sumCount[k] > 20) {
                        fprintf(stderr, "[task] frame %llu: %d tasks of type %d took %.1f ms\n",
                                (unsigned long long)sumTick, sumCount[k], k, sumMs[k]);
                    }
                    sumMs[k] = 0;
                    sumCount[k] = 0;
                }
                sumTick = tick;
            }
            int k = (int)std::min<uint32_t>(t->t.type, 7);
            sumMs[k] += ms;
            sumCount[k]++;
        }
        // The completion is posted before the task counts as processed: in between, the lockstep clock saw the game
        // idle with all tasks drained and could deliver the next retrace first (runs then depended on host speed).
        port_PostEvent(OS_EVENT_SP);
        {
            std::lock_guard<std::mutex> g(gTaskLock);
            gTasksProcessed++;
        }
        gTaskDoneCv.notify_all();
    }
    TryPresentCpuFrame(); // a frame buffer swapped while the tasks above ran
}

void Port::WaitForRspTasks(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> g(gTaskLock);
    gTaskQueuedCv.wait_for(g, timeout, [] { return !gTasks.empty() || gStop; });
}

void Port::StopDevices() {
    gStop = true;
}

// ---- N64 address translation for the renderer -----------------------------------------------------------
// Display lists built by the game can reference data by segment address (0x0Nxxxxxx, resolved through the
// segment table the game sets up each frame) or by fragment link address (0x8Nxxxxxx -> where that fragment's
// data image was loaded). Fast3D calls this for every address below 4 GB.
extern "C" uintptr_t Memmap_GetSegmentVaddr(uintptr_t mask);
extern "C" uintptr_t Memmap_GetFragmentVaddr(void* addr);

extern "C" void* Port_ResolveN64Address(uintptr_t addr) {
    uintptr_t r = addr;
    if ((addr >> 28) == 0) {
        r = Memmap_GetSegmentVaddr(addr);
    } else if (addr >= 0x81000000 && addr < 0x90000000) {
        r = Memmap_GetFragmentVaddr((void*)addr);
    } else if (addr >= 0x30000000 && addr < 0x50000000) {
        r = Memmap_GetFragmentVaddr((void*)addr); // pseudo address of a relocated data blob
    }
    return (r == addr) ? nullptr : (void*)r; // unresolved (< 4 GB can never be a host pointer) // unresolvable segment address: let Fast3D skip it
}

