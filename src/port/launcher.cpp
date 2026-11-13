// Launcher shown inside the game window when the port is started without a ROM argument (macOS, Linux, Windows). Same
// design as the macOS app's launcher (tools/launcher/Launcher.swift): four player cards with the Game Boy cartridge
// of each Transfer Pak (picture, game, trainer from the save, save file) and a bar with the Stadium ROM, its save and
// PLAY. The choices are remembered in pokestadium.cfg.json and passed to the game the way the command line does
// (PORT_FLASH_FILE, PORT_GB_ROM*, PORT_GB_SAVE*). Auto-play skips it next time (hold Shift while starting to see it
// again); Debug adds a button that opens the save editor (save_editor/save_editor_ui.cpp, built in: it takes the
// launcher's place until it is closed; on the handheld layout it is under Files & help). Controls
// swaps the player cards for the key and controller bindings of each player (libultraship's ControlDeck mappings).
// Renderer picks libultraship's window backend (Window.Backend.Id); it applies the next time the game starts.
// Graphics swaps the player cards for the drawing settings (resolution scale, anti-aliasing, VSync, Smooth motion,
// frame skipping when the graphics chip falls behind), with a preset for slow ones such as an Intel HD 520; they apply
// at once and are kept in pokestadium.cfg.json.
// Android (or PORT_LAUNCHER_HANDHELD=1) shows a handheld layout instead: the Stadium cartridge and a big PLAY, the
// Transfer Pak cards and the controls on pages of their own, everything reachable with the gamepad (D-pad or stick
// moves the focus, A presses, B goes back) as well as by touch.
#include "launcher.h"
#include "launcher_files.h"
#include "launcher_reliability.h"
#include "port_platform.h"
#include "Engine.h"
#include "rom_check.h"
#include "save_editor/save_editor_ui.h"

#include <chrono>
#include <libultraship/libultraship.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // BringWindowToDisplayFront
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
#include <ship/config/Config.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/gui/GuiWindow.h>
#include <fast/Fast3dGui.h>
#include <fast/Fast3dWindow.h>

#include <algorithm>
#include <array>
#include <cmath>
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
    bool smooth = false; // frame interpolation (interp.cpp)
    int aspect = 0;      // the game picture: 0 = 4:3, 1 = 16:9 (Engine.cpp, AspectSetting)
    int renderer = 0;    // the requested window backend (Fast::WindowBackend), used from the next start
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

std::array<Port::LauncherSlot, 4> PreflightSlots(const State& s) {
    std::array<Port::LauncherSlot, 4> slots;
    for (size_t i = 0; i < slots.size(); i++) slots[i] = {s.gb[i].enabled, s.gb[i].rom, GbSave(s.gb[i])};
    return slots;
}
std::string CheckText(const Port::LauncherCheck& check) {
    std::string text;
    for (const auto& e : check.errors) text += "Error: " + e + "\n\n";
    for (const auto& w : check.warnings) text += "Notice: " + w + "\n\n";
    return text.empty() ? "All selected files and save locations are ready." : text;
}
bool ReadyToPlay(const State& s) {
    const auto check = Port::CheckLauncherFiles(fs::u8path(Ship::Context::GetAppDirectoryPath()), s.save, PreflightSlots(s));
    if (check.errors.empty() && check.warnings.empty()) return true;
    const auto text = CheckText(check);
    if (!check.errors.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Check launcher settings", text.c_str(), nullptr);
        return false;
    }
    const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Back"},
                                               {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Play anyway"}};
    SDL_MessageBoxData box{}; box.flags = SDL_MESSAGEBOX_WARNING; box.title = "Check cartridge saves";
    box.message = text.c_str(); box.numbuttons = 2; box.buttons = buttons;
    int selected = 0;
    return SDL_ShowMessageBox(&box, &selected) && selected == 1;
}
void OpenFolder(const fs::path& path) {
    std::error_code ec; fs::create_directories(path, ec);
#if !defined(__ANDROID__)
    if (!ec) {
        std::string url = "file://";
        const auto value = Port::PathUtf8(fs::absolute(path));
#ifdef _WIN32
        url += "/";
#endif
        const char hex[] = "0123456789ABCDEF";
        for (unsigned char c : value) {
            if (c == '\\') url += '/';
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') url += c;
            else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
        }
        if (SDL_OpenURL(url.c_str())) return;
    }
#endif
    const auto text = "Folder: " + Port::PathUtf8(path) + "\nOpen this location in your system's file browser. On Android, use your file manager's app-files view.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Launcher files", text.c_str(), nullptr);
}

// ---- the renderer: libultraship's window backends ------------------------------------------------------------------

// short (the launcher's button); config: libultraship's own name, as Fast3dWindow::GetWindowBackendName writes it
const char* RendererName(int id, bool config = false) {
    switch (id) {
        case Fast::FAST3D_DXGI_DX11:
            return config ? "DirectX 11" : "DX11";
        case Fast::FAST3D_SDL_OPENGL:
            return "OpenGL";
        case Fast::FAST3D_SDL_METAL:
            return "Metal";
        case Fast::FAST3D_SDL_VULKAN:
            return "Vulkan";
    }
    return "?";
}

// the backends this build has; Vulkan is listed when built in (whether a driver is installed is only checked when the
// game starts with it: without one the game starts with the default renderer)
std::vector<int> Renderers() {
    std::vector<int> ids;
    if (auto list = Port::GetWindow()->GetAvailableWindowBackends()) {
        ids = *list;
    }
#ifdef ENABLE_VULKAN
    if (std::find(ids.begin(), ids.end(), (int)Fast::FAST3D_SDL_VULKAN) == ids.end()) {
        ids.push_back(Fast::FAST3D_SDL_VULKAN);
    }
#endif
    return ids;
}

int ActiveRenderer() {
    return Port::GetWindow()->GetWindowBackend();
}

State Load() {
    State s;
    const auto directory = fs::u8path(Ship::Context::GetAppDirectoryPath());
    s.rom = Port::ResolveLauncherPath(CVarGetString(Key("Rom").c_str(), ""), directory);
    s.save = Port::ResolveLauncherPath(CVarGetString(Key("Save").c_str(), DefaultSave().c_str()), directory);
    for (int i = 0; i < 4; i++) {
        s.gb[i].enabled = CVarGetInteger(GbKey(i, "Enabled").c_str(), 0) != 0;
        s.gb[i].rom = Port::ResolveLauncherPath(CVarGetString(GbKey(i, "Rom").c_str(), ""), directory);
        s.gb[i].save = Port::ResolveLauncherPath(CVarGetString(GbKey(i, "Save").c_str(), ""), directory);
    }
    s.autoPlay = CVarGetInteger(Key("AutoPlay").c_str(), 0) != 0;
    s.debug = CVarGetInteger(Key("Debug").c_str(), 0) != 0;
    s.smooth = CVarGetInteger("gPort.Interpolate", 0) != 0;
    s.aspect = Port::AspectSetting();
    s.renderer = CVarGetInteger(Key("Renderer").c_str(), ActiveRenderer());
    const std::vector<int> renderers = Renderers();
    if (std::find(renderers.begin(), renderers.end(), s.renderer) == renderers.end()) {
        s.renderer = ActiveRenderer();
    }
    return s;
}

void Store(const State& s) {
    const auto directory = fs::u8path(Ship::Context::GetAppDirectoryPath());
    CVarSetString(Key("Rom").c_str(), Port::StoreLauncherPath(s.rom, directory).c_str());
    CVarSetString(Key("Save").c_str(), Port::StoreLauncherPath(s.save, directory).c_str());
    for (int i = 0; i < 4; i++) {
        CVarSetInteger(GbKey(i, "Enabled").c_str(), s.gb[i].enabled ? 1 : 0);
        CVarSetString(GbKey(i, "Rom").c_str(), Port::StoreLauncherPath(s.gb[i].rom, directory).c_str());
        CVarSetString(GbKey(i, "Save").c_str(), Port::StoreLauncherPath(s.gb[i].save, directory).c_str());
    }
    CVarSetInteger(Key("AutoPlay").c_str(), s.autoPlay ? 1 : 0);
    CVarSetInteger(Key("Debug").c_str(), s.debug ? 1 : 0);
    CVarSetInteger("gPort.Interpolate", s.smooth ? 1 : 0);
    CVarSetInteger("gPort.Aspect", s.aspect);
    Port::ApplyAspect();
    // the window reads Window.Backend.Id when it starts (and writes the backend it got: the request is kept apart)
    CVarSetInteger(Key("Renderer").c_str(), s.renderer);
    auto config = Port::GetWindow()->GetContext()->GetChildren().GetFirst<Ship::Config>();
    if (config != nullptr && config->GetInt("Window.Backend.Id", -1) != s.renderer) {
        config->SetInt("Window.Backend.Id", s.renderer);
        config->SetString("Window.Backend.Name", RendererName(s.renderer, true));
        config->Save();
    }
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
    // the whole file: the original dump's SHA-1 (rom_check.h). Hashed once per file version (this runs every frame).
    static std::string sPath, sResult;
    static uintmax_t sSize = 0;
    static fs::file_time_type sTime;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    const uintmax_t size = fs::file_size(p, ec);
    const fs::file_time_type time = ec ? fs::file_time_type() : fs::last_write_time(p, ec);
    if (path != sPath || size != sSize || time != sTime) {
        sPath = path;
        sSize = size;
        sTime = time;
        sResult = Port::CheckRomDumpFile(path);
    }
    return sResult;
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
    return path.empty() ? std::string("-") : fs::u8path(path).filename().string();
}

// ---- native file dialogs (SDL); they report back through a callback, possibly on another thread ----------------------

std::mutex gPickLock;
std::string* gPickTarget = nullptr;
std::string gPickResult, gPickError;
bool gPickDone = false;
bool gPickStadiumSave = false;
std::string gPickExportSource;

#if defined(__ANDROID__)
// Android's file picker returns content:// URIs, which the game's file functions cannot open: copy the picked file into
// the app's files (a separate imported/pick-N/<name> for each selection) and use that. The name comes from the URI's last part, e.g.
// ".../document/primary%3ADownload%2FPokemon%20Stadium.z64" -> "Pokemon Stadium.z64".
std::string ImportPicked(const std::string& uri, std::string& error) {
    if (uri.rfind("content://", 0) != 0) {
        return uri;
    }
    std::string decoded;
    for (size_t i = 0; i < uri.size(); i++) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            decoded += (char)strtol(uri.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            decoded += uri[i];
        }
    }
    std::string name = decoded.substr(decoded.find_last_of("/:") + 1);
    if (name.empty() || name == "." || name == "..") name = "picked";
    std::error_code ec;
    const auto root = fs::u8path(Ship::Context::GetPathRelativeToAppDirectory("imported"));
    fs::create_directories(root, ec);
    if (ec) { error = "Cannot create the app's imported-files folder."; return ""; }
    fs::path dir;
    for (int n = 1; n <= 10000; n++) {
        const auto candidate = root / ("pick-" + std::to_string(n));
        if (fs::create_directory(candidate, ec)) { dir = candidate; break; }
        if (ec) break;
    }
    if (dir.empty()) { error = "Cannot create a separate copy of the selected file."; return ""; }
    const auto destination = dir / fs::u8path(name);
    const std::string dst = Port::PathUtf8(destination);
    SDL_IOStream* in = SDL_IOFromFile(uri.c_str(), "rb");
    SDL_IOStream* out = in != nullptr ? SDL_IOFromFile(dst.c_str(), "wb") : nullptr;
    bool copied = in != nullptr && out != nullptr;
    if (copied) {
        char chunk[65536];
        size_t n;
        while ((n = SDL_ReadIO(in, chunk, sizeof chunk)) > 0) {
            if (SDL_WriteIO(out, chunk, n) != n) { copied = false; break; }
        }
        copied = copied && SDL_GetIOStatus(in) == SDL_IO_STATUS_EOF;
    }
    if (out != nullptr && !SDL_CloseIO(out)) copied = false;
    if (in != nullptr) SDL_CloseIO(in);
    if (!copied) {
        fs::remove(destination, ec);
        fs::remove(dir, ec);
        error = "Could not copy the complete selected file into the app's files.";
        return "";
    }
    return dst;
}
#endif

void PickCallback(void*, const char* const* files, int) {
    std::string picked = files != nullptr && files[0] != nullptr ? files[0] : "";
    std::string error;
#if defined(__ANDROID__)
    picked = ImportPicked(picked, error);
#endif
    std::lock_guard<std::mutex> g(gPickLock);
    gPickResult = picked;
    gPickError = error;
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
          const std::string& suggested = "", bool stadiumSave = false) {
    {
        std::lock_guard<std::mutex> g(gPickLock);
        if (gPickTarget != nullptr || !gPickExportSource.empty()) {
            return; // one dialog at a time
        }
        gPickTarget = target;
        gPickDone = false;
        gPickError.clear();
        gPickStadiumSave = stadiumSave;
    }
    std::string start = !target->empty() ? *target : suggested;
#if defined(__ANDROID__)
    save = false; // saves stay in the app's files: "Change" imports an existing one (ImportPicked)
#endif
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
    if (!gPickExportSource.empty() && gPickDone) {
        if (!gPickError.empty())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not export document", gPickError.c_str(), MainWindow());
        else if (!gPickResult.empty())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Export complete", "The document was saved to the selected location. The local copy and active save were kept.", MainWindow());
        gPickExportSource.clear();
        return false;
    }
    if (gPickTarget == nullptr || !gPickDone) {
        return false;
    }
    if (!gPickError.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not import selected file", gPickError.c_str(), MainWindow());
    } else if (!gPickResult.empty()) {
        if (gPickStadiumSave) {
            std::string imported, error;
            if (Port::ImportStadiumSave(fs::u8path(gPickResult), fs::u8path(Ship::Context::GetAppDirectoryPath()),
                                       imported, error)) {
                *gPickTarget = imported;
            } else {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not import Stadium save", error.c_str(), MainWindow());
            }
        } else {
            *gPickTarget = gPickResult;
        }
    }
    gPickTarget = nullptr;
    return true;
}

const SDL_DialogFileFilter kRomFilter[] = { { "N64 ROM (.z64)", "z64" }, { "All files", "*" } };
const SDL_DialogFileFilter kSaveFilter[] = { { "N64 cartridge save", "flash;fla;sra;sav;bin" }, { "All files", "*" } };
const SDL_DialogFileFilter kGbRomFilter[] = { { "Game Boy ROM", "gb;gbc" }, { "All files", "*" } };
const SDL_DialogFileFilter kGbSaveFilter[] = { { "Game Boy save (.sav)", "sav" }, { "All files", "*" } };

// Android's app-files directory is restricted in system file browsers. Export through the
// Storage Access Framework rather than requiring broad storage permissions or a share provider.
#if defined(__ANDROID__)
void ExportPicked(void*, const char* const* files, int) {
    std::string source;
    { std::lock_guard<std::mutex> guard(gPickLock); source = gPickExportSource; }
    std::string result, error;
    if (files == nullptr) {
        error = "Could not open the system Save as picker. The local export was kept.";
    } else if (files[0] != nullptr) {
        std::ifstream input(fs::u8path(source), std::ios::binary | std::ios::ate);
        const auto length = input ? input.tellg() : std::streampos(-1);
        std::vector<uint8_t> data;
        bool readable = length > 0 && length <= 1024 * 1024;
        if (readable) {
            data.resize(static_cast<size_t>(length)); input.seekg(0);
            readable = bool(input.read(reinterpret_cast<char*>(data.data()), data.size()));
        }
        SDL_IOStream* output = readable ? SDL_IOFromFile(files[0], "wb") : nullptr;
        bool ok = output != nullptr && SDL_WriteIO(output, data.data(), data.size()) == data.size();
        if (output != nullptr && !SDL_CloseIO(output)) ok = false;
        if (ok) result = files[0];
        else error = "The selected document could not be completely written; it may be incomplete. The local export and active save were kept. Try another location.";
    }
    std::lock_guard<std::mutex> guard(gPickLock);
    gPickError = error; gPickResult = result; gPickDone = true;
}
#endif
void OfferExport(const std::string& path) {
#if defined(__ANDROID__)
    bool busy;
    {
        std::lock_guard<std::mutex> guard(gPickLock);
        busy = gPickTarget != nullptr || !gPickExportSource.empty();
        if (!busy) { gPickExportSource = path; gPickResult.clear(); gPickError.clear(); gPickDone = false; }
    }
    if (busy) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Export saved locally", "Close the existing file picker before exporting another document. The local export was kept.", MainWindow());
        return;
    }
    static const SDL_DialogFileFilter saveFilter[] = {{"N64 save (.fla)", "fla"}};
    static const SDL_DialogFileFilter reportFilter[] = {{"Diagnostic report (.txt)", "txt"}};
    const bool report = fs::u8path(path).extension() == ".txt";
    SDL_ShowSaveFileDialog(ExportPicked, nullptr, MainWindow(), report ? reportFilter : saveFilter, 1, path.c_str());
#else
    (void)path;
#endif
}

// ---- save editor (debug option) --------------------------------------------------------------------------------------

std::string gEditorStatus;
Port::SaveEditor gSaveEditor; // the editor, drawn in place of the launcher's panels while it is open

// Opens the save editor on the Stadium save, with the Game Boy saves of the Transfer Paks that are on; gbSlot: the
// player card whose cartridge to show first (-1: the overview).
void OpenSaveEditor(const State& s, int gbSlot = -1) {
    std::error_code ec;
    if (s.save.empty() || !fs::exists(fs::u8path(s.save), ec)) { // a new install: the game writes the save when it starts
        gEditorStatus = "No save yet: start the game once";
        return;
    }
    std::vector<Port::SaveEditorGb> gbs;
    for (int i = 0; i < 4; i++) {
        const GbSlot& g = s.gb[i];
        const bool shown = (g.enabled && !g.rom.empty()) || i == gbSlot;
        gbs.push_back({ "Player " + std::to_string(i + 1), shown ? GbSave(g) : "", g.rom });
    }
    std::string error;
    if (gSaveEditor.Open(s.save, gbs, gbSlot, &error)) {
        gEditorStatus.clear();
    } else {
        gEditorStatus = "Cannot open the save: " + error;
        SDL_Log("save editor: %s", gEditorStatus.c_str());
    }
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

// ImGui's built-in font in the sizes the design uses; no external font files needed
struct Fonts {
    ImFont* body = nullptr;  // 13
    ImFont* small = nullptr; // 11
    ImFont* game = nullptr;  // 16
    ImFont* title = nullptr; // 20
    ImFont* play = nullptr;  // 32
} gFonts;
// ImGui 1.91 rasterizes its atlas once. Keep a small bank of native pixel
// sizes so a resized window can switch fonts without rebuilding GPU textures.
struct FontSet {
    float rasterScale;
    Fonts fonts;
};
std::vector<FontSet> gFontSets;

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    auto add = [&](float size, float rasterScale) {
        ImFontConfig cfg;
        cfg.SizePixels = size * rasterScale;
        ImFont* font = io.Fonts->AddFontDefault(&cfg);
        font->Scale = 1.0f / rasterScale;
        return font;
    };
    for (float scale : {0.625f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f}) {
        gFontSets.push_back({scale, {add(13, scale), add(11, scale), add(16, scale),
                                    add(20, scale), add(32, scale)}});
    }
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
constexpr float kHandheldH = 540;   // the handheld layout's design height (16:9, the Thor's and most phones' shape)

bool Handheld() {
#if defined(__ANDROID__)
    return true;
#else
    static const bool on = getenv("PORT_LAUNCHER_HANDHELD") != nullptr;
    return on;
#endif
}

// The gamepads' navigation inputs (all connected pads; the left stick counts as the D-pad), plus the keyboard's.
struct NavInput {
    bool up = false, down = false, left = false, right = false, accept = false, back = false;
};

NavInput ReadNavInput() {
    NavInput in;
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
        for (int i = 0; i < n; i++) {
            SDL_Gamepad* g = SDL_GetGamepadFromID(ids[i]);
            if (g == nullptr) {
                g = SDL_OpenGamepad(ids[i]); // stays open (libultraship's deck opens them too)
            }
            if (g == nullptr) {
                continue;
            }
            const int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
            in.up |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -20000;
            in.down |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 20000;
            in.left |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -20000;
            in.right |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 20000;
            in.accept |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START);
            in.back |= SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_EAST);
        }
        SDL_free(ids);
    }
    in.up |= ImGui::IsKeyDown(ImGuiKey_UpArrow);
    in.down |= ImGui::IsKeyDown(ImGuiKey_DownArrow);
    in.left |= ImGui::IsKeyDown(ImGuiKey_LeftArrow);
    in.right |= ImGui::IsKeyDown(ImGuiKey_RightArrow);
    in.accept |= ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_Space);
    in.back |= ImGui::IsKeyDown(ImGuiKey_Escape) || ImGui::IsKeyDown(ImGuiKey_AppBack); // (Android's back button)
    return in;
}

class LauncherWindow : public Ship::GuiWindow {
  public:
    LauncherWindow(State* state, bool* play, bool* quit, bool* restart, Fast::Fast3dGui* gui, std::shared_ptr<Ship::Window> window)
        : GuiWindow("gLauncherWindowOpen", "Pokémon Stadium Launcher", ImVec2(kW, kH)), mState(state), mPlay(play),
          mQuit(quit), mRestart(restart), mGui(gui), mWindow(std::move(window)) {
        // requested before, but the game started with another one: that renderer could not start here
        mRendererFailed = state->renderer != ActiveRenderer() ? state->renderer : 0;
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
        // above libultraship's game view window ("Main Game"), which would otherwise cover it (and take the mouse)
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        // the 960x556 design, scaled to the window and centred
        const float designH = Handheld() ? kHandheldH : kH;
        mScale = std::min(vp->Size.x / kW, vp->Size.y / designH);
        mOrigin = ImVec2(vp->Pos.x + (vp->Size.x - kW * mScale) / 2, vp->Pos.y + (vp->Size.y - designH * mScale) / 2);
        mPixelDensity = 1.0f;
        if (SDL_Window* sdl = MainWindow()) {
            mPixelDensity = std::max(1.0f, SDL_GetWindowPixelDensity(sdl));
        }
        const float target = std::max(0.01f, mScale * mPixelDensity);
        if (!gFontSets.empty()) {
            const auto nearest = std::min_element(gFontSets.begin(), gFontSets.end(), [target](const FontSet& a, const FontSet& b) {
                return std::abs(std::log(a.rasterScale / target)) < std::abs(std::log(b.rasterScale / target));
            });
            gFonts = nearest->fonts;
        }
        if (gFonts.body) ImGui::PushFont(gFonts.body);
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
        if (gFonts.body) ImGui::PopFont();
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
        if (Handheld()) {
            DrawHandheld();
            if (mChanged) {
                Store(s);
            }
            return;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // header: title | subtitle, line
        Text(16, 10, "Pokémon Stadium", gFonts.title);
        Text(190, 11, "|", gFonts.title, Theme::cardBorder);
        Text(206, 14, "PC port · Transfer Pak", gFonts.body, Theme::dim);
        dl->AddRectFilled(P(0, 44), P(kW, 45), Hex(Theme::cardBorder));
        Tools();
        if (getenv("PORT_LAUNCHER_OPEN_GRAPHICS") != nullptr && !mGraphicsTestOpened) { // test hook (as OPEN_TOOLS)
            mGraphicsTestOpened = mGraphics = true;
        }
        if (getenv("PORT_LAUNCHER_OPEN_SOUND") != nullptr && !mSoundTestOpened) { // test hook (as OPEN_TOOLS)
            mSoundTestOpened = mSound = true;
        }

        if (gSaveEditor.IsOpen()) {
            EditorPanel(16, 58, kW - 32, kH - 58 - 16);
            if (mChanged) {
                Store(s);
            }
            return;
        }
        if (mTools) {
            ToolsPanel(16, 58, kW - 32, 366);
        } else if (mControls) {
            ControlsPanel(16, 58, kW - 32, 366);
        } else if (mGraphics) {
            GraphicsPanel(16, 58, kW - 32, 366);
        } else if (mSound) {
            SoundPanel(16, 58, kW - 32, 366);
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
    std::string mToolsStatus;
    bool mToolsTestOpened = false, mTools = false;
    void Tools() {
        const bool testOpen = getenv("PORT_LAUNCHER_OPEN_TOOLS") != nullptr && !mToolsTestOpened;
        ImGui::BeginDisabled(mCapture >= 0 || (gSaveEditor.IsOpen() && !Handheld()));
        const bool opened = Button("Files & help", 800, 8, 144, 30);
        ImGui::EndDisabled();
        if ((opened && mCapture < 0) || testOpen) {
            mToolsTestOpened = true;
            mTools = !mTools;
            if (Handheld()) GoToPage(mTools ? kPageTools : kPageHome);
        }
    }
    // the save editor in the rectangle (design coordinates), with the launcher's scale, fonts and gamepad focus
    void EditorPanel(float x, float y, float w, float h) {
        Port::SaveEditorStyle style;
        style.scale = mScale;
        style.body = gFonts.body;
        style.small = gFonts.small;
        style.game = gFonts.game;
        style.title = gFonts.title;
        style.handheld = Handheld();
        style.navItem = [this] { return NavItem(); };
        style.setDefaultFocus = [this] { mDefaultFocus = (int)mNavItems.size(); };
        style.resetFocus = [this] { mFocusReset = true; };
        gSaveEditor.Draw(style, P(x, y), P(x + w, y + h));
    }
    bool ToolAction(const char* label) {
        const bool clicked = ImGui::Button(label, ImVec2(std::min(280.0f * mScale, ImGui::GetContentRegionAvail().x),
                                                       (Handheld() ? 44.0f : 26.0f) * mScale));
        return NavItem() || clicked;
    }
    void ToolsPanel(float x, float y, float w, float h) {
        const auto directory = fs::u8path(Ship::Context::GetAppDirectoryPath());
        ImGui::SetCursorScreenPos(P(x, y));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, HexV(Theme::card));
        ImGui::BeginChild("launcher-files", ImVec2(w * mScale, h * mScale), true);
        ImGui::SetWindowFontScale(mScale);
            ImGui::TextWrapped("Settings folder: %s", Port::PathUtf8(directory).c_str());
            ImGui::TextWrapped("Active Stadium save: %s", mState->save.c_str());
            if (ToolAction("Check setup")) {
                auto check = Port::CheckLauncherFiles(directory, mState->save, PreflightSlots(*mState));
                const auto romError = CheckRom(mState->rom);
                if (!romError.empty()) check.errors.insert(check.errors.begin(), "Stadium ROM: " + romError);
                mToolsStatus = CheckText(check);
            }
            if (Handheld() && ToolAction("Save editor")) { // (the desktop layout has it under Debug)
                OpenSaveEditor(*mState);
                if (gSaveEditor.IsOpen()) {
                    mTools = false;
                    GoToPage(kPageSaveEditor);
                    mFocusReset = true; // (this frame still draws the old page: its default would win)
                } else {
                    mToolsStatus = gEditorStatus;
                }
            }
            if (ToolAction("Open settings folder")) OpenFolder(directory);
            if (ToolAction("Open saves folder")) OpenFolder(fs::u8path(mState->save).parent_path());
            if (ToolAction("Export N64 save")) {
                std::string path, error;
                if (Port::ExportStadiumSave(fs::u8path(mState->save), directory, path, error)) {
                    mToolsStatus = "Exported: " + path + "\nThe active save was kept unchanged.";
                    OfferExport(path);
                } else mToolsStatus = error;
            }
            if (ToolAction("Export diagnostics")) {
                std::string path, error;
                if (Port::ExportLauncherDiagnostics(directory, PORT_BUILD_VERSION,
                    RendererName(mState->renderer), RendererName(ActiveRenderer()), path, error)) {
                    mToolsStatus = "Exported: " + path + "\nReview the report before sharing it.";
                    OfferExport(path);
                } else mToolsStatus = error;
            }
            if (ToolAction("Disable Auto-play")) { mState->autoPlay = false; mChanged = true; mToolsStatus = "Auto-play disabled."; }
            if (ToolAction("Use safe renderer next start")) {
#ifdef __APPLE__
                mState->renderer = Fast::FAST3D_SDL_METAL;
#else
                mState->renderer = Fast::FAST3D_SDL_OPENGL;
#endif
                mState->autoPlay = false; mChanged = true;
                mToolsStatus = "Auto-play disabled and safe renderer selected. Restart to apply.";
            }
            ImGui::TextWrapped("Recovery: start with --launcher to bypass Auto-play, or --safe-mode to reset the renderer. Saves and controller mappings are kept.");
            ImGui::TextWrapped("Five previous session saves are kept in <save>.backups. Exports are under exports/; reports under diagnostics/.");
            if (!mToolsStatus.empty()) ImGui::TextWrapped("%s", mToolsStatus.c_str());
            if (ToolAction("Back to players")) { mTools = false; if (Handheld()) GoToPage(kPageHome); }

        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    int mRendererFailed = 0; // the requested renderer, when the game started with another (it could not start)
    // design coordinates -> screen
    ImVec2 P(float x, float y) const {
        return ImVec2(std::round((mOrigin.x + x * mScale) * mPixelDensity) / mPixelDensity,
                      std::round((mOrigin.y + y * mScale) * mPixelDensity) / mPixelDensity);
    }

    void Text(float x, float y, const std::string& t, ImFont* font = nullptr, uint32_t color = Theme::text) {
        ImFont* f = font != nullptr ? font : ImGui::GetFont();
        float size = (font != nullptr ? font->FontSize * font->Scale : ImGui::GetFontSize()) * mScale;
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
        return NavItem() || clicked;
    }

    bool Check(const char* label, bool* v, float x, float y) {
        ImGui::SetCursorScreenPos(P(x, y));
        if (gFonts.body != nullptr) ImGui::PushFont(gFonts.body);
        ImGui::SetWindowFontScale(mScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * mScale, 2 * mScale));
        bool changed = ImGui::Checkbox(label, v);
        if (NavItem()) {
            *v = !*v;
            changed = true;
        }
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
        Text(x + 72, y + 246, info.trainer.empty() ? "-" : info.trainer, gFonts.body);
        Text(x + 12, y + 266, "ID", gFonts.body, Theme::dim);
        Text(x + 72, y + 266, info.trainerId.empty() ? "-" : info.trainerId, gFonts.body);

        Text(x + 12, y + 300, "Save", gFonts.body, Theme::dim);
        std::string save = GbSave(g);
        std::string saveName = FileName(save);
        if (saveName.size() > 18) saveName = saveName.substr(0, 15) + "...";
        Text(x + 52, y + 300, saveName, gFonts.small);
        if (Button("Change", x + w - 76, y + 294, 66, 26)) {
            if (g.save.empty()) g.save = save;
            Pick(&g.save, false, kGbSaveFilter, 2, save);
        }

        Text(x + 12, y + h - 26, "Transfer Pak · controller " + std::to_string(i + 1), gFonts.small, Theme::dim);
        if (s.debug && !save.empty() && Button("Edit save", x + w - 90, y + h - 33, 80, 26)) {
            OpenSaveEditor(s, i);
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
        // whether the game sees this controller (Port_ControllerBits): player 1's always, 2-4 only when switched on,
        // so the multiplayer choices (2-4 players, team battles) are offered only with players to make them
        if (mPort == 0) {
            Text(x + 462, y + 15, "Always plugged in", gFonts.small, Theme::dim);
        } else {
            std::string key = "gPort.Controller" + std::to_string(mPort + 1) + ".PluggedIn";
            bool plugged = CVarGetInteger(key.c_str(), 0) != 0;
            if (Check("Plugged in", &plugged, x + 458, y + 11)) {
                CVarSetInteger(key.c_str(), plugged ? 1 : 0);
                CVarSave();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The game sees controller %d (it offers 2-4 player battles and team battles)", mPort + 1);
            }
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
            std::string label = b.name.size() > 12 ? b.name.substr(0, 9) + "..." : b.name;
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

    // ---- graphics settings ----
    // (the port's internal resolution, Port::ResolutionSetting in Engine.h (gPort.Resolution.*); libultraship's
    // gMSAAValue and gVsyncEnabled; the config's Window.Fullscreen.Enabled, applied on Play; gPort.Interpolate and
    // gPort.FrameSkip, read by os_devices.cpp). Layout after the usual PC-game and emulator settings pages: one
    // Internal resolution list (Dolphin, DuckStation), Scaling next to it, two presets that light up when they match.
    struct ResEntry {
        int mode, value;
    };
    static constexpr ResEntry kResEntries[] = {
        { Port::kResWindow, 0 }, { Port::kResN64, 1 },     { Port::kResN64, 2 },     { Port::kResN64, 3 },
        { Port::kResN64, 4 },    { Port::kResN64, 5 },     { Port::kResN64, 6 },     { Port::kResHeight, 480 },
        { Port::kResHeight, 720 }, { Port::kResHeight, 1080 }, { Port::kResHeight, 1440 }, { Port::kResHeight, 2160 },
    };
    static constexpr int kMsaa[] = { 1, 2, 4, 8 };

    static int ResIndex(const Port::ResolutionSetting& r) {
        for (int i = 0; i < (int)std::size(kResEntries); i++) {
            const ResEntry& e = kResEntries[i];
            if (e.mode == r.mode && (e.mode == Port::kResWindow || e.value == (e.mode == Port::kResN64 ? r.multiple : r.height))) {
                return i;
            }
        }
        return -1;
    }
    static void SetResolution(const ResEntry& e) {
        Port::ResolutionSetting r = Port::GetResolutionSetting();
        // the N64's own sizes look their best with Integer scaling: picking one from another kind switches Scaling to
        // it (it can be changed back); leaving them goes back to Smooth
        if (e.mode == Port::kResN64 && r.mode != Port::kResN64) r.upscale = Port::kUpscaleExact;
        else if (e.mode != Port::kResN64 && r.mode == Port::kResN64 && r.upscale == Port::kUpscaleExact) {
            r.upscale = Port::kUpscaleSmooth;
        }
        r.mode = e.mode;
        if (e.mode == Port::kResN64) r.multiple = e.value;
        if (e.mode == Port::kResHeight) r.height = e.value;
        Port::SetResolutionSetting(r);
    }
    static void SetUpscale(int upscale) {
        Port::ResolutionSetting r = Port::GetResolutionSetting();
        r.upscale = upscale;
        Port::SetResolutionSetting(r);
    }
    static void SetMsaa(int level) {
        CVarSetInteger("gMSAAValue", level);
        Port::GetWindow()->SetMsaaLevel(level);
    }
    static std::shared_ptr<Ship::Config> WindowConfig() {
        return Port::GetWindow()->GetContext()->GetChildren().GetFirst<Ship::Config>();
    }

    // The Internal resolution list: a combo the width of four buttons, entries named with their pixel size for the
    // current shape. Left/Right on the closed list step to the previous/next entry (keyboard and gamepad, like the
    // button rows); A/Enter opens it on the current entry, B/Escape closes it unchanged.
    void ResolutionCombo(float x, float y, float w) {
        const Port::ResolutionSetting r = Port::GetResolutionSetting();
        const int cur = ResIndex(r);
        ImGui::SetCursorScreenPos(P(x, y));
        ImGui::SetWindowFontScale(mScale);
        const float fh = ImGui::GetFontSize();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * mScale, std::max(0.0f, (26 * mScale - fh) / 2)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetStyle().FrameRounding);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, HexV(Theme::button));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, HexV(Theme::buttonHover));
        ImGui::PushStyleColor(ImGuiCol_Button, HexV(Theme::button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HexV(Theme::buttonHover));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, HexV(Theme::card));
        ImGui::PushStyleColor(ImGuiCol_Header, HexV(0x23406B));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, HexV(Theme::buttonHover));
        ImGui::SetNextItemWidth(w * mScale);
        const std::string preview = cur >= 0 ? Port::ResolutionLabel(kResEntries[cur].mode, kResEntries[cur].value) : "Custom";
        static int sOpenForTest = getenv("PORT_LAUNCHER_OPEN_RESOLUTION") != nullptr ? 2 : 0; // test hook: the list open
        if (sOpenForTest > 0 && --sOpenForTest == 0) {
            ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID("##internalres")));
        }
        const bool open = ImGui::BeginCombo("##internalres", preview.c_str(), ImGuiComboFlags_HeightLargest);
        const bool focused = ImGui::IsItemFocused();
        int pick = -1;
        if (open) {
            // (the launcher brings its own window to the front every frame, Draw: the list goes above it)
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            ImGui::SetWindowFontScale(mScale);
            for (int i = 0; i < (int)std::size(kResEntries); i++) {
                const ResEntry& e = kResEntries[i];
                if (i == 1 || i == 7) ImGui::Separator();
                const std::string label = Port::ResolutionLabel(e.mode, e.value);
                const size_t sp = e.mode == Port::kResWindow ? std::string::npos : label.find(' ');
                const std::string name = label.substr(0, sp), size = sp == std::string::npos ? "" : label.substr(sp + 1);
                ImGui::PushID(i);
                if (ImGui::Selectable(name.c_str(), i == cur)) pick = i;
                if (i == cur && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
                if (!size.empty()) {
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(size.c_str()).x);
                    ImGui::TextColored(HexV(Theme::dim), "%s", size.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        } else if (focused && cur >= 0) {
            const bool left = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
            const bool right = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_RightArrow);
            if (left && cur > 0) pick = cur - 1;
            if (right && cur + 1 < (int)std::size(kResEntries)) pick = cur + 1;
        }
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(2);
        ImGui::SetWindowFontScale(1.0f);
        if (pick >= 0 && pick != cur) SetResolution(kResEntries[pick]);
    }

    // the pixel height of the display the window is on (a Retina display's pixels, not its points); 0 if unknown
    static int DisplayPixelHeight() {
        SDL_Window* sdl = MainWindow();
        if (sdl == nullptr) return 0;
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(sdl));
        if (mode == nullptr || mode->h <= 0) return 0;
        return (int)std::lround(mode->h * std::max(1.0f, mode->pixel_density));
    }

    // a row of buttons, the chosen one highlighted; returns the index clicked, or -1
    int Choice(const char* id, const std::vector<std::string>& labels, int chosen, float x, float y, float bw = 64) {
        int clicked = -1;
        ImGui::PushID(id);
        for (int i = 0; i < (int)labels.size(); i++) {
            ImGui::PushID(i);
            const bool on = i == chosen;
            if (ColorButton(labels[i].c_str(), x + i * (bw + 4), y, bw, 26, on ? 0x23406B : Theme::button,
                            on ? 0x23406B : Theme::buttonHover)) {
                clicked = i;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        return clicked;
    }

    void GraphicsPanel(float x, float y, float w, float h) {
        State& s = *mState;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(P(x, y), P(x + w, y + h), Hex(Theme::card), 8 * mScale);
        dl->AddRect(P(x, y), P(x + w, y + h), Hex(Theme::cardBorder), 8 * mScale);
        Text(x + 14, y + 14, "GRAPHICS", gFonts.body, Theme::text);
        Text(x + 110, y + 15, "Changes apply at once and are kept for the next start.", gFonts.small, Theme::dim);
        if (Button("Done", x + w - 108, y + 9, 94, 26)) {
            mGraphics = false;
        }
        dl->AddRectFilled(P(x, y + 44), P(x + w, y + 45), Hex(Theme::cardBorder));

        const float lx = x + 14, cx = x + 190, tx = x + 560, pitch = 37;
        float ry = y + 58;
        auto label = [&](const char* name, const std::string& hint) {
            Text(lx, ry + 5, name, gFonts.body);
            Text(tx, ry + 1, hint, gFonts.small, Theme::dim);
        };
        auto onOff = [&](const char* id, bool on) { return Choice(id, { "On", "Off" }, on ? 0 : 1, cx, ry); };

        // Display: windowed or (borderless) fullscreen; the launcher keeps its own window, the game gets this on Play
        {
            auto config = WindowConfig();
            const bool full = config != nullptr && config->GetBool("Window.Fullscreen.Enabled", false);
            label("Display", "Fullscreen is borderless;\nF11 switches in-game.");
            int c = Choice("display", { "Windowed", "Fullscreen" }, full ? 1 : 0, cx, ry, 96);
            if (c >= 0 && config != nullptr && (c == 1) != full) {
                config->SetBool("Window.Fullscreen.Enabled", c == 1);
                config->Save();
            }
        }
        ry += pitch;
        const Port::ResolutionSetting r = Port::GetResolutionSetting();
        // Internal resolution: how many pixels the game is drawn with (Port::ResolutionSetting)
        {
            int rw = 0, rh = 0;
            Port::RenderSize(&rw, &rh);
            char hint[128];
            // a fixed size taller than the display is drawn and then shrunk to the display (or the window): it costs
            // the work of the larger picture (2160p is four times the pixels of 1080p; on an M5 MacBook Air's GPU a
            // menu picture took 4.6 ms at 2160p, 1.6 ms at 1080p) for little sharpness
            const int lines = r.mode == Port::kResN64 ? 240 * r.multiple : r.mode == Port::kResHeight ? r.height : 0;
            const int displayLines = DisplayPixelHeight();
            if (lines > 0 && displayLines > 0 && lines > displayLines) {
                snprintf(hint, sizeof hint, "Taller than this display (%d lines):\nslower, hardly sharper. Try %dp.",
                         displayLines, displayLines >= 1440 ? 1440 : displayLines >= 1080 ? 1080 : 720);
            } else if (rw > 0) {
                snprintf(hint, sizeof hint, "Draws %d x %d.\nLower it if the game is slow.", rw, rh);
            } else {
                snprintf(hint, sizeof hint, "Lower it if the game is slow.");
            }
            label("Internal resolution", hint);
            ResolutionCombo(cx, ry, 4 * 64 + 3 * 4);
        }
        ry += pitch;
        // Scaling: how a fixed resolution's picture is stretched to the window (not used by Match window)
        {
            const bool used = r.mode != Port::kResWindow;
            label("Scaling", used ? "How the picture is stretched to the window.\nInteger: largest whole multiple, black borders."
                                  : "Not used: the picture already\nmatches the window.");
            ImGui::BeginDisabled(!used);
            int c = Choice("scaling", { "Integer", "Sharp", "Smooth" }, used ? r.upscale : Port::kUpscaleSmooth, cx, ry, 84);
            ImGui::EndDisabled();
            if (c >= 0 && used) SetUpscale(c);
        }
        ry += pitch;
        // anti-aliasing (MSAA)
        const int msaa = CVarGetInteger("gMSAAValue", 1);
        {
            int chosen = 0;
            for (int i = 0; i < (int)std::size(kMsaa); i++) {
                if (msaa == kMsaa[i]) chosen = i;
            }
            label("Anti-aliasing", "Smooths jagged edges.\nCosts speed on weak chips.");
            int c = Choice("msaa", { "Off", "2x", "4x", "8x" }, chosen, cx, ry);
            if (c >= 0) SetMsaa(kMsaa[c]);
        }
        ry += pitch;
        const bool vsync = CVarGetInteger("gVsyncEnabled", 1) != 0;
        {
            label("VSync", "Stops tearing.");
            int c = onOff("vsync", vsync);
            if (c >= 0) CVarSetInteger("gVsyncEnabled", c == 0 ? 1 : 0);
        }
        ry += pitch;
        // Smooth motion (frame interpolation)
        {
            label("Smooth motion", "In-between frames for the 3D scenes the game\ndraws at 30 or 20 per second.");
            int c = onOff("smooth", s.smooth);
            if (c >= 0) {
                s.smooth = c == 0;
                mChanged = true;
            }
        }
        ry += pitch;
        // frame skipping when drawing falls behind (os_devices.cpp, WantSkipWhileBehind)
        const bool skip = CVarGetInteger("gPort.FrameSkip", 1) != 0;
        {
            label("Keep full speed", "Skips frames rather than\nslowing the game down.");
            int c = onOff("skip", skip);
            if (c >= 0) CVarSetInteger("gPort.FrameSkip", c == 0 ? 1 : 0);
        }

        // presets: lit when the settings match one (Display is not part of them)
        const bool recommended = r.mode == Port::kResWindow && msaa == 4 && vsync && s.smooth && skip;
        const bool lowPower = r.mode == Port::kResN64 && r.multiple == 2 && r.upscale == Port::kUpscaleSmooth &&
                              msaa == 1 && vsync && !s.smooth && skip;
        auto apply = [&](const ResEntry& e, int msaaLevel, bool smooth) {
            SetResolution(e);
            SetUpscale(Port::kUpscaleSmooth);
            SetMsaa(msaaLevel);
            CVarSetInteger("gVsyncEnabled", 1);
            CVarSetInteger("gPort.FrameSkip", 1);
            s.smooth = smooth;
            mChanged = true;
        };
        const float by = y + h - 40;
        dl->AddRectFilled(P(x, by - 8), P(x + w, by - 7), Hex(Theme::cardBorder));
        Text(lx, by + 6, "Preset:", gFonts.body);
        const uint32_t lit = 0x23406B;
        if (ColorButton("Recommended", lx + 66, by, 120, 28, recommended ? lit : Theme::button,
                        recommended ? lit : Theme::buttonHover)) {
            apply({ Port::kResWindow, 0 }, 4, true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Match window, 4x anti-aliasing, VSync, Smooth motion and Keep full speed on.");
        }
        if (ColorButton("Low-power", lx + 192, by, 110, 28, lowPower ? lit : Theme::button,
                        lowPower ? lit : Theme::buttonHover)) {
            apply({ Port::kResN64, 2 }, 1, false);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("For integrated graphics such as Intel HD 520: 2x (640 x 480), Smooth scaling,\n"
                              "anti-aliasing and Smooth motion off, VSync and Keep full speed on.");
        }
        Text(x + w - 160, by + 7, std::string("Current: ") + (recommended ? "Recommended" : lowPower ? "Low-power" : "Custom"),
             gFonts.small, Theme::dim);
        CVarSave();
    }

    // launcher > Sound: CVars gSound.*, handed to the audio paths at once (Port::ApplySoundSettings, sound_settings.h)
    void SoundPanel(float x, float y, float w, float h) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(P(x, y), P(x + w, y + h), Hex(Theme::card), 8 * mScale);
        dl->AddRect(P(x, y), P(x + w, y + h), Hex(Theme::cardBorder), 8 * mScale);
        Text(x + 14, y + 14, "SOUND", gFonts.body, Theme::text);
        Text(x + 90, y + 15, "Changes apply at once and are kept for the next start.", gFonts.small, Theme::dim);
        if (Button("Done", x + w - 108, y + 9, 94, 26)) {
            mSound = false;
        }
        dl->AddRectFilled(P(x, y + 44), P(x + w, y + 45), Hex(Theme::cardBorder));
        bool changed = false;

        // volumes (left column)
        const float lx = x + 14, sx = x + 150, sw = 250;
        float ry = y + 60;
        auto volume = [&](const char* name, const char* cvar, const char* hint) {
            Text(lx, ry + 4, name, gFonts.body);
            int v = std::clamp(CVarGetInteger(cvar, 100), 0, 100);
            ImGui::SetCursorScreenPos(P(sx, ry));
            ImGui::SetNextItemWidth(sw * mScale);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, HexV(0x4C7BD9));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, HexV(0x6E95E6));
            const std::string id = std::string("##") + cvar;
            if (ImGui::SliderInt(id.c_str(), &v, 0, 100, v == 0 ? "off" : "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                CVarSetInteger(cvar, v);
                changed = true;
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered() && hint != nullptr) ImGui::SetTooltip("%s", hint);
            ry += 44;
        };
        volume("Master", "gSound.Master", "All sound, the Game Boy included");
        volume("Music", "gSound.Music", "The game's songs");
        volume("Sound effects", "gSound.Effects", "Menus, moves and the other effects");
        volume("Voices", "gSound.Voices", "Pokémon cries and the announcer");
        volume("Game Boy", "gSound.GameBoy", "The Game Boy games in the GB Tower");

        // switches (right column)
        const float cx = x + 470, ox = x + 640;
        float oy = y + 60;
        auto option = [&](const char* name, const char* hint, const char* id, const std::vector<std::string>& labels,
                          int chosen, float bw) {
            Text(cx, oy + 5, name, gFonts.body);
            const int c = Choice(id, labels, chosen, ox, oy, bw);
            Text(cx, oy + 30, hint, gFonts.small, Theme::dim);
            oy += 74;
            return c;
        };
        {
            const int c = option("Sound", "Off silences everything (the game runs as before).", "snd-on", { "On", "Off" },
                                 CVarGetInteger("gSound.On", 1) != 0 ? 0 : 1, 64);
            if (c >= 0) {
                CVarSetInteger("gSound.On", c == 0 ? 1 : 0);
                changed = true;
            }
        }
        {
            const int c = option("In the background", "Silent while another window is in front.", "snd-bg",
                                 { "Play", "Mute" }, CVarGetInteger("gSound.MuteInBackground", 0) != 0 ? 1 : 0, 64);
            if (c >= 0) {
                CVarSetInteger("gSound.MuteInBackground", c);
                changed = true;
            }
        }
        {
            const int c = option("Sound delay", "Long helps when the sound crackles on a slow PC.\nApplies when the game starts.",
                                 "snd-delay", { "Short", "Normal", "Long" }, std::array<int, 3>{ 1, 0, 2 }[std::clamp(
                                                                               CVarGetInteger("gSound.Delay", 0), 0, 2)],
                                 72);
            if (c >= 0) {
                CVarSetInteger("gSound.Delay", std::array<int, 3>{ 1, 0, 2 }[c]); // buttons: short, normal, long
                changed = true;
            }
        }

        // defaults
        const float by = y + h - 40;
        dl->AddRectFilled(P(x, by - 8), P(x + w, by - 7), Hex(Theme::cardBorder));
        if (Button("Defaults", lx, by, 90, 28)) {
            for (const char* cvar : { "gSound.Master", "gSound.Music", "gSound.Effects", "gSound.Voices", "gSound.GameBoy" }) {
                CVarSetInteger(cvar, 100);
            }
            CVarSetInteger("gSound.On", 1);
            CVarSetInteger("gSound.MuteInBackground", 0);
            CVarSetInteger("gSound.Delay", 0);
            changed = true;
        }
        Text(lx + 104, by + 7, "Stereo or mono: the game's own Options menu.", gFonts.small, Theme::dim);
        if (changed) {
            Port::ApplySoundSettings();
            CVarSave();
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
        if (saveName.size() > 30) saveName = saveName.substr(0, 27) + "...";
        Text(x + 250, y + 61, saveName, gFonts.small, Theme::dim);
        if (Button("Import save", x + 450, y + 56, 100, 26)) {
            Pick(&s.save, false, kSaveFilter, 2, s.save, true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import a raw N64 Stadium cartridge save. A working copy is saved beside the launcher; the original is kept.");
        if (s.debug) {
            static bool sTestClicked = false; // test hook: PORT_LAUNCHER_TEST_SAVE_EDITOR=1 clicks it once
            if (Button("Save editor", x + 556, y + 56, 96, 26) ||
                (!sTestClicked && getenv("PORT_LAUNCHER_TEST_SAVE_EDITOR") != nullptr && (sTestClicked = true))) {
                OpenSaveEditor(s);
                if (getenv("PORT_LAUNCHER_TEST_SAVE_EDITOR") != nullptr) {
                    fprintf(stderr, "[launcher] save editor: %s\n", gSaveEditor.IsOpen() ? "open" : gEditorStatus.c_str());
                }
            }
            if (!gEditorStatus.empty()) {
                Text(x + 658, y + 61, gEditorStatus, gFonts.small, Theme::dim);
            }
        }
        Check("Debug", &s.debug, x + 164, y + 88);
        if (Button(mSound ? "Players" : "Sound", x + 234, y + 86, 96, 26)) {
            mSound = !mSound;
            mGraphics = mControls = mTools = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Volume of music, sound effects, voices and the Game Boy; mute in the background");
        }
        if (Button(mGraphics ? "Players" : "Graphics", x + 340, y + 86, 100, 26)) {
            mGraphics = !mGraphics;
            mControls = mTools = mSound = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Resolution, anti-aliasing, VSync, Smooth motion; a preset for slow graphics chips");
        }
        if (Button(mControls ? "Players" : "Controls", x + 450, y + 86, 100, 26)) {
            mControls = !mControls;
            mGraphics = mTools = mSound = false;
        }
        // the game picture's shape (the rest of the window gets black bars)
        Text(x + 566, y + 91, "Screen", gFonts.small, Theme::dim);
        for (int a = 0; a < 2; a++) {
            const bool on = s.aspect == a;
            if (ColorButton(a == 0 ? "4:3" : "16:9", x + 616 + a * 56, y + 86, 52, 26, on ? 0x23406B : Theme::button,
                            on ? 0x23406B : Theme::buttonHover)) {
                s.aspect = a;
                mChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(a == 0 ? "The N64's picture shape" : "Widescreen: 3D scenes show more at the sides");
            }
        }

        // the renderer (from the next start): cycles through the ones this build has
        {
            const int active = ActiveRenderer();
            const bool pending = s.renderer != active;
            const std::string label = std::string("Renderer: ") + RendererName(s.renderer);
            if (ColorButton(label.c_str(), x + 616, y + 16, 128, 24, pending ? 0x23406B : Theme::button,
                            pending ? 0x23406B : Theme::buttonHover)) {
                const std::vector<int> ids = Renderers();
                auto it = std::find(ids.begin(), ids.end(), s.renderer);
                s.renderer = it == ids.end() || it + 1 == ids.end() ? ids.front() : *(it + 1);
                mChanged = true;
            }
            if (ImGui::IsItemHovered()) {
                std::string tip = "The graphics API the game draws with. A change applies the next time the game starts.";
                if (mRendererFailed != 0 && s.renderer == mRendererFailed) {
                    tip = std::string(RendererName(mRendererFailed)) + " could not start on this computer (no driver for "
                          "it): the game uses " + RendererName(active) + ".";
                } else if (pending) {
                    tip += std::string("\nNow: ") + RendererName(active) + ".";
                }
                ImGui::SetTooltip("%s", tip.c_str());
            }
            if (pending) {
                const std::string status = std::string("Active: ") + RendererName(active);
                Text(x + 616, y + 43, status.c_str(), gFonts.small, Theme::dim);
                if (mRendererFailed == s.renderer) {
                    Text(x + 616, y + 63, "Selected unavailable", gFonts.small, Theme::dim);
                }
#ifdef _WIN32
                if (mRendererFailed != s.renderer &&
                    Button("Restart and apply", x + 616, y + 60, 128, 22, gFonts.small)) {
                    *mRestart = true;
                }
#endif
            }
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
            *mPlay = ReadyToPlay(s);
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::EndDisabled();
        Check("Auto-play", &s.autoPlay, x + w - 158, y + 86);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Start the game directly next time (hold Shift while starting to see this launcher)");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && romError.empty() && !mControls && !mTools && !mGraphics && !mSound) {
            *mPlay = ReadyToPlay(s);
        }
        (void)mQuit;
    }

    // ---- handheld layout ----

    // The last ImGui item as a gamepad focus target: true when it has the focus and A was pressed this frame.
    bool NavItem() {
        if (!Handheld()) {
            return false;
        }
        const int index = (int)mNavItems.size();
        mNavItems.push_back({ ImGui::GetItemRectMin(), ImGui::GetItemRectMax() });
        if (index != mFocus || mCapture >= 0) {
            return false;
        }
        if ((ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_ChildWindow) && !ImGui::IsItemVisible())
            ImGui::SetScrollHereY(0.5f);
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float pad = 3 * mScale;
        ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad),
                                            Hex(Theme::players[0]), 8 * mScale, 0, 3 * mScale);
        return mNavAccept;
    }

    // Moves the focus to the nearest item in the pressed direction (last frame's item rectangles); presses repeat
    // while held. A presses the focused item (NavItem), B leaves the page.
    void NavBeginFrame() {
        NavInput in = ReadNavInput();
        auto edge = [](bool now, bool& was) {
            bool pressed = now && !was;
            was = now;
            return pressed;
        };
        mNavAccept = edge(in.accept, mWasAccept) && mCapture < 0;
        mNavBack = edge(in.back, mWasBack) && mCapture < 0;
        int dx = in.left ? -1 : in.right ? 1 : 0, dy = in.up ? -1 : in.down ? 1 : 0;
        auto now = std::chrono::steady_clock::now();
        bool move = false;
        if (dx != 0 || dy != 0) {
            if (dx != mHeldDx || dy != mHeldDy) {
                move = true;
                mRepeatAt = now + std::chrono::milliseconds(400);
            } else if (now >= mRepeatAt) {
                move = true;
                mRepeatAt = now + std::chrono::milliseconds(130);
            }
        }
        mHeldDx = dx;
        mHeldDy = dy;
        if (mFocusReset) { // no focus this frame; NavEndFrame picks the default the new view named
            mFocusReset = false;
            mFocus = -1;
            mNavPrev.clear();
            move = false;
        }
        if (mNavPrev.empty()) {
            mFocus = -1; // (the page's items are not known before its first frame)
        } else if (mFocus < 0 || mFocus >= (int)mNavPrev.size()) {
            mFocus = mDefaultFocus;
        }
        if (move && mCapture < 0 && mFocus >= 0 && mFocus < (int)mNavPrev.size()) {
            auto center = [](const NavRect& r) { return ImVec2((r.min.x + r.max.x) / 2, (r.min.y + r.max.y) / 2); };
            ImVec2 c = center(mNavPrev[mFocus]);
            float best = 1e30f;
            int bestIndex = -1;
            for (int i = 0; i < (int)mNavPrev.size(); i++) {
                ImVec2 o = center(mNavPrev[i]);
                float along = dx != 0 ? (o.x - c.x) * dx : (o.y - c.y) * dy;
                float across = dx != 0 ? std::fabs(o.y - c.y) : std::fabs(o.x - c.x);
                if (i == mFocus || along <= 1.0f) {
                    continue;
                }
                float score = along + 2.0f * across; // straight ahead first
                if (score < best) {
                    best = score;
                    bestIndex = i;
                }
            }
            if (bestIndex >= 0) {
                mFocus = bestIndex;
            }
        }
        mNavItems.clear();
    }

    void NavEndFrame() {
        mNavPrev = mNavItems;
        if (mFocus < 0 && !mNavPrev.empty()) {
            mFocus = mDefaultFocus;
        }
    }

    void GoToPage(int page) {
        mPage = page;
        mControls = page == kPageControls;
        mFocus = -1; // the page's default item
        mNavItems.clear();
    }

    void DrawHandheld() {
        NavBeginFrame();
        Tools();
        if (mNavBack && mPage != kPageHome) {
            mTools = false;
            GoToPage(kPageHome);
            mNavBack = false;
        }
        if (mPage == kPageControls && !mControls) { // the panel's Done button
            GoToPage(kPageHome);
        }
        if (mPage == kPageSaveEditor) {
            if (mNavBack) { // B: out of a picker or the Pokémon dialog first, then close (asking about unsaved edits)
                gSaveEditor.Back();
                mNavBack = false;
            }
            if (!gSaveEditor.IsOpen()) {
                GoToPage(kPageHome);
            }
        } else if (gSaveEditor.IsOpen()) { // opened from a Transfer Pak card
            GoToPage(kPageSaveEditor);
            mFocusReset = true;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Text(24, 14, "Pokémon Stadium", gFonts.title);
        const char* pageName = mPage == kPageTransferPak ? "Transfer Pak" : mPage == kPageControls ? "Controls"
                               : mPage == kPageTools ? "Files & help" : mPage == kPageSaveEditor ? "Save editor" : "";
        Text(220, 17, pageName, gFonts.body, Theme::dim);
        dl->AddRectFilled(P(0, 50), P(kW, 51), Hex(Theme::cardBorder));

        if (mPage == kPageSaveEditor) {
            mDefaultFocus = 0; // (the editor names its own: the current section)
            EditorPanel(16, 62, kW - 32, 438);
        } else if (mPage == kPageTools) {
            ToolsPanel(16, 62, kW - 32, 430);
            mDefaultFocus = 0;
        } else if (mPage == kPageTransferPak) {
            for (int i = 0; i < 4; i++) {
                PlayerCard(i, 16 + i * (222 + 14), 62, 222, 366);
            }
            mDefaultFocus = 0;
            if (Button("Back", 16, 444, 160, 48, gFonts.game)) {
                GoToPage(kPageHome);
            }
        } else if (mPage == kPageControls) {
            ControlsPanel(16, 62, kW - 32, 366);
            mDefaultFocus = 0;
            ImGui::BeginDisabled(mCapture >= 0);
            if (Button("Back", 16, 444, 160, 48, gFonts.game)) {
                GoToPage(kPageHome);
            }
            ImGui::EndDisabled();
        } else {
            HandheldHome();
        }
        Text(24, 512, mPage == kPageHome ? "A  select     D-pad  move     touch works too"
                                         : "A  select     D-pad  move     B  back",
             gFonts.small, Theme::dim);
        NavEndFrame();
    }

    void HandheldHome() {
        State& s = *mState;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // the Stadium cartridge: picture, ROM and save
        const float cx = 24, cy = 66, cw = 440, ch = 426;
        dl->AddRectFilled(P(cx, cy), P(cx + cw, cy + ch), Hex(Theme::card), 10 * mScale);
        dl->AddRect(P(cx, cy), P(cx + cw, cy + ch), Hex(Theme::cardBorder), 10 * mScale);
        Image("pokestadium", cx + (cw - 190) / 2, cy + 20, 190, 190);
        std::string romError = CheckRom(s.rom);
        Text(cx + 24, cy + 228, romError.empty() ? "Pokémon Stadium (USA)" : (s.rom.empty() ? "No Stadium ROM" : FileName(s.rom)),
             gFonts.title);
        dl->AddCircleFilled(P(cx + 30, cy + 272), 5 * mScale, Hex(romError.empty() ? Theme::green : Theme::red));
        Text(cx + 42, cy + 263, romError.empty() ? "ROM OK" : romError, gFonts.game, romError.empty() ? Theme::green : Theme::red);
        std::string saveName = "Save: " + FileName(s.save);
        if (saveName.size() > 40) saveName = saveName.substr(0, 37) + "...";
        Text(cx + 24, cy + 296, saveName, gFonts.body, Theme::dim);
        if (Button(s.rom.empty() ? "Load ROM" : "Change ROM", cx + 24, cy + 340, 190, 56, gFonts.game)) {
            Pick(&s.rom, false, kRomFilter, 2);
        }
        if (Button("Import save", cx + cw - 214, cy + 340, 190, 56, gFonts.game)) {
            Pick(&s.save, false, kSaveFilter, 2, s.save, true);
        }

        // PLAY (the default focus: A starts the game right away)
        const float rx = 488, rw = kW - 24 - rx;
        ImGui::BeginDisabled(!romError.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, HexV(Theme::green));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(Hex(0x37B24D)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(Hex(0x268A39)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10 * mScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2 * mScale);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.85f));
        mDefaultFocus = (int)mNavItems.size();
        if (Button("PLAY", rx, cy, rw, 150, gFonts.play) && romError.empty()) {
            *mPlay = ReadyToPlay(s);
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        ImGui::EndDisabled();
        if (!romError.empty()) {
            mDefaultFocus = -1; // (set below: the ROM button)
        }

        // options, two per row
        const float bw = (rw - 14) / 2, bh = 60;
        float y = cy + 170;
        int on = 0;
        for (const GbSlot& g : s.gb) on += g.enabled && !g.rom.empty();
        std::string pak = "Transfer Pak · " + (on == 0 ? std::string("off") : std::to_string(on) + " on");
        if (Button(pak.c_str(), rx, y, bw, bh, gFonts.game)) {
            GoToPage(kPageTransferPak);
        }
        if (Button("Controls", rx + bw + 14, y, bw, bh, gFonts.game)) {
            GoToPage(kPageControls);
        }
        y += bh + 14;
        if (Button(s.aspect == 1 ? "Screen · 16:9" : "Screen · 4:3", rx, y, bw, bh, gFonts.game)) {
            s.aspect = 1 - s.aspect;
            mChanged = true;
        }
        if (Button(s.smooth ? "Smooth motion · on" : "Smooth motion · off", rx + bw + 14, y, bw, bh, gFonts.game)) {
            s.smooth = !s.smooth;
            mChanged = true;
        }
        y += bh + 14;
        Text(rx + 4, y + 8, s.aspect == 1 ? "16:9: 3D scenes show more at the sides." : "4:3: the N64's picture shape.",
             gFonts.small, Theme::dim);
        Text(rx + 4, y + 28, "Smooth motion draws in-between frames (one frame of extra delay).", gFonts.small, Theme::dim);
        if (mDefaultFocus < 0) {
            mDefaultFocus = 0; // no valid ROM: the first item, Load ROM
        }
    }

    struct NavRect {
        ImVec2 min, max;
    };
    static constexpr int kPageHome = 0, kPageTransferPak = 1, kPageControls = 2, kPageTools = 3, kPageSaveEditor = 4;
    int mPage = kPageHome;
    std::vector<NavRect> mNavItems, mNavPrev; // this frame's and last frame's focus targets, in drawing order
    int mFocus = -1, mDefaultFocus = 0;
    bool mFocusReset = false; // the save editor changed its view: the new view's default focus
    bool mNavAccept = false, mNavBack = false, mWasAccept = true, mWasBack = true; // (held at start: not a press)
    int mHeldDx = 0, mHeldDy = 0;
    std::chrono::steady_clock::time_point mRepeatAt;

    State* mState;
    bool* mPlay;
    bool* mQuit;
    bool* mRestart;
    Fast::Fast3dGui* mGui;
    std::shared_ptr<Ship::Window> mWindow;
    bool mControls = false; // the controls panel instead of the player cards
    bool mGraphics = false; // the graphics panel instead of the player cards
    bool mSound = false;    // the sound panel instead of the player cards
    bool mSoundTestOpened = false;
    bool mGraphicsTestOpened = false;
    int mPort = 0;          // whose controls it shows
    int mCapture = -1;      // the input (kInputs) waiting for a key or controller input, -1: none
    std::string mCaptureId; // the binding it replaces ("": adds one)
    bool mArmed = false;    // the inputs held when the capture started were released
    float mScale = 1.0f, mPixelDensity = 1.0f;
    ImVec2 mOrigin;
    bool mChanged = false;
};

// Test hook: PORT_LAUNCHER_SCRIPT=<file> drives the launcher with ImGui input, one command per line:
//   <frame> click <x> <y>   left click at design coordinates (the 960x556 layout; 960x540 handheld)
//   <frame> move <x> <y>    <frame> wheel <dy>
//   <frame> text <string>   typed characters      <frame> key <name>   Enter, Escape, Tab, Backspace, Up, Down,
//                                                                       Left, Right, Ctrl+S, Ctrl+A
//   <frame> shot <file.ppm> the picture (saved twice: Metal reads the screen back a frame late)
//   <frame> quit
// It goes through ImGui's input queue like the mouse and keyboard, so it checks the real controls (also under Wine).
struct ScriptStep {
    int frame;
    std::string cmd, arg;
};

std::vector<ScriptStep> LoadScript(const char* path) {
    std::vector<ScriptStep> steps;
    std::ifstream f(fs::u8path(path));
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        ScriptStep st{};
        size_t a = line.find(' ');
        if (a == std::string::npos) continue;
        st.frame = atoi(line.substr(0, a).c_str());
        size_t b = line.find(' ', a + 1);
        st.cmd = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        st.arg = b == std::string::npos ? "" : line.substr(b + 1);
        steps.push_back(st);
    }
    return steps;
}

// returns true for quit
bool RunScriptStep(const ScriptStep& st) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float designH = Handheld() ? kHandheldH : kH;
    const float scale = std::min(vp->Size.x / kW, vp->Size.y / designH);
    auto pos = [&](const std::string& arg) {
        float x = 0, y = 0;
        sscanf(arg.c_str(), "%f %f", &x, &y);
        return ImVec2(vp->Pos.x + (vp->Size.x - kW * scale) / 2 + x * scale, vp->Pos.y + (vp->Size.y - designH * scale) / 2 + y * scale);
    };
    if (st.cmd == "click" || st.cmd == "move") {
        ImVec2 p = pos(st.arg);
        io.AddMousePosEvent(p.x, p.y);
        if (st.cmd == "click") {
            io.AddMouseButtonEvent(0, true);
            io.AddMouseButtonEvent(0, false);
        }
    } else if (st.cmd == "wheel") {
        io.AddMouseWheelEvent(0, (float)atof(st.arg.c_str()));
    } else if (st.cmd == "text") {
        io.AddInputCharactersUTF8(st.arg.c_str());
    } else if (st.cmd == "key") {
        static const std::pair<const char*, ImGuiKey> keys[] = {
            { "Enter", ImGuiKey_Enter }, { "Escape", ImGuiKey_Escape }, { "Tab", ImGuiKey_Tab },
            { "Backspace", ImGuiKey_Backspace }, { "Up", ImGuiKey_UpArrow }, { "Down", ImGuiKey_DownArrow },
            { "Left", ImGuiKey_LeftArrow }, { "Right", ImGuiKey_RightArrow }, { "S", ImGuiKey_S }, { "A", ImGuiKey_A },
        };
        std::string name = st.arg;
        const bool ctrl = name.rfind("Ctrl+", 0) == 0;
        if (ctrl) name = name.substr(5);
        for (auto [n, k] : keys) {
            if (name == n) {
                if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(k, true);
                io.AddKeyEvent(k, false);
                if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
        }
    } else if (st.cmd == "quit") {
        return true;
    }
    return false;
}

} // namespace

namespace Port {

std::string RunLauncher(const std::shared_ptr<Fast::Fast3dWindow>& window, bool& restart) {
    State state = Load();
    bool play = false, quit = false;

    // start directly: the stored ROM, unless Shift is held (or it is no longer valid)
    window->HandleEvents();
    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
#if defined(_WIN32)
    // DirectX uses a native Win32 window, so SDL does not receive its keyboard events.
    shift = shift || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#endif
    // (the handheld layout has no Auto-play: without a Shift key there would be no way back to the launcher)
    if (!(state.autoPlay && !shift && getenv("PORT_FORCE_LAUNCHER") == nullptr && !Handheld() && CheckRom(state.rom).empty() &&
          [&] { const auto c = CheckLauncherFiles(fs::u8path(Ship::Context::GetAppDirectoryPath()), state.save, PreflightSlots(state));
                return c.errors.empty() && c.warnings.empty(); }())) {
        // Give the launcher its own readable window and restore the game size on Play.
        SDL_Window* sdl = MainWindow();
        int gameW = 0, gameH = 0;
        const float launcherHeight = Handheld() ? kHandheldH : kH;
        if (sdl != nullptr && !window->IsFullscreen()) {
            SDL_GetWindowSize(sdl, &gameW, &gameH);
            float scale = SDL_GetWindowDisplayScale(sdl) / std::max(1.0f, SDL_GetWindowPixelDensity(sdl));
            SDL_Rect usable{};
            if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(sdl), &usable)) {
                scale = std::min({scale, (usable.w - 32) / kW, (usable.h - 80) / launcherHeight});
            }
            if (scale > 0) {
                SDL_SetWindowSize(sdl, int(std::round(kW * scale)), int(std::round(launcherHeight * scale)));
                SDL_SetWindowPosition(sdl, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
            SDL_SetWindowTitle(sdl, "Pokémon Stadium — Launcher");
        }
#ifdef _WIN32
        HWND nativeWindow = nullptr;
        RECT gameRect{};
        if (sdl == nullptr && !window->IsFullscreen()) {
            nativeWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MONITORINFO monitor{sizeof(monitor)};
            if (nativeWindow && GetWindowRect(nativeWindow, &gameRect) &&
                GetMonitorInfoW(MonitorFromWindow(nativeWindow, MONITOR_DEFAULTTONEAREST), &monitor)) {
                using GetWindowDpi = UINT (WINAPI*)(HWND);
                const auto getWindowDpi = reinterpret_cast<GetWindowDpi>(
                    GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
                const UINT dpi = getWindowDpi ? getWindowDpi(nativeWindow) : 96;
                RECT client{};
                GetClientRect(nativeWindow, &client);
                const int frameWidth = (gameRect.right - gameRect.left) - client.right;
                const int frameHeight = (gameRect.bottom - gameRect.top) - client.bottom;
                const RECT& work = monitor.rcWork;
                const float scale = std::min({dpi / 96.0f,
                    (work.right - work.left - frameWidth) / kW,
                    (work.bottom - work.top - frameHeight) / launcherHeight});
                if (scale > 0) {
                    const int width = int(std::round(kW * scale)) + frameWidth;
                    const int height = int(std::round(launcherHeight * scale)) + frameHeight;
                    SetWindowPos(nativeWindow, nullptr, work.left + (work.right - work.left - width) / 2,
                                 work.top + (work.bottom - work.top - height) / 2, width, height,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
            } else {
                nativeWindow = nullptr;
            }
        }
#endif
        LoadFonts(); // add the launcher fonts before the first frame builds the atlas
        auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(window->GetGui());
        // the controller deck, for the controls panel (the game's osContInit sets it up again, which is harmless)
        static uint8_t controllerBits;
        static OSContStatus controllerStatus[MAXCONTROLLERS];
        osContInit(nullptr, &controllerBits, controllerStatus);
        auto launcher = std::make_shared<LauncherWindow>(&state, &play, &quit, &restart, gui.get(), window);
        launcher->Init();
        launcher->Show();
        window->GetGui()->AddGuiWindow(launcher);
        static Gfx empty[1];
        gSPEndDisplayList(&empty[0]);
        // test hook: PORT_LAUNCHER_PLAY_AFTER=<frames> presses PLAY after that many frames (when the ROM is valid)
        const char* playAfter = getenv("PORT_LAUNCHER_PLAY_AFTER");
        // PORT_TRACE_PACING=1: launcher frames more than 40 ms apart
        static const bool tracePacing = getenv("PORT_TRACE_PACING") != nullptr;
        auto lastFrame = std::chrono::steady_clock::now();
        static const char* scriptPath = getenv("PORT_LAUNCHER_SCRIPT");
        std::vector<ScriptStep> script = scriptPath != nullptr ? LoadScript(scriptPath) : std::vector<ScriptStep>{};
        for (int frame = 0; !play && !quit && !restart && window->IsRunning(); frame++) {
            window->HandleEvents();
            Port::DebugMinimize((unsigned long long)frame); // test hook: PORT_DEBUG_MINIMIZE
            for (const ScriptStep& st : script) {
                if (st.frame == frame) quit |= RunScriptStep(st);
                if (st.cmd == "shot" && (st.frame == frame - 1 || st.frame == frame - 2)) Port::SaveScreen(st.arg.c_str());
            }
            window->DrawAndRunGraphicsCommands(empty, {});
            auto now = std::chrono::steady_clock::now();
            if (tracePacing && frame > 0 && now - lastFrame > std::chrono::milliseconds(40)) {
                fprintf(stderr, "[launcher] frame %d came %.0f ms after the last\n", frame,
                        std::chrono::duration<double, std::milli>(now - lastFrame).count());
            }
            lastFrame = now;
            if (playAfter != nullptr && frame >= atoi(playAfter) && CheckRom(state.rom).empty() && ReadyToPlay(state)) {
                play = true;
            }
            // test hook: PORT_LAUNCHER_SHOT=<file.ppm> saves the launcher's picture at frame 60 and closes it
            static const char* shot = getenv("PORT_LAUNCHER_SHOT");
            if (shot != nullptr && frame >= 58) { // (Metal reads the screen back a frame late)
                Port::SaveScreen(shot);
                quit = frame >= 60;
            }
        }
        window->GetGui()->RemoveGuiWindow(launcher->GetName());
        Store(state);
        gSaveEditor.Close(); // (unsaved edits are dropped with the window, as the browser page did)
        if (!play || restart) {
            return "";
        }
#ifdef _WIN32
        if (nativeWindow && !window->IsFullscreen()) {
            SetWindowPos(nativeWindow, nullptr, gameRect.left, gameRect.top,
                         gameRect.right - gameRect.left, gameRect.bottom - gameRect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
#endif
        if (sdl != nullptr && gameW > 0) {
            // a window of the chosen shape (other shapes still work, with black bars)
            if (gameH > 0) {
                gameW = state.aspect == 1 ? gameH * 16 / 9 : gameH * 4 / 3;
            }
            SDL_SetWindowSize(sdl, gameW, gameH);
            SDL_SetWindowPosition(sdl, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            SDL_SetWindowTitle(sdl, "Pokémon Stadium");
        }
        // Graphics > Display: the game window windowed or fullscreen (the launcher kept its own window)
        if (auto config = window->GetContext()->GetChildren().GetFirst<Ship::Config>()) {
            const bool full = config->GetBool("Window.Fullscreen.Enabled", false);
            if (full != window->IsFullscreen()) {
                window->SetFullscreen(full);
            }
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

void UseShipHomeDefaults() {
    if (getenv("PORT_RECORD") == nullptr) {
        SetEnv("PORT_RECORD", Ship::Context::GetPathRelativeToAppDirectory("last_input.rec"));
    }
    if (getenv("PORT_FLASH_FILE") == nullptr) { // the launcher's default save
        std::error_code ec;
        fs::create_directories(fs::u8path(DefaultSave()).parent_path(), ec);
        SetEnv("PORT_FLASH_FILE", DefaultSave());
    }
}

} // namespace Port
