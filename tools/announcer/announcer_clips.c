// Decodes every announcer clip of a Stadium 1 ROM to WAV files, for checking subtitle transcriptions against the
// audio (docs: hippodrome-utils docs/specs/SUBTITLES.md). Nothing decoded from a ROM goes into a repository.
//
//   cc -O2 -I tools/announcer -I game/src tools/announcer/announcer_clips.c game/src/libgsm/libgsm.c -o announcer_clips
//   ./announcer_clips <rom.z64> <out dir>
//
// Writes <out dir>/clipNNN.wav (NNN = the clip id the game queues: Audio_QueueSequenceId, one-based, decimal) and
// <out dir>/clips.tsv (id, ROM offset, bytes, frames, seconds).
//
// Layout (all releases): the audio header at ROM 0x15C0000 holds seven big-endian words, [4] the offset of a sequence
// file whose third entry is the announcer bank, itself a sequence file (u16 revision, u16 count, then {u32 offset,
// u32 length} relative to its start). A clip is "MORT", u16 frame count, u16 sample rate, u32 data words, then a GSM
// 06.10 bit stream read from little-endian bit positions of big-endian 32-bit words (game/src/gsm_decoder.c), 160
// samples a frame, with runs of silent frames.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libgsm/libgsm.h"

static u8* gRom;
static long gRomSize;

static u32 Be32(u32 off) {
    return ((u32)gRom[off] << 24) | ((u32)gRom[off + 1] << 16) | ((u32)gRom[off + 2] << 8) | gRom[off + 3];
}
static u32 Be16(u32 off) {
    return ((u32)gRom[off] << 8) | gRom[off + 1];
}

#define STEP(x, off, k, scale) ((s16)((((s32)(((((x) + (off)) << 10) + (k)) * (scale)) + 0x4000) >> 15) * 2))

static void DecodeFrame(gsm_state* state, s16* LARc, s16* Nc, s16* bc, s16* Mc, s16* xmaxc, xMc* xM, s16* out) {
    s16 erp[40];
    s16* LARpp;
    int i;

    for (i = 0; i < 4; i++) {
        Gsm_RPE_Decoding(xmaxc[i], Mc[i], &xM[i], erp);
        Gsm_Long_term_synthesis_filtering(state, Nc[i], bc[i], erp);
    }
    LARpp = state->LARpp[state->frame_index];
    LARpp[0] = STEP(LARc[0], -32, 0, 13107);
    LARpp[1] = STEP(LARc[1], -32, 0, 13107);
    LARpp[2] = STEP(LARc[2], -16, -4096, 13107);
    LARpp[3] = STEP(LARc[3], -16, 5120, 13107);
    LARpp[4] = STEP(LARc[4], -8, -188, 19223);
    LARpp[5] = STEP(LARc[5], -8, 3584, 17476);
    LARpp[6] = STEP(LARc[6], -4, 682, 31454);
    LARpp[7] = STEP(LARc[7], -4, 2288, 29708);
    Gsm_Short_Term_Synthesis_Filter(state, (s16*)state, out);
    state->frame_index ^= 1;
}

typedef struct {
    u32 base; // the clip's ROM offset
    u32 pos;  // bit position
    u32 end;
} Bits;

static u32 ReadBits(Bits* b, int n) {
    u32 v = 0;
    int i;
    for (i = 0; i < n; i++, b->pos++) {
        u32 word = (b->base + (b->pos >> 5) * 4 + 4 <= b->end) ? Be32(b->base + (b->pos >> 5) * 4) : 0;
        v |= ((word >> (b->pos & 31)) & 1) << i;
    }
    return v;
}

static void PutLe(FILE* f, u32 v, int n) {
    int i;
    for (i = 0; i < n; i++) {
        fputc((v >> (8 * i)) & 0xFF, f);
    }
}

int main(int argc, char** argv) {
    FILE* f;
    u32 header, bank, count, id;
    FILE* tsv;
    char path[4096];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <rom.z64> <out dir>\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    gRomSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    gRom = malloc(gRomSize);
    if (fread(gRom, 1, gRomSize, f) != (size_t)gRomSize) {
        fprintf(stderr, "short read\n");
        return 1;
    }
    fclose(f);
    if (memcmp(gRom, "\x80\x37\x12\x40", 4) != 0) {
        fprintf(stderr, "not a big-endian (.z64) ROM\n");
        return 1;
    }

    header = 0x15C0000 + Be32(0x15C0000 + 4 * 4);
    bank = header + Be32(header + 4 + 2 * 8);
    count = Be16(bank + 2);
    snprintf(path, sizeof path, "%s/clips.tsv", argv[2]);
    tsv = fopen(path, "w");
    if (tsv == NULL) {
        perror(path);
        return 1;
    }
    fprintf(tsv, "id\toffset\tbytes\tframes\tseconds\n");
    for (id = 1; id <= count; id++) {
        u32 off = bank + Be32(bank + 4 + (id - 1) * 8);
        u32 len = Be32(bank + 8 + (id - 1) * 8);
        u32 frames, rate, n;
        s16* pcm;
        gsm_state state;
        s16 silentRun = 0, voicedRun = 0;
        Bits b;

        if (off + 12 > (u32)gRomSize || memcmp(gRom + off, "MORT", 4) != 0) {
            fprintf(stderr, "clip %u: no MORT header at 0x%x\n", id, off);
            continue;
        }
        frames = Be16(off + 4);
        rate = Be16(off + 6);
        pcm = calloc(frames * 160, sizeof(s16));
        memset(&state, 0, sizeof state);
        state.nrp = 40;
        b.base = off;
        b.pos = 0x60;
        b.end = off + len;
        for (n = 0; n < frames; n++) {
            if (silentRun == 0 && voicedRun == 0) {
                if (ReadBits(&b, 1)) {
                    silentRun = (s16)(ReadBits(&b, 4) + 1);
                } else {
                    voicedRun = (s16)(ReadBits(&b, 7) + 1);
                }
            }
            if (silentRun != 0) {
                silentRun--;
            } else {
                s16 LARc[8], Nc[4], bc[4], Mc[4], xmaxc[4];
                xMc xM[4];
                int i;
                static const int kLar[8] = { 6, 6, 5, 5, 4, 4, 3, 3 };
                for (i = 0; i < 8; i++) {
                    LARc[i] = (s16)ReadBits(&b, kLar[i]);
                }
                for (i = 0; i < 4; i++) {
                    s16* x = (s16*)&xM[i];
                    int k;
                    Nc[i] = (s16)ReadBits(&b, 7);
                    bc[i] = (s16)ReadBits(&b, 2);
                    Mc[i] = (s16)ReadBits(&b, 2);
                    xmaxc[i] = (s16)ReadBits(&b, 6);
                    for (k = 0; k < 13; k++) {
                        x[k] = (s16)ReadBits(&b, 3);
                    }
                }
                voicedRun--;
                DecodeFrame(&state, LARc, Nc, bc, Mc, xmaxc, xM, pcm + n * 160);
            }
        }
        snprintf(path, sizeof path, "%s/clip%03u.wav", argv[2], id);
        f = fopen(path, "wb");
        if (f != NULL) {
            u32 bytes = frames * 160 * 2;
            fwrite("RIFF", 1, 4, f);
            PutLe(f, 36 + bytes, 4);
            fwrite("WAVEfmt ", 1, 8, f);
            PutLe(f, 16, 4);
            PutLe(f, 1, 2);
            PutLe(f, 1, 2);
            PutLe(f, rate, 4);
            PutLe(f, rate * 2, 4);
            PutLe(f, 2, 2);
            PutLe(f, 16, 2);
            fwrite("data", 1, 4, f);
            PutLe(f, bytes, 4);
            for (n = 0; n < frames * 160; n++) {
                PutLe(f, (u16)pcm[n], 2);
            }
            fclose(f);
        }
        fprintf(tsv, "%u\t0x%x\t%u\t%u\t%.2f\n", id, off, len, frames, frames * 160.0 / rate);
        free(pcm);
    }
    fclose(tsv);
    printf("%u clips\n", count);
    return 0;
}
