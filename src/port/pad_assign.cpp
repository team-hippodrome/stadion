// Which physical controller plays which N64 player: see pad_assign.h.
#include "pad_assign.h"

#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/controllerbridge.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/Controller.h>
#include <ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h>
#include <ship/core/Context.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <map>

namespace Port {
namespace {

constexpr int kPorts = 4;

std::string Guid(SDL_JoystickID id) {
    char text[33] = "";
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), text, sizeof text);
    return text;
}

std::string PrefKey(const std::string& guid) {
    return "gPort.PadPlayer." + guid;
}

std::map<int32_t, int> gAssigned; // instance id -> port, as last applied
std::vector<int32_t> gAppliedIds; // the connected ids it was applied for
int gGeneration = 0, gAppliedGeneration = -1; // SetPadPort bumps it
std::atomic<int> gPortBits{0}; // PadPortBits (the game thread reads it)

std::shared_ptr<Ship::ConnectedPhysicalDeviceManager> Manager() {
    auto deck = ControllerGetControlDeck();
    return deck != nullptr ? deck->GetConnectedPhysicalDeviceManager() : nullptr;
}

// the connected gamepads' instance ids in connection order (SDL numbers them upwards)
std::vector<int32_t> ConnectedIds(const std::shared_ptr<Ship::ConnectedPhysicalDeviceManager>& m) {
    std::vector<int32_t> ids;
    for (const auto& [id, name] : m->GetConnectedSDLGamepadNames()) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

SDL_Gamepad* PadOnPort(int port) {
    for (const auto& [id, p] : gAssigned) {
        if (p == port) return SDL_GetGamepadFromID(id);
    }
    return nullptr;
}

} // namespace

void UpdatePadAssignment() {
    auto m = Manager();
    if (m == nullptr) return;
    const std::vector<int32_t> ids = ConnectedIds(m);
    if (ids == gAppliedIds && gGeneration == gAppliedGeneration) return;

    // the chosen player first (when it is free), then the free players in connection order
    std::map<int32_t, int> assigned;
    std::array<bool, kPorts> taken{};
    for (int32_t id : ids) {
        const int pref = CVarGetInteger(PrefKey(Guid(id)).c_str(), 0) - 1;
        if (pref >= 0 && pref < kPorts && !taken[pref]) {
            assigned[id] = pref;
            taken[pref] = true;
        }
    }
    for (int32_t id : ids) {
        if (assigned.count(id)) continue;
        for (int p = 0; p < kPorts; p++) {
            if (!taken[p]) {
                assigned[id] = p;
                taken[p] = true;
                break;
            }
        }
    }
    auto deck = ControllerGetControlDeck();
    bool saved = false;
    for (int p = 0; p < kPorts; p++) {
        for (int32_t id : ids) {
            auto it = assigned.find(id);
            if (it != assigned.end() && it->second == p) m->UnignoreInstanceIdForPort((uint8_t)p, id);
            else m->IgnoreInstanceIdForPort((uint8_t)p, id);
        }
        // players 2-4 have no controller bindings until someone sets them (libultraship gives defaults only to
        // player 1): the defaults when a controller arrives for one
        if (p > 0 && taken[p] && deck != nullptr) {
            auto c = deck->GetControllerByPort((uint8_t)p);
            if (c != nullptr && !c->HasConfig()) {
                c->AddDefaultMappings(Ship::PhysicalDeviceType::SDLGamepad);
                CVarSetInteger(("gControllers.Port" + std::to_string(p + 1) + ".HasConfig").c_str(), 1);
                saved = true;
            }
        }
    }
    if (saved) CVarSave();
    if (ids != gAppliedIds) {
        std::string line;
        for (int32_t id : ids) {
            auto it = assigned.find(id);
            line += " [" + (it != assigned.end() ? "player " + std::to_string(it->second + 1) : std::string("none")) +
                    ": " + (SDL_GetGamepadNameForID(id) ? SDL_GetGamepadNameForID(id) : "?") + "]";
        }
        SDL_Log("[pads] %zu controller(s):%s", ids.size(), line.c_str());
    }
    int bits = 0;
    for (const auto& [id, p] : assigned) bits |= 1 << p;
    gPortBits = bits;
    gAssigned = std::move(assigned);
    gAppliedIds = ids;
    gAppliedGeneration = gGeneration;
}

std::vector<PadInfo> ConnectedPads() {
    std::vector<PadInfo> pads;
    auto m = Manager();
    if (m == nullptr) return pads;
    const auto names = m->GetConnectedSDLGamepadNames();
    for (int32_t id : ConnectedIds(m)) {
        PadInfo info;
        info.id = id;
        info.name = names.at(id);
        info.guid = Guid(id);
        auto it = gAssigned.find(id);
        info.port = it != gAssigned.end() ? it->second : -1;
        info.pad = SDL_GetGamepadFromID(id);
        pads.push_back(info);
    }
    std::stable_sort(pads.begin(), pads.end(), [](const PadInfo& a, const PadInfo& b) {
        return (unsigned)a.port < (unsigned)b.port; // (-1 last)
    });
    return pads;
}

int PadPortBits() {
    return gPortBits;
}

void SetPadPort(int32_t id, int port) {
    if (port < 0 || port >= kPorts) return;
    auto it = gAssigned.find(id);
    const int old = it != gAssigned.end() ? it->second : -1;
    for (const auto& [other, p] : gAssigned) {
        if (p == port && other != id) {
            // the controller that was there takes this one's old player (or the first free one, from none)
            CVarSetInteger(PrefKey(Guid(other)).c_str(), old >= 0 ? old + 1 : 0);
        }
    }
    CVarSetInteger(PrefKey(Guid(id)).c_str(), port + 1);
    CVarSave();
    gGeneration++;
    UpdatePadAssignment();
}

bool PadActive(int port) {
    SDL_Gamepad* g = PadOnPort(port);
    if (g == nullptr) return false;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        if (SDL_GetGamepadButton(g, (SDL_GamepadButton)b)) return true;
    }
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
        if (std::abs((int)SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a)) > 16000) return true;
    }
    return false;
}

std::string PadInputLabel(int port, const std::string& xbox) {
    SDL_Gamepad* g = PadOnPort(port);
    if (g == nullptr) return xbox;
    // the face buttons by position (south = N64 A), named as printed on this controller
    const std::pair<const char*, SDL_GamepadButton> face[] = {
        { "A", SDL_GAMEPAD_BUTTON_SOUTH }, { "B", SDL_GAMEPAD_BUTTON_EAST },
        { "X", SDL_GAMEPAD_BUTTON_WEST },  { "Y", SDL_GAMEPAD_BUTTON_NORTH },
    };
    for (auto [name, button] : face) {
        if (xbox != name) continue;
        switch (SDL_GetGamepadButtonLabel(g, button)) {
            case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
            case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
            case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
            case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
            case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
            case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
            case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
            default: return xbox;
        }
    }
    struct Names { const char *lb, *rb, *lt, *rt, *view, *guide; };
    Names n{ "LB", "RB", "LT", "RT", "View", "Xbox" };
    switch (SDL_GetGamepadType(g)) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            n = { "L1", "R1", "L2", "R2", SDL_GetGamepadType(g) == SDL_GAMEPAD_TYPE_PS3 ? "Select" : "Share", "PS" };
            break;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            n = { "L", "R", "ZL", "ZR", "-", "Home" };
            break;
        case SDL_GAMEPAD_TYPE_XBOX360:
            n = { "LB", "RB", "LT", "RT", "Back", "Guide" };
            break;
        default:
            break;
    }
    const std::pair<const char*, const char*> other[] = {
        { "LB", n.lb }, { "RB", n.rb }, { "LT", n.lt }, { "RT", n.rt }, { "View", n.view }, { "Xbox", n.guide },
    };
    for (auto [from, to] : other) {
        if (xbox == from) return to;
    }
    return xbox;
}

std::vector<UnknownJoystick> UnknownJoysticks() {
    std::vector<UnknownJoystick> list;
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) {
        for (int i = 0; i < n; i++) {
            if (SDL_IsGamepad(ids[i])) continue;
            const char* name = SDL_GetJoystickNameForID(ids[i]);
            list.push_back({ (int32_t)ids[i], name != nullptr ? name : "Controller", Guid(ids[i]) });
        }
        SDL_free(ids);
    }
    return list;
}

bool AddPadMapping(const std::string& mapping, std::string& error) {
    if (SDL_AddGamepadMapping(mapping.c_str()) < 0) {
        error = std::string("SDL did not accept the mapping: ") + SDL_GetError();
        return false;
    }
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("gamecontrollerdb.txt");
    FILE* f = fopen(path.c_str(), "a");
    if (f == nullptr || fprintf(f, "%s\n", mapping.c_str()) < 0) {
        if (f != nullptr) fclose(f);
        error = "The controller works now, but gamecontrollerdb.txt could not be written: set it up again next time.";
        return false;
    }
    fclose(f);
    return true;
}

namespace {
SDL_Joystick* gTestPad = nullptr;
}
void AttachTestPad() {
    if (gTestPad != nullptr) return;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    desc.naxes = 4;
    desc.nbuttons = 16;
    desc.nhats = 1;
    desc.vendor_id = 0x1234;
    desc.product_id = 0x5678;
    desc.name = "Test Unknown Pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    gTestPad = id != 0 ? SDL_OpenJoystick(id) : nullptr;
    SDL_Log("[pads] test pad %s (gamepad: %d)", gTestPad ? "attached" : SDL_GetError(), id != 0 && SDL_IsGamepad(id));
}
void PressTestPad(int button) {
    if (gTestPad == nullptr) return;
    for (int b = 0; b < 16; b++) SDL_SetJoystickVirtualButton(gTestPad, b, b == button);
    SDL_UpdateJoysticks();
}

} // namespace Port
