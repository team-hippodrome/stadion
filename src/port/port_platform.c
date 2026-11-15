// Platform helpers that need a single definition (see port_platform.h for the inline ones).
#include "port_platform.h"
#include "StadionPort.h"

// The port tells N64 addresses (segment / fragment / pseudo addresses, all below 4 GB) from host pointers by their
// value. macOS and Linux place 64-bit programs above 4 GB; Windows may hand out heap and DLL memory below it, so the
// free address space there is reserved before anything else is allocated.
void Port_ReserveLowAddressSpace(void) {
#if defined(_WIN32) && defined(_WIN64)
    uintptr_t addr = 0x10000;
    const uintptr_t limit = 0x100000000ull;
    while (addr < limit) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void*)addr, &mbi, sizeof mbi) == 0) {
            break;
        }
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        uintptr_t end = base + mbi.RegionSize;
        if (end > limit) {
            end = limit;
        }
        if (mbi.State == MEM_FREE) {
            // reservations are 64 KiB aligned
            uintptr_t start = (base + 0xFFFF) & ~(uintptr_t)0xFFFF;
            if (start < end) {
                VirtualAlloc((void*)start, end - start, MEM_RESERVE, PAGE_NOACCESS);
            }
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
#endif
}

#if !defined(__EMSCRIPTEN__)
void Port_SaveWritten(void) {
}
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
void Port_SaveWritten(void) {
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.onSaveWritten) Module.onSaveWritten();
    });
}
// Web build: function pointers are indices into WebAssembly's function table, so they lie below 4 GB like the N64
// addresses the game converts (Util_ConvertAddrToVirtAddr). The table has some 10^4 entries; the N64 addresses that low
// (the start of physical memory) are never handed to the game.
EM_JS(int, Port_FunctionTableSize, (void), { return Number(wasmExports["__indirect_function_table"].length); });
int Port_IsHostFunction(uintptr_t addr) {
    static int size = 0;
    if (size == 0) {
        size = Port_FunctionTableSize();
    }
    return addr < (uintptr_t)size;
}
#endif
