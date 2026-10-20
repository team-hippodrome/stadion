// Small platform layer for the port's debug output (stack traces, image load address, readable-memory checks).
// macOS, Linux, Windows and Android. C and C++.
#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <signal.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <unistd.h>
#elif defined(__ANDROID__)
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <unwind.h>
#else
#include <errno.h>
#include <execinfo.h>
#include <unistd.h>
extern char __executable_start[]; // provided by the ELF linkers (GNU ld, gold, lld)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Load offset of the executable (what to subtract from a return address to get a static address for symbolizing).
static inline unsigned long Port_ImageSlide(void) {
#if defined(_WIN32)
    return (unsigned long)(uintptr_t)GetModuleHandleA(NULL);
#elif defined(__APPLE__)
    return (unsigned long)_dyld_get_image_vmaddr_slide(0);
#elif defined(__ANDROID__)
    // the game is libmain.so, loaded by the app process: its own load address
    Dl_info info;
    return dladdr((void*)&Port_ImageSlide, &info) ? (unsigned long)(uintptr_t)info.dli_fbase : 0;
#else
    return (unsigned long)(uintptr_t)__executable_start;
#endif
}

#if defined(__ANDROID__)
// backtrace() only exists from API 33: walk the stack with the unwinder instead
struct Port_BacktraceState {
    void** frames;
    int n, max;
};
static inline _Unwind_Reason_Code Port_BacktraceStep(struct _Unwind_Context* ctx, void* arg) {
    struct Port_BacktraceState* st = (struct Port_BacktraceState*)arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc != 0) {
        if (st->n >= st->max) {
            return _URC_END_OF_STACK;
        }
        st->frames[st->n++] = (void*)pc;
    }
    return _URC_NO_REASON;
}
#endif

static inline int Port_Backtrace(void** frames, int max) {
#if defined(_WIN32)
    return (int)CaptureStackBackTrace(0, (DWORD)max, frames, NULL);
#elif defined(__ANDROID__)
    struct Port_BacktraceState st = { frames, 0, max };
    _Unwind_Backtrace(Port_BacktraceStep, &st);
    return st.n;
#else
    return backtrace(frames, max);
#endif
}

// Whether n bytes at p can be read (debug checks only).
static inline int Port_Readable(const void* p, size_t n) {
    char tmp[16];
    if (n > sizeof tmp) {
        n = sizeof tmp;
    }
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof mbi) == 0) {
        return 0;
    }
    return mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
#elif defined(__APPLE__)
    vm_size_t got = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)p, n, (vm_address_t)tmp, &got) == KERN_SUCCESS;
#else
    // the kernel reads the bytes for write(); an unreadable address fails with EFAULT instead of a crash
    static int fds[2] = { -1, -1 };
    (void)tmp;
    if (fds[0] < 0 && pipe(fds) != 0) {
        return 1;
    }
    ssize_t w = write(fds[1], p, n);
    if (w > 0) {
        char drain[16];
        ssize_t r = read(fds[0], drain, (size_t)w);
        (void)r;
    }
    return w == (ssize_t)n;
#endif
}

// Gives the calling thread its own signal stack, so the crash handler (main.cpp) also reports stack overflows.
static inline void Port_AltSignalStack(void) {
#if !defined(_WIN32)
    static const size_t size = 64 * 1024;
    stack_t ss;
    ss.ss_sp = malloc(size);
    ss.ss_size = size;
    ss.ss_flags = 0;
    if (ss.ss_sp != NULL) {
        sigaltstack(&ss, NULL);
    }
#endif
}

#ifdef __cplusplus
}
#endif

#endif // PORT_PLATFORM_H
