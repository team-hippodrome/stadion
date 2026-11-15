// Host replacement for the game's audio microcode (aspMain, M_AUDTASK): the "n_audio" ABI of the SDK's n_audio
// library, the variant whose ucode data has 0x127c at offset 0x10 (mupen64plus-rsp-hle calls it "naudio"). The
// synthesizer (game/src/libnaudio) builds a command list per audio frame; this runs it.
//
// DMEM is modelled byte for byte in N64 byte order. Buffers live at fixed DMEM places (offsets in the commands are
// relative to MAIN). Commands that carry a DRAM address got the full host pointer recorded beside them when the list
// was built (Port_AcmdAddr, called by the n_abi.h macros): the 24/32-bit command fields cannot hold one. DRAM data is
// host-order 16-bit samples except the sample data DMA'd from ROM (PORT_ACMD_BE), and the RSP-private state blocks
// (ADPCM / resampler / envelope state) are only read back by this file, so their layout is ours.
//
// Command 14 is A_POLEF in the library, but this microcode's jump table sends it into the middle of SETVOL (IMEM
// 0x12b0: it only replaces the low half of the right-channel ramp rate), so that is what it does here as well.
#include "Engine.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include "StadionPort.h"

namespace {

constexpr uint16_t kCount = 0x170; // bytes per buffer: 184 samples
constexpr uint16_t kMain = 0x4f0;
constexpr uint16_t kMain2 = 0x660;
constexpr uint16_t kDryL = 0x9d0;
constexpr uint16_t kDryR = 0xb40;
constexpr uint16_t kWetL = 0xcb0;
constexpr uint16_t kWetR = 0xe20;

enum { kInit = 1, kLoop = 2, kLeft = 2, kVol = 4 }; // A_INIT, A_LOOP, A_LEFT, A_VOL

struct AddrEntry {
    uintptr_t addr;
    int kind; // 0 host-order samples, 1 N64 byte order
};
std::mutex gAddrLock;
std::unordered_map<const void*, AddrEntry> gAddr;

uint8_t gDmem[0x1000];
int16_t gTable[0x100]; // ADPCM codebook (LOADADPCM)
int16_t gVol[2], gTarget[2], gDry, gWet;
int32_t gRate[2];
uintptr_t gLoop;
int16_t gLut[64 * 4];
bool gLutLoaded = false;

int16_t Clamp16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

int16_t Rd(uint16_t a) {
    a &= 0xffe;
    return (int16_t)((gDmem[a] << 8) | gDmem[a + 1]);
}
void Wr(uint16_t a, int16_t v) {
    a &= 0xffe;
    gDmem[a] = (uint8_t)((uint16_t)v >> 8);
    gDmem[a + 1] = (uint8_t)v;
}
uint8_t Rb(uint16_t a) {
    return gDmem[a & 0xfff];
}

bool LookupAddr(const void* cmd, AddrEntry* out) {
    std::lock_guard<std::mutex> g(gAddrLock);
    auto it = gAddr.find(cmd);
    if (it == gAddr.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

void LoadLut() {
    if (gLutLoaded) {
        return;
    }
    // the resampler's 64 x 4 tap table is part of the microcode data (ROM 0x7F670 + 0xB0)
    const uint8_t* rom = Port::RomData();
    size_t off = 0x7F670 + 0xB0;
    if (rom != nullptr && Port::RomSize() >= off + sizeof gLut) {
        for (int i = 0; i < 64 * 4; i++) {
            gLut[i] = (int16_t)((rom[off + 2 * i] << 8) | rom[off + 2 * i + 1]);
        }
    }
    if (gLut[0] != 0x0c39 || gLut[1] != 0x66ad) {
        fprintf(stderr, "[audio] resampler table not found in the ROM's microcode data\n");
    }
    gLutLoaded = true;
}

bool Trace() {
    static const bool on = getenv("PORT_TRACE_ACMD") != nullptr;
    return on;
}

// ---- DMA ---------------------------------------------------------------------------------------------------------
void Load(uint16_t dmem, const AddrEntry& e, uint16_t count) {
    dmem &= ~3;
    uintptr_t addr = e.addr & ~(uintptr_t)7;
    count = (count + 7) & ~7;
    const uint8_t* src = (const uint8_t*)addr;
    if (e.kind == 1) {
        for (uint16_t i = 0; i < count; i++) {
            gDmem[(dmem + i) & 0xfff] = src[i];
        }
    } else {
        const int16_t* s = (const int16_t*)src;
        for (uint16_t i = 0; i < count / 2; i++) {
            Wr(dmem + 2 * i, s[i]);
        }
    }
}

void Save(uint16_t dmem, const AddrEntry& e, uint16_t count) {
    dmem &= ~3;
    uintptr_t addr = e.addr & ~(uintptr_t)7;
    count = (count + 7) & ~7;
    uint8_t* dst = (uint8_t*)addr;
    if (e.kind == 1) {
        for (uint16_t i = 0; i < count; i++) {
            dst[i] = gDmem[(dmem + i) & 0xfff];
        }
    } else {
        int16_t* d = (int16_t*)dst;
        for (uint16_t i = 0; i < count / 2; i++) {
            d[i] = Rd(dmem + 2 * i);
        }
    }
}

// ---- commands ------------------------------------------------------------------------------------------------------
void Adpcm(uint32_t w0, uint32_t w1, const AddrEntry& state) {
    uint8_t flags = w1 >> 28;
    uint16_t count = ((w1 >> 16) & 0xfff);
    uint16_t dmemi = ((w1 >> 12) & 0xf) + kMain;
    uint16_t dmemo = (w1 & 0xfff) + kMain;
    (void)w0;
    count = (count + 0x1f) & ~0x1f;

    int16_t last[16];
    if (flags & kInit) {
        memset(last, 0, sizeof last);
    } else {
        const int16_t* src = (const int16_t*)((flags & kLoop) ? gLoop : state.addr);
        if (src == nullptr) {
            memset(last, 0, sizeof last);
        } else {
            memcpy(last, src, sizeof last);
        }
    }
    for (int i = 0; i < 16; i++, dmemo += 2) {
        Wr(dmemo, last[i]);
    }
    while (count != 0) {
        uint8_t code = Rb(dmemi++);
        unsigned scale = (code & 0xf0) >> 4;
        const int16_t* book1 = gTable + ((code & 0xf) << 4);
        const int16_t* book2 = book1 + 8;
        unsigned rshift = scale < 12 ? 12 - scale : 0;
        int16_t frame[16];
        for (int i = 0; i < 8; i++) {
            uint8_t b = Rb(dmemi++);
            frame[2 * i] = (int16_t)((uint16_t)(b & 0xf0) << 8) >> rshift;
            frame[2 * i + 1] = (int16_t)((uint16_t)(b & 0x0f) << 12) >> rshift;
        }
        for (int half = 0; half < 2; half++) {
            const int16_t* src = frame + 8 * half;
            int16_t* dst = last + 8 * half;
            // the predictor uses the two samples before this half: the previous frame's last two, then this frame's
            int16_t l1 = half == 0 ? last[14] : last[6];
            int16_t l2 = half == 0 ? last[15] : last[7];
            int16_t out[8];
            for (int i = 0; i < 8; i++) {
                int32_t accu = (int32_t)src[i] << 11;
                accu += book1[i] * l1 + book2[i] * l2;
                for (int j = 0; j < i; j++) {
                    accu += book2[i - 1 - j] * src[j];
                }
                out[i] = Clamp16(accu >> 11);
            }
            memcpy(dst, out, sizeof out);
        }
        for (int i = 0; i < 16; i++, dmemo += 2) {
            Wr(dmemo, last[i]);
        }
        count -= 32;
    }
    if (state.addr != 0) {
        memcpy((void*)state.addr, last, sizeof last);
    }
}

void Resample(uint32_t w1, const AddrEntry& state) {
    uint8_t flags = w1 >> 30;
    uint32_t pitch = ((w1 >> 14) & 0xffff) << 1; // Q16.16
    uint16_t dmemi = ((w1 >> 2) & 0xfff) + kMain;
    uint16_t dmemo = (w1 & 3) ? kMain2 : kMain;
    int16_t* st = (int16_t*)state.addr; // [0..3] last input samples, [4] fractional position
    uint32_t accu;
    uint16_t ipos = (uint16_t)(dmemi - 8);
    LoadLut();
    if ((flags & kInit) || st == nullptr) {
        for (int k = 0; k < 4; k++) {
            Wr(ipos + 2 * k, 0);
        }
        accu = 0;
    } else {
        for (int k = 0; k < 4; k++) {
            Wr(ipos + 2 * k, st[k]);
        }
        accu = (uint16_t)st[4];
    }
    for (uint16_t n = kCount / 2; n != 0; n--) {
        const int16_t* lut = gLut + ((accu & 0xfc00) >> 8);
        int32_t v = Rd(ipos) * lut[0] + Rd(ipos + 2) * lut[1] + Rd(ipos + 4) * lut[2] + Rd(ipos + 6) * lut[3];
        Wr(dmemo, Clamp16(v >> 15));
        dmemo += 2;
        accu += pitch;
        ipos += (uint16_t)((accu >> 16) * 2);
        accu &= 0xffff;
    }
    if (st != nullptr) {
        for (int k = 0; k < 4; k++) {
            st[k] = Rd(ipos + 2 * k);
        }
        st[4] = (int16_t)accu;
    }
}

struct Ramp {
    int64_t value, step, target;
    int16_t Step() {
        value += step;
        bool reached = step <= 0 ? value <= target : value >= target;
        if (reached) {
            value = target;
            step = 0;
        }
        return (int16_t)(value >> 16);
    }
};

// state block (ENVMIX_STATE, 80 bytes): wet, dry, two targets, two steps, two values
struct EnvState {
    int16_t wet, dry, target0, target1;
    int32_t step0, step1, value0, value1;
};

void EnvMixer(uint32_t w0, const AddrEntry& state) {
    uint8_t flags = (w0 >> 16) & 0xff;
    gVol[1] = (int16_t)w0;
    EnvState* st = (EnvState*)state.addr;
    Ramp ramps[2];
    int16_t dry = gDry, wet = gWet;
    if ((flags & kInit) || st == nullptr) {
        ramps[0].step = gRate[0] / 8;
        ramps[0].value = (int64_t)gVol[0] << 16;
        ramps[0].target = (int64_t)gTarget[0] << 16;
        ramps[1].step = gRate[1] / 8;
        ramps[1].value = (int64_t)gVol[1] << 16;
        ramps[1].target = (int64_t)gTarget[1] << 16;
    } else {
        wet = st->wet;
        dry = st->dry;
        ramps[0].target = (int64_t)st->target0 << 16;
        ramps[1].target = (int64_t)st->target1 << 16;
        ramps[0].step = st->step0;
        ramps[1].step = st->step1;
        ramps[0].value = st->value0;
        ramps[1].value = st->value1;
    }
    for (uint16_t k = 0; k < kCount / 2; k++) {
        int16_t lv = ramps[0].Step();
        int16_t rv = ramps[1].Step();
        int16_t in = Rd(kMain + 2 * k);
        int16_t g[4] = {
            Clamp16((lv * dry + 0x4000) >> 15),
            Clamp16((rv * dry + 0x4000) >> 15),
            Clamp16((lv * wet + 0x4000) >> 15),
            Clamp16((rv * wet + 0x4000) >> 15),
        };
        const uint16_t dst[4] = { kDryL, kDryR, kWetL, kWetR };
        for (int i = 0; i < 4; i++) {
            uint16_t a = dst[i] + 2 * k;
            Wr(a, Clamp16(Rd(a) + ((in * g[i]) >> 15)));
        }
    }
    if (st != nullptr) {
        st->wet = wet;
        st->dry = dry;
        st->target0 = (int16_t)(ramps[0].target >> 16);
        st->target1 = (int16_t)(ramps[1].target >> 16);
        st->step0 = (int32_t)ramps[0].step;
        st->step1 = (int32_t)ramps[1].step;
        st->value0 = (int32_t)ramps[0].value;
        st->value1 = (int32_t)ramps[1].value;
    }
}

void Mix(uint32_t w0, uint32_t w1) {
    int16_t gain = (int16_t)w0;
    uint16_t dmemi = (uint16_t)((w1 >> 16) + kMain);
    uint16_t dmemo = (uint16_t)((w1 & 0xffff) + kMain);
    for (uint16_t k = 0; k < kCount / 2; k++) {
        uint16_t o = dmemo + 2 * k;
        Wr(o, Clamp16(Rd(o) + ((Rd(dmemi + 2 * k) * gain) >> 15)));
    }
}

void Interleave() {
    int16_t l[kCount / 2], r[kCount / 2];
    for (uint16_t k = 0; k < kCount / 2; k++) {
        l[k] = Rd(kDryL + 2 * k);
        r[k] = Rd(kDryR + 2 * k);
    }
    for (uint16_t k = 0; k < kCount / 2; k++) {
        Wr(kMain + 4 * k, l[k]);
        Wr(kMain + 4 * k + 2, r[k]);
    }
}

void Warn(const char* what, uint32_t w0, uint32_t w1) {
    static int shown = 0;
    if (shown++ < 8) {
        fprintf(stderr, "[audio] %s: %08x %08x\n", what, (unsigned)w0, (unsigned)w1);
    }
}

} // namespace

extern "C" {

// Called by the n_abi.h macros while the list is built: remember the full address of the command's DRAM operand.
unsigned int Port_AcmdAddr(void* cmd, uintptr_t addr, int kind) {
    std::lock_guard<std::mutex> g(gAddrLock);
    gAddr[cmd] = AddrEntry{ addr, kind };
    return (unsigned int)addr;
}

// One M_AUDTASK: data is the command list, size its length in bytes.
void Port_RunAudioTask(const void* data, uint32_t size) {
    static const bool skip = getenv("PORT_AUDIO_HLE") != nullptr && atoi(getenv("PORT_AUDIO_HLE")) == 0; // debug
    if (skip) {
        return;
    }
    const uint32_t* list = (const uint32_t*)data;
    uint32_t n = size / 8;
    static uint64_t sTasks = 0;
    bool trace = Trace() && sTasks < 3;
    sTasks++;
    for (uint32_t i = 0; i < n; i++) {
        const void* cmd = list + 2 * i;
        uint32_t w0 = list[2 * i];
        uint32_t w1 = list[2 * i + 1];
        AddrEntry e{ 0, 0 };
        uint8_t op = (w0 >> 24) & 0x7f;
        if (trace) {
            AddrEntry t{ 0, 0 };
            bool has = LookupAddr(cmd, &t);
            fprintf(stderr, "[acmd] %2u %08x %08x", (unsigned)op, (unsigned)w0, (unsigned)w1);
            if (has) {
                fprintf(stderr, "  @%p%s", (void*)t.addr, t.kind ? " be" : "");
            }
            fputc('\n', stderr);
        }
        switch (op) {
            case 0: // SPNOOP
                break;
            case 1: // ADPCM
                LookupAddr(cmd, &e);
                Adpcm(w0, w1, e);
                break;
            case 2: { // CLEARBUFF
                uint16_t dmem = (uint16_t)(w0 + kMain);
                for (uint16_t c = w1 & 0xfff; c != 0; c--) {
                    gDmem[dmem++ & 0xfff] = 0;
                }
                break;
            }
            case 3: // ENVMIXER
                LookupAddr(cmd, &e);
                EnvMixer(w0, e);
                break;
            case 4: // LOADBUFF
                if (LookupAddr(cmd, &e) && e.addr != 0) {
                    Load((uint16_t)((w0 & 0xfff) + kMain), e, (w0 >> 12) & 0xfff);
                } else {
                    Warn("LOADBUFF without address", w0, w1);
                }
                break;
            case 5: // RESAMPLE
                LookupAddr(cmd, &e);
                Resample(w1, e);
                break;
            case 6: // SAVEBUFF
                if (LookupAddr(cmd, &e) && e.addr != 0) {
                    Save((uint16_t)((w0 & 0xfff) + kMain), e, (w0 >> 12) & 0xfff);
                } else {
                    Warn("SAVEBUFF without address", w0, w1);
                }
                break;
            case 9: { // SETVOL
                uint8_t flags = (w0 >> 16) & 0xff;
                if (flags & kVol) {
                    if (flags & kLeft) {
                        gVol[0] = (int16_t)w0;
                        gDry = (int16_t)(w1 >> 16);
                        gWet = (int16_t)w1;
                    } else {
                        gTarget[1] = (int16_t)w0;
                        gRate[1] = (int32_t)w1;
                    }
                } else {
                    gTarget[0] = (int16_t)w0;
                    gRate[0] = (int32_t)w1;
                }
                break;
            }
            case 10: { // DMEMMOVE
                uint16_t dmemi = (uint16_t)(w0 + kMain);
                uint16_t dmemo = (uint16_t)((w1 >> 16) + kMain);
                uint16_t count = (uint16_t)(((w1 & 0xffff) + 3) & ~3);
                for (; count != 0; count--) {
                    gDmem[dmemo++ & 0xfff] = gDmem[dmemi++ & 0xfff];
                }
                break;
            }
            case 11: // LOADADPCM
                if (LookupAddr(cmd, &e) && e.addr != 0) {
                    uint32_t count = (w0 & 0xffff) >> 1;
                    if (count > 0x100) {
                        count = 0x100;
                    }
                    memcpy(gTable, (const void*)e.addr, count * 2);
                }
                break;
            case 12: // MIXER
                Mix(w0, w1);
                break;
            case 13: // INTERLEAVE
                Interleave();
                break;
            case 14: // jumps into SETVOL (see the top of the file)
                gRate[1] = (int32_t)(((uint32_t)gRate[1] & ~0xffffu) | (w1 & 0xffff));
                break;
            case 15: // SETLOOP
                if (LookupAddr(cmd, &e)) {
                    gLoop = e.addr;
                }
                break;
            default:
                Warn("unknown audio command", w0, w1);
                break;
        }
    }
}

} // extern "C"
