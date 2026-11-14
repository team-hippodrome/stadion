#ifndef PORT_ULTRA64_H
#define PORT_ULTRA64_H
// Replacement for the N64 SDK <ultra64.h>: routes the game's SDK includes to libultraship.
// Only the libultra half of libultraship is included; its color.h defines Color_* types that
// collide with the game's own include/color.h.
#define PORT_NO_LUS_ABI
// The N64 SDK declares OSMesg as void*; libultraship uses a union. The game relies on the void*
// form (casts, arithmetic), so the union is hidden under another name for the duration of the
// libultraship headers. Message queues are re-implemented by the port (blocking semantics for
// the game's threads), so the libultraship entry points are renamed to the port_* versions.
#define OSMesg LUS_OSMesg
// libultra.h minus abi.h: libultraship's abi.h is a different (HLE) audio command set.
#include <libultraship/libultra/controller.h>
#include <libultraship/libultra/convert.h>
#include <libultraship/libultra/eeprom.h>
#include <libultraship/libultra/exception.h>
#include <libultraship/libultra/gbi.h>
#include <libultraship/libultra/gs2dex.h>
#include <libultraship/libultra/gu.h>
#include <libultraship/libultra/os.h>
#include <libultraship/libultra/internal.h>
#include <libultraship/libultra/interrupt.h>
#include <libultraship/libultra/mbi.h>
#include <libultraship/libultra/message.h>
#include <libultraship/libultra/motor.h>
#include <libultraship/libultra/pfs.h>
#include <libultraship/libultra/pi.h>
#include <libultraship/libultra/printf.h>
#include <libultraship/libultra/r4300.h>
#include <libultraship/libultra/rcp.h>
#include <libultraship/libultra/rdp.h>
#include <libultraship/libultra/sptask.h>
#include <libultraship/libultra/thread.h>
#include <libultraship/libultra/time.h>
#include <libultraship/libultra/types.h>
#include <libultraship/libultra/vi.h>
#undef OSMesg
typedef void* OSMesg;
// HarbourMasters-style message constructors, rewritten for the void* OSMesg. Use OS_MESG_32 for integer
// messages ('DONE', event codes) and OS_MESG_PTR for pointers; read integers back with OS_MESG_GET_32.
#undef OS_MESG_8
#undef OS_MESG_16
#undef OS_MESG_32
#undef OS_MESG_PTR
#undef osSendMesg8
#undef osSendMesg16
#undef osSendMesg32
#undef osSendMesgPtr
#define OS_MESG_32(x) ((OSMesg)(uintptr_t)(u32)(x))
#define OS_MESG_PTR(x) ((OSMesg)(x))
#define OS_MESG_GET_32(msg) ((u32)(uintptr_t)(msg))
#ifdef __cplusplus
extern "C" {
#endif
void port_osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count);
s32 port_osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag);
s32 port_osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag);
s32 port_osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag);
void port_osSetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg);
#ifdef __cplusplus
}
#endif
#define osCreateMesgQueue port_osCreateMesgQueue
#define osSendMesg port_osSendMesg
#define osJamMesg port_osJamMesg
#ifdef __cplusplus
#define osRecvMesg port_osRecvMesg
#else
// The decomp passes the address of an s32 (or smaller) local as the receive slot in many places; the host's
// message slot is pointer-sized, so the write width follows the type of the pointer the caller supplied.
s32 port_osRecvMesgSz(OSMesgQueue* mq, void* msg, s32 flag, int bytes);
#define osRecvMesg(mq, msg, flag) \
    port_osRecvMesgSz((mq), (msg), (flag), _Generic((msg), void*: 8, default: (int)sizeof(*(msg))))
// (typed destinations get exactly their size: a struct or enum of 4 bytes no longer receives an 8-byte write)
#endif
#define osSetEventMesg port_osSetEventMesg
#include <stdint.h>
#include <stddef.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Present in the SDK's os_gbpak.h but not in libultraship.
typedef struct {
    u16 fixed1;
    u16 start_address;
    u8 nintendo_chr[0x30];
    u8 game_title[16];
    u16 company_code;
    u8 body_code;
    u8 cart_type;
    u8 rom_size;
    u8 ram_size;
    u8 country_code;
    u8 fixed2;
    u8 version;
    u8 isum;
    u16 sum;
} OSGbpakId;

// libultraship only provides the __-prefixed forms of these; ports route the public names to them.
// Raw N64 display lists, vertices and matrices from ROM-loaded assets are translated on first use (gfx_translate.cpp).
#ifdef __cplusplus
extern "C" {
#endif
void* Port_GfxPrepareDL(uintptr_t dl);
void* Port_GfxPrepareVtx(void* v, int n);
void* Port_GfxPrepareMtx(void* m);
void Port_GfxFrameDone(void);
#ifdef __cplusplus
}
#endif
#define gSPDisplayList(pkt, dl) __gSPDisplayList(pkt, Port_GfxPrepareDL((uintptr_t)(dl)))
#define gSPVertex(pkt, v, n, v0) __gSPVertex(pkt, Port_GfxPrepareVtx((void*)(v), (n)), n, v0)
#define gSPSegment(pkt, segment, base) __gSPSegment(pkt, segment, base)

// Original SDK audio command set (libnaudio builds Acmd lists with these numbers).
#include "PR/abi.h"

// Same translation for the other GBI macros that carry data addresses.
#undef gSPBranchList
// A branch to NULL is the placeholder game code leaves to be patched later; if it never is, the list should simply
// end (the RSP would have jumped to garbage), so a NULL target is emitted as G_ENDDL.
#define gSPBranchList(pkt, dl)                                                         \
    do {                                                                               \
        void* _dl = Port_GfxPrepareDL((uintptr_t)(dl));                                \
        if (_dl == NULL) {                                                             \
            Gfx* _e = (Gfx*)(pkt);                                                     \
            _e->words.w0 = (uintptr_t)(G_ENDDL) << 24;                                 \
            _e->words.w1 = 0;                                                          \
        } else {                                                                       \
            gDma1p(pkt, G_DL, _dl, 0, G_DL_NOPUSH);                                    \
        }                                                                              \
    } while (0)
#undef gSPMatrix
#define gSPMatrix(pkt, m, p) gDma2p((pkt), G_MTX, Port_GfxPrepareMtx((void*)(m)), sizeof(Mtx), (p) ^ G_MTX_PUSH, 0)

// Hardware register access (IO_READ/IO_WRITE) goes to a scratch page; see port_hw_reg in src/port/stubs.c.
#undef IO_READ
#undef IO_WRITE
#ifdef __cplusplus
extern "C" {
#endif
void* port_hw_reg(unsigned long addr);
#ifdef __cplusplus
}
#endif
#define IO_READ(addr) (*(vu32*)port_hw_reg((unsigned long)(addr)))
#define IO_WRITE(addr, data) (*(vu32*)port_hw_reg((unsigned long)(addr)) = (u32)(data))

#endif
