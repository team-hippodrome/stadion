// Restored content: the Nintendo Cups (hippodrome-utils docs/specs/game_specific/1/RESTORED_CONTENT.md).
//
// The Japanese release (ポケモンスタジアム２) has three Nintendo Cups where the western ones have the Poké Cup: '97
// and '98 (session modes 1 and 2, one division each) and '99 (mode 3, four divisions, the slot the Poké Cup took).
// With a Japanese ROM provided as an extra ROM, the stadium select screen offers all seven cups, the Poké Cup and the
// three Nintendo Cups side by side. The western code still runs modes 1 and 2; '99 runs as mode 3 with Port_NcIs99()
// set, which makes the port read the '99 data instead of the Poké Cup's.
//
// What comes from the Japanese ROM, read here and handed to the game in the western format:
// - the COM trainers (trainer archive files 6, 7 and 8-11, round 2 +31): Japanese records are 0x218 bytes with a
//   0x50-byte Pokémon stride, western ones 0x230 and 0x54. Each trainer takes the class name the loaded ROM gives the
//   same portrait (every generic Nintendo Cup class is also a western cup trainer), so the class is in the loaded
//   ROM's own words. The Nintendo Cup '98 finalists (round 2), who are no western class, take their names from
//   translation/jp.csv (kanji.csv in Japanese). Nicknames come from translation/jp_localized.csv.
// - the rental lists of '98 (file 27, 33 Pokémon at level 30) and '99 (file 28, 126 Pokémon), which exist only in the
//   Japanese release. Their nicknames and trainer name are the loaded ROM's own for that species. '97 rents the
//   149-Pokémon list (file 26) every release has, so it keeps the loaded ROM's (international) sets.
// - the cups' names and descriptions: translation/jp.csv's cells for the Japanese ROM's strings.
//
// PORT_NINTENDO_CUP=0 hides the Nintendo Cups even with a Japanese ROM (tests).
#include "restored_content.h"
#include "StadionPort.h"
#include "Engine.h"
#include "language_prompt.h"
#include "rom_version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Japanese ROM offsets (CP2J): the trainer archive and the strings the cups' text is translated from
constexpr uint32_t kJpTrainerArchive = 0x890000;
constexpr uint32_t kTrainerArchive = 0x895000; // Rev 2
constexpr uint32_t kJpRecord = 0x218;
constexpr uint32_t kRecord = 0x230;
constexpr uint32_t kJpMon = 0x50;
constexpr uint32_t kMon = 0x54;
constexpr uint32_t kMonsAt = 0x3C;   // a record's first Pokémon
constexpr uint32_t kNickAt = 0x30;   // a Pokémon's nickname
constexpr uint32_t kOtAt = 0x3B;     // western: its trainer's name (11 bytes)
constexpr int kRound2 = 0x1F;

int gIs99 = 0;
int gMode = 0; // the session mode (Session_SetMode): Nintendo Cup '97 is 1

uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void PutBe32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// File `index` of the archive at the marked offset `archive`, and the marked offset it starts at
std::vector<uint8_t> ArchiveFile(uint32_t archive, int index, uint32_t* start = nullptr) {
    std::vector<uint8_t> out;
    uint8_t head[0x10], entry[0x10];
    if (archive == 0 || Port_RomRead(archive, head, sizeof head) != sizeof head) {
        return out;
    }
    const uint32_t count = Be32(head + 0x0C);
    if ((uint32_t)index >= count || count > 0x400 ||
        Port_RomRead(archive + 0x10 + (uint32_t)index * 0x10, entry, sizeof entry) != sizeof entry) {
        return out;
    }
    const uint32_t at = archive + Be32(entry), size = Be32(entry + 4);
    if (size == 0 || size > 0x100000) {
        return out;
    }
    out.resize(size);
    if (Port_RomRead(at, out.data(), size) != size) {
        out.clear();
    }
    if (start != nullptr) {
        *start = at;
    }
    return out;
}

// --- text ---------------------------------------------------------------------------------------------------------

struct Csv {
    bool read = false;
    std::vector<std::string> head;
    std::vector<std::vector<std::string>> rows;
    std::unordered_map<std::string, size_t> byId;
    std::unordered_map<uint32_t, size_t> byAddress; // every ROM offset a row lists
};

Csv gJp, gJpLocalized, gKanji;

const Csv& Table(Csv& csv, const char* path) {
    if (!csv.read) {
        csv.read = true;
        csv.rows = Port::ParseCsv(Port::ArchiveText(path));
        if (!csv.rows.empty()) {
            csv.head = csv.rows[0];
            csv.rows.erase(csv.rows.begin());
        }
        for (size_t r = 0; r < csv.rows.size(); r++) {
            const auto& row = csv.rows[r];
            if (row.size() < 3) {
                continue;
            }
            csv.byId[row[0]] = r;
            size_t at = 0;
            while (at < row[2].size()) {
                const size_t end = std::min(row[2].find(' ', at), row[2].size());
                if (end > at) {
                    csv.byAddress.emplace((uint32_t)strtoul(row[2].substr(at, end - at).c_str(), nullptr, 16), r);
                }
                at = end + 1;
            }
        }
        if (csv.rows.empty()) {
            fprintf(stderr, "[nintendo cup] %s is not in the archive\n", path);
        }
    }
    return csv;
}

std::string Cell(const Csv& csv, size_t row, const std::string& lang) {
    for (size_t c = 3; c < csv.head.size() && c < csv.rows[row].size(); c++) {
        if (csv.head[c] == lang) {
            return csv.rows[row][c];
        }
    }
    return "";
}

// The translation of the Japanese ROM's string with id `id` (or listing ROM offset `address`) in `lang` (UTF-8; ""
// when there is none). Japanese is kanji.csv's; another language falls back to English.
std::string JpText(const std::string& id, uint32_t address, const std::string& lang) {
    const Csv& jp = Table(gJp, "translation/jp.csv");
    size_t row = SIZE_MAX;
    if (!id.empty()) {
        auto it = jp.byId.find(id);
        if (it != jp.byId.end()) row = it->second;
    } else {
        auto it = jp.byAddress.find(address);
        if (it != jp.byAddress.end()) row = it->second;
    }
    if (row == SIZE_MAX) {
        return "";
    }
    if (lang == "jp") {
        const Csv& kanji = Table(gKanji, "translation/kanji.csv");
        auto it = kanji.byId.find(jp.rows[row][0]);
        if (it != kanji.byId.end()) {
            const std::string cell = Cell(kanji, it->second, "kanji");
            if (!cell.empty()) return cell;
        }
        return Cell(jp, row, "en");
    }
    std::string cell = Cell(jp, row, lang);
    return cell.empty() ? Cell(jp, row, "en") : cell;
}

// A COM Pokémon's nickname at the Japanese ROM offset `address` in `lang` (translation/jp_localized.csv; "" if none)
std::string JpNickname(uint32_t address, const std::string& lang) {
    const Csv& loc = Table(gJpLocalized, "translation/jp_localized.csv");
    auto it = loc.byAddress.find(address);
    if (it == loc.byAddress.end()) {
        return "";
    }
    std::string cell = Cell(loc, it->second, lang == "jp" ? "en" : lang);
    return cell.empty() ? Cell(loc, it->second, "en") : cell;
}

// `text` (the game's encoding) cut to fit `room` bytes with its terminator, never inside a two-byte character
void PutName(uint8_t* dst, size_t room, const std::string& text) {
    memset(dst, 0, room);
    size_t n = 0;
    while (n < text.size()) {
        const uint8_t c = (uint8_t)text[n];
        const size_t w = (c >= PORT_TEXT_WIDE_FIRST && c <= PORT_TEXT_WIDE_LAST) ? 2 : 1;
        if (n + w > room - 1) break;
        n += w;
    }
    memcpy(dst, text.data(), n);
}

std::string OneLine(std::string s) {
    for (char& c : s) {
        if (c == '\n') c = ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// --- trainer data ---------------------------------------------------------------------------------------------------

struct LoadedNames {
    bool read = false;
    std::map<uint8_t, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> classes; // portrait -> name1, name2
    std::map<uint8_t, std::vector<uint8_t>> species; // species id -> a rental's nickname and trainer name (22 bytes)
};
LoadedNames gLoaded;

void ReadLoadedNames() {
    if (gLoaded.read) {
        return;
    }
    gLoaded.read = true;
    const uint32_t archive = Port_RomToRaw(kTrainerArchive);
    // the cups' trainers: files 0-11, round 2 31-42
    for (int round = 0; round < 2; round++) {
        for (int f = 0; f < 12; f++) {
            const std::vector<uint8_t> file = ArchiveFile(archive, f + round * kRound2);
            if (file.size() < 4) continue;
            const uint32_t n = Be32(file.data());
            for (uint32_t i = 0; i < n && (i + 1) * kRecord <= file.size(); i++) {
                const uint8_t* r = file.data() + i * kRecord;
                const uint8_t portrait = r[0x38];
                if (gLoaded.classes.count(portrait) == 0) {
                    gLoaded.classes[portrait] = { std::vector<uint8_t>(r + 4, r + 0x10), std::vector<uint8_t>(r + 0x10, r + 0x38) };
                }
            }
        }
    }
    // the 149 rental Pokémon (file 26)
    const std::vector<uint8_t> rentals = ArchiveFile(archive, 26);
    if (rentals.size() >= 4) {
        const uint32_t n = Be32(rentals.data());
        for (uint32_t i = 0; i < n && 4 + (i + 1) * kMon <= rentals.size(); i++) {
            const uint8_t* m = rentals.data() + 4 + i * kMon;
            gLoaded.species[m[0]] = std::vector<uint8_t>(m + kNickAt, m + kNickAt + 22);
        }
    }
}

// a Japanese Pokémon record (0x50 bytes at Japanese ROM offset `address`) in the western layout (0x54 bytes)
void ConvertMon(uint8_t* out, const uint8_t* in, uint32_t address, const uint8_t* trainerName, bool rental,
                const std::string& lang) {
    memset(out, 0, kMon);
    memcpy(out, in, kNickAt); // species to stats: the same layout
    auto own = gLoaded.species.find(in[0]);
    if (rental) {
        if (own != gLoaded.species.end()) {
            memcpy(out + kNickAt, own->second.data(), 22); // the loaded ROM's nickname and "TRAINER"
        }
        return;
    }
    const std::string nick = JpNickname(address + kNickAt, lang);
    if (!nick.empty()) {
        PutName(out + kNickAt, 11, Port::TextEncode(nick));
    } else if (own != gLoaded.species.end()) {
        memcpy(out + kNickAt, own->second.data(), 11); // the species' name
    }
    PutName(out + kOtAt, 11, std::string((const char*)trainerName, strnlen((const char*)trainerName, 12)));
}

// Japanese trainer archive file `index` (COM trainers) in the western format
std::vector<uint8_t> ConvertTrainers(int index, const std::string& lang) {
    uint32_t start = 0;
    const std::vector<uint8_t> in = ArchiveFile(Port_RomRawIn(Port::JapanRomSlot(), kJpTrainerArchive), index, &start);
    std::vector<uint8_t> out;
    if (in.size() < 4) {
        return out;
    }
    const uint32_t romAt = start & ~(PORT_ROM_RAW | PORT_ROM_SLOT_MASK);
    const uint32_t n = Be32(in.data());
    out.assign(n * kRecord + 0x10, 0);
    for (uint32_t i = 0; i < n && (i + 1) * kJpRecord <= in.size(); i++) {
        const uint8_t* r = in.data() + i * kJpRecord;
        uint8_t* w = out.data() + i * kRecord;
        const uint32_t address = romAt + i * kJpRecord;
        memcpy(w, r, 4);           // the file's count (first record), else 0
        memcpy(w + 0x38, r + 0x38, 4); // portrait and Pokémon count
        auto cls = gLoaded.classes.find(r[0x38]);
        if (cls != gLoaded.classes.end()) {
            memcpy(w + 4, cls->second.first.data(), 12);
            memcpy(w + 0x10, cls->second.second.data(), 40);
        } else {
            // a Nintendo Cup '98 finalist: their names from the translation tables, in capitals as the cups' are
            const std::string name1 = Port::TextUpper(OneLine(JpText("", address + 4, lang)));
            const std::string name2 = Port::TextUpper(OneLine(JpText("", address + 0x10, lang)));
            PutName(w + 4, 12, Port::TextEncode(name1.empty() ? std::string("?") : name1));
            PutName(w + 0x10, 40, Port::TextEncode(name2.empty() ? name1 : name2));
        }
        const uint32_t mons = std::min<uint32_t>(((uint32_t)r[0x3A] << 8) | r[0x3B], 6);
        std::vector<uint8_t> mon(kMon);
        for (uint32_t k = 0; k < mons; k++) {
            ConvertMon(mon.data(), r + kMonsAt + k * kJpMon, address + kMonsAt + k * kJpMon, w + 4, false, lang);
            // the last Pokémon of a record runs 4 bytes into the next record's (zero) head, as in the western files
            memcpy(w + kMonsAt + k * kMon, mon.data(), std::min<size_t>(kMon, out.size() - (i * kRecord + kMonsAt + k * kMon)));
        }
    }
    PutBe32(out.data(), n);
    return out;
}

// Japanese trainer archive file `index` (a rental list) in the western format
std::vector<uint8_t> ConvertRentals(int index, const std::string& lang) {
    uint32_t start = 0;
    const std::vector<uint8_t> in = ArchiveFile(Port_RomRawIn(Port::JapanRomSlot(), kJpTrainerArchive), index, &start);
    std::vector<uint8_t> out;
    if (in.size() < 4) {
        return out;
    }
    const uint32_t romAt = start & ~(PORT_ROM_RAW | PORT_ROM_SLOT_MASK);
    const uint32_t n = std::min<uint32_t>(Be32(in.data()), (uint32_t)((in.size() - 4) / kJpMon));
    out.assign((4 + n * kMon + 0xF) & ~0xFu, 0);
    PutBe32(out.data(), n);
    for (uint32_t i = 0; i < n; i++) {
        ConvertMon(out.data() + 4 + i * kMon, in.data() + 4 + i * kJpMon, romAt + 4 + i * kJpMon, nullptr, true, lang);
    }
    return out;
}

std::map<std::pair<std::string, int>, std::vector<uint8_t>> gFiles;

} // namespace

namespace Port {

void ResetNintendoCup() {
    gIs99 = 0;
    gMode = 0;
    gLoaded = LoadedNames();
    gFiles.clear();
}

} // namespace Port

extern "C" {

int Port_NcAvailable(void) {
    static int env = -2;
    if (env == -2) {
        const char* v = getenv("PORT_NINTENDO_CUP");
        env = (v != nullptr && *v != '\0') ? atoi(v) : -1;
    }
    return env != 0 && Port::JapanRomSlot() != 0 && Port::LoadedRomVersion() != PORT_ROM_JP;
}

int Port_NcIs99(void) {
    return gIs99 && Port_NcAvailable();
}

void Port_NcSet99(int on) {
    gIs99 = on ? 1 : 0;
}

void Port_NcSetMode(int mode) {
    gMode = mode;
}

int Port_NcMode(void) {
    return gMode;
}

const void* Port_NcTrainerFile(uint32_t archiveRaw, int index, uint32_t* size) {
    if (!Port_NcAvailable() || archiveRaw != Port_RomToRaw(kTrainerArchive)) {
        return nullptr;
    }
    const int base = index >= kRound2 ? index - kRound2 : index;
    bool trainers = false, rentals = false;
    switch (base) {
        case 6: // Nintendo Cup '97
        case 7: // '98
            trainers = true;
            break;
        case 26: // the 149 rentals: Nintendo Cup '97 takes the Japanese release's, with their own movesets
            rentals = gMode == 1;
            break;
        case 27: // '98 rentals
            rentals = true;
            break;
        case 8: // '99, four divisions
        case 9:
        case 10:
        case 11:
            trainers = gIs99 != 0;
            break;
        case 28: // '99 rentals
            rentals = gIs99 != 0;
            break;
    }
    if (!trainers && !rentals) {
        return nullptr;
    }
    ReadLoadedNames();
    const std::string lang = Port::TextTargetLanguage();
    auto key = std::make_pair(lang, index);
    auto it = gFiles.find(key);
    if (it == gFiles.end()) {
        it = gFiles.emplace(key, trainers ? ConvertTrainers(index, lang) : ConvertRentals(index, lang)).first;
        fprintf(stderr, "[nintendo cup] trainer file %d from the Japanese ROM (%s): %zu bytes\n", index, lang.c_str(),
                it->second.size());
    }
    if (it->second.empty()) {
        return nullptr;
    }
    *size = (uint32_t)it->second.size();
    return it->second.data();
}

const char* Port_NcText(int id) {
    static const char* const kIds[] = {
        "ROM_00290F84", // PORT_NC_TEXT_NAME_97: "Nintendo Cup '97"
        "ROM_00290F68", // "Nintendo Cup '98"
        "ROM_00290F4C", // "Nintendo Cup '99"
        "ROM_00290CF8", // PORT_NC_TEXT_DESC_97: the stadium select screen's description
        "ROM_00290C30", // '98
        "ROM_00290B70", // '99
        "ROM_002C1304", // PORT_NC_TEXT_NO_CHAMPIONS: the '99 rule's details
    };
    static std::map<std::pair<std::string, int>, std::string> sCache; // kept for the run: pointers stay valid
    if (id < 0 || id >= (int)(sizeof kIds / sizeof kIds[0])) {
        return "";
    }
    const std::string lang = Port::TextTargetLanguage();
    auto key = std::make_pair(lang, id);
    auto it = sCache.find(key);
    if (it == sCache.end()) {
        std::string text = JpText(kIds[id], 0, lang);
        if (id <= PORT_NC_TEXT_NAME_99 && lang != "jp") {
            text = Port::TextUpper(text); // the western cups' names are capitals (POKé CUP)
        }
        it = sCache.emplace(key, Port::TextEncode(text)).first;
    }
    return it->second.c_str();
}

const char* Port_NcTableText(uint32_t table, uint32_t index) {
    // The western releases' message tables keep three "POKé CUP" slots wherever a cup list is indexed by mode or rule
    // category (the Japanese release's Nintendo Cups '97, '98 and '99). The '97 and '98 slots are only ever shown for
    // those cups; the '99 one is the Poké Cup's too, so it is renamed only while Nintendo Cup '99 is being played.
    static const struct {
        uint32_t table;
        uint32_t first; // the '97 slot
        int step;       // +1, or -1 where the table lists them the other way round
    } kSlots[] = {
        { 0x0E, 0, 1 },   // Transfer Pak check: a saved session's cup
        { 0x13, 0, 1 },   // stadium select
        { 0x1A, 64, 1 },  // team entry: rule category + 0x3F
        { 0x1B, 0, 1 },   // battle prep: mode - 1
        { 0x1F, 7, 1 },   // victory palace and Transfer Pak select
        { 0x20, 77, -1 }, // rental rules: '97 = 77, '99 = 75
    };
    if (!Port_NcAvailable()) {
        return nullptr;
    }
    for (const auto& slot : kSlots) {
        if (slot.table != table) {
            continue;
        }
        const int cup = ((int)index - (int)slot.first) * slot.step; // 0 = '97, 1 = '98, 2 = '99
        if (cup < 0 || cup > 2 || (cup == 2 && !gIs99)) {
            return nullptr;
        }
        return Port_NcText(PORT_NC_TEXT_NAME_97 + cup);
    }
    return nullptr;
}

int Port_NcBackgroundFile(int mode, int file) {
    // backgrounds archive: the western releases keep the Japanese '97 picture as file 9 (the Poké Cup's) and the '99
    // one as file 7, where the Japanese release has them the other way round
    if (!Port_NcAvailable()) {
        return file;
    }
    if (mode == 1) {
        return 9;
    }
    if (mode == 3 && gIs99) {
        return 7;
    }
    return file;
}

uint32_t Port_NcJapanRom(uint32_t jpOffset) {
    return Port::JapanRomSlot() != 0 ? Port_RomRawIn(Port::JapanRomSlot(), jpOffset) : 0;
}

} // extern "C"
