#ifndef _UTIL_H_
#define _UTIL_H_

extern u32 gExpansionRAMStart;

uintptr_t Util_ConvertAddrToVirtAddr(uintptr_t addr);
// Util_ConvertAddrToVirtAddr for a pointer result: segmented, physical, fragment-space or host address in, pointer out.
// ADDR_TO_VIRTUAL takes an address held in an integer, ADDR_PTR_TO_VIRTUAL a pointer that may hold one (see
// SEGMENTED_TO_VIRTUAL in memmap.h for why these are functions).
static inline void* Util_AddrToVirtual(uintptr_t addr) {
    return (void*)Util_ConvertAddrToVirtAddr(addr);
}
static inline void* Util_AddrPtrToVirtual(const void* addr) {
    return (void*)Util_ConvertAddrToVirtAddr((uintptr_t)addr);
}
#define ADDR_TO_VIRTUAL(addr) Util_AddrToVirtual(addr)
#define ADDR_PTR_TO_VIRTUAL(addr) Util_AddrPtrToVirtual(addr)
void Util_Memcpy(u32* dest, u32* src, int size);
void Util_InitMainPools(void);
void* Util_Malloc(s32 size);
void Util_Free(void* ptr);
void Util_DrawRect(Gfx** dlist, s32 ulx, s32 lrx, u16 color);
void Util_DrawMemProfiler(Gfx** dlist);
void Util_Memclear(u64* dest, u32 size);
s32 Util_CheckStubMemArea(u64* ptr); // unused

#endif // _UTIL_H_
