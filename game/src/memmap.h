#ifndef _MEMMAP_H_
#define _MEMMAP_H_

#ifdef M2C
#define RELOCATIONS_SIZE 1
#else
#define RELOCATIONS_SIZE
#endif

/* MIPS Relocation Types */
#define R_MIPS_32 2
#define R_MIPS_26 4
#define R_MIPS_HI16 5
#define R_MIPS_LO16 6

struct RelocTable {
    /* 0x00 */ u32 nRelocations;
    /* 0x04 */ u32 relocations[RELOCATIONS_SIZE]; // variable size
};

typedef struct Fragment {
    /* 0x00 */ u32
        inst[2]; // MIPS instructions to jump to the main function, typically something like "j 0x8xx00020; nop"
    /* 0x08 */ char magic[8];   // "FRAGMENT"
    /* 0x10 */ u32 headerSize;  // typically 0x20
    /* 0x14 */ u32 relocOffset; // relocOffset
    /* 0x18 */ u32 sizeInRom;
    /* 0x1C */ u32 sizeInRam;
    /* 0x20 */ char data[RELOCATIONS_SIZE]; // variable size
} Fragment;                                 // size = 0x20

typedef s32 (*FragmentEntry)(s32, intptr_t); // PORT64: UI callbacks pass a pointer as the second argument (was s32)

void Memmap_SetSegmentMap(u32 id, uintptr_t vaddr, size_t size);
uintptr_t Memmap_GetSegmentBaseVaddr(u32 id);
uintptr_t Memmap_GetSegmentVaddr(uintptr_t mask);
// Segment table lookup for a pointer result (HarbourMasters' name). Anything that is not segmented (a host pointer, a
// KSEG0 address) comes back unchanged. These are functions rather than casting macros so the strict type groups still
// check the argument: SEGMENTED_TO_VIRTUAL takes an address held in an integer, SEGMENTED_PTR_TO_VIRTUAL a pointer
// field that may hold a segmented address.
static inline void* Memmap_SegmentedToVirtual(uintptr_t addr) {
    return (void*)Memmap_GetSegmentVaddr(addr);
}
static inline void* Memmap_SegmentedPtrToVirtual(const void* addr) {
    return (void*)Memmap_GetSegmentVaddr((uintptr_t)addr);
}
#define SEGMENTED_TO_VIRTUAL(addr) Memmap_SegmentedToVirtual(addr)
#define SEGMENTED_PTR_TO_VIRTUAL(addr) Memmap_SegmentedPtrToVirtual(addr)
u32 Memmap_GetSegmentVaddrMask(u32 i, uintptr_t addr);
void Memmap_ClearSegmentMemmap(u32 id);
void Memmap_SetSegments(Gfx** gfxDl);
void Memmap_RelocateFragment(u32 id, struct Fragment* fragment);
u32 Port_VAlloc(void* host, u32 size, u32 link);
void Port_VFree(void* host);
u32 Port_VLinkOf(uintptr_t addr);
u32 Port_VAddrOfLink(void* host, u32 linkAddr);
u32 Port_RelocateDataBlob(struct Fragment* f);
uintptr_t Port_CallBlobEntry(void* frag, s32 a0, s32 a1);
void Memmap_NormalizeFragmentHeader(struct Fragment* f);
void Memmap_SetFragmentMap(u32 id, uintptr_t vaddr, size_t size);
uintptr_t Memmap_GetFragmentBaseVaddr(u32 id);
uintptr_t Memmap_GetFragmentVaddr(void* addr);
u32 Memmap_GetFragmentVaddrMask(u32 i, uintptr_t addr);
void Memmap_ClearFragmentMemmap(u32 id);
u32 Memmap_GetLoadedFragmentVaddr(uintptr_t addr);

#endif // _MEMMAP_H_
