#include "gb_jp_save.h"
#include <algorithm>
#include <cstring>

namespace Port {
namespace {
#include "gb_jp_names.inc"

constexpr uint8_t kEnd = 0x50; // string terminator in both character sets

// ---- character sets ----
// Japanese (pret/pokered constants/charmap.asm, the kana part): katakana 0x80-0xAB (no ヘ or リ: the hiragana へ and
// り serve for both), hiragana 0xB1-0xDE, voiced kana 0x05-0x48.
const char* const kKatakana[44] = {
    "A", "I", "U", "E", "O", "KA", "KI", "KU", "KE", "KO", "SA", "SHI", "SU", "SE", "SO", "TA", "CHI", "TSU", "TE", "TO", "NA",
    "NI", "NU", "NE", "NO", "HA", "HI", "FU", "HO", "MA", "MI", "MU", "ME", "MO", "YA", "YU", "YO", "RA", "RU", "RE", "RO",
    "WA", "WO", "N"};
const char* const kHiragana[46] = {
    "A", "I", "U", "E", "O", "KA", "KI", "KU", "KE", "KO", "SA", "SHI", "SU", "SE", "SO", "TA", "CHI", "TSU", "TE", "TO", "NA",
    "NI", "NU", "NE", "NO", "HA", "HI", "FU", "HE", "HO", "MA", "MI", "MU", "ME", "MO", "YA", "YU", "YO", "RA", "RI", "RU",
    "RE", "RO", "WA", "WO", "N"};
const char* const kVoiced[15] = {"GA", "GI", "GU", "GE", "GO", "ZA", "JI", "ZU", "ZE", "ZO", "DA", "JI", "ZU", "DE", "DO"};

// Kinds of Japanese characters that change the syllable before or after them.
enum class Kana { None, Syllable, Sokuon, SmallY, SmallVowel, Other };

Kana Classify(uint8_t c, std::string& text) {
    text.clear();
    if (c >= 0x80 && c <= 0xAB) { text = kKatakana[c - 0x80]; return Kana::Syllable; }
    if (c >= 0xB1 && c <= 0xDE) { text = kHiragana[c - 0xB1]; return Kana::Syllable; }
    if (c >= 0x05 && c <= 0x13) { text = kVoiced[c - 0x05]; return Kana::Syllable; }
    if (c >= 0x26 && c <= 0x34) { text = kVoiced[c - 0x26]; return Kana::Syllable; }
    if (c >= 0x19 && c <= 0x1C) { static const char* b[] = {"BA", "BI", "BU", "BO"}; text = b[c - 0x19]; return Kana::Syllable; }
    if (c >= 0x3A && c <= 0x3E) { static const char* b[] = {"BA", "BI", "BU", "BE", "BO"}; text = b[c - 0x3A]; return Kana::Syllable; }
    if (c >= 0x40 && c <= 0x43) { static const char* p[] = {"PA", "PI", "PU", "PO"}; text = p[c - 0x40]; return Kana::Syllable; }
    if (c >= 0x44 && c <= 0x48) { static const char* p[] = {"PA", "PI", "PU", "PE", "PO"}; text = p[c - 0x44]; return Kana::Syllable; }
    switch (c) {
        case 0xAC: case 0xDF: return Kana::Sokuon;
        case 0xAD: case 0xE0: text = "A"; return Kana::SmallY;
        case 0xAE: case 0xE1: text = "U"; return Kana::SmallY;
        case 0xAF: case 0xE2: text = "O"; return Kana::SmallY;
        case 0xE9: case 0x76: text = "A"; return Kana::SmallVowel;
        case 0xB0: case 0x6E: text = "I"; return Kana::SmallVowel;
        case 0xEA: case 0x6F: text = "U"; return Kana::SmallVowel;
        case 0xEB: case 0x77: text = "E"; return Kana::SmallVowel;
        case 0xF4: case 0x78: text = "O"; return Kana::SmallVowel;
        case 0xE3: text = "-"; return Kana::Other;
        case 0x7F: text = " "; return Kana::Other;
        case 0xE6: text = "?"; return Kana::Other;
        case 0xE7: text = "!"; return Kana::Other;
        case 0xE8: case 0xF2: text = "."; return Kana::Other;
        case 0xEF: text = "\x01"; return Kana::Other; // ♂
        case 0xF5: text = "\x02"; return Kana::Other; // ♀
        default: break;
    }
    if (c >= 0xF6) { text = std::string(1, static_cast<char>('0' + (c - 0xF6))); return Kana::Other; }
    return Kana::None;
}

// International character set: A-Z 0x80, a-z 0xA0, digits 0xF6, ♂ 0xEF, ♀ 0xF5.
uint8_t EncodeIntl(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(0x80 + (c - 'A'));
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(0xF6 + (c - '0'));
    switch (c) {
        case ' ': return 0x7F;
        case '-': return 0xE3;
        case '?': return 0xE6;
        case '!': return 0xE7;
        case '.': return 0xE8;
        case '\x01': return 0xEF;
        case '\x02': return 0xF5;
        default: return 0;
    }
}
std::string DecodeIntl(const uint8_t* s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n && s[i] != kEnd; i++) {
        const uint8_t c = s[i];
        if (c >= 0x80 && c <= 0x99) out += static_cast<char>('A' + (c - 0x80));
        else if (c >= 0xA0 && c <= 0xB9) out += static_cast<char>('A' + (c - 0xA0));
        else if (c >= 0xF6) out += static_cast<char>('0' + (c - 0xF6));
        else if (c == 0x7F) out += ' ';
        else if (c == 0xE3) out += '-';
        else if (c == 0xE6) out += '?';
        else if (c == 0xE7) out += '!';
        else if (c == 0xE8 || c == 0xF2) out += '.';
        else if (c == 0xEF) out += '\x01';
        else if (c == 0xF5) out += '\x02';
    }
    return out;
}

// romaji -> katakana (or the hiragana that stands in for a missing katakana)
struct KanaCode { const char* romaji; uint8_t first, second; };
std::vector<KanaCode> BuildReverse() {
    std::vector<KanaCode> table;
    for (int i = 0; i < 44; i++) table.push_back({kKatakana[i], static_cast<uint8_t>(0x80 + i), 0});
    table.push_back({"HE", 0xCD, 0});
    table.push_back({"RI", 0xD8, 0});
    for (int i = 0; i < 15; i++) if (i != 11 && i != 12) table.push_back({kVoiced[i], static_cast<uint8_t>(0x05 + i), 0});
    table.push_back({"BA", 0x19, 0}); table.push_back({"BI", 0x1A, 0}); table.push_back({"BU", 0x1B, 0});
    table.push_back({"BE", 0x3D, 0}); table.push_back({"BO", 0x1C, 0});
    table.push_back({"PA", 0x40, 0}); table.push_back({"PI", 0x41, 0}); table.push_back({"PU", 0x42, 0});
    table.push_back({"PE", 0x47, 0}); table.push_back({"PO", 0x43, 0});
    // syllables with a small ャ/ュ/ョ
    static const struct { const char* stem; uint8_t kana; } y[] = {
        {"KY", 0x86}, {"GY", 0x06}, {"NY", 0x95}, {"HY", 0x9A}, {"BY", 0x1A}, {"PY", 0x41}, {"MY", 0x9E}, {"RY", 0xD8},
        {"SH", 0x8B}, {"CH", 0x90}, {"J", 0x0B}};
    static std::vector<std::string> keep; // storage for the generated strings
    keep.reserve(64);
    const char* vowels[3] = {"A", "U", "O"};
    const uint8_t small[3] = {0xAD, 0xAE, 0xAF};
    for (const auto& s : y)
        for (int v = 0; v < 3; v++) {
            keep.push_back(std::string(s.stem) + vowels[v]);
            table.push_back({keep.back().c_str(), s.kana, small[v]});
        }
    table.push_back({"FA", 0x9B, 0xE9}); table.push_back({"FI", 0x9B, 0xB0}); table.push_back({"FE", 0x9B, 0xEB});
    table.push_back({"FO", 0x9B, 0xF4}); table.push_back({"TI", 0x92, 0xB0}); table.push_back({"DI", 0x12, 0xB0});
    table.push_back({"VA", 0x82, 0xE9}); table.push_back({"WI", 0x82, 0xB0}); table.push_back({"WE", 0x82, 0xEB});
    table.push_back({"SI", 0x8B, 0}); table.push_back({"TU", 0x91, 0}); table.push_back({"HU", 0x9B, 0});
    table.push_back({"ZI", 0x0B, 0}); table.push_back({"YE", 0x83, 0}); table.push_back({"WU", 0x82, 0});
    table.push_back({"YI", 0x81, 0}); table.push_back({"WO", 0xAA, 0});
    return table;
}

bool IsVowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }

// ---- save layout ----
struct Layout {
    int nameLen, boxCap, boxesPerBank;
    int player, rival, dayCare, party, currentBox, currentBoxIndex, checksum;
    int BoxListSize() const { return 1 + (boxCap + 1) + boxCap * 33 + 2 * boxCap * nameLen; }
    int BoxAddress(int box) const { return (box < boxesPerBank ? 0x4000 : 0x6000) + (box % boxesPerBank) * BoxListSize(); }
};
// Offsets: PKHeX SAV1Offsets (INT/JPN); list sizes: game/src/gb_data.h (both layouts).
constexpr Layout kIntl{11, 20, 6, 0x2598, 0x25F6, 0x2CF4, 0x2F2C, 0x30C0, 0x284C, 0x3523};
constexpr Layout kJp{6, 30, 4, 0x2598, 0x25F1, 0x2CA7, 0x2ED5, 0x302D, 0x2842, 0x3594};
// Regions that are the same data in both layouts (Japanese offset, international offset, length).
struct Segment { int jp, intl, length; };
constexpr Segment kSegments[] = {
    {0x0000, 0x0000, 0x2598},              // bank 0 (Hall of Fame, sprite buffers) and the start of bank 1
    {0x259E, 0x25A3, 0x53},                // Pokédex owned/seen, bag items, money
    {0x25F7, 0x2601, 0x532},               // options, badges, trainer ID ... PC items, current box, coins, events
    {0x2CA0, 0x2CED, 7},                   // play time, Safari Zone state
    {0x2CD5, 0x2D2C, 0x200},               // sprite data
    {0x3593, 0x3522, 1},                   // tile animations
};

uint8_t Checksum(const uint8_t* p, size_t n) {
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++) sum = static_cast<uint8_t>(sum + p[i]);
    return static_cast<uint8_t>(~sum);
}
void SetChecksums(uint8_t* ram, const Layout& l) {
    ram[l.checksum] = Checksum(ram + l.player, l.checksum - l.player);
    const int boxes = l.boxesPerBank * l.BoxListSize();
    for (int bank = 0x4000; bank <= 0x6000; bank += 0x2000) {
        ram[bank + boxes] = Checksum(ram + bank, boxes);
        for (int b = 0; b < l.boxesPerBank; b++)
            ram[bank + boxes + 1 + b] = Checksum(ram + bank + b * l.BoxListSize(), l.BoxListSize());
    }
}

struct Mon {
    uint8_t listSpecies = 0;
    uint8_t data[44] = {};
    uint8_t ot[11] = {}, nick[11] = {};
};
using MonList = std::vector<Mon>;

MonList ReadList(const uint8_t* p, int cap, int monSize, int nameLen) {
    MonList list;
    const int count = p[0] <= cap ? p[0] : 0;
    const uint8_t* mons = p + 1 + cap + 1;
    const uint8_t* ots = mons + cap * monSize;
    const uint8_t* nicks = ots + cap * nameLen;
    for (int i = 0; i < count; i++) {
        Mon m;
        m.listSpecies = p[1 + i];
        memcpy(m.data, mons + i * monSize, monSize);
        memcpy(m.ot, ots + i * nameLen, nameLen);
        memcpy(m.nick, nicks + i * nameLen, nameLen);
        list.push_back(m);
    }
    return list;
}
void WriteList(uint8_t* p, int cap, int monSize, int nameLen, const MonList& list) {
    const int count = std::min<int>(static_cast<int>(list.size()), cap);
    p[0] = static_cast<uint8_t>(count);
    uint8_t* mons = p + 1 + cap + 1;
    uint8_t* ots = mons + cap * monSize;
    uint8_t* nicks = ots + cap * nameLen;
    for (int i = 0; i < count; i++) {
        p[1 + i] = list[i].listSpecies;
        memcpy(mons + i * monSize, list[i].data, monSize);
        memcpy(ots + i * nameLen, list[i].ot, nameLen);
        memcpy(nicks + i * nameLen, list[i].nick, nameLen);
    }
    p[1 + count] = 0xFF;
}

int SpeciesIndex(const Mon& m) { return m.data[0] >= 1 && m.data[0] <= 190 ? m.data[0] : 0; }
// Species, OT ID, experience and DVs: what identifies a Pokémon however Stadium moves it.
std::string MonKey(const Mon& m, const uint8_t* nick, const uint8_t* ot) {
    std::string key(reinterpret_cast<const char*>(m.data), 1);
    key.append(reinterpret_cast<const char*>(m.data + 12), 5);
    key.append(reinterpret_cast<const char*>(m.data + 27), 2);
    key.append(reinterpret_cast<const char*>(nick), 11);
    key.append(reinterpret_cast<const char*>(ot), 11);
    return key;
}
bool SameJapaneseName(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == kEnd) return true;
    }
    return true;
}

Mon MonToIntl(const Mon& jp, GbJpSaveMap& map) {
    Mon m = jp;
    const int species = SpeciesIndex(jp);
    std::array<uint8_t, 11> nick;
    const uint8_t* speciesName = species ? kSpeciesNameJp[species - 1] : nullptr;
    if (speciesName && SameJapaneseName(jp.nick, speciesName, 5) &&
        (std::find(speciesName, speciesName + 5, kEnd) != speciesName + 5 || jp.nick[5] == kEnd)) {
        nick.fill(kEnd);
        memcpy(nick.data(), kSpeciesNameIntl[species - 1], 10);
    } else {
        nick = JapaneseNameToInternational(jp.nick, 10);
    }
    const auto ot = JapaneseNameToInternational(jp.ot, 7);
    memcpy(m.nick, nick.data(), 11);
    memcpy(m.ot, ot.data(), 11);
    std::array<uint8_t, 12> names;
    memcpy(names.data(), jp.nick, 6);
    memcpy(names.data() + 6, jp.ot, 6);
    map.mons.emplace(MonKey(m, m.nick, m.ot), names);
    std::array<uint8_t, 6> j;
    memcpy(j.data(), jp.nick, 6);
    map.nicks.emplace(std::string(reinterpret_cast<const char*>(m.nick), 11), j);
    memcpy(j.data(), jp.ot, 6);
    map.ots.emplace(std::string(reinterpret_cast<const char*>(m.ot), 11), j);
    return m;
}

Mon MonToJp(const Mon& intl, const GbJpSaveMap& map) {
    Mon m = intl;
    memset(m.nick, kEnd, sizeof m.nick);
    memset(m.ot, kEnd, sizeof m.ot);
    const auto known = map.mons.find(MonKey(intl, intl.nick, intl.ot));
    if (known != map.mons.end()) {
        memcpy(m.nick, known->second.data(), 6);
        memcpy(m.ot, known->second.data() + 6, 6);
        return m;
    }
    const std::string nick(reinterpret_cast<const char*>(intl.nick), 11), ot(reinterpret_cast<const char*>(intl.ot), 11);
    const int species = SpeciesIndex(intl);
    if (auto n = map.nicks.find(nick); n != map.nicks.end()) {
        memcpy(m.nick, n->second.data(), 6);
    } else if (species && DecodeIntl(intl.nick, 11) == DecodeIntl(kSpeciesNameIntl[species - 1], 10)) {
        memcpy(m.nick, kSpeciesNameJp[species - 1], 5);
    } else {
        const auto j = InternationalNameToJapanese(intl.nick);
        if (j[0] == kEnd && species) memcpy(m.nick, kSpeciesNameJp[species - 1], 5);
        else memcpy(m.nick, j.data(), 6);
    }
    if (auto o = map.ots.find(ot); o != map.ots.end()) {
        memcpy(m.ot, o->second.data(), 6);
    } else {
        memcpy(m.ot, InternationalNameToJapanese(intl.ot).data(), 6);
    }
    return m;
}

} // namespace

std::array<uint8_t, 11> JapaneseNameToInternational(const uint8_t* jp, int maxChars) {
    std::string out;      // ASCII, with \x01 = ♂ and \x02 = ♀
    size_t lastStart = 0; // where the last syllable starts in 'out'
    std::string last;     // the last syllable
    bool sokuon = false;
    std::string text;
    for (int i = 0; i < 6 && jp[i] != kEnd; i++) {
        switch (Classify(jp[i], text)) {
            case Kana::Syllable:
                if (sokuon && !IsVowel(text[0])) out += text.compare(0, 2, "CH") == 0 ? 'T' : text[0];
                sokuon = false;
                lastStart = out.size();
                out += text;
                last = text;
                break;
            case Kana::Sokuon:
                sokuon = true;
                break;
            case Kana::SmallY:
                if (!last.empty() && last.back() == 'I' && last.size() > 1) {
                    out.pop_back();
                    if (last == "SHI" || last == "CHI" || last == "JI") out += text;
                    else out += "Y" + text;
                } else {
                    out += "Y" + text;
                }
                last.clear();
                break;
            case Kana::SmallVowel:
                if (last.size() > 1 && IsVowel(out.back())) out.back() = text[0];
                else out += text;
                last.clear();
                break;
            case Kana::Other:
                out += text;
                last.clear();
                break;
            case Kana::None:
                break;
        }
    }
    (void)lastStart;
    std::array<uint8_t, 11> result;
    result.fill(kEnd);
    int n = 0;
    for (char c : out) {
        if (n >= maxChars || n >= 10) break;
        if (uint8_t e = EncodeIntl(c)) result[n++] = e;
    }
    return result;
}

std::array<uint8_t, 6> InternationalNameToJapanese(const uint8_t* intl) {
    static const std::vector<KanaCode> table = BuildReverse();
    std::string s = DecodeIntl(intl, 11);
    std::vector<uint8_t> out;
    auto push = [&](uint8_t c) { if (out.size() < 5) out.push_back(c); };
    for (size_t i = 0; i < s.size() && out.size() < 5;) {
        const char c = s[i];
        if (c == ' ') { push(0x7F); i++; continue; }
        if (c == '-') { push(0xE3); i++; continue; }
        if (c == '?') { push(0xE6); i++; continue; }
        if (c == '!') { push(0xE7); i++; continue; }
        if (c == '.') { push(0xE8); i++; continue; }
        if (c == '\x01') { push(0xEF); i++; continue; }
        if (c == '\x02') { push(0xF5); i++; continue; }
        if (c >= '0' && c <= '9') { push(static_cast<uint8_t>(0xF6 + (c - '0'))); i++; continue; }
        // a doubled consonant is a small ッ
        if (!IsVowel(c) && c != 'N' && i + 1 < s.size() && (s[i + 1] == c || (c == 'T' && s[i + 1] == 'C'))) {
            push(0xAC); i++; continue;
        }
        if (c == 'N' && (i + 1 >= s.size() || (!IsVowel(s[i + 1]) && s[i + 1] != 'Y'))) { push(0xAB); i++; continue; }
        const KanaCode* best = nullptr;
        size_t bestLen = 0;
        for (const auto& k : table) {
            const size_t len = strlen(k.romaji);
            if (len > bestLen && s.compare(i, len, k.romaji) == 0) { best = &k; bestLen = len; }
        }
        if (best) {
            push(best->first);
            if (best->second) push(best->second);
            i += bestLen;
            continue;
        }
        // a consonant without a vowel: the nearest katakana sound with U (O after T and D)
        char base = c;
        switch (c) { case 'L': base = 'R'; break; case 'V': base = 'B'; break; case 'C': case 'Q': case 'X': base = 'K'; break; default: break; }
        const std::string syllable = std::string(1, base) + ((base == 'T' || base == 'D') ? "O" : "U");
        for (const auto& k : table)
            if (syllable == k.romaji) { push(k.first); break; }
        i++;
    }
    std::array<uint8_t, 6> result;
    result.fill(kEnd);
    std::copy(out.begin(), out.end(), result.begin());
    return result;
}

bool IsJapaneseGen1Rom(const uint8_t* rom, size_t size) {
    if (size < 0x150) return false;
    const char* title = reinterpret_cast<const char*>(rom + 0x134);
    const bool gen1 = strncmp(title, "POKEMON RED", 11) == 0 || strncmp(title, "POKEMON GREEN", 13) == 0 ||
                      strncmp(title, "POKEMON BLUE", 12) == 0 || strncmp(title, "POKEMON YELLOW", 14) == 0;
    return gen1 && rom[0x14A] == 0 && rom[0x149] == 3;
}

std::vector<uint8_t> OverseasRomImage(const std::vector<uint8_t>& rom) {
    std::vector<uint8_t> image(std::max<size_t>(rom.size(), 0x100000));
    for (size_t i = 0; i < image.size(); i++) image[i] = rom[i % rom.size()];
    image[0x14A] = 1;
    if (image.size() == 0x100000) image[0x148] = 5;
    // MBC1 (Japanese Red/Green/Blue) cannot select banks 0x20/0x40/0x60 the way the GB Tower reads a 1 MB cartridge
    // (bank number, then upper bits), so the image says MBC3, the international carts' mapper. The Japanese games only
    // switch ROM banks below 0x20 and select RAM banks in MBC1's RAM banking mode, which MBC3 does the same way.
    if (image[0x147] >= 0x01 && image[0x147] <= 0x03) image[0x147] = 0x13;
    uint8_t header = 0;
    for (int i = 0x134; i <= 0x14C; i++) header = static_cast<uint8_t>(header - image[i] - 1);
    image[0x14D] = header;
    uint16_t global = 0;
    for (size_t i = 0; i < image.size(); i++)
        if (i != 0x14E && i != 0x14F) global = static_cast<uint16_t>(global + image[i]);
    image[0x14E] = static_cast<uint8_t>(global >> 8);
    image[0x14F] = static_cast<uint8_t>(global);
    return image;
}

void JapaneseSaveToInternational(const uint8_t* jp, uint8_t* intl, GbJpSaveMap& map) {
    map = GbJpSaveMap{};
    memset(intl, 0, kGbSaveSize);
    for (const auto& s : kSegments) memcpy(intl + s.intl, jp + s.jp, s.length);
    map.player = JapaneseNameToInternational(jp + kJp.player, 7);
    map.rival = JapaneseNameToInternational(jp + kJp.rival, 7);
    memcpy(intl + kIntl.player, map.player.data(), 11);
    memcpy(intl + kIntl.rival, map.rival.data(), 11);

    // Day Care: in use, nickname, OT, box structure
    intl[kIntl.dayCare] = jp[kJp.dayCare];
    {
        Mon m;
        memcpy(m.nick, jp + kJp.dayCare + 1, 6);
        memcpy(m.ot, jp + kJp.dayCare + 7, 6);
        memcpy(m.data, jp + kJp.dayCare + 13, 33);
        const Mon u = jp[kJp.dayCare] ? MonToIntl(m, map) : m;
        if (jp[kJp.dayCare]) {
            memcpy(intl + kIntl.dayCare + 1, u.nick, 11);
            memcpy(intl + kIntl.dayCare + 12, u.ot, 11);
        }
        memcpy(intl + kIntl.dayCare + 23, m.data, 33);
    }

    MonList party;
    for (const auto& m : ReadList(jp + kJp.party, 6, 44, 6)) party.push_back(MonToIntl(m, map));
    WriteList(intl + kIntl.party, 6, 44, 11, party);

    map.currentBox = jp[kJp.currentBoxIndex];
    const int current = (map.currentBox & 0x7F) < 8 ? (map.currentBox & 0x7F) : 0;
    const bool initialized = (map.currentBox & 0x80) != 0;
    MonList boxes[12];
    bool overflow = false;
    for (int b = 0; b < 8; b++) {
        MonList jpBox;
        if (b == current) jpBox = ReadList(jp + kJp.currentBox, 30, 33, 6);
        else if (initialized) jpBox = ReadList(jp + kJp.BoxAddress(b), 30, 33, 6);
        for (size_t i = 0; i < jpBox.size(); i++) {
            const Mon m = MonToIntl(jpBox[i], map);
            if (i < 20) boxes[b].push_back(m);
            else { boxes[8 + b / 2].push_back(m); overflow = true; }
        }
        if (b % 2 == 0) map.overflowSplit[b / 2] = static_cast<int>(boxes[8 + b / 2].size());
    }
    intl[kIntl.currentBoxIndex] = static_cast<uint8_t>(current | ((initialized || overflow) ? 0x80 : 0));
    for (int k = 0; k < 12; k++) WriteList(intl + kIntl.BoxAddress(k), 20, 33, 11, boxes[k]);
    WriteList(intl + kIntl.currentBox, 20, 33, 11, boxes[current]);
    SetChecksums(intl, kIntl);
}

void InternationalSaveToJapanese(const uint8_t* intl, uint8_t* jp, const GbJpSaveMap& map) {
    for (const auto& s : kSegments) memcpy(jp + s.jp, intl + s.intl, s.length);
    auto name = [&](int jpOffset, int intlOffset, const std::array<uint8_t, 11>& presented) {
        if (memcmp(intl + intlOffset, presented.data(), 11) == 0) return; // unchanged: keep the cartridge's bytes
        memcpy(jp + jpOffset, InternationalNameToJapanese(intl + intlOffset).data(), 6);
    };
    name(kJp.player, kIntl.player, map.player);
    name(kJp.rival, kIntl.rival, map.rival);

    jp[kJp.dayCare] = intl[kIntl.dayCare];
    if (intl[kIntl.dayCare]) {
        Mon m;
        memcpy(m.nick, intl + kIntl.dayCare + 1, 11);
        memcpy(m.ot, intl + kIntl.dayCare + 12, 11);
        memcpy(m.data, intl + kIntl.dayCare + 23, 33);
        const Mon j = MonToJp(m, map);
        memcpy(jp + kJp.dayCare + 1, j.nick, 6);
        memcpy(jp + kJp.dayCare + 7, j.ot, 6);
    }
    memcpy(jp + kJp.dayCare + 13, intl + kIntl.dayCare + 23, 33);

    MonList party;
    for (const auto& m : ReadList(intl + kIntl.party, 6, 44, 11)) party.push_back(MonToJp(m, map));
    WriteList(jp + kJp.party, 6, 44, 6, party);

    const uint8_t view = intl[kIntl.currentBoxIndex];
    const int viewCurrent = view & 0x7F;
    const bool viewInitialized = (view & 0x80) != 0;
    MonList boxes[12];
    for (int k = 0; k < 12; k++) {
        if (k == viewCurrent) boxes[k] = ReadList(intl + kIntl.currentBox, 20, 33, 11);
        else if (viewInitialized) boxes[k] = ReadList(intl + kIntl.BoxAddress(k), 20, 33, 11);
        for (auto& m : boxes[k]) m = MonToJp(m, map);
    }
    MonList jpBoxes[8];
    for (int j = 0; j < 4; j++) {
        const MonList& extra = boxes[8 + j];
        MonList& a = jpBoxes[2 * j];
        MonList& b = jpBoxes[2 * j + 1];
        a = boxes[2 * j];
        b = boxes[2 * j + 1];
        const size_t n = extra.size();
        size_t toA = std::min<size_t>({static_cast<size_t>(map.overflowSplit[j]), n, 30 - a.size()});
        if (n - toA > 30 - b.size()) toA = n - (30 - b.size());
        a.insert(a.end(), extra.begin(), extra.begin() + toA);
        b.insert(b.end(), extra.begin() + toA, extra.end());
    }
    const int current = viewCurrent < 8 ? viewCurrent : (map.currentBox & 0x7F) % 8;
    bool storage = viewInitialized;
    for (int b = 0; b < 8; b++) storage = storage || (b != current && !jpBoxes[b].empty());
    WriteList(jp + kJp.currentBox, 30, 33, 6, jpBoxes[current]);
    if (storage) {
        for (int b = 0; b < 8; b++) WriteList(jp + kJp.BoxAddress(b), 30, 33, 6, jpBoxes[b]);
    }
    jp[kJp.currentBoxIndex] = static_cast<uint8_t>(current | ((storage || (map.currentBox & 0x80)) ? 0x80 : 0));
    // Box bank checksums only when the banks hold boxes (the game formats them on the first box change otherwise).
    if (storage || (map.currentBox & 0x80)) {
        SetChecksums(jp, kJp);
    } else {
        jp[kJp.checksum] = Checksum(jp + kJp.player, kJp.checksum - kJp.player);
    }
}

bool JapaneseSaveTrainer(const uint8_t* jp, size_t size, std::string& name, unsigned& id) {
    if (size < kGbSaveSize || jp[kJp.checksum] != Checksum(jp + kJp.player, kJp.checksum - kJp.player)) return false;
    const auto n = JapaneseNameToInternational(jp + kJp.player, 7);
    name.clear();
    for (int i = 0; i < 11 && n[i] != kEnd; i++) {
        const uint8_t c = n[i];
        if (c >= 0x80 && c <= 0x99) name += static_cast<char>('A' + (c - 0x80));
        else if (c >= 0xF6) name += static_cast<char>('0' + (c - 0xF6));
        else if (c == 0xE3) name += '-';
        else if (c == 0x7F) name += ' ';
    }
    id = (jp[0x25FB] << 8) | jp[0x25FC];
    return true;
}

} // namespace Port
