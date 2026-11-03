// Launcher shown inside the game window when the port is started without a ROM argument (macOS, Linux, Windows). Same
// design as the macOS app's launcher (tools/launcher/Launcher.swift): four player cards with the Game Boy cartridge
// of each Transfer Pak (picture, game, trainer from the save, save file) and a bar with the Stadium ROM, its save and
// PLAY. The choices are remembered in pokestadium.cfg.json and passed to the game the way the command line does
// (PORT_FLASH_FILE, PORT_GB_ROM*, PORT_GB_SAVE*). Auto-play skips it next time (hold Shift while starting to see it
// again); Debug adds a button that opens the save editor (tools/save_editor, needs python3) in the browser. Controls
// swaps the player cards for the key and controller bindings of each player (libultraship's ControlDeck mappings).
// Renderer picks libultraship's window backend (Window.Backend.Id); it applies the next time the game starts.
// Mods lists the mods in the mods folder (mods.cpp): switch them on or off, change their priority, Apply & Restart.
// Android (or PORT_LAUNCHER_HANDHELD=1) shows a handheld layout instead: the Stadium cartridge and a big PLAY, the
// Transfer Pak cards and the controls on pages of their own, everything reachable with the gamepad (D-pad or stick
// moves the focus, A presses, B goes back) as well as by touch.
#include "launcher.h"
#include "launcher_files.h"
#include "launcher_reliability.h"
#include "port_platform.h"
#include "Engine.h"
#include "mods.h"
#include "rom_check.h"

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
    return path.empty() ? std::string("—") : fs::u8path(path).filename().string();
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
// ImGui 1.91 rasterizes its atlas once. Keep a small bank of native pixel
// sizes so a resized window can switch fonts without rebuilding GPU textures.
struct FontSet {
    float rasterScale;
    Fonts fonts;
};
std::vector<FontSet> gFontSets;

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
    auto add = [&](float size, float rasterScale) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 2;
        static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2013, 0x2014, 0x2026, 0x2026, 0x2713, 0x2713, 0 };
        ImFont* font = io.Fonts->AddFontFromMemoryTTF(ttf.data(), (int)ttf.size(), size * rasterScale, &cfg, ranges);
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
    LauncherWindow(State* state, bool* play, bool* quit, Fast::Fast3dGui* gui, std::shared_ptr<Ship::Window> window)
        : GuiWindow("gLauncherWindowOpen", "Pokémon Stadium Launcher", ImVec2(kW, kH)), mState(state), mPlay(play),
          mQuit(quit), mGui(gui), mWindow(std::move(window)) {
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

    void ShowMods() {
        if (Handheld()) {
            GoToPage(kPageMods);
        } else {
            OpenMods(true);
        }
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
        if (Button(mMods ? "Players##mods" : ModsButtonLabel().c_str(), 800 - 8 - 130, 10, 130, 26)) {
            OpenMods(!mMods);
        }
        dl->AddRectFilled(P(0, 44), P(kW, 45), Hex(Theme::cardBorder));
        Tools();

        if (mTools) {
            ToolsPanel(16, 58, kW - 32, 366);
        } else if (mMods) {
            ModsPanel(16, 58, kW - 32, 366);
        } else if (mControls) {
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
    std::string mToolsStatus;
    bool mToolsTestOpened = false, mTools = false;
    void Tools() {
        const bool testOpen = getenv("PORT_LAUNCHER_OPEN_TOOLS") != nullptr && !mToolsTestOpened;
        ImGui::BeginDisabled(mCapture >= 0);
        const bool opened = Button("Files & help", 800, 8, 144, 30);
        ImGui::EndDisabled();
        if ((opened && mCapture < 0) || testOpen) {
            mToolsTestOpened = true;
            mTools = !mTools;
            if (mTools) mMods = false;
            if (Handheld()) GoToPage(mTools ? kPageTools : kPageHome);
        }
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
            Pick(&g.save, false, kGbSaveFilter, 2, save);
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

    // ---- mods (mods.cpp) ----

    std::string ModsButtonLabel() const {
        size_t on = 0;
        for (const auto& m : Port::Mods::Mounted()) on += m.mounted;
        return "Mods · " + (on == 0 ? std::string("off") : std::to_string(on) + " on");
    }

    // the folder's mods in the stored order, with this run's results (what was mounted, problems, conflicts)
    void OpenMods(bool open) {
        mMods = open;
        if (!open) {
            return;
        }
        mControls = false;
        mTools = false;
        mModList = Port::Mods::Scan();
        for (auto& m : mModList) {
            for (const auto& r : Port::Mods::Mounted()) {
                if (r.id != m.id) {
                    continue;
                }
                m.mounted = r.mounted;
                m.problems = r.problems;
                m.notes = r.notes;
                // (manifests are only read for the mods opened at start)
                m.name = r.name;
                m.version = r.version;
                m.author = r.author;
                m.description = r.description;
            }
        }
        mModsPage = 0;
        mModsStatus.clear();
    }

    // true when the list differs from what this run mounted
    bool ModsPending() const {
        std::vector<std::string> now, listed;
        for (const auto& m : Port::Mods::Mounted()) {
            if (m.mounted) now.push_back(m.id);
        }
        for (const auto& m : mModList) {
            if (m.enabled && m.problems.empty()) listed.push_back(m.id);
        }
        return now != listed;
    }

    void ApplyMods() {
        std::vector<std::string> enabled, disabled;
        for (const auto& m : mModList) {
            (m.enabled ? enabled : disabled).push_back(m.id);
        }
        Port::Mods::StoreLists(enabled, disabled);
        Store(*mState);
        // a restart is a normal end of this launch: without the marker (main.cpp) the next start is not a recovery
        std::error_code ec;
        fs::remove(fs::u8path(Port::AppPath("launcher-session.txt")), ec);
        if (!Port::Mods::Restart()) {
            mModsStatus = "Saved. Restart the game to use the new list.";
        }
    }

    // Mods: one row per mod in priority order (the top one wins where two mods change the same file). A row: on/off,
    // name, version and author, then what it overrides or why it did not load; Up/Down change the priority.
    // Shown in place of the player cards.
    void ModsPanel(float x, float y, float w, float h) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(P(x, y), P(x + w, y + h), Hex(Theme::card), 8 * mScale);
        dl->AddRect(P(x, y), P(x + w, y + h), Hex(Theme::cardBorder), 8 * mScale);
        Text(x + 14, y + 14, "MODS", gFonts.body, Theme::players[0]);
        std::string folder = Port::Mods::Folder();
        if (folder.size() > 70) folder = "…" + folder.substr(folder.size() - 69);
        Text(x + 70, y + 15, folder, gFonts.small, Theme::dim);
        if (!Handheld() && Button("Open folder", x + w - 216, y + 9, 100, 26)) {
            std::string url = "file://" + Port::Mods::Folder();
            SDL_OpenURL(url.c_str());
        }
        if (Button("Done", x + w - 108, y + 9, 94, 26)) {
            mMods = false;
        }
        dl->AddRectFilled(P(x, y + 44), P(x + w, y + 45), Hex(Theme::cardBorder));

        constexpr int kRows = 6;
        constexpr float kRowH = 44;
        if (mModList.empty()) {
            Text(x + 14, y + 60, "No mods yet. Put .o2r files or mod folders into the folder above.", gFonts.body);
            Text(x + 14, y + 82, "How to make one: docs/MODDING.md", gFonts.small, Theme::dim);
        }
        const int pages = std::max(1, ((int)mModList.size() + kRows - 1) / kRows);
        mModsPage = std::min(mModsPage, pages - 1);
        int priority = 0;
        for (int i = 0; i < (int)mModList.size(); i++) {
            priority += mModList[i].enabled;
            int row = i - mModsPage * kRows;
            if (row < 0 || row >= kRows) {
                continue;
            }
            ModRow(i, mModList[i].enabled ? priority : 0, x + 14, y + 52 + row * kRowH, w - 28);
        }

        // footer: pages, the result, Apply & Restart
        const float fy = y + h - 38;
        dl->AddRectFilled(P(x, fy - 8), P(x + w, fy - 7), Hex(Theme::cardBorder));
        if (pages > 1) {
            if (Button("<##modpage", x + 14, fy, 30, 26)) mModsPage = std::max(0, mModsPage - 1);
            Text(x + 52, fy + 5, std::to_string(mModsPage + 1) + " / " + std::to_string(pages), gFonts.small, Theme::dim);
            if (Button(">##modpage", x + 92, fy, 30, 26)) mModsPage = std::min(pages - 1, mModsPage + 1);
        }
        std::string info = !mModsStatus.empty()          ? mModsStatus
                           : Port::Mods::ListFromEnvironment() ? "PORT_MODS chose this run's mods; the list applies without it."
                                                               : "Top of the list wins. Mods load when the game starts.";
        Text(x + (pages > 1 ? 136 : 14), fy + 6, info, gFonts.small, Theme::dim);
        const bool pending = ModsPending();
        ImGui::BeginDisabled(!pending && !Port::Mods::ListFromEnvironment());
        if (ColorButton("Apply & Restart", x + w - 164, fy, 150, 26, pending ? Theme::green : Theme::button,
                        pending ? 0x37B24D : Theme::buttonHover)) {
            ApplyMods();
        }
        ImGui::EndDisabled();
    }

    void ModRow(int i, int priority, float x, float y, float w) {
        Port::Mods::Mod& m = mModList[i];
        ImGui::PushID(i);
        // state: green loaded, red enabled but not loaded, yellow after a restart, grey off
        uint32_t color = !m.enabled                    ? Theme::dim
                         : !m.problems.empty()         ? Theme::red
                         : m.mounted                   ? Theme::green
                                                       : Theme::players[0];
        Check("##on", &m.enabled, x, y + 6);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(m.enabled ? "On: loaded when the game starts" : "Off");
        }
        ImGui::GetWindowDrawList()->AddCircleFilled(P(x + 34, y + 14), 4 * mScale, Hex(color));
        std::string title = (priority > 0 ? std::to_string(priority) + ". " : "") + m.Title();
        if (title.size() > 40) title = title.substr(0, 39) + "…";
        Text(x + 44, y + 4, title, gFonts.game, m.enabled ? Theme::text : Theme::dim);
        std::string meta = m.version;
        if (!m.author.empty()) meta += (meta.empty() ? "by " : " · by ") + m.author;
        if (m.folder) meta += meta.empty() ? "folder" : " · folder";
        Text(x + 380, y + 7, meta, gFonts.small, Theme::dim);
        // second line: why it did not load, what it changes, or what it is
        std::string detail;
        uint32_t detailColor = Theme::dim;
        if (m.enabled && !m.problems.empty()) {
            detail = "Not loaded: " + m.problems[0];
            detailColor = Theme::red;
        } else if (m.enabled && !m.mounted) {
            detail = "Loads after Apply & Restart";
        } else if (m.mounted && !m.notes.empty()) {
            for (const auto& n : m.notes) detail += (detail.empty() ? "" : " · ") + n;
        } else {
            detail = m.description;
        }
        if (detail.size() > 110) detail = detail.substr(0, 109) + "…";
        Text(x + 44, y + 25, detail, gFonts.small, detailColor);
        if (Button("Up", x + w - 112, y + 6, 52, 26) && i > 0) {
            std::swap(mModList[i], mModList[i - 1]);
        }
        if (Button("Down", x + w - 54, y + 6, 54, 26) && i + 1 < (int)mModList.size()) {
            std::swap(mModList[i], mModList[i + 1]);
        }
        ImGui::PopID();
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
        if (Button("Import save", x + 450, y + 56, 100, 26)) {
            Pick(&s.save, false, kSaveFilter, 2, s.save, true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import a raw N64 Stadium cartridge save. A working copy is saved beside the launcher; the original is kept.");
        if (s.debug) {
            if (Button("Save editor", x + 556, y + 56, 96, 26)) {
                OpenSaveEditor(s.save);
            }
            if (!gEditorStatus.empty()) {
                Text(x + 658, y + 61, gEditorStatus, gFonts.small, Theme::dim);
            }
        }
        Check("Debug", &s.debug, x + 164, y + 88);
        Check("Smooth motion", &s.smooth, x + 250, y + 88);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("3D scenes at the display's refresh rate: in-between frames are drawn between the game's\n"
                              "own 30 or 20 frames per second (one game frame of extra delay)");
        }
        if (Button(mControls ? "Players" : "Controls", x + 450, y + 86, 100, 26)) {
            mControls = !mControls;
            mMods = false;
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
            if (pending) {
                Text(x + 616, y + 43, mRendererFailed == s.renderer ? "Unavailable" : "Restart required",
                     gFonts.small, Theme::dim);
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
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && romError.empty() && !mControls && !mTools && !mMods) {
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
        if ((page == kPageMods) != mMods) {
            OpenMods(page == kPageMods);
        }
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
        if ((mPage == kPageControls && !mControls) || (mPage == kPageMods && !mMods)) { // the panel's Done button
            GoToPage(kPageHome);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Text(24, 14, "Pokémon Stadium", gFonts.title);
        const char* pageName = mPage == kPageTransferPak ? "Transfer Pak"
                               : mPage == kPageControls ? "Controls"
                               : mPage == kPageTools    ? "Files & help"
                               : mPage == kPageMods     ? "Mods"
                                                        : "";
        Text(220, 17, pageName, gFonts.body, Theme::dim);
        dl->AddRectFilled(P(0, 50), P(kW, 51), Hex(Theme::cardBorder));

        if (mPage == kPageTools) {
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
        } else if (mPage == kPageMods) {
            ModsPanel(16, 62, kW - 32, 366);
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
        if (saveName.size() > 40) saveName = saveName.substr(0, 39) + "…";
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
        if (Button(ModsButtonLabel().c_str(), rx, y, bw, bh, gFonts.game)) {
            GoToPage(kPageMods);
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
    static constexpr int kPageHome = 0, kPageTransferPak = 1, kPageControls = 2, kPageTools = 3, kPageMods = 4;
    int mPage = kPageHome;
    std::vector<NavRect> mNavItems, mNavPrev; // this frame's and last frame's focus targets, in drawing order
    int mFocus = -1, mDefaultFocus = 0;
    bool mNavAccept = false, mNavBack = false, mWasAccept = true, mWasBack = true; // (held at start: not a press)
    int mHeldDx = 0, mHeldDy = 0;
    std::chrono::steady_clock::time_point mRepeatAt;

    State* mState;
    bool* mPlay;
    bool* mQuit;
    Fast::Fast3dGui* mGui;
    std::shared_ptr<Ship::Window> mWindow;
    bool mControls = false; // the controls panel instead of the player cards
    bool mMods = false;     // the mods panel instead of the player cards
    std::vector<Port::Mods::Mod> mModList; // the panel's list: changed there, stored by Apply & Restart
    int mModsPage = 0;
    std::string mModsStatus;
    int mPort = 0;          // whose controls it shows
    int mCapture = -1;      // the input (kInputs) waiting for a key or controller input, -1: none
    std::string mCaptureId; // the binding it replaces ("": adds one)
    bool mArmed = false;    // the inputs held when the capture started were released
    float mScale = 1.0f, mPixelDensity = 1.0f;
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
        auto launcher = std::make_shared<LauncherWindow>(&state, &play, &quit, gui.get(), window);
        launcher->Init();
        launcher->Show();
        // test hook: PORT_LAUNCHER_MODS=1 opens the mods panel
        if (getenv("PORT_LAUNCHER_MODS") != nullptr) {
            launcher->ShowMods();
        }
        window->GetGui()->AddGuiWindow(launcher);
        static Gfx empty[1];
        gSPEndDisplayList(&empty[0]);
        // test hook: PORT_LAUNCHER_PLAY_AFTER=<frames> presses PLAY after that many frames (when the ROM is valid)
        const char* playAfter = getenv("PORT_LAUNCHER_PLAY_AFTER");
        // PORT_TRACE_PACING=1: launcher frames more than 40 ms apart (the shader warm-up runs meanwhile)
        static const bool tracePacing = getenv("PORT_TRACE_PACING") != nullptr;
        auto lastFrame = std::chrono::steady_clock::now();
        for (int frame = 0; !play && !quit && window->IsRunning(); frame++) {
            window->HandleEvents();
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
        if (!play) {
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
