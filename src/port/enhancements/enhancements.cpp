// Settings of the hippodrome-utils enhancements (enhancements.h).
#include "enhancements.h"

#include <libultraship/libultraship.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" int Port_Enh_Log(void) {
    static const int on = [] {
        const char* v = getenv("PORT_ENH_LOG");
        return (v != nullptr && *v != '\0' && *v != '0') ? 1 : 0;
    }();
    return on;
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
