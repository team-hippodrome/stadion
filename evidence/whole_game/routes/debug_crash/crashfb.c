// Evidence helper for the crash screen (DYLD_INSERT_LIBRARIES): maps a frame buffer at the fixed host address
// 0x<k>A0000000 (0x70A0000000 in these runs), whose low 32 bits (0xA0000000) survive crash_screen_set_draw_info's "| 0xA0000000" (the N64 KSEG1
// view of RDRAM), so the game's crash screen can draw into it. A watcher thread writes the buffer as a PPM
// (CRASHFB_OUT, RGBA5551 320 or 640 wide per CRASHFB_W) whenever it changes.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach.h>
static void* BASE; // the first free 0x<k>A0000000 (CRASHFB_K, default the first free k >= 0x10)
#define SIZE (4u << 20)
static void save(const uint16_t* fb, int w, int h, const char* out) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", out);
    FILE* f = fopen(tmp, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t p = fb[i];
        unsigned char c[3] = { (unsigned char)(((p >> 11) & 31) << 3), (unsigned char)(((p >> 6) & 31) << 3),
                               (unsigned char)(((p >> 1) & 31) << 3) };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    rename(tmp, out);
}
static void* watch(void* arg) {
    (void)arg;
    const char* out = getenv("CRASHFB_OUT");
    int w = getenv("CRASHFB_W") ? atoi(getenv("CRASHFB_W")) : 320, h = w == 640 ? 480 : 240;
    uint64_t last = 0;
    for (;;) {
        usleep(50);
        const uint64_t* q = (const uint64_t*)BASE;
        uint64_t sum = 1469598103934665603ull;
        for (int i = 0; i < w * h / 4; i++) sum = (sum ^ q[i]) * 1099511628211ull;
        if (sum != last) {
            last = sum;
            if (out) save((const uint16_t*)BASE, w, h, out);
        }
    }
    return NULL;
}
__attribute__((constructor)) static void init(void) {
    // VM_FLAGS_FIXED without VM_FLAGS_OVERWRITE: fails rather than replace an existing mapping
    kern_return_t kr = 1;
    vm_address_t a = 0;
    for (unsigned long k = getenv("CRASHFB_K") ? strtoul(getenv("CRASHFB_K"), 0, 0) : 0x10; k < 0x400 && kr != KERN_SUCCESS; k++) {
        a = (vm_address_t)((k << 32) | 0xA0000000ull);
        kr = vm_allocate(mach_task_self(), &a, SIZE, VM_FLAGS_FIXED);
        if (getenv("CRASHFB_K")) break;
    }
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[crashfb] no free 0x..A0000000 (%d)\n", kr);
        return;
    }
    BASE = (void*)a;
    void* p = (void*)a;
    uint16_t* fb = (uint16_t*)p;
    for (unsigned i = 0; i < SIZE / 2; i++) fb[i] = (uint16_t)((16 << 11) | (20 << 6) | (24 << 1) | 1); // a light blue "game picture"
    if (getenv("CRASHFB_OUT")) {
        pthread_t t;
        pthread_create(&t, NULL, watch, NULL);
        pthread_detach(t);
    }
    fprintf(stderr, "[crashfb] frame buffer at %p\n", p);
}
