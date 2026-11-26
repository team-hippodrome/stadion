// Settings of the hippodrome-utils enhancements (enhancements.h).
#include "enhancements.h"

#include <libultraship/libultraship.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

extern "C" const char* Port_FlashFilePath(void); // os_devices.cpp

extern "C" int Port_Enh_Log(void) {
    static const int on = [] {
        const char* v = getenv("PORT_ENH_LOG");
        return (v != nullptr && *v != '\0' && *v != '0') ? 1 : 0;
    }();
    return on;
}

// ---- the On / Off settings ----

namespace {

int EnvOverride(const char* name) {
    const char* v = getenv(name);
    if (v == nullptr || *v == '\0') {
        return -1;
    }
    return atoi(v) != 0 ? 1 : 0;
}

int Setting(const char* env_name, const char* cvar) {
    const int env = EnvOverride(env_name);
    return env >= 0 ? env : CVarGetInteger(cvar, 1) != 0; // on by default (Julius: customisability first)
}

void SetSetting(const char* cvar, int on) {
    CVarSetInteger(cvar, on ? 1 : 0);
    CVarSave();
}

bool SameAsciiFull(const char* a, const char* b) { // a is b, ignoring case
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

} // namespace

extern "C" int Port_Enh_MoveDescriptionsEnabled(void) {
    return Setting("PORT_MOVE_DESCRIPTIONS", "gEnhancements.MoveDescriptions");
}

extern "C" void Port_Enh_SetMoveDescriptionsEnabled(int on) {
    SetSetting("gEnhancements.MoveDescriptions", on);
}

extern "C" int Port_Enh_SummaryBarsEnabled(void) {
    return Setting("PORT_SUMMARY_BARS", "gEnhancements.SummaryBars");
}

extern "C" void Port_Enh_SetSummaryBarsEnabled(int on) {
    SetSetting("gEnhancements.SummaryBars", on);
}

extern "C" int Port_Enh_OrderQueueEnabled(void) {
    return Setting("PORT_ORDER_QUEUE", "gEnhancements.OrderQueue");
}

extern "C" void Port_Enh_SetOrderQueueEnabled(int on) {
    SetSetting("gEnhancements.OrderQueue", on);
}

extern "C" int Port_Enh_TrainerMemoryEnabled(void) {
    return Setting("PORT_TRAINER_MEMORY", "gEnhancements.TrainerMemory");
}

extern "C" void Port_Enh_SetTrainerMemoryEnabled(int on) {
    SetSetting("gEnhancements.TrainerMemory", on);
}

// ---- the rival bug fix ----

extern "C" int Port_Enh_RivalFixEnabled(void) {
    return Setting("PORT_RIVAL_FIX", "gEnhancements.RivalFix");
}

extern "C" void Port_Enh_SetRivalFixEnabled(int on) {
    SetSetting("gEnhancements.RivalFix", on);
}

namespace {

// <N64 save>.rival: one line per registered team, "<key, 16 hex digits> <starter choice, 0-255>"
std::map<uint64_t, int> gRival;
bool gRivalRead = false;

std::string RivalPath() {
    return std::string(Port_FlashFilePath()) + ".rival";
}

void RivalRead() {
    if (gRivalRead) {
        return;
    }
    gRivalRead = true;
    FILE* f = fopen(RivalPath().c_str(), "r");
    if (f == nullptr) {
        return;
    }
    unsigned long long key;
    int starter;
    while (fscanf(f, "%llx %d", &key, &starter) == 2) {
        if (starter >= 0 && starter <= 255) {
            gRival[(uint64_t)key] = starter;
        }
    }
    fclose(f);
}

} // namespace

extern "C" void Port_Enh_RivalRecord(uint64_t key, int starter) {
    RivalRead();
    if (starter < 0 || starter > 255) {
        return;
    }
    auto it = gRival.find(key);
    if (it != gRival.end() && it->second == starter) {
        return;
    }
    gRival[key] = starter;
    const std::string path = RivalPath();
    FILE* f = fopen(path.c_str(), "w");
    if (f == nullptr) {
        fprintf(stderr, "[enhancements] could not write %s\n", path.c_str());
        return;
    }
    for (const auto& [k, v] : gRival) {
        fprintf(f, "%016llx %d\n", (unsigned long long)k, v);
    }
    fclose(f);
}

extern "C" int Port_Enh_RivalLookup(uint64_t key) {
    RivalRead();
    auto it = gRival.find(key);
    return it == gRival.end() ? -1 : it->second;
}

// ---- Harder vs Mewtwo ----

namespace {
const char* const kMewtwoTierNames[PORT_MEWTWO_TIERS] = { "off", "easy", "medium", "hard", "lunatic", "kaizo" };
} // namespace

extern "C" int Port_Enh_HarderMewtwo(void) {
    const char* v = getenv("PORT_HARDER_MEWTWO");
    int tier;
    if (v != nullptr && *v != '\0') {
        for (int i = 0; i < PORT_MEWTWO_TIERS; i++) {
            if (SameAsciiFull(v, kMewtwoTierNames[i])) {
                return i;
            }
        }
        tier = atoi(v);
    } else {
        tier = CVarGetInteger("gEnhancements.HarderMewtwo", PORT_MEWTWO_EASY);
    }
    return (tier >= 0 && tier < PORT_MEWTWO_TIERS) ? tier : PORT_MEWTWO_OFF;
}

extern "C" void Port_Enh_SetHarderMewtwo(int tier) {
    CVarSetInteger("gEnhancements.HarderMewtwo", tier);
    CVarSave();
}

// ---- Stat Exp difficulty ----

namespace {

const char* const kSeGroup[2] = { "Rental", "Opponent" };

std::string SeCvar(int group, const char* bound, const char* field) {
    return std::string("gEnhancements.StatExpDifficulty.") + kSeGroup[group] + "." + bound + "." + field;
}

bool SameAscii(const char* a, const char* b, size_t n) { // the first n characters of a are b, ignoring case
    size_t i = 0;
    for (; i < n && b[i] != '\0'; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return i == n && b[i] == '\0';
}

// one bound of PORT_STATEXP_*: a mode name or a number (Custom)
bool SeEnvBound(const char* v, size_t n, hu_se_bound* out) {
    for (int m = 0; m < HU_SE_MODE_COUNT; m++) {
        if (m != HU_SE_CUSTOM && SameAscii(v, hu_se_mode_name(m), n)) {
            out->mode = (uint8_t)m;
            return true;
        }
    }
    char* end = nullptr;
    const unsigned long value = strtoul(std::string(v, n).c_str(), &end, 10);
    if (n == 0 || end == nullptr || *end != '\0') {
        return false;
    }
    out->mode = HU_SE_CUSTOM;
    out->custom = (uint16_t)std::min(value, 65535ul);
    return true;
}

bool SeEnv(int group, hu_se_bounds* out) {
    const char* v = getenv(group == PORT_SE_RENTAL ? "PORT_STATEXP_RENTAL" : "PORT_STATEXP_OPPONENT");
    if (v == nullptr || *v == '\0') {
        return false;
    }
    *out = hu_se_bounds_vanilla();
    const char* colon = strchr(v, ':');
    const bool ok = colon != nullptr
                        ? SeEnvBound(v, (size_t)(colon - v), &out->min) && SeEnvBound(colon + 1, strlen(colon + 1), &out->max)
                        : SeEnvBound(v, strlen(v), &out->min) && SeEnvBound(v, strlen(v), &out->max);
    if (!ok) {
        fprintf(stderr, "[enhancements] could not read %s (use <min>:<max>, each vanilla, casual ... kaizo or 0-65535)\n", v);
        *out = hu_se_bounds_vanilla();
        return false;
    }
    return true;
}

hu_se_bound SeRead(int group, const char* bound, uint16_t fallback) {
    hu_se_bound b;
    const int mode = CVarGetInteger(SeCvar(group, bound, "Mode").c_str(), HU_SE_VANILLA);
    b.mode = (uint8_t)((mode >= 0 && mode < HU_SE_MODE_COUNT) ? mode : HU_SE_VANILLA);
    b.custom = (uint16_t)std::clamp(CVarGetInteger(SeCvar(group, bound, "Value").c_str(), fallback), 0, 65535);
    return b;
}

void SeWrite(int group, const char* bound, const hu_se_bound& b) {
    CVarSetInteger(SeCvar(group, bound, "Mode").c_str(), b.mode);
    CVarSetInteger(SeCvar(group, bound, "Value").c_str(), b.custom);
}

} // namespace

extern "C" void Port_Enh_StatExpBounds(int group, hu_se_bounds* out) {
    *out = hu_se_bounds_vanilla();
    if (group != PORT_SE_RENTAL && group != PORT_SE_OPPONENT) {
        return;
    }
    if (SeEnv(group, out)) {
        return;
    }
    out->min = SeRead(group, "Min", 0);
    out->max = SeRead(group, "Max", 65535);
}

extern "C" void Port_Enh_SetStatExpBounds(int group, const hu_se_bounds* bounds) {
    if (group != PORT_SE_RENTAL && group != PORT_SE_OPPONENT) {
        return;
    }
    SeWrite(group, "Min", bounds->min);
    SeWrite(group, "Max", bounds->max);
    CVarSave();
}

extern "C" void Port_Enh_OpponentStatExp(uint16_t* lo, uint16_t* hi) {
    hu_se_bounds b;
    Port_Enh_StatExpBounds(PORT_SE_OPPONENT, &b);
    hu_se_bounds_se_range(&b, HU_SE_OPPONENT, lo, hi);
}

// ---- Hue choice ----

extern "C" int Port_Enh_HueChoiceMode(void) {
    const char* v = getenv("PORT_HUE_CHOICE");
    int mode;
    if (v != nullptr && *v != '\0') {
        if (strcmp(v, "restricted") == 0) return PORT_HUE_RESTRICTED;
        if (strcmp(v, "unrestricted") == 0) return PORT_HUE_UNRESTRICTED;
        if (strcmp(v, "off") == 0) return PORT_HUE_OFF;
        mode = atoi(v);
    } else {
        mode = CVarGetInteger("gEnhancements.HueChoice", PORT_HUE_RESTRICTED);
    }
    return (mode == PORT_HUE_UNRESTRICTED || mode == PORT_HUE_RESTRICTED) ? mode : PORT_HUE_OFF;
}

extern "C" void Port_Enh_SetHueChoiceMode(int mode) {
    CVarSetInteger("gEnhancements.HueChoice", mode);
    CVarSave();
}

extern "C" int Port_Enh_HueChoiceEnabled(void) {
    return Port_Enh_HueChoiceMode() != PORT_HUE_OFF;
}

namespace {

hu_hue_store* gHues = nullptr;
std::string gHuePath;

// A side file that could not be read is kept as <file>.bad, so the next write does not lose what was in it.
void KeepDamaged(const std::string& path, int code) {
    std::error_code ec;
    const auto from = std::filesystem::u8path(path);
    const auto to = std::filesystem::u8path(path + ".bad");
    std::filesystem::remove(to, ec);
    std::filesystem::rename(from, to, ec);
    fprintf(stderr, "[enhancements] %s could not be read (%d), kept as %s.bad\n", path.c_str(), code, path.c_str());
}

} // namespace

extern "C" hu_hue_store* Port_Enh_Hues(void) {
    if (gHues != nullptr) {
        return gHues;
    }
    gHues = hu_hue_store_new();
    gHuePath = std::string(Port_FlashFilePath()) + ".hue";
    const hu_hue_io r = hu_hue_load(gHues, gHuePath.c_str());
    if (r == HU_HUE_CORRUPT || r == HU_HUE_IO_ERROR) {
        KeepDamaged(gHuePath, (int)r);
    }
    return gHues;
}

extern "C" void Port_Enh_SaveHues(void) {
    if (gHues == nullptr || !hu_hue_dirty(gHues)) {
        return;
    }
    if (hu_hue_save(gHues, gHuePath.c_str()) != HU_HUE_OK) {
        fprintf(stderr, "[enhancements] could not write %s\n", gHuePath.c_str());
    }
}

// ---- trainer memory ----

namespace {
hu_tm* gMemory = nullptr;
std::string gMemoryPath;
} // namespace

extern "C" const char* Port_Enh_MemoryPath(void) {
    if (gMemoryPath.empty()) {
        gMemoryPath = std::string(Port_FlashFilePath()) + ".memory";
    }
    return gMemoryPath.c_str();
}

extern "C" hu_tm* Port_Enh_Memory(void) {
    if (gMemory != nullptr) {
        return gMemory;
    }
    gMemory = hu_tm_new();
    const char* path = Port_Enh_MemoryPath();
    const hu_tm_io r = hu_tm_load(gMemory, path);
    if (r == HU_TM_CORRUPT || r == HU_TM_IO_ERROR) {
        KeepDamaged(path, (int)r);
    }
    return gMemory;
}

extern "C" void Port_Enh_SaveMemory(void) {
    if (gMemory == nullptr || !hu_tm_dirty(gMemory)) {
        return;
    }
    if (hu_tm_save(gMemory, Port_Enh_MemoryPath()) != HU_TM_OK) {
        fprintf(stderr, "[enhancements] could not write %s\n", Port_Enh_MemoryPath());
    }
}

// ---- the summary bars' and the memory screen's views (game thread -> render thread) ----

namespace {

std::mutex gViewMutex;
PortEnhBattleView gView;
std::chrono::steady_clock::time_point gViewTime;
bool gViewSet = false;
std::map<uint32_t, std::vector<uint8_t>> gIcons; // never erased: the pointers handed out stay valid
std::map<uint32_t, std::vector<uint8_t>> gPortraits; // (the same)

} // namespace

extern "C" void Port_Enh_PublishBattle(const PortEnhBattleView* view) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    gView = *view;
    gViewTime = std::chrono::steady_clock::now();
    gViewSet = true;
}

extern "C" int Port_Enh_LatestBattle(PortEnhBattleView* out) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    // a battle draws its HUD every game frame: a view older than a quarter of a second is a battle that has ended
    if (!gViewSet || std::chrono::steady_clock::now() - gViewTime > std::chrono::milliseconds(250)) {
        return 0;
    }
    *out = gView;
    return 1;
}

namespace {
PortEnhMemoryView gMemoryView;
std::chrono::steady_clock::time_point gMemoryViewTime;
bool gMemoryViewSet = false;
} // namespace

extern "C" void Port_Enh_PublishMemory(const PortEnhMemoryView* view) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    gMemoryView = *view;
    gMemoryViewTime = std::chrono::steady_clock::now();
    gMemoryViewSet = true;
}

extern "C" int Port_Enh_LatestMemory(PortEnhMemoryView* out) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    // the screen publishes every frame it runs: an older view is a screen that has gone
    if (!gMemoryViewSet || std::chrono::steady_clock::now() - gMemoryViewTime > std::chrono::milliseconds(250)) {
        return 0;
    }
    *out = gMemoryView;
    return 1;
}

extern "C" int Port_Enh_HasCardIcon(uint32_t key) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    return gIcons.count(key) != 0;
}

extern "C" void Port_Enh_SetCardIcon(uint32_t key, const uint8_t* rgba) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    if (gIcons.count(key) == 0) {
        gIcons[key].assign(rgba, rgba + PORT_ENH_ICON * PORT_ENH_ICON * 4);
    }
}

extern "C" int Port_Enh_HasPortrait(uint32_t index) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    return gPortraits.count(index) != 0;
}

extern "C" void Port_Enh_SetPortrait(uint32_t index, const uint8_t* rgba) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    if (gPortraits.count(index) == 0) {
        gPortraits[index].assign(rgba, rgba + PORT_ENH_PORTRAIT * PORT_ENH_PORTRAIT * 4);
    }
}

extern "C" const uint8_t* Port_Enh_Portrait(uint32_t index) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    auto it = gPortraits.find(index);
    return it == gPortraits.end() ? nullptr : it->second.data();
}

extern "C" const uint8_t* Port_Enh_CardIcon(uint32_t key) {
    std::lock_guard<std::mutex> lock(gViewMutex);
    auto it = gIcons.find(key);
    return it == gIcons.end() ? nullptr : it->second.data();
}
