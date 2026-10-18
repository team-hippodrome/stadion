#ifndef PORT_COMPAT_H
#define PORT_COMPAT_H
// Declarations the game needs that libultraship does not provide. Implementations live in the
// port layer (src/port/); everything here is currently declared only.
#include <ultra64.h>
#include <string.h>
#include <strings.h>
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
void port_osViBlack(u8 black);
void port_osViSetXScale(f32 scale);
void port_osViSetMode(void* mode); // OSViMode*
s32 port_osSetTimer(OSTimer* t, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg);
#define osViSetEvent port_osViSetEvent
#define osViSwapBuffer port_osViSwapBuffer
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
// Time comes from the port's clock: virtual (frame-locked) in lockstep mode, so runs and input replays are
// reproducible (the game seeds its RNG from osGetCount).
OSTime port_osGetTime(void);
u32 port_osGetCount(void);
#define osGetTime port_osGetTime
#define osGetCount port_osGetCount

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

#ifdef __cplusplus
}
#endif
#endif
