// See gb_cart_art.h. The decompressor follows Gen 1's UncompressSpriteData (pret/pokered home/uncompress.asm): two
// bit planes, each run-length coded in 2-pixel-wide columns, then differential (and optionally XOR) decoded.
#include "gb_cart_art.h"

#include <algorithm>
#include <cstring>

namespace Port {

namespace {

size_t Find(const std::vector<uint8_t>& rom, const uint8_t* pat, size_t n, size_t from = 0, size_t to = SIZE_MAX) {
    to = std::min(to, rom.size());
    if (to < n) return SIZE_MAX;
    for (size_t i = from; i + n <= to; i++) {
        if (memcmp(&rom[i], pat, n) == 0) return i;
    }
    return SIZE_MAX;
}

struct BitReader {
    const std::vector<uint8_t>& d;
    size_t bit;
    bool bad = false;
    int Bit() {
        if ((bit >> 3) >= d.size()) {
            bad = true;
            return 0;
        }
        int b = (d[bit >> 3] >> (7 - (bit & 7))) & 1;
        bit++;
        return b;
    }
    int Int(int n) {
        int v = 0;
        while (n--) v = (v << 1) | Bit();
        return v;
    }
};

const uint8_t kNybble[2][16] = {
    { 0x0, 0x1, 0x3, 0x2, 0x7, 0x6, 0x4, 0x5, 0xf, 0xe, 0xc, 0xd, 0x8, 0x9, 0xb, 0xa },
    { 0xf, 0xe, 0xc, 0xd, 0x8, 0x9, 0xb, 0xa, 0x0, 0x1, 0x3, 0x2, 0x7, 0x6, 0x4, 0x5 },
};

// planes[k][c * rows + r]: byte of plane k, tile column c, pixel row r (the game's column-major sprite buffers)
bool Uncompress(const std::vector<uint8_t>& rom, size_t at, int& tilesW, int& tilesH, std::vector<uint8_t> planes[2]) {
    BitReader in{ rom, at * 8 };
    tilesW = in.Int(4);
    tilesH = in.Int(4);
    if (tilesW == 0 || tilesH == 0 || tilesW > 7 || tilesH > 7) return false;
    const int rows = tilesH * 8, cols = tilesW, size = rows * cols;

    auto fill = [&](std::vector<uint8_t>& out) -> bool {
        std::vector<uint8_t> groups; // 2-bit groups in stream order: (cols * 4) strips, each `rows` tall
        const size_t n = (size_t)size * 4;
        groups.reserve(n);
        bool rle = in.Bit() == 0;
        while (groups.size() < n && !in.bad) {
            if (rle) {
                int i = 0;
                while (in.Bit() && !in.bad) {
                    if (++i > 15) return false;
                }
                size_t count = ((size_t)2 << i) - 1 + (size_t)in.Int(i + 1);
                if (groups.size() + count > n) return false;
                groups.insert(groups.end(), count, 0);
            } else {
                for (;;) {
                    int g = in.Int(2);
                    if (g == 0 || in.bad) break;
                    groups.push_back((uint8_t)g);
                    if (groups.size() >= n) break;
                }
            }
            rle = !rle;
        }
        if (in.bad || groups.size() != n) return false;
        out.assign(size, 0);
        for (int c = 0; c < cols; c++) {
            for (int r = 0; r < rows; r++) {
                uint8_t v = 0;
                for (int j = 0; j < 4; j++) v = (uint8_t)((v << 2) | groups[(size_t)(c * 4 + j) * rows + r]);
                out[c * rows + r] = v;
            }
        }
        return true;
    };
    auto delta = [&](std::vector<uint8_t>& p) {
        for (int r = 0; r < rows; r++) {
            int last = 0;
            for (int c = 0; c < cols; c++) {
                uint8_t& b = p[c * rows + r];
                uint8_t hi = kNybble[last][b >> 4];
                last = hi & 1;
                uint8_t lo = kNybble[last][b & 15];
                last = lo & 1;
                b = (uint8_t)((hi << 4) | lo);
            }
        }
    };

    const int first = in.Bit(); // which buffer the first plane goes to
    if (!fill(planes[first])) return false;
    int mode = in.Bit();
    if (mode) mode += in.Bit();
    if (!fill(planes[first ^ 1])) return false;
    std::vector<uint8_t>& p1 = planes[first];
    std::vector<uint8_t>& p2 = planes[first ^ 1];
    if (mode == 0) {
        delta(planes[0]);
        delta(planes[1]);
    } else {
        if (mode == 2) delta(p2);
        delta(p1);
        for (int i = 0; i < size; i++) p2[i] ^= p1[i];
    }
    return true;
}

// UncompressMonSprite's bank choice: "cp index / ld a, bank / jr z|c" groups, separated by "ld a, b", then a default
struct BankRule {
    struct Step { uint8_t op, index, bank; };
    std::vector<Step> steps;
    uint8_t fallback = 0;
    int Bank(uint8_t index) const {
        for (const Step& s : steps) {
            if ((s.op == 0x28 && index == s.index) || (s.op == 0x38 && index < s.index)) return s.bank;
        }
        return fallback;
    }
};

// every such chain in the home bank (other code can look alike: the caller keeps the one whose pic decodes)
std::vector<BankRule> FindBankRules(const std::vector<uint8_t>& rom) {
    std::vector<BankRule> rules;
    auto group = [&](size_t p) {
        return p + 6 < rom.size() && rom[p] == 0xFE && rom[p + 2] == 0x3E && (rom[p + 4] == 0x28 || rom[p + 4] == 0x38);
    };
    const size_t end = std::min<size_t>(rom.size(), 0x4000); // home bank
    for (size_t p = 0; p < end; p++) {
        BankRule r;
        size_t q = p;
        while (group(q)) {
            r.steps.push_back({ rom[q + 4], rom[q + 1], rom[q + 3] });
            q += 6;
            if (q < rom.size() && rom[q] == 0x78) { // ld a, b: another group follows
                q++;
                continue;
            }
            break;
        }
        // the last group jumps over "ld a, <default bank>"
        if (r.steps.size() >= 4 && rom[q - 2] == 0x38 && rom[q - 1] == 0x02 && q + 1 < rom.size() && rom[q] == 0x3E) {
            r.fallback = rom[q + 1];
            rules.push_back(r);
            p = q;
        }
    }
    return rules;
}

uint32_t Rgb555(uint16_t c) {
    auto x = [](int v) { return (uint32_t)((v << 3) | (v >> 2)); };
    return (x(c & 31) << 16) | (x((c >> 5) & 31) << 8) | x((c >> 10) & 31);
}

} // namespace

std::string GbCartVersion(const std::vector<uint8_t>& rom) {
    if (rom.size() < 0x150) return "";
    std::string title(rom.begin() + 0x134, rom.begin() + 0x144);
    if (title.rfind("POKEMON ", 0) != 0) return "";
    if (title.find("YEL") != std::string::npos) return "yellow"; // "POKEMON YELLOW", "POKEMON YELAPSD" ...
    if (title.find("GREEN") != std::string::npos) return "green";
    if (title.find("BLUE") != std::string::npos) return "blue";
    if (title.find("RED") != std::string::npos) return "red";
    return "";
}

bool DecodeGbCartArt(const std::vector<uint8_t>& rom, GbCartArt& art) {
    art = GbCartArt();
    art.version = GbCartVersion(rom);
    if (art.version.empty()) return false;
    art.dex = art.version == "yellow" ? 25 : art.version == "green" ? 1 : art.version == "blue" ? 7 : 4;

    // Bulbasaur's base stats: No. 1, 45/49/49/45/65, Grass/Poison, catch rate 45, base exp 64, 5x5 sprite
    static const uint8_t kBulbasaur[] = { 0x01, 0x2D, 0x31, 0x31, 0x2D, 0x41, 0x16, 0x03, 0x2D, 0x40, 0x55 };
    const size_t stats = Find(rom, kBulbasaur, sizeof kBulbasaur);
    if (stats == SIZE_MAX || stats + (size_t)art.dex * 28 > rom.size()) return false;
    const uint8_t* entry = &rom[stats + (size_t)(art.dex - 1) * 28];
    if (entry[0] != art.dex) return false;
    const uint8_t sizeByte = entry[10];
    const uint16_t pic = (uint16_t)(entry[11] | (entry[12] << 8));
    if (pic < 0x4000 || pic >= 0x8000) return false;

    // PokedexOrder starts Rhydon, Kangaskhan, Nidoran♂, Clefairy, Spearow, Voltorb, Nidoking, Slowbro
    static const uint8_t kOrder[] = { 112, 115, 32, 35, 21, 100, 34, 80 };
    const size_t order = Find(rom, kOrder, sizeof kOrder);
    if (order == SIZE_MAX) return false;
    int index = 0;
    for (int i = 0; i < 190 && order + i < rom.size(); i++) {
        if (rom[order + i] == art.dex) {
            index = i + 1;
            break;
        }
    }
    if (index == 0) return false;
    int tw = 0, th = 0;
    std::vector<uint8_t> planes[2];
    bool found = false;
    for (const BankRule& rule : FindBankRules(rom)) {
        const size_t at = (size_t)rule.Bank((uint8_t)index) * 0x4000 + (pic - 0x4000);
        if (at < rom.size() && rom[at] == sizeByte && Uncompress(rom, at, tw, th, planes)) {
            found = true;
            break;
        }
    }
    if (!found) return false;
    art.width = tw * 8;
    art.height = th * 8;
    art.shade.resize((size_t)art.width * art.height);
    for (int y = 0; y < art.height; y++) {
        for (int x = 0; x < art.width; x++) {
            const int i = (x / 8) * art.height + y, b = 7 - (x & 7);
            art.shade[(size_t)y * art.width + x] = (uint8_t)(((planes[0][i] >> b) & 1) | (((planes[1][i] >> b) & 1) << 1));
        }
    }

    // MonsterPalettes: MissingNo. (PAL_MEWMON), then Bulbasaur..Blastoise; SuperPalettes (4 RGB555 each) follow its 152
    static const uint8_t kMonPals[] = { 0x10, 0x16, 0x16, 0x16, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13 };
    const size_t pals = Find(rom, kMonPals, sizeof kMonPals);
    if (pals != SIZE_MAX) {
        const size_t pal = pals + 152 + (size_t)rom[pals + art.dex] * 8;
        if (rom[pals + art.dex] < 0x40 && pal + 8 <= rom.size()) {
            for (int k = 0; k < 4; k++) art.rgb[k] = Rgb555((uint16_t)(rom[pal + 2 * k] | (rom[pal + 2 * k + 1] << 8)));
            art.sgbPalette = true;
        }
    }
    return true;
}

} // namespace Port
