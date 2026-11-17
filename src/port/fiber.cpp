// ucontext-based fibers (see fiber.h). A separate file: macOS only declares <ucontext.h> under _XOPEN_SOURCE, which
// would change what the other headers declare.
#include "fiber.h"

#ifdef PORT_HAVE_FIBERS
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include <sys/mman.h>
#include <ucontext.h>
#include <stdint.h>
#include <stdlib.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

struct PortFiber {
    ucontext_t uc;
    void (*fn)(void*);
    void* arg;
    void* stack;
    size_t size;
};

static void Trampoline(unsigned hi, unsigned lo) {
    PortFiber* f = (PortFiber*)(((uintptr_t)hi << 32) | (uintptr_t)lo);
    f->fn(f->arg);
    abort(); // fn must not return
}

extern "C" PortFiber* Port_FiberForHostThread(void) {
    return (PortFiber*)calloc(1, sizeof(PortFiber));
}

extern "C" PortFiber* Port_FiberCreate(void (*fn)(void*), void* arg, size_t stackBytes) {
    PortFiber* f = (PortFiber*)calloc(1, sizeof(PortFiber));
    if (f == nullptr) return nullptr;
    const size_t guard = 16384;
    f->size = stackBytes + guard;
    f->stack = mmap(nullptr, f->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (f->stack == MAP_FAILED) {
        free(f);
        return nullptr;
    }
    mprotect(f->stack, guard, PROT_NONE); // overflow faults instead of corrupting a neighbour
    f->fn = fn;
    f->arg = arg;
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = (char*)f->stack + guard;
    f->uc.uc_stack.ss_size = stackBytes;
    f->uc.uc_link = nullptr;
    makecontext(&f->uc, (void (*)(void))Trampoline, 2, (unsigned)((uintptr_t)f >> 32), (unsigned)(uintptr_t)f);
    // The frame pointer getcontext saved is the creator's: end the fiber's frame chain instead (stack walkers, such as
    // Port_BacktraceFast, follow it and would read the creator's stack).
#if defined(__APPLE__) && defined(__aarch64__)
    f->uc.uc_mcontext->__ss.__fp = 0;
#elif defined(__APPLE__) && defined(__x86_64__)
    f->uc.uc_mcontext->__ss.__rbp = 0;
#elif defined(__linux__) && defined(__x86_64__)
    f->uc.uc_mcontext.gregs[REG_RBP] = 0;
#elif defined(__linux__) && defined(__aarch64__)
    f->uc.uc_mcontext.regs[29] = 0;
#endif
    return f;
}

extern "C" void Port_FiberSwitch(PortFiber* from, PortFiber* to) {
    swapcontext(&from->uc, &to->uc);
}
#endif
