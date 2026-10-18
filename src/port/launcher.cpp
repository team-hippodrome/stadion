// Launcher shown inside the game window when the port is started without a ROM argument (macOS, Linux, Windows). Same
// design as the macOS app's launcher (tools/launcher/Launcher.swift): four player cards with the Game Boy cartridge
// of each Transfer Pak (picture, game, trainer from the save, save file) and a bar with the Stadium ROM, its save and
// PLAY. The choices are remembered in pokestadium.cfg.json and passed to the game the way the command line does
// (PORT_FLASH_FILE, PORT_GB_ROM*, PORT_GB_SAVE*). Auto-play skips it next time (hold Shift while starting to see it
// again); Debug adds a button that opens the save editor (tools/save_editor, needs python3) in the browser. Controls
// swaps the player cards for the key and controller bindings of each player (libultraship's ControlDeck mappings).
#include "launcher.h"

#include <libultraship/libultraship.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/controllerbridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/Controller.h>
#include <ship/controller/controldevice/controller/mapping/factories/AxisDirectionMappingFactory.h>
#include <ship/controller/controldevice/controller/mapping/factories/ButtonMappingFactory.h>
#include <ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToAxisDirectionMapping.h>
#include <ship/controller/controldevice/controller/mapping/keyboard/KeyboardKeyToButtonMapping.h>
#include <ship/window/gui/IconsFontAwesome4.h>
#include <ship/core/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/gui/GuiWindow.h>
#include <fast/Fast3dGui.h>
#include <fast/Fast3dWindow.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// ---- choices, stored as CVars (gLauncher.*) -------------------------------------------------------------------------

struct GbSlot {
    bool enabled = false;
    std::string rom, save;
};

struct State {
    std::string rom, save;
    std::array<GbSlot, 4> gb;
    bool autoPlay = false, debug = false;
};

std::string Key(const char* name) {
    return std::string("gLauncher.") + name;
}
std::string GbKey(int i, const char* name) {
    return "gLauncher.Gb" + std::to_string(i + 1) + "." + name;
}

std::string DefaultSave() {
    return Ship::Context::GetPathRelativeToAppDirectory("saves/stadium.flash");
}

// the Game Boy save next to the cartridge unless one was chosen
std::string GbSave(const GbSlot& g) {
    if (!g.save.empty() || g.rom.empty()) {
        return g.save;
    }
    return fs::path(g.rom).replace_extension(".sav").string();
}

State Load() {
    State s;
    s.rom = CVarGetString(Key("Rom").c_str(), "");
    s.save = CVarGetString(Key("Save").c_str(), DefaultSave().c_str());
    for (int i = 0; i < 4; i++) {
        s.gb[i].enabled = CVarGetInteger(GbKey(i, "Enabled").c_str(), 0) != 0;
        s.gb[i].rom = CVarGetString(GbKey(i, "Rom").c_str(), "");
        s.gb[i].save = CVarGetString(GbKey(i, "Save").c_str(), "");
    }
    s.autoPlay = CVarGetInteger(Key("AutoPlay").c_str(), 0) != 0;
    s.debug = CVarGetInteger(Key("Debug").c_str(), 0) != 0;
    return s;
}

void Store(const State& s) {
    CVarSetString(Key("Rom").c_str(), s.rom.c_str());
    CVarSetString(Key("Save").c_str(), s.save.c_str());
    for (int i = 0; i < 4; i++) {
        CVarSetInteger(GbKey(i, "Enabled").c_str(), s.gb[i].enabled ? 1 : 0);
        CVarSetString(GbKey(i, "Rom").c_str(), s.gb[i].rom.c_str());
        CVarSetString(GbKey(i, "Save").c_str(), s.gb[i].save.c_str());
    }
    CVarSetInteger(Key("AutoPlay").c_str(), s.autoPlay ? 1 : 0);
    CVarSetInteger(Key("Debug").c_str(), s.debug ? 1 : 0);
    CVarSave();
}

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

std::vector<uint8_t> ReadFile(const std::string& path, size_t maxBytes = SIZE_MAX) {
    std::vector<uint8_t> d;
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) {
        return d;
    }
    f.seekg(0, std::ios::end);
    size_t n = std::min((size_t)f.tellg(), maxBytes);
    f.seekg(0);
    d.resize(n);
    f.read((char*)d.data(), (std::streamsize)n);
    return d;
}

// ---- checks and cartridge information --------------------------------------------------------------------------------

// "" when the file is a Pokémon Stadium (USA) ROM in .z64 byte order, else what is wrong with it
std::string CheckRom(const std::string& path) {
    if (path.empty()) {
        return "No ROM loaded";
    }
    std::vector<uint8_t> h = ReadFile(path, 0x40);
    if (h.empty()) {
        return "Cannot open the file";
    }
    if (h.size() < 0x40) {
        return "Not an N64 ROM";
    }
    if (h[0] == 0x37 && h[1] == 0x80) {
        return "Byte-swapped (.v64): use .z64";
    }
    if (h[0] == 0x40 && h[1] == 0x12) {
        return "Little-endian (.n64): use .z64";
    }
    if (h[0] != 0x80 || h[1] != 0x37) {
        return "Not an N64 ROM";
    }
    if (memcmp(h.data() + 0x20, "POKEMON STADIUM", 15) != 0 || h[0x3E] != 'E') {
        return "Not Pokémon Stadium (USA)";
    }
    return "";
}

struct CartInfo {
    std::string kind;  // "yellow" | "red" | "blue" | "" : picks the cartridge picture
    std::string label; // e.g. "Pokémon Gelb"
    std::string trainer, trainerId; // from the save, when its main-data checksum is right
};

const char* Gen1Char(uint8_t b, char* buf) {
    if (b >= 0x80 && b <= 0x99) { buf[0] = (char)('A' + (b - 0x80)); buf[1] = 0; return buf; }
    if (b >= 0xA0 && b <= 0xB9) { buf[0] = (char)('a' + (b - 0xA0)); buf[1] = 0; return buf; }
    if (b >= 0xF6) { buf[0] = (char)('0' + (b - 0xF6)); buf[1] = 0; return buf; }
    switch (b) { // German charmap extras
        case 0x7F: return " ";
        case 0xC0: return "Ä";
        case 0xC1: return "Ö";
        case 0xC2: return "Ü";
        case 0xC3: return "ä";
        case 0xC4: return "ö";
        case 0xC5: return "ü";
        default: return "";
    }
}

// as Launcher.swift's inspectCart
CartInfo InspectCart(const std::string& rom, const std::string& save) {
    CartInfo info;
    std::vector<uint8_t> r = ReadFile(rom, 0x150);
    if (r.size() < 0x150) {
        info.label = rom.empty() ? "" : "Not a Game Boy ROM";
        return info;
    }
    std::string title;
    for (int i = 0x134; i < 0x144; i++) {
        title += (r[i] < 0x20 || r[i] > 0x7E) ? ' ' : (char)r[i];
    }
    bool german = rom.find("Germany") != std::string::npos || rom.find("(G)") != std::string::npos;
    if (title.find("YEL") != std::string::npos || title.find("GELB") != std::string::npos) {
        info.kind = "yellow";
        info.label = german ? "Pokémon Gelb" : "Pokémon Yellow";
    } else if (title.find("RED") != std::string::npos || title.find("ROT") != std::string::npos) {
        info.kind = "red";
        info.label = german ? "Pokémon Rot" : "Pokémon Red";
    } else if (title.find("BLUE") != std::string::npos || title.find("BLAU") != std::string::npos) {
        info.kind = "blue";
        info.label = german ? "Pokémon Blau" : "Pokémon Blue";
    } else if (title.find("GREEN") != std::string::npos) {
        info.label = "Pokémon Green";
    } else {
        size_t end = title.find_last_not_of(' ');
        info.label = end == std::string::npos ? "Unknown cartridge" : title.substr(0, end + 1);
    }
    std::vector<uint8_t> s = ReadFile(save);
    if (s.size() >= 0x3524) {
        uint32_t sum = 0;
        for (int i = 0x2598; i <= 0x3522; i++) {
            sum += s[i];
        }
        if ((uint8_t)~sum == s[0x3523]) {
            char buf[2];
            for (int i = 0x2598; i <= 0x25A2 && s[i] != 0x50 && s[i] != 0; i++) {
                info.trainer += Gen1Char(s[i], buf);
            }
            char id[8];
            snprintf(id, sizeof id, "%05d", (s[0x2605] << 8) | s[0x2606]);
            info.trainerId = id;
        }
    }
    return info;
}

std::string FileName(const std::string& path) {
    return path.empty() ? std::string("—") : fs::u8path(path).filename().string();
}

// ---- native file dialogs (SDL); they report back through a callback, possibly on another thread ----------------------

std::mutex gPickLock;
std::string* gPickTarget = nullptr;
std::string gPickResult;
bool gPickDone = false;

void PickCallback(void*, const char* const* files, int) {
    std::lock_guard<std::mutex> g(gPickLock);
    gPickResult = files != nullptr && files[0] != nullptr ? files[0] : "";
    gPickDone = true;
}

SDL_Window* MainWindow() {
    int count = 0;
    SDL_Window* w = nullptr;
    if (SDL_Window** ws = SDL_GetWindows(&count)) {
        if (count > 0) {
            w = ws[0];
        }
        SDL_free(ws);
    }
    return w;
}

void Pick(std::string* target, bool save, const SDL_DialogFileFilter* filters, int nfilters,
          const std::string& suggested = "") {
    {
        std::lock_guard<std::mutex> g(gPickLock);
        if (gPickTarget != nullptr) {
            return; // one dialog at a time
        }
        gPickTarget = target;
        gPickDone = false;
    }
    std::string start = !target->empty() ? *target : suggested;
    if (save) {
        SDL_ShowSaveFileDialog(PickCallback, nullptr, MainWindow(), filters, nfilters, start.empty() ? nullptr : start.c_str());
    } else {
        std::string dir = start.empty() ? "" : fs::u8path(start).parent_path().string();
        SDL_ShowOpenFileDialog(PickCallback, nullptr, MainWindow(), filters, nfilters, dir.empty() ? nullptr : dir.c_str(),
                               false);
    }
}

// a dialog that finished since the last frame: store its file
bool ApplyPick() {
    std::lock_guard<std::mutex> g(gPickLock);
    if (gPickTarget == nullptr || !gPickDone) {
        return false;
    }
    if (!gPickResult.empty()) {
        *gPickTarget = gPickResult;
    }
    gPickTarget = nullptr;
    return true;
}

const SDL_DialogFileFilter kRomFilter[] = { { "N64 ROM (.z64)", "z64" }, { "All files", "*" } };
const SDL_DialogFileFilter kSaveFilter[] = { { "Stadium save (.flash)", "flash;fla" }, { "All files", "*" } };
const SDL_DialogFileFilter kGbRomFilter[] = { { "Game Boy ROM", "gb;gbc" }, { "All files", "*" } };
const SDL_DialogFileFilter kGbSaveFilter[] = { { "Game Boy save (.sav)", "sav" }, { "All files", "*" } };

// ---- save editor (debug option) --------------------------------------------------------------------------------------

// tools/save_editor next to the executable or in the source tree above it
std::string SaveEditorDir() {
    const char* base = SDL_GetBasePath();
    fs::path p = base != nullptr ? fs::path(base) : fs::current_path();
    for (int up = 0; up < 3 && !p.empty(); up++) {
        if (fs::exists(p / "tools" / "save_editor" / "stadium_save")) {
            return (p / "tools" / "save_editor").string();
        }
        p = p.parent_path();
    }
    return "";
}

std::string gEditorStatus;

void OpenSaveEditor(const std::string& save) {
    std::string dir = SaveEditorDir();
    if (dir.empty()) {
        gEditorStatus = "tools/save_editor not found";
        return;
    }
    static SDL_Process* sEditor = nullptr;
    if (sEditor == nullptr) {
#ifdef _WIN32
        const char* python = "python";
#else
        const char* python = "python3";
#endif
        const char* args[] = { python, "-m", "stadium_save", "serve", save.c_str(), "--port", "8765", "--write", nullptr };
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
#ifdef SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING // SDL 3.4+
        SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, dir.c_str());
#endif
        // the module is found through PYTHONPATH too (older SDL cannot set the child's working directory)
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PYTHONPATH", dir.c_str(), true);
        sEditor = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (sEditor == nullptr) {
            gEditorStatus = "Needs python3";
            return;
        }
        SDL_Delay(800); // let the server bind its port
    }
    SDL_OpenURL("http://127.0.0.1:8765/");
    gEditorStatus = "Opened in the browser";
}

// ---- look: Launcher.swift's Theme and fonts --------------------------------------------------------------------------

ImU32 Hex(uint32_t rgb, float a = 1.0f) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(a * 255));
}
ImVec4 HexV(uint32_t rgb) {
    return ImGui::ColorConvertU32ToFloat4(Hex(rgb));
}
namespace Theme {
constexpr uint32_t window = 0x12161D, card = 0x1A1F27, cardBorder = 0x2A313C, text = 0xE6E9EE, dim = 0x8B95A3,
                   green = 0x2EA043, red = 0xE5484D, button = 0x2A313C, buttonHover = 0x353D4A;
constexpr uint32_t players[4] = { 0xF0B429, 0xE5484D, 0x3B82F6, 0x8E7FD3 };
} // namespace Theme

// Montserrat (from pokestadium.o2r) in the sizes the design uses; nullptr: ImGui's default font
struct Fonts {
    ImFont* body = nullptr;  // 13
    ImFont* small = nullptr; // 11
    ImFont* game = nullptr;  // 16
    ImFont* title = nullptr; // 20
    ImFont* play = nullptr;  // 32
} gFonts;

void LoadFonts() {
    auto rm = ResourceGetResourceManager();
    if (!rm) {
        return;
    }
    auto file = rm->GetArchiveManager()->LoadFile("fonts/Montserrat-Regular.ttf");
    if (!file || !file->Buffer || file->Buffer->empty()) {
        return;
    }
    static std::vector<char> ttf; // ImGui keeps pointing at the data
    ttf.assign(file->Buffer->begin() + (std::ptrdiff_t)file->BufferOffset, file->Buffer->end());
    ImGuiIO& io = ImGui::GetIO();
    auto add = [&](float size) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 2;
        static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2013, 0x2014, 0x2026, 0x2026, 0x2713, 0x2713, 0 };
        return io.Fonts->AddFontFromMemoryTTF(ttf.data(), (int)ttf.size(), size, &cfg, ranges);
    };
    gFonts.body = add(13.0f);
    gFonts.small = add(11.0f);
    gFonts.game = add(16.0f);
    gFonts.title = add(20.0f);
    gFonts.play = add(32.0f);
}

// cartridge and N64 pictures (tools/launcher/assets, packed into pokestadium.o2r)
ImTextureID Picture(Fast::Fast3dGui* gui, const char* name) {
    if (gui == nullptr) {
        return nullptr;
    }
    std::string key = std::string("launcher_") + name;
    if (!gui->HasTextureByName(key)) {
        try {
            gui->LoadTextureFromRawImage(key, std::string("textures/launcher/") + name + ".png");
        } catch (...) {
            return nullptr;
        }
    }
    return gui->GetTextureByName(key);
}

// ---- controls: libultraship's ControlDeck mappings (gControllers.* in pokestadium.cfg.json) --------------------------

// one N64 input: a button, or a direction of the stick (button == 0)
struct Input {
    const char* name;
    CONTROLLERBUTTONS_T button;
    Ship::Direction dir;
};
const Input kInputs[] = {
    { "A", BTN_A, Ship::UP },           { "B", BTN_B, Ship::UP },
    { "Z", BTN_Z, Ship::UP },           { "Start", BTN_START, Ship::UP },
    { "L", BTN_L, Ship::UP },           { "R", BTN_R, Ship::UP },
    { "D-Pad Up", BTN_DUP, Ship::UP },  { "D-Pad Down", BTN_DDOWN, Ship::UP },
    { "D-Pad Left", BTN_DLEFT, Ship::UP }, { "D-Pad Right", BTN_DRIGHT, Ship::UP },
    { "C Up", BTN_CUP, Ship::UP },      { "C Down", BTN_CDOWN, Ship::UP },
    { "C Left", BTN_CLEFT, Ship::UP },  { "C Right", BTN_CRIGHT, Ship::UP },
    { "Stick Up", 0, Ship::UP },        { "Stick Down", 0, Ship::DOWN },
    { "Stick Left", 0, Ship::LEFT },    { "Stick Right", 0, Ship::RIGHT },
};
constexpr int kInputCount = sizeof(kInputs) / sizeof(kInputs[0]);

struct Binding {
    std::string id, name;
    bool pad; // a controller binding (else keyboard or mouse)
};

std::shared_ptr<Ship::Controller> PortController(int port) {
    auto deck = ControllerGetControlDeck();
    return deck ? deck->GetControllerByPort((uint8_t)port) : nullptr;
}

// libultraship's names use Font Awesome arrows, which the launcher's font does not have
std::string InputName(std::string s) {
    const std::pair<const char*, const char*> words[] = {
        { ICON_FA_ARROW_UP, "Up" }, { ICON_FA_ARROW_DOWN, "Down" }, { ICON_FA_ARROW_LEFT, "Left" },
        { ICON_FA_ARROW_RIGHT, "Right" }, { ICON_FA_BARS, "Start" }, { ICON_FA_PLUS, "+" },
        { ICON_FA_MINUS, "-" }, { "Left Stick", "LS" }, { "Right Stick", "RS" },
    };
    for (auto [from, to] : words) {
        for (size_t at; (at = s.find(from)) != std::string::npos;) {
            s.replace(at, strlen(from), to);
        }
    }
    return s;
}

std::vector<Binding> Bindings(int port, const Input& in) {
    std::vector<Binding> list;
    auto c = PortController(port);
    if (c == nullptr) {
        return list;
    }
    auto add = [&](const std::string& id, const auto& m) {
        list.push_back({ id, InputName(m->GetPhysicalInputName()),
                         m->GetPhysicalDeviceType() == Ship::PhysicalDeviceType::SDLGamepad });
    };
    if (in.button != 0) {
        for (auto& [id, m] : c->GetButton(in.button)->GetAllButtonMappings()) add(id, m);
    } else {
        for (auto& [id, m] : c->GetLeftStick()->GetAllAxisDirectionMappingByDirection(in.dir)) add(id, m);
    }
    // keyboard first, then the controller; stable order (the mappings live in unordered maps)
    std::sort(list.begin(), list.end(),
              [](const Binding& a, const Binding& b) { return a.pad != b.pad ? b.pad : a.id < b.id; });
    return list;
}

void RemoveBinding(int port, const Input& in, const std::string& id) {
    auto c = PortController(port);
    if (c == nullptr) {
        return;
    }
    if (in.button != 0) {
        c->GetButton(in.button)->ClearButtonMapping(id);
    } else {
        c->GetLeftStick()->ClearAxisDirectionMapping(in.dir, id);
    }
    CVarSave();
}

// Binds the key or controller input held right now to `in` (replacing binding `replaceId`, when set) and returns true
// when one is held; with save false it only reports that, for waiting until the inputs are released. Keyboard keys
// come from the window's last key-down scancode, controller buttons and axes from libultraship's factories (they poll
// SDL).
bool CaptureBinding(int port, const Input& in, const std::string& replaceId,
                    const std::shared_ptr<Ship::Window>& window, bool save) {
    auto deck = ControllerGetControlDeck();
    auto c = PortController(port);
    auto cvars = CVarGetConsoleVariable();
    if (deck == nullptr || c == nullptr || cvars == nullptr) {
        return false;
    }
    int32_t scancode = window->GetLastScancode();
    if (scancode == Ship::KbScancode::LUS_KB_ESCAPE) {
        return true; // Esc cancels (the launcher window handles it)
    }
    auto key = (Ship::KbScancode)scancode;
    bool isKey = scancode > 0;
    if (in.button != 0) {
        std::shared_ptr<Ship::ControllerButtonMapping> m =
            isKey ? std::make_shared<Ship::KeyboardKeyToButtonMapping>(port, in.button, key, deck, window, cvars)
                  : Ship::ButtonMappingFactory::CreateButtonMappingFromSDLInput(port, in.button, cvars, deck);
        if (m == nullptr || !save) {
            return m != nullptr;
        }
        auto button = c->GetButton(in.button);
        if (!replaceId.empty() && replaceId != m->GetButtonMappingId()) {
            button->ClearButtonMapping(replaceId);
        }
        button->AddButtonMapping(m);
        m->SaveToConfig();
        button->SaveButtonMappingIdsToConfig();
    } else {
        std::shared_ptr<Ship::ControllerAxisDirectionMapping> m =
            isKey ? std::make_shared<Ship::KeyboardKeyToAxisDirectionMapping>(port, Ship::LEFT_STICK, in.dir, key,
                                                                              deck, window, cvars)
                  : Ship::AxisDirectionMappingFactory::CreateAxisDirectionMappingFromSDLInput(port, Ship::LEFT_STICK,
                                                                                              in.dir, cvars, deck);
        if (m == nullptr || !save) {
            return m != nullptr;
        }
        auto stick = c->GetLeftStick();
        if (!replaceId.empty() && replaceId != m->GetAxisDirectionMappingId()) {
            stick->ClearAxisDirectionMapping(in.dir, replaceId);
        }
        stick->AddAxisDirectionMapping(in.dir, m);
        m->SaveToConfig();
        stick->SaveAxisDirectionMappingIdsToConfig();
    }
    CVarSetInteger(("gControllers.Port" + std::to_string(port + 1) + ".HasConfig").c_str(), 1);
    CVarSave();
    return true;
}

// the defaults (Engine.cpp, the same as pokemonStadiumRecomp's): keyboard and controller for player 1, the controller
// only for the others
void ResetBindings(int port) {
    auto c = PortController(port);
    if (c == nullptr) {
        return;
    }
    c->ClearAllMappings();
    if (port == 0) {
        c->AddDefaultMappings(Ship::PhysicalDeviceType::Keyboard);
    }
    c->AddDefaultMappings(Ship::PhysicalDeviceType::SDLGamepad);
    CVarSave();
}

// ---- the launcher window ---------------------------------------------------------------------------------------------

constexpr float kW = 960, kH = 556; // the design size (Launcher.swift)

class LauncherWindow : public Ship::GuiWindow {
  public:
    LauncherWindow(State* state, bool* play, bool* quit, Fast::Fast3dGui* gui, std::shared_ptr<Ship::Window> window)
        : GuiWindow("gLauncherWindowOpen", "Pokémon Stadium Launcher", ImVec2(kW, kH)), mState(state), mPlay(play),
          mQuit(quit), mGui(gui), mWindow(std::move(window)) {
    }

    void Draw() override {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, HexV(Theme::window));
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
        // the 960x556 design, scaled to the window and centred
        mScale = std::min(vp->Size.x / kW, vp->Size.y / kH);
        mOrigin = ImVec2(vp->Pos.x + (vp->Size.x - kW * mScale) / 2, vp->Pos.y + (vp->Size.y - kH * mScale) / 2);
        ImGui::PushStyleColor(ImGuiCol_Text, HexV(Theme::text));
        ImGui::PushStyleColor(ImGuiCol_Button, HexV(Theme::button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HexV(Theme::buttonHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, HexV(Theme::cardBorder));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, HexV(Theme::button));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, HexV(Theme::buttonHover));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, HexV(Theme::text));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5 * mScale);
        DrawElement();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(7);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

  protected:
    void UpdateElement() override {
    }

    void DrawElement() override {
        State& s = *mState;
        mChanged = ApplyPick();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // header: title | subtitle, line
        Text(16, 10, "Pokémon Stadium", gFonts.title);
        Text(190, 11, "|", gFonts.title, Theme::cardBorder);
        Text(206, 14, "PC port · Transfer Pak", gFonts.body, Theme::dim);
        dl->AddRectFilled(P(0, 44), P(kW, 45), Hex(Theme::cardBorder));

        if (mControls) {
            ControlsPanel(16, 58, kW - 32, 366);
        } else {
            for (int i = 0; i < 4; i++) {
                PlayerCard(i, 16 + i * (222 + 14), 58, 222, 366);
            }
        }
        ImGui::BeginDisabled(mCapture >= 0);
        BottomBar(0, 440, kW, 116);
        ImGui::EndDisabled();
        if (mChanged) {
            Store(s);
        }
    }

  private:
    // design coordinates -> screen
    ImVec2 P(float x, float y) const {
        return ImVec2(mOrigin.x + x * mScale, mOrigin.y + y * mScale);
    }

    void Text(float x, float y, const std::string& t, ImFont* font = nullptr, uint32_t color = Theme::text) {
        ImFont* f = font != nullptr ? font : ImGui::GetFont();
        float size = (font != nullptr ? font->FontSize : ImGui::GetFontSize()) * mScale;
        ImGui::GetWindowDrawList()->AddText(f, size, P(x, y), Hex(color), t.c_str());
    }

    // a button at design coordinates; true when clicked
    bool Button(const char* label, float x, float y, float w, float h, ImFont* font = nullptr) {
        ImGui::SetCursorScreenPos(P(x, y));
        if (font != nullptr) ImGui::PushFont(font);
        ImGui::SetWindowFontScale(mScale);
        bool clicked = ImGui::Button(label, ImVec2(w * mScale, h * mScale));
        ImGui::SetWindowFontScale(1.0f);
        if (font != nullptr) ImGui::PopFont();
        return clicked;
    }

    bool Check(const char* label, bool* v, float x, float y) {
        ImGui::SetCursorScreenPos(P(x, y));
        if (gFonts.body != nullptr) ImGui::PushFont(gFonts.body);
        ImGui::SetWindowFontScale(mScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * mScale, 2 * mScale));
        bool changed = ImGui::Checkbox(label, v);
        ImGui::PopStyleVar();
        ImGui::SetWindowFontScale(1.0f);
        if (gFonts.body != nullptr) ImGui::PopFont();
        mChanged |= changed;
        return changed;
    }

    void Image(const char* name, float x, float y, float w, float h, float alpha = 1.0f) {
        ImTextureID tex = Picture(mGui, name);
        if (tex == nullptr) {
            return;
        }
        // keep the picture's aspect ratio inside the box
        ImVec2 size = mGui->GetTextureSize(std::string("launcher_") + name);
        float sc = std::min(w / std::max(1.0f, size.x), h / std::max(1.0f, size.y));
        float iw = size.x * sc, ih = size.y * sc;
        ImGui::GetWindowDrawList()->AddImage(tex, P(x + (w - iw) / 2, y + (h - ih) / 2), P(x + (w + iw) / 2, y + (h + ih) / 2),
                                             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(alpha * 255)));
    }

    void PlayerCard(int i, float x, float y, float w, float h) {
        State& s = *mState;
        GbSlot& g = s.gb[i];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushID(i);
        uint32_t accent = Theme::players[i];
        dl->AddRectFilled(P(x, y), P(x + w, y + h), Hex(Theme::card), 8 * mScale);
        dl->AddRect(P(x, y), P(x + w, y + h), Hex(Theme::cardBorder), 8 * mScale);
        dl->AddRectFilled(P(x, y), P(x + w, y + 3), Hex(accent), 8 * mScale, ImDrawFlags_RoundCornersTop);

        Text(x + 12, y + 14, "PLAYER " + std::to_string(i + 1), gFonts.body, accent);
        bool wasOn = g.enabled;
        Check("On", &g.enabled, x + w - 56, y + 11);
        if (g.enabled && !wasOn && g.rom.empty()) {
            Pick(&g.rom, false, kGbRomFilter, 2); // switching a slot on without a cartridge asks for one
        }

        CartInfo info = InspectCart(g.rom, GbSave(g));
        const char* picture = g.rom.empty() ? "empty" : info.kind.empty() ? "empty" : info.kind.c_str();
        Image(picture, x + (w - 128) / 2, y + 40, 128, 134, g.enabled || g.rom.empty() ? 1.0f : 0.45f);

        if (g.rom.empty()) {
            if (Button("Load ROM", x + w / 2 - 45, y + 182, 90, 26)) {
                Pick(&g.rom, false, kGbRomFilter, 2);
                g.enabled = true;
            }
        } else {
            if (Button("Change ROM", x + w / 2 - 70, y + 182, 100, 26)) {
                Pick(&g.rom, false, kGbRomFilter, 2);
            }
            if (Button("x", x + w / 2 + 36, y + 182, 34, 26)) {
                g = GbSlot();
                mChanged = true;
            }
        }

        if (!g.rom.empty()) {
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddCircleFilled(P(x + 17, y + 225), 4 * mScale, Hex(g.enabled ? Theme::green : Theme::dim));
            Text(x + 26, y + 214, info.label, gFonts.game);
        } else {
            Text(x + 12, y + 214, "No cartridge", gFonts.game, Theme::dim);
        }
        Text(x + 12, y + 246, "Trainer", gFonts.body, Theme::dim);
        Text(x + 72, y + 246, info.trainer.empty() ? "—" : info.trainer, gFonts.body);
        Text(x + 12, y + 266, "ID", gFonts.body, Theme::dim);
        Text(x + 72, y + 266, info.trainerId.empty() ? "—" : info.trainerId, gFonts.body);

        Text(x + 12, y + 300, "Save", gFonts.body, Theme::dim);
        std::string save = GbSave(g);
        std::string saveName = FileName(save);
        if (saveName.size() > 18) saveName = saveName.substr(0, 17) + "…";
        Text(x + 52, y + 300, saveName, gFonts.small);
        if (Button("Change", x + w - 76, y + 294, 66, 26)) {
            if (g.save.empty()) g.save = save;
            Pick(&g.save, true, kGbSaveFilter, 2, save);
        }

        Text(x + 12, y + h - 26, "Transfer Pak · controller " + std::to_string(i + 1), gFonts.small, Theme::dim);
        if (s.debug && !save.empty() && Button("Edit save", x + w - 90, y + h - 33, 80, 26)) {
            OpenSaveEditor(save);
        }
        ImGui::PopID();
    }

    // a button in the given colour
    bool ColorButton(const char* label, float x, float y, float w, float h, uint32_t color, uint32_t hover) {
        ImGui::PushStyleColor(ImGuiCol_Button, HexV(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HexV(hover));
        bool clicked = Button(label, x, y, w, h);
        ImGui::PopStyleColor(2);
        return clicked;
    }

    // Controls: the bindings of one player's N64 inputs (keyboard grey, controller blue); click a binding to change
    // it, right-click to remove it, + adds one. Shown in place of the player cards.
    void ControlsPanel(float x, float y, float w, float h) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(P(x, y), P(x + w, y + h), Hex(Theme::card), 8 * mScale);
        dl->AddRect(P(x, y), P(x + w, y + h), Hex(Theme::cardBorder), 8 * mScale);
        if (mCapture >= 0) {
            UpdateCapture();
        }
        ImGui::BeginDisabled(mCapture >= 0);

        Text(x + 14, y + 14, "CONTROLS", gFonts.body, Theme::players[mPort]);
        for (int i = 0; i < 4; i++) {
            ImGui::PushID(i);
            bool on = i == mPort;
            if (ColorButton(("Player " + std::to_string(i + 1)).c_str(), x + 110 + i * 84, y + 9, 78, 26,
                            on ? Theme::cardBorder : Theme::card, Theme::buttonHover)) {
                mPort = i;
            }
            if (on) {
                dl->AddRectFilled(P(x + 110 + i * 84, y + 35), P(x + 188 + i * 84, y + 37), Hex(Theme::players[i]));
            }
            ImGui::PopID();
        }
        if (Button("Reset to defaults", x + w - 246, y + 9, 130, 26)) {
            ResetBindings(mPort);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(mPort == 0 ? "Keyboard and controller defaults (the same as pokemonStadiumRecomp's)"
                                         : "Controller defaults (the keyboard plays player 1)");
        }
        if (Button("Done", x + w - 108, y + 9, 94, 26)) {
            mControls = false;
        }
        dl->AddRectFilled(P(x, y + 44), P(x + w, y + 45), Hex(Theme::cardBorder));

        constexpr int kRows = 10;
        for (int i = 0; i < kInputCount; i++) {
            float cx = x + 14 + (i / kRows) * 466, cy = y + 56 + (i % kRows) * 29;
            InputRow(i, cx, cy);
        }
        Text(x + 14, y + h - 22,
             "Click a binding to change it, right-click to remove it, + to add one. Grey: keyboard, blue: controller.",
             gFonts.small, Theme::dim);
        ImGui::EndDisabled();

        if (mCapture >= 0) {
            dl->AddRectFilled(P(x, y + 45), P(x + w, y + h), Hex(Theme::window, 0.75f), 8 * mScale,
                              ImDrawFlags_RoundCornersBottom);
            float bw = 380, bh = 84, bx = x + (w - bw) / 2, by = y + (h - bh) / 2;
            dl->AddRectFilled(P(bx, by), P(bx + bw, by + bh), Hex(Theme::card), 8 * mScale);
            dl->AddRect(P(bx, by), P(bx + bw, by + bh), Hex(Theme::players[mPort]), 8 * mScale);
            Text(bx + 20, by + 18, std::string("Press a key or controller input for ") + kInputs[mCapture].name,
                 gFonts.body);
            Text(bx + 20, by + 48, "Esc cancels", gFonts.small, Theme::dim);
        }
    }

    void InputRow(int i, float x, float y) {
        const Input& in = kInputs[i];
        ImGui::PushID(i);
        Text(x, y + 5, in.name, gFonts.body, Theme::dim);
        auto list = Bindings(mPort, in);
        constexpr size_t kShown = 3;
        float bx = x + 96;
        for (size_t n = 0; n < list.size() && n < kShown; n++) {
            const Binding& b = list[n];
            std::string label = b.name.size() > 12 ? b.name.substr(0, 11) + "…" : b.name;
            label += "##" + b.id;
            if (ColorButton(label.c_str(), bx, y, 92, 24, b.pad ? 0x23406B : Theme::button,
                            b.pad ? 0x2D528A : Theme::buttonHover)) {
                StartCapture(i, b.id);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                RemoveBinding(mPort, in, b.id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s (%s)", b.name.c_str(), b.pad ? "controller" : "keyboard");
            }
            bx += 98;
        }
        if (list.size() > kShown) {
            Text(bx, y + 6, "+" + std::to_string(list.size() - kShown), gFonts.small, Theme::dim);
        }
        if (Button("+", x + 96 + kShown * 98 + 22, y, 26, 24)) {
            StartCapture(i, "");
        }
        ImGui::PopID();
    }

    void StartCapture(int input, const std::string& replaceId) {
        mCapture = input;
        mCaptureId = replaceId;
        mArmed = false; // the click (or key) that started it must be released first
    }

    void UpdateCapture() {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            mCapture = -1;
            return;
        }
        if (!mArmed) {
            mArmed = !CaptureBinding(mPort, kInputs[mCapture], mCaptureId, mWindow, false) &&
                     !ImGui::IsAnyMouseDown();
            return;
        }
        if (CaptureBinding(mPort, kInputs[mCapture], mCaptureId, mWindow, true)) {
            mCapture = -1;
        }
    }

    void BottomBar(float x, float y, float w, float h) {
        State& s = *mState;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(P(x, y), P(x + w, y + h), Hex(Theme::card));
        dl->AddRectFilled(P(x, y), P(x + w, y + 1), Hex(Theme::cardBorder));
        Image("n64", x + 24, y + 20, 124, 80);

        std::string romError = CheckRom(s.rom);
        Text(x + 168, y + 22, romError.empty() ? "Pokémon Stadium (USA)" : (s.rom.empty() ? "No Stadium ROM" : FileName(s.rom)),
             gFonts.game);
        if (romError.empty()) {
            dl->AddCircleFilled(P(x + 406, y + 33), 4 * mScale, Hex(Theme::green));
            Text(x + 414, y + 26, "ROM OK", gFonts.body, Theme::green);
        } else {
            dl->AddCircleFilled(P(x + 406, y + 33), 4 * mScale, Hex(Theme::red));
            Text(x + 414, y + 26, romError, gFonts.body, Theme::red);
        }
        if (Button(s.rom.empty() ? "Load ROM" : "Change", x + 164, y + 56, 76, 26)) {
            Pick(&s.rom, false, kRomFilter, 2);
        }
        std::string saveName = "Save: " + FileName(s.save);
        if (saveName.size() > 30) saveName = saveName.substr(0, 29) + "…";
        Text(x + 250, y + 61, saveName, gFonts.small, Theme::dim);
        if (Button("Change save", x + 450, y + 56, 100, 26)) {
            Pick(&s.save, true, kSaveFilter, 2, s.save);
        }
        if (s.debug) {
            if (Button("Save editor", x + 556, y + 56, 96, 26)) {
                OpenSaveEditor(s.save);
            }
            if (!gEditorStatus.empty()) {
                Text(x + 556, y + 88, gEditorStatus, gFonts.small, Theme::dim);
            }
        }
        Check("Debug", &s.debug, x + 164, y + 88);
        if (Button(mControls ? "Players" : "Controls", x + 450, y + 86, 100, 26)) {
            mControls = !mControls;
        }

        // PLAY: green, rounded, white border
        ImGui::BeginDisabled(!romError.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, HexV(Theme::green));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(Hex(0x37B24D)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(Hex(0x268A39)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * mScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2 * mScale);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.85f));
        if (Button("PLAY", x + w - 206, y + 12, 182, 66, gFonts.play)) {
            *mPlay = true;
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::EndDisabled();
        Check("Auto-play", &s.autoPlay, x + w - 158, y + 86);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Start the game directly next time (hold Shift while starting to see this launcher)");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && romError.empty() && !mControls) {
            *mPlay = true;
        }
        (void)mQuit;
    }

    State* mState;
    bool* mPlay;
    bool* mQuit;
    Fast::Fast3dGui* mGui;
    std::shared_ptr<Ship::Window> mWindow;
    bool mControls = false; // the controls panel instead of the player cards
    int mPort = 0;          // whose controls it shows
    int mCapture = -1;      // the input (kInputs) waiting for a key or controller input, -1: none
    std::string mCaptureId; // the binding it replaces ("": adds one)
    bool mArmed = false;    // the inputs held when the capture started were released
    float mScale = 1.0f;
    ImVec2 mOrigin;
    bool mChanged = false;
};

} // namespace

namespace Port {

std::string RunLauncher(const std::shared_ptr<Fast::Fast3dWindow>& window) {
    State state = Load();
    bool play = false, quit = false;

    // start directly: the stored ROM, unless Shift is held (or it is no longer valid)
    window->HandleEvents();
    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (!(state.autoPlay && !shift && CheckRom(state.rom).empty())) {
        LoadFonts(); // before the first frame: ImGui builds its font atlas then
        if (gFonts.body != nullptr) {
            ImGui::GetIO().FontDefault = gFonts.body;
        }
        // the launcher's own window size while it shows; the game's size comes back for PLAY
        SDL_Window* sdl = MainWindow();
        int gameW = 0, gameH = 0;
        if (sdl != nullptr) {
            SDL_GetWindowSize(sdl, &gameW, &gameH);
            SDL_SetWindowSize(sdl, (int)kW, (int)kH);
            SDL_SetWindowPosition(sdl, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            SDL_SetWindowTitle(sdl, "Pokémon Stadium — Launcher");
        }
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(window->GetGui());
        // the controller deck, for the controls panel (the game's osContInit sets it up again, which is harmless)
        static uint8_t controllerBits;
        static OSContStatus controllerStatus[MAXCONTROLLERS];
        osContInit(nullptr, &controllerBits, controllerStatus);
        auto launcher = std::make_shared<LauncherWindow>(&state, &play, &quit, gui.get(), window);
        launcher->Init();
        launcher->Show();
        window->GetGui()->AddGuiWindow(launcher);
        static Gfx empty[1];
        gSPEndDisplayList(&empty[0]);
        // test hook: PORT_LAUNCHER_PLAY_AFTER=<frames> presses PLAY after that many frames (when the ROM is valid)
        const char* playAfter = getenv("PORT_LAUNCHER_PLAY_AFTER");
        for (int frame = 0; !play && !quit && window->IsRunning(); frame++) {
            window->HandleEvents();
            window->DrawAndRunGraphicsCommands(empty, {});
            if (playAfter != nullptr && frame >= atoi(playAfter) && CheckRom(state.rom).empty()) {
                play = true;
            }
        }
        window->GetGui()->RemoveGuiWindow(launcher->GetName());
        ImGui::GetIO().FontDefault = nullptr;
        Store(state);
        if (!play) {
            return "";
        }
        if (sdl != nullptr && gameW > 0) {
            SDL_SetWindowSize(sdl, gameW, gameH);
            SDL_SetWindowPosition(sdl, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            SDL_SetWindowTitle(sdl, "Pokémon Stadium");
        }
    }

    // the choices go to the game the way the command line passes them
    if (!state.save.empty()) {
        std::error_code ec;
        fs::create_directories(fs::u8path(state.save).parent_path(), ec);
        SetEnv("PORT_FLASH_FILE", state.save);
    }
    static const char* romVars[4] = { "PORT_GB_ROM", "PORT_GB_ROM2", "PORT_GB_ROM3", "PORT_GB_ROM4" };
    static const char* saveVars[4] = { "PORT_GB_SAVE", "PORT_GB_SAVE2", "PORT_GB_SAVE3", "PORT_GB_SAVE4" };
    for (int i = 0; i < 4; i++) {
        const GbSlot& g = state.gb[i];
        if (g.enabled && !g.rom.empty()) {
            SetEnv(romVars[i], g.rom);
            SetEnv(saveVars[i], GbSave(g));
        }
    }
    return state.rom;
}

} // namespace Port
