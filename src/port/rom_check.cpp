// SHA-1 (FIPS 180-4) for the ROM check: the port only needs it to tell the original dump from any other file.
#include "rom_check.h"
#include "file_io.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

class Sha1 {
  public:
    void Update(const uint8_t* data, size_t size) {
        mLength += size;
        if (mBuffered > 0) {
            const size_t n = std::min(size, sizeof mBuffer - mBuffered);
            memcpy(mBuffer + mBuffered, data, n);
            mBuffered += n;
            data += n;
            size -= n;
            if (mBuffered < sizeof mBuffer) {
                return;
            }
            Block(mBuffer);
            mBuffered = 0;
        }
        for (; size >= 64; data += 64, size -= 64) {
            Block(data);
        }
        memcpy(mBuffer, data, size);
        mBuffered = size;
    }

    std::string HexDigest() {
        const uint64_t bits = mLength * 8;
        static const uint8_t pad[64] = { 0x80 };
        Update(pad, mBuffered < 56 ? 56 - mBuffered : 120 - mBuffered);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) {
            len[i] = (uint8_t)(bits >> (56 - 8 * i));
        }
        Update(len, 8);
        char hex[41];
        for (int i = 0; i < 5; i++) {
            snprintf(hex + 8 * i, 9, "%08x", mH[i]);
        }
        return hex;
    }

  private:
    static uint32_t Rol(uint32_t v, int n) {
        return (v << n) | (v >> (32 - n));
    }

    void Block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = mH[0], b = mH[1], c = mH[2], d = mH[3], e = mH[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t t = Rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rol(b, 30);
            b = a;
            a = t;
        }
        mH[0] += a;
        mH[1] += b;
        mH[2] += c;
        mH[3] += d;
        mH[4] += e;
    }

    uint32_t mH[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint8_t mBuffer[64];
    size_t mBuffered = 0;
    uint64_t mLength = 0;
};

bool AnyRomAllowed() {
    static const bool any = getenv("PORT_ANY_ROM") != nullptr;
    return any;
}

} // namespace

namespace Port {

std::string Sha1Hex(const uint8_t* data, size_t size) {
    Sha1 sha;
    sha.Update(data, size);
    return sha.HexDigest();
}

std::string Sha1HexFile(const std::string& path) {
    FILE* f = OpenFileUtf8(path.c_str(), "rb");
    if (f == nullptr) {
        return "";
    }
    Sha1 sha;
    std::vector<uint8_t> buf(1 << 20);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        sha.Update(buf.data(), n);
    }
    const bool ok = !ferror(f);
    fclose(f);
    return ok ? sha.HexDigest() : "";
}

std::string CheckRomDump(const uint8_t* data, size_t size) {
    if (AnyRomAllowed()) {
        return "";
    }
    if (size != kRomSize || Sha1Hex(data, size) != kRomSha1) {
        return "Not the original dump";
    }
    return "";
}

std::string CheckRomDumpFile(const std::string& path) {
    if (AnyRomAllowed()) {
        return "";
    }
    FILE* f = OpenFileUtf8(path.c_str(), "rb");
    if (f == nullptr) {
        return "Cannot open the file";
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    if (size != (long)kRomSize || Sha1HexFile(path) != kRomSha1) {
        return "Not the original dump";
    }
    return "";
}

} // namespace Port
