// The announcer's voice lines: the Japanese announcer on the restored Nintendo Cups, and the lines' subtitles
// (StadionPort.h "Announcer and subtitles"; hippodrome-utils docs/specs/SUBTITLES.md and
// docs/specs/game_specific/1/RESTORED_CONTENT.md).
//
// Every line goes through Audio_UpdateBattleAudioFrame (game/src/3D140_2.c), which starts clip `id` of the announcer
// bank on stream channel 1. Port_AnnouncerClip picks the bank the clip comes from: the Japanese release's (from the
// Japanese ROM the player provided) where the restored content says so, else the loaded ROM's. The line it picked is
// published for the subtitles (game side: src/port/enhancements/subtitles.c), keyed by its bank's region and its
// index there, so translation/subtitles.csv has a row per line of each dub: SUBTITLE_ANNOUNCER_<EN|JP>_<%03X>.
#include "StadionPort.h"
#include "Engine.h"
#include "language_prompt.h"
#include "restored_content.h"
#include "rom_version.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// The audio header (seven big-endian words; [4] is the offset of the stream banks' file, whose third entry is the
// announcer's bank) in the Japanese release.
constexpr uint32_t kJpAudioHeader = 0x15C0000;

// The western clip ids (US, English EU: the same bank) the western code asks for, and the Japanese bank's clip that
// says the same line. The Japanese bank has 665 clips to the western 657: lines the western release cut (low-level
// remarks, the Nintendo Cups' names) sit between the shared ones, so the ids drift apart. Each run maps `count` ids
// from `west` on to the Japanese ids from `jp` on. A western id in no run (a line the Japanese release does not have)
// keeps the western clip.
struct ClipRun {
    uint16_t west;
    uint16_t jp;
    uint16_t count;
};
#include "announcer_jp_map.inc"

uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// The Japanese announcer bank's clips as marked offsets in the Japanese ROM's slot (index 0 = clip 1).
const std::vector<uint32_t>& JapaneseClips() {
    static std::vector<uint32_t> clips;
    static bool read = false;
    if (read) {
        return clips;
    }
    read = true;
    const int slot = Port::JapanRomSlot();
    if (slot == 0) {
        return clips;
    }
    auto raw = [&](uint32_t off) { return Port_RomRawIn(slot, off); };
    uint8_t head[28];
    if (Port_RomRead(raw(kJpAudioHeader), head, sizeof head) != sizeof head) {
        return clips;
    }
    // a seq file: u16 revision, u16 count, then {u32 offset, u32 length} from the file's start
    const uint32_t files = kJpAudioHeader + Be32(head + 16);
    uint8_t entry[12];
    if (Port_RomRead(raw(files), entry, 4) != 4 || ((entry[2] << 8) | entry[3]) < 3 ||
        Port_RomRead(raw(files + 4 + 2 * 8), entry, 8) != 8) {
        return clips;
    }
    const uint32_t bank = files + Be32(entry);
    uint8_t count[4];
    if (Port_RomRead(raw(bank), count, 4) != 4) {
        return clips;
    }
    const int n = (count[2] << 8) | count[3];
    std::vector<uint8_t> table((size_t)n * 8);
    if (n <= 0 || n > 2000 || Port_RomRead(raw(bank + 4), table.data(), table.size()) != table.size()) {
        return clips;
    }
    for (int i = 0; i < n; i++) {
        clips.push_back(raw(bank + Be32(&table[(size_t)i * 8])));
    }
    SDL_Log("[announcer] the Japanese announcer: %d clips", n);
    return clips;
}

// The Japanese clip (1-based) that says western clip `id`'s line, or 0.
int JapaneseId(uint32_t id) {
    for (const ClipRun& r : kJpClipRuns) {
        if (id >= r.west && id < (uint32_t)r.west + r.count) {
            return r.jp + (int)(id - r.west);
        }
    }
    return 0;
}

// Whether the line plays in Japanese: on a western ROM with the Japanese ROM provided, the Nintendo Cups take the
// Japanese announcer; with Japanese text, everything but the Poké Cup does (RESTORED_CONTENT.md).
bool JapaneseAnnouncer() {
    if (!Port_NcAvailable()) {
        return false;
    }
    const int mode = Port_NcMode();
    const bool ncup = mode == 1 || mode == 2 || (mode == 3 && Port_NcIs99());
    if (Port::TextTargetLanguage() == "jp") {
        return !(mode == 3 && !Port_NcIs99());
    }
    return ncup;
}

// The dub of the loaded ROM's bank, as the subtitle rows name it ("" for one without rows yet).
const char* LoadedRegion() {
    static std::string region;
    static bool read = false;
    if (!read) {
        read = true;
        const std::string lang = Port::RomSize() >= 0x40 ? Port::RomLanguageFromHeader(Port::RomData()) : "";
        region = lang == "en" ? "EN" : ""; // US and English EU share their bank; FR/DE/ES/IT have their own dubs
    }
    return region.c_str();
}

// The row of the mode music table (Audio_SelectModeMusic) the battle's opening lines come from.
int gModeRow = -1;

// The line on screen: published here (the audio thread may start lines), read by the game thread's draw.
volatile int gSerial = 0;
volatile int gRegion = 0; // 0 none, 1 EN, 2 JP
volatile int gKey = 0;

const char* RegionName(int region) {
    return region == 2 ? "JP" : region == 1 ? "EN" : "";
}

// PORT_SUBTITLE_TEST=<region>_<hex id> (or just the hex id, the loaded ROM's dub): that line, pinned on screen.
bool TestLine(int* region, int* key) {
    static int r = -1, k = 0;
    if (r < 0) {
        r = 0;
        const char* v = getenv("PORT_SUBTITLE_TEST");
        if (v != nullptr && *v != '\0') {
            if (strncmp(v, "JP_", 3) == 0 || strncmp(v, "EN_", 3) == 0) {
                r = v[0] == 'J' ? 2 : 1;
                v += 3;
            } else {
                r = strcmp(LoadedRegion(), "EN") == 0 ? 1 : 0;
            }
            k = (int)strtol(v, nullptr, 16);
        }
    }
    *region = r;
    *key = k;
    return r != 0;
}

// translation/subtitles.csv: id -> language -> its cell (UTF-8)
const std::unordered_map<std::string, std::map<std::string, std::string>>& Rows() {
    static std::unordered_map<std::string, std::map<std::string, std::string>> rows;
    static bool read = false;
    if (read) {
        return rows;
    }
    read = true;
    const std::string text = Port::ArchiveText("translation/subtitles.csv");
    if (text.empty()) {
        SDL_Log("[subtitles] translation/subtitles.csv is not in pokestadium.o2r: no subtitles");
        return rows;
    }
    auto csv = Port::ParseCsv(text);
    if (csv.empty()) {
        return rows;
    }
    const auto head = csv[0];
    for (size_t r = 1; r < csv.size(); r++) {
        if (csv[r].empty()) {
            continue;
        }
        auto& row = rows[csv[r][0]];
        for (size_t c = 1; c < head.size() && c < csv[r].size(); c++) {
            row[head[c]] = csv[r][c];
        }
    }
    return rows;
}

} // namespace

extern "C" {

void Port_AnnouncerModeRow(int row) {
    gModeRow = row;
}

uint32_t Port_AnnouncerClip(uint32_t id, uint32_t romOffset) {
    int region = strcmp(LoadedRegion(), "EN") == 0 ? 1 : 0;
    int key = (int)id;
    if (JapaneseAnnouncer()) {
        int jp = JapaneseId(id);
        // The western tables give the Nintendo Cups '97 and '98 (rows 10 and 11 of the mode music table) the Poké Cup's
        // rules line and opening (the Japanese release's Nintendo Cup '99's): the Japanese bank has their own.
        if (gModeRow == 10 || gModeRow == 11) {
            if (id == 56) {
                jp = gModeRow == 10 ? 60 : 61; // "this battle follows the '97/'98 Cup's rules"
            } else if (id >= 48 && id <= 51) {
                jp = gModeRow == 10 ? 54 : 55; // the cup's opening
            }
        }
        const auto& clips = JapaneseClips();
        if (jp > 0 && jp <= (int)clips.size()) {
            romOffset = clips[jp - 1];
            region = 2;
            key = jp;
        }
    }
    if (getenv("PORT_TRACE_SFX") != nullptr) {
        fprintf(stderr, "[announcer] clip %u -> %s %03X (rom 0x%x)\n", (unsigned)id, region ? RegionName(region) : "-",
                key, (unsigned)romOffset);
    }
    gRegion = region;
    gKey = key;
    gSerial = gSerial + 1;
    return romOffset;
}

int Port_SubtitlesSetting(void) {
    return CVarGetInteger("gPort.Subtitles", PORT_SUBTITLES_NON_NATIVE);
}

void Port_SubtitlesSetSetting(int value) {
    CVarSetInteger("gPort.Subtitles", value);
    CVarSave();
}

int Port_SubtitleSerial(void) {
    int region, key;
    if (TestLine(&region, &key)) {
        return 1;
    }
    return gSerial;
}

int Port_SubtitleTestActive(void) {
    int region, key;
    return TestLine(&region, &key) ? 1 : 0;
}

const char* Port_SubtitleText(void) {
    int region, key;
    if (!TestLine(&region, &key)) {
        region = gRegion;
        key = gKey;
    }
    if (region == 0) {
        return nullptr;
    }
    const int setting = Port_SubtitlesSetting();
    const std::string lang = Port::TextTargetLanguage();
    const std::string spoken = region == 2 ? "jp" : "en";
    if (setting == PORT_SUBTITLES_OFF || (setting == PORT_SUBTITLES_NON_NATIVE && lang == spoken)) {
        return nullptr;
    }
    char id[48];
    snprintf(id, sizeof id, "SUBTITLE_ANNOUNCER_%s_%03X", RegionName(region), key);
    // (language, id) -> the cell in the game's encoding; kept for the run, so the pointers stay valid
    static std::map<std::string, std::string> sCache;
    const std::string cacheKey = lang + '\n' + id;
    auto it = sCache.find(cacheKey);
    if (it == sCache.end()) {
        std::string cell;
        const auto& rows = Rows();
        auto row = rows.find(id);
        if (row != rows.end()) {
            auto c = row->second.find(lang);
            if (c != row->second.end()) {
                cell = c->second;
            }
        }
        it = sCache.emplace(cacheKey, cell.empty() ? std::string() : Port::TextEncode(cell)).first;
    }
    return it->second.empty() ? nullptr : it->second.c_str();
}

// The options screen's subtitles row (game/src/fragments/options/options.c): its label and values, in capitals like
// the screen's own rows, in the game's encoding.
static const char* OptionText(const char* id) {
    static std::map<std::string, std::string> sCache; // kept for the run: pointers stay valid
    const std::string lang = Port::TextTargetLanguage();
    const std::string key = lang + '\n' + id;
    auto it = sCache.find(key);
    if (it == sCache.end()) {
        it = sCache.emplace(key, Port::TextEncode(Port::TextUpper(Port::PortText(id, lang)))).first;
    }
    return it->second.c_str();
}

const char* Port_SubtitlesOptionLabel(void) {
    return OptionText("OPTIONS_TEXT_SUBTITLES");
}

const char* Port_SubtitlesOptionValue(int value) {
    (void)value; // off and on are the game's own words
    return OptionText("OPTIONS_SUBTITLES_NON_NATIVE");
}

} // extern "C"
