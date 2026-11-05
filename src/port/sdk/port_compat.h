#ifndef PORT_COMPAT_H
#define PORT_COMPAT_H
// Declarations the game needs that libultraship does not provide. Implementations live in the
// port layer (src/port/); everything here is currently declared only.
#include <ultra64.h>
#include <string.h>
#include <strings.h>
#ifdef _WIN32
// the BSD byte-string functions libultra (and so the game) uses; Windows' C runtime has none (src/port/stubs.c)
void bcopy(const void* src, void* dst, int n);
int bcmp(const void* a, const void* b, int n);
void bzero(void* p, int n);
#elif defined(__ANDROID__)
// Android's C library (bionic) has no bcmp, and its bzero/bcopy macros only take object pointers (the decomp passes
// function addresses too)
#undef bzero
#undef bcopy
#define bcmp(a, b, n) memcmp((const void*)(a), (const void*)(b), (size_t)(n))
#define bzero(p, n) memset((void*)(p), 0, (size_t)(n))
#define bcopy(src, dst, n) memmove((void*)(dst), (const void*)(src), (size_t)(n))
#endif
#include <stdio.h>
#include <stdarg.h>

#ifndef OS_GBPAK_POWER
#define OS_GBPAK_POWER 0x01
#define OS_GBPAK_RSTB_DETECTION 0x04
#define OS_GBPAK_RSTB_STATUS 0x08
#define OS_GBPAK_GBCART_PULL 0x40
#define OS_GBPAK_GBCART_ON 0x80
#define OS_GBPAK_POWER_OFF 0x00
#define OS_GBPAK_POWER_ON 0x01
#define OS_GBPAK_ROM_ID_SIZE 0x50
#endif

#define SP_UCODE_DATA_SIZE 2048

#ifdef __cplusplus
extern "C" {
#endif

// Flash (os_internal_flash.h / os_flash.h)
#define FLASH_BLOCK_SIZE 128
#define FLASH_PAGE_SIZE 128
#define FLASH_START_ADDR 0x08000000
#define FLASH_CMD_REG 0x10000
#define FLASH_CMD_CHIP_ERASE 0x3C000000
#define FLASH_CMD_SECTOR_ERASE 0x4B000000
#define FLASH_CMD_EXECUTE_ERASE 0x78000000
#define FLASH_CMD_PROGRAM_PAGE 0xA5000000
#define FLASH_CMD_PAGE_PROGRAM 0xB4000000
#define FLASH_CMD_STATUS 0xD2000000
#define FLASH_CMD_ID 0xE1000000
#define FLASH_CMD_READ_ARRAY 0xF0000000
#define FLASH_STATUS_ERASE_BUSY 0x02
#define FLASH_STATUS_ERASE_OK 0 /* libultra os_flash.h values; the game's callers test == -1 / != 0 */
#define FLASH_STATUS_ERASE_ERROR -1
#define FLASH_VERSION_MX_PROTO_A 0x00C20000
#define FLASH_VERSION_MX_A 0x00C20001
#define FLASH_VERSION_MX_C 0x00C2001E
#define FLASH_LATENCY 0x5
#define FLASH_PULSE 0x0C
#define FLASH_PAGE_SIZE_SHIFT 0xF
#define FLASH_REL_DURATION 0x2
extern OSIoMesg __osFlashMsg;
extern OSMesgQueue __osFlashMessageQ;
extern OSPiHandle __osFlashHandler;
extern OSMesg __osFlashMsgBuf[1];
extern s32 __osFlashVersion;
extern u32 __osFlashID[4];
u32 __osFlashGetAddr(u32 page_num);
#define DEVICE_TYPE_FLASH 0x08

// Threads
void osCreateThread(OSThread* t, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri);
void osStartThread(OSThread* t);
void osStopThread(OSThread* t);
void osDestroyThread(OSThread* t);
OSPri osGetThreadPri(OSThread* t);
void osSetThreadPri(OSThread* t, OSPri pri);
OSThread* __osGetActiveQueue(void);

// PI / EPI / devices
extern OSPiHandle* __osPiTable;
OSPiHandle* osDriveRomInit(void);
OSPiHandle* osLeoDiskInit(void);
s32 osEPiWriteIo(OSPiHandle*, u32, u32);
s32 osEPiReadIo(OSPiHandle*, u32, u32*);
s32 osEPiLinkHandle(OSPiHandle*);
uintptr_t osVirtualToPhysical(void*); // host: identity (pointers are not physical addresses)
u32 osGetMemSize(void);
extern s32 osTvType;
extern s32 osResetType;
extern u32 osMemSize;
void osInitialize(void);
s32 osAfterPreNMI(void);

// Video / audio / RCP
extern OSViMode osViModeTable[];
extern OSViMode osViModeNtscLpn1;
extern OSViMode osViModeMpalLpn1;
void* osViGetCurrentFramebuffer(void);
u32 osAiGetStatus(void);
void osDpSetStatus(u32);
u32 osDpGetCounters(u32* buf);
s32 osSpTaskYielded(OSTask*);
void osSpTaskYield(void);
void osSpTaskLoad(OSTask*);
void osSpTaskStartGo(OSTask*);
s32 __osAiDeviceBusy(void);

// Controller pak / Transfer Pak
s32 osPfsIsPlug(OSMesgQueue*, u8*);
s32 osGbpakInit(OSMesgQueue*, OSPfs*, int);
s32 osGbpakPower(OSPfs*, s32);
s32 osGbpakGetStatus(OSPfs*, u8*);
s32 osGbpakReadWrite(OSPfs*, u16, u16, u8*, u16);
s32 osGbpakReadId(OSPfs*, OSGbpakId*, u8*);
s32 osGbpakCheckConnector(OSPfs*, u8*);

// CPU register access
void __osSetCause(u32);
void __osSetCompare(u32);
void __osSetCount(u32);
void __osSetSR(u32);
u32 __osSetFpcCsr(u32);
u32 __osDisableInt(void);
void __osRestoreInt(u32);

// libc-ish pieces the SDK supplied

// Formatter used by the game's text output (SDK xprintf.c); the callback receives output chunks.
typedef char* _PrintfOutFun(char*, const char*, size_t);
int _Printf(_PrintfOutFun* prout, char* arg, const char* fmt, va_list args);

// libultraship's versions take its union OSMesg; the game passes plain integers/pointers.
void port_osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount);
void port_osViSwapBuffer(void* fb);
void* port_osViGetCurrentFramebuffer(void);
void port_osViBlack(u8 black);
void port_osViSetXScale(f32 scale);
void port_osViSetMode(void* mode); // OSViMode*
s32 port_osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg);
#define osViSetEvent port_osViSetEvent
#define osViSwapBuffer port_osViSwapBuffer
#define osViGetCurrentFramebuffer port_osViGetCurrentFramebuffer
#define osViBlack port_osViBlack
#define osViSetXScale port_osViSetXScale
#define osViSetMode port_osViSetMode
#define osCreateViManager port_osCreateViManager
#define osCartRomInit port_osCartRomInit
#define osEPiStartDma port_osEPiStartDma
OSPiHandle* port_osCartRomInit(void);
void port_osCreateViManager(OSPri pri);
s32 port_osEPiStartDma(OSPiHandle*, OSIoMesg*, s32);
#define osSetTimer port_osSetTimer
// The game's sound engine runs unless switched off (PORT_GAME_AUDIO=0, or replaying an input recording made before
// the engine was ported: src/port/os_devices.cpp). Off, its entry points return at once as they did then.
int Port_GameAudio(void);
// The N64's loading time where the game's timing counts on it (the boot diorama's model loads, which the music is timed
// around), as waits; off when replaying an input recording made before them (src/port/os_devices.cpp).
int Port_LoadWaits(void);
// The controllers plugged in, as osContInit's controller bits: 1 always, 2-4 as the launcher's Controls panel,
// PORT_CONTROLLERS or a replayed recording say (src/port/os_devices.cpp).
int Port_ControllerBits(void);
// Blocks the calling game thread for that many retraces when Port_LoadWaits() is on (game/src/port_audio.c).
void Port_LoadStall(int retraces);
// A horizontal view half-angle (binary angle, 0x10000 = 360 degrees) widened for the picture's shape: unchanged at 4:3,
// wider at 16:9 (src/port/Engine.cpp). For game checks of what the camera sees, which assume the N64's 4:3 picture.
s16 Port_WidenViewAngle(s16 angle);
// Horizontal camera spans follow the projection's width relative to the original 4:3 view.
float Port_WidenViewSpan(float span);
// Full-screen 2D effects made for the 4:3 picture (the battle's strip wipe): the textured rectangles between these
// markers are stretched over the whole width of a widescreen picture (libultraship patch, interpreter.cpp sWideRects).
#define gDPPortWideRectsBegin(pkt) gDPNoOpTag3(pkt, 0x50, 0, 1)
#define gDPPortWideRectsEnd(pkt) gDPNoOpTag3(pkt, 0x50, 0, 0)
// The battle HUD in a widescreen picture: what is drawn between these markers moves to the left or right edge of the
// picture (by the margin beside the 4:3 frame) and is not cut at the 4:3 sides (libultraship patch, interpreter.cpp
// sAnchor), so the status boxes, portraits and menus slide in from the screen's edges. Nothing changes at 4:3.
#define gDPPortAnchorLeft(pkt) gDPNoOpTag3(pkt, 0x52, 0, 1)
#define gDPPortAnchorRight(pkt) gDPNoOpTag3(pkt, 0x52, 0, 2)
#define gDPPortAnchorEnd(pkt) gDPNoOpTag3(pkt, 0x52, 0, 0)
// The battle's two portrait cameras (CameraNode*): their portraits are anchored like the HUD when drawn
// (battle_engine_main.c Battle_FillActivePokemonThumbnail);
// set by the battle every frame it draws its HUD; a registration older than a few frames no longer counts.
void Port_SetHudAnchorCameras(const void* left, const void* right);
int Port_HudAnchorSide(const void* camera); // -1 left, 1 right, 0 none
// Preserve the CPU pixels of the next colour image before a GPU blend (interpreter.cpp RttInitialize).
#define gDPPortPreserveColorImage(pkt) gDPNoOpTag3(pkt, 0x51, 0, 1)
#define PORT_AUDIO_OFF() (!Port_GameAudio())
// Audio interface: a virtual DMA FIFO in game time, played through SDL3 (src/port/audio_out.cpp)
s32 port_osAiSetNextBuffer(void* buf, u32 size);
u32 port_osAiGetLength(void);
#define osAiSetNextBuffer port_osAiSetNextBuffer
#define osAiGetLength port_osAiGetLength
// Time comes from the port's clock: virtual (frame-locked) in lockstep mode, so runs and input replays are
// reproducible (the game seeds its RNG from osGetCount).
OSTime port_osGetTime(void);
u32 port_osGetCount(void);
#define osGetTime port_osGetTime
#define osGetCount port_osGetCount

// Controller setup touches SDL and the mapping CVars; serialize it with polling on the window thread.
s32 port_osContInit(OSMesgQueue* mq, u8* bits, OSContStatus* status);
#define osContInit port_osContInit

// libultraship's controller read posts its completion with its own non-blocking queue code, which does not wake a
// game thread blocked in osRecvMesg; the wrapper posts then wakes.
s32 port_osContStartReadData(OSMesgQueue* mq);
#define osContStartReadData port_osContStartReadData
// Pads are polled on the window thread (SDL/Cocoa input must not be touched from game threads); game threads get
// the latest snapshot.
void port_osContGetReadData(OSContPad* pad);
#define osContGetReadData port_osContGetReadData

// RSP microcode symbols (linked from ROM data in the original)
extern long long int rspbootTextStart[], rspbootTextEnd[];
extern long long int aspMainTextStart[], aspMainTextEnd[];
extern long long int aspMainDataStart[], aspMainDataEnd[];

// Battle cross-check against the original game in ares (src/port/xcheck.cpp, tools/battle_xcheck.py); all off unless
// PORT_XCHECK* is set
int Port_XcheckEnabled(void);
int Port_XcheckForcingRandom(void);
int Port_XcheckCom(void);
int Port_XcheckRandom(int fn, int arg, int* out, void* caller);
int Port_XcheckHasCase(void);
void Port_XcheckPatchMon(int side, int slot, void* battleMon);
void Port_XcheckApplyCaseParties(void);
int Port_XcheckWantDraw(void);
int Port_XcheckNoMusic(void);
void Port_XcheckGameFrame(void);
void Port_XcheckSfx(int kind, unsigned a, int b, int c);
unsigned long long Port_XcheckOsGetTime(void);
void Port_XcheckWriteEvent(const char* name, const void* state0, const void* state1, const void* scene, unsigned damage,
                           const void* messageQueues);
void Port_XcheckBattleEvent(const char* name);
void Port_XcheckMoveAnim(int side, int move);
int Port_XcheckLoadWait(void);
int Port_XcheckAnimActive(void);
const char* Port_XcheckFramePath(void);

// A debug switch from the environment, read once per call site: getenv scans the whole environment, and the port's
// debug checks sit in per-frame and per-draw code (Font_DrawChar, the geo renderer, the battle HUD). The switches
// are set before launch and never change while the game runs. (Statement expression: the game builds with clang.)
#define PORT_GETENV_ONCE(name)                                                  \
    ({                                                                          \
        static const char* sPortEnvValue_ = (const char*)1; /* 1: not read */   \
        if (sPortEnvValue_ == (const char*)1) sPortEnvValue_ = getenv(name);    \
        sPortEnvValue_;                                                         \
    })

#ifdef __cplusplus
}
#endif
#endif
