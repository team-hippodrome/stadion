// Settings of the hippodrome-utils enhancements (enhancements.h).
#include "enhancements.h"

#include <libultraship/libultraship.h>

#include <algorithm>
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
    return env >= 0 ? env : CVarGetInteger(cvar, 0) != 0;
}

void SetSetting(const char* cvar, int on) {
    CVarSetInteger(cvar, on ? 1 : 0);
    CVarSave();
}

} // namespace

extern "C" int Port_Enh_MoveDescriptionsEnabled(void) {
    return Setting("PORT_MOVE_DESCRIPTIONS", "gEnhancements.MoveDescriptions");
}

extern "C" void Port_Enh_SetMoveDescriptionsEnabled(int on) {
    SetSetting("gEnhancements.MoveDescriptions", on);
}

extern "C" int Port_Enh_OrderQueueEnabled(void) {
    return Setting("PORT_ORDER_QUEUE", "gEnhancements.OrderQueue");
}

extern "C" void Port_Enh_SetOrderQueueEnabled(int on) {
    SetSetting("gEnhancements.OrderQueue", on);
}

// ---- Stat Exp difficulty ----

namespace {

const char* const kSeGroup[2] = { "Rental", "Opponent" };

std::string SeCvar(int group, const char* field) {
    return std::string("gEnhancements.StatExpDifficulty.") + kSeGroup[group] + "." + field;
}

// PORT_STATEXP_*: a mode name or "<min>-<max>" (Custom); false when unset or unreadable
bool SeEnv(int group, hu_se_setting* out) {
    const char* v = getenv(group == PORT_SE_RENTAL ? "PORT_STATEXP_RENTAL" : "PORT_STATEXP_OPPONENT");
    if (v == nullptr || *v == '\0') {
        return false;
    }
    *out = hu_se_vanilla();
    auto same = [](const char* a, const char* b) { // ASCII, ignoring case
        for (; *a && *b; a++, b++) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
                return false;
            }
        }
        return *a == *b;
    };
    for (int m = 0; m < HU_SE_MODE_COUNT; m++) {
        if (m != HU_SE_CUSTOM && same(v, hu_se_mode_name(m))) {
            out->mode = (uint8_t)m;
            return true;
        }
    }
    unsigned a, b;
    if (sscanf(v, "%u-%u", &a, &b) != 2) {
        fprintf(stderr, "[enhancements] could not read %s (use vanilla, casual ... kaizo, or 0-65535)\n", v);
        return false;
    }
    out->mode = HU_SE_CUSTOM;
    out->custom_min = (uint16_t)std::min(a, 65535u);
    out->custom_max = (uint16_t)std::min(b, 65535u);
    return true;
}

} // namespace

extern "C" void Port_Enh_StatExpDifficulty(int group, hu_se_setting* out) {
    *out = hu_se_vanilla();
    if (group != PORT_SE_RENTAL && group != PORT_SE_OPPONENT) {
        return;
    }
    if (SeEnv(group, out)) {
        return;
    }
    const int mode = CVarGetInteger(SeCvar(group, "Mode").c_str(), HU_SE_VANILLA);
    out->mode = (uint8_t)((mode >= 0 && mode < HU_SE_MODE_COUNT) ? mode : HU_SE_VANILLA);
    out->custom_min = (uint16_t)std::clamp(CVarGetInteger(SeCvar(group, "Min").c_str(), 0), 0, 65535);
    out->custom_max = (uint16_t)std::clamp(CVarGetInteger(SeCvar(group, "Max").c_str(), 65535), 0, 65535);
}

extern "C" void Port_Enh_SetStatExpDifficulty(int group, const hu_se_setting* setting) {
    if (group != PORT_SE_RENTAL && group != PORT_SE_OPPONENT) {
        return;
    }
    CVarSetInteger(SeCvar(group, "Mode").c_str(), setting->mode);
    CVarSetInteger(SeCvar(group, "Min").c_str(), setting->custom_min);
    CVarSetInteger(SeCvar(group, "Max").c_str(), setting->custom_max);
    CVarSave();
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
        mode = CVarGetInteger("gEnhancements.HueChoice", PORT_HUE_OFF);
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
