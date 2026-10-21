// Host replacement for the RSP JPEG microcode (njpgdsp, task type 4). The game already Huffman-decodes each
// macroblock on the CPU into 64 zigzag-ordered s16 coefficients per 8x8 block; the microcode dequantizes, runs the
// inverse DCT and converts YCbCr to RGBA5551. Output replaces the input in place (0x200 bytes per 16x16 macroblock,
// row-major, big-endian 16-bit pixels because Fast3D reads RGBA16 textures as raw N64 bytes).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace {

struct JpegTaskData {
    uintptr_t address;
    uintptr_t mbCount;
    uintptr_t mode;
    uintptr_t qY, qU, qV;
};

const uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// block: 64 zigzag-ordered coefficients, q: 64 zigzag-ordered quantizers -> out: 64 samples (level shifted by +128)
void Idct8x8(const int16_t* block, const uint16_t* q, float* out) {
    static float cosTable[8][8];
    static bool init = false;
    if (!init) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++)
                cosTable[x][u] = (u == 0 ? std::sqrt(0.125f) : 0.5f) * std::cos((2 * x + 1) * u * 3.14159265358979f / 16.0f);
        init = true;
    }
    float nat[64];
    for (int i = 0; i < 64; i++) nat[kZigzag[i]] = (float)block[i] * (float)q[i];
    float tmp[64];
    for (int v = 0; v < 8; v++) // rows
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += cosTable[x][u] * nat[v * 8 + u];
            tmp[v * 8 + x] = s;
        }
    for (int x = 0; x < 8; x++) // columns
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += cosTable[y][v] * tmp[v * 8 + x];
            out[y * 8 + x] = s + 128.0f;
        }
}

inline int Clamp(float v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)(v + 0.5f)); }

} // namespace

extern "C" void Port_RunJpegTask(void* data) {
    auto* t = static_cast<JpegTaskData*>(data);
    auto* mb = reinterpret_cast<uint8_t*>(t->address);
    const auto* qY = reinterpret_cast<const uint16_t*>(t->qY);
    const auto* qU = reinterpret_cast<const uint16_t*>(t->qU);
    const auto* qV = reinterpret_cast<const uint16_t*>(t->qV);
    static int n = 0;
    static const bool trace = getenv("PORT_TRACE_JPEG") != nullptr;
    if (trace && n++ < 5) fprintf(stderr, "[jpeg] addr=%p mb=%zu mode=%zu q=%p %p %p\n", (void*)mb, (size_t)t->mbCount, (size_t)t->mode, (const void*)qY, (const void*)qU, (const void*)qV);
    if (!mb || !qY || !qU || !qV) return;
    int yBlocks = t->mode == 0 ? 2 : 4; // 4:2:2 has two luma blocks per macroblock, 4:2:0 has four
    for (uintptr_t m = 0; m < t->mbCount; m++) {
        const int16_t* blocks = reinterpret_cast<const int16_t*>(mb);
        float y[4][64], cb[64], cr[64];
        for (int b = 0; b < yBlocks; b++) Idct8x8(blocks + b * 64, qY, y[b]);
        Idct8x8(blocks + yBlocks * 64, qU, cb);
        Idct8x8(blocks + (yBlocks + 1) * 64, qV, cr);
        uint8_t out[16 * 16 * 2];
        int rows = yBlocks == 2 ? 8 : 16;
        for (int py = 0; py < rows; py++)
            for (int px = 0; px < 16; px++) {
                int b = (py >> 3) * 2 + (px >> 3);
                float Y = y[b][(py & 7) * 8 + (px & 7)];
                int cx = px >> 1, cy = yBlocks == 2 ? py : py >> 1;
                float Cb = cb[cy * 8 + cx] - 128.0f, Cr = cr[cy * 8 + cx] - 128.0f;
                int r = Clamp(Y + 1.402f * Cr);
                int g = Clamp(Y - 0.344136f * Cb - 0.714136f * Cr);
                int bl = Clamp(Y + 1.772f * Cb);
                uint16_t px16 = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((bl >> 3) << 1) | 1);
                out[(py * 16 + px) * 2] = px16 >> 8;
                out[(py * 16 + px) * 2 + 1] = px16 & 0xFF;
            }
        memcpy(mb, out, rows * 16 * 2);
        break; // the game submits one macroblock per task (mbCount == 1); the output replaces the input in place
    }
}
