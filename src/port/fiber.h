// User-mode context switching for the libultra threads (os_thread.cpp). Only where ucontext exists (macOS, glibc);
// elsewhere PORT_HAVE_FIBERS is not defined and every libultra thread stays a host thread.
#ifndef PORT_FIBER_H
#define PORT_FIBER_H

#include <stddef.h>
#if defined(__linux__)
#include <features.h> // (defines __GLIBC__: no header before this one may have pulled it in)
#endif

#if defined(__APPLE__) || (defined(__linux__) && defined(__GLIBC__))
#define PORT_HAVE_FIBERS 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortFiber PortFiber;

// The context of the calling host thread (a place to switch away from and back to).
PortFiber* Port_FiberForHostThread(void);
// A new fiber with its own stack that runs fn(arg) when first switched to. fn must never return.
PortFiber* Port_FiberCreate(void (*fn)(void*), void* arg, size_t stackBytes);
// Saves the current context in 'from' and continues 'to'.
void Port_FiberSwitch(PortFiber* from, PortFiber* to);

#ifdef __cplusplus
}
#endif

#endif
