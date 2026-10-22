// Mods: discovery in <app dir>/mods, the ordered enabled/disabled lists (CVars), validation of the manifests, mounting
// into libultraship's virtual file system on top of pokestadium.o2r, and the conflict report. This is phase 0 of
// docs/MODS_CONCEPT.md: mods replace files of the port's own archive (launcher pictures, fonts, shaders); the
// override paths for game data (textures by hash, rom/, text/, data/) come in later phases and use the same mounting.
#include "mods.h"

#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/core/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/resource/archive/FolderArchive.h>
#include <ship/resource/archive/O2rArchive.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX // (std::max below)
#endif
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef PORT_VERSION_STRING
#define PORT_VERSION_STRING "0.0.0"
#endif

namespace Port::Mods {
namespace {

namespace fs = std::filesystem;

constexpr const char* kEnabledVar = "gSettings.Mods.Enabled";
constexpr const char* kDisabledVar = "gSettings.Mods.Disabled";
// The manifest's "pokestadium.format" this port reads
constexpr int kFormat = 1;
// manifest "game_version": the header CRC1 of Pokémon Stadium (USA) 1.0 (ROM offset 0x10), the only ROM the port runs
constexpr uint32_t kGameVersion = 0x90F5D9B3;

std::vector<Mod> gMounted;
std::string gSignature;
bool gFromEnvironment = false;
std::vector<std::string> gArgs;

void Log(const std::string& line) {
    fprintf(stderr, "[mods] %s\n", line.c_str());
    SPDLOG_INFO("[mods] {}", line);
}

// "a|b|c" -> {a, b, c} ("|" cannot appear in file names on Windows, so it is safe as a separator)
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t bar = s.find('|', start);
        if (bar == std::string::npos) {
            bar = s.size();
        }
        if (bar > start) {
            out.push_back(s.substr(start, bar - start));
        }
        start = bar + 1;
    }
    return out;
}

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        out += (out.empty() ? "" : "|") + s;
    }
    return out;
}

bool IEndsWith(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    if (s.size() < n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)s[s.size() - n + i]) != tolower((unsigned char)suffix[i])) {
            return false;
        }
    }
    return true;
}

// "0.3.1" < "0.10" etc.: numeric parts compared in order, missing parts are 0
int CompareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v) {
        std::vector<long> out;
        for (const char* p = v.c_str(); *p != '\0';) {
            if (isdigit((unsigned char)*p)) {
                char* end;
                out.push_back(strtol(p, &end, 10));
                p = end;
            } else {
                p++;
            }
        }
        return out;
    };
    std::vector<long> x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        long u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
        if (u != v) {
            return u < v ? -1 : 1;
        }
    }
    return 0;
}

std::string Utf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

// The entries of mods/: .o2r and .zip files and folders. Names starting with "." or "~" are skipped ("~" is
// reserved for later folder kinds, like Lighthouse's ~lang and ~romhacks).
std::vector<Mod> ListFolder() {
    std::vector<Mod> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(Folder()), ec)) {
        std::string id = Utf8(entry.path().filename());
        if (id.empty() || id[0] == '.' || id[0] == '~') {
            continue;
        }
        Mod m;
        m.id = id;
        m.path = Utf8(entry.path());
        m.folder = entry.is_directory(ec);
        if (m.folder || IEndsWith(id, ".o2r") || IEndsWith(id, ".zip")) {
            found.push_back(m);
        }
    }
    std::sort(found.begin(), found.end(), [](const Mod& a, const Mod& b) { return a.id < b.id; });
    return found;
}

std::shared_ptr<Ship::Archive> Open(const Mod& m, const std::shared_ptr<Ship::ResourceManager>& rm) {
    std::shared_ptr<Ship::Archive> a;
    if (m.folder) {
        a = std::make_shared<Ship::FolderArchive>(m.path, rm, nullptr);
    } else {
        a = std::make_shared<Ship::O2rArchive>(m.path, rm, nullptr);
    }
    a->Load();
    return a;
}

// manifest.json: libultraship's fields plus the "pokestadium" object (docs/MODDING.md). Returns the problems that
// keep the mod from loading.
std::vector<std::string> ReadManifest(Mod& m, Ship::Archive& a) {
    std::vector<std::string> problems;
    auto file = a.LoadFile("manifest.json");
    if (file == nullptr || !file->IsLoaded || file->Buffer == nullptr) {
        return problems; // optional
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file->Buffer->begin() + (std::ptrdiff_t)file->BufferOffset, file->Buffer->end());
    } catch (const std::exception& e) {
        problems.push_back(std::string("manifest.json is not valid JSON: ") + e.what());
        return problems;
    }
    auto str = [&](const nlohmann::json& o, const char* key) {
        return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string();
    };
    auto list = [&](const nlohmann::json& o, const char* key) {
        std::vector<std::string> out;
        if (o.contains(key) && o[key].is_array()) {
            for (const auto& v : o[key]) {
                if (v.is_string()) {
                    out.push_back(v.get<std::string>());
                }
            }
        }
        return out;
    };
    m.name = str(j, "name");
    m.version = str(j, "version");
    m.author = str(j, "author");
    m.description = str(j, "description");
    if (j.contains("game_version") && j["game_version"].is_number_unsigned() &&
        j["game_version"].get<uint64_t>() != kGameVersion) {
        problems.push_back("made for another game version");
    }
    // No code in mods (concept §5.5): gameplay changes are compiled into the port; a mod would switch them on
    if (!str(j, "main").empty() || (j.contains("binaries") && !j["binaries"].empty())) {
        problems.push_back("contains code (\"main\"/\"binaries\"): this port does not run code from mods");
    }
    if (j.contains("pokestadium") && j["pokestadium"].is_object()) {
        const auto& p = j["pokestadium"];
        int format = p.value("format", kFormat);
        if (format > kFormat) {
            problems.push_back("needs a newer port (mod format " + std::to_string(format) + ")");
        }
        std::string minVersion = str(p, "min_port_version");
        if (!minVersion.empty() && CompareVersions(PORT_VERSION_STRING, minVersion) < 0) {
            problems.push_back("needs port version " + minVersion + " or newer (this is " PORT_VERSION_STRING ")");
        }
        m.required = list(p, "requires");
        m.conflicts = list(p, "conflicts");
        m.exclusive = p.value("exclusive", false);
    }
    return problems;
}

bool Names(const Mod& m, const std::string& name) {
    return m.id == name || (!m.name.empty() && m.name == name);
}

std::string Plural(size_t n, const char* word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

} // namespace

std::string Folder() {
    std::error_code ec;
    fs::path p = fs::absolute(fs::u8path(Ship::Context::GetPathRelativeToAppDirectory("mods")), ec).lexically_normal();
    return Utf8(p);
}

std::vector<Mod> Scan() {
    std::vector<Mod> found = ListFolder();
    std::vector<std::string> enabled = Split(CVarGetString(kEnabledVar, "")),
                             disabled = Split(CVarGetString(kDisabledVar, ""));
    auto take = [&](const std::string& id) -> Mod* {
        for (Mod& m : found) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    };
    std::vector<Mod> out;
    std::set<std::string> listed(enabled.begin(), enabled.end());
    listed.insert(disabled.begin(), disabled.end());
    // new mods first: a mod dropped into the folder is meant to be used
    for (Mod& m : found) {
        if (!listed.count(m.id)) {
            m.enabled = true;
            out.push_back(m);
        }
    }
    for (const auto& id : enabled) {
        if (Mod* m = take(id); m != nullptr && std::none_of(out.begin(), out.end(), [&](const Mod& o) { return o.id == id; })) {
            m->enabled = true;
            out.push_back(*m);
        }
    }
    for (const auto& id : disabled) {
        if (Mod* m = take(id); m != nullptr && std::none_of(out.begin(), out.end(), [&](const Mod& o) { return o.id == id; })) {
            m->enabled = false;
            out.push_back(*m);
        }
    }
    return out;
}

void StoreLists(const std::vector<std::string>& enabled, const std::vector<std::string>& disabled) {
    CVarSetString(kEnabledVar, Join(enabled).c_str());
    CVarSetString(kDisabledVar, Join(disabled).c_str());
    CVarSave();
}

void Mount() {
    auto rm = ResourceGetResourceManager();
    if (rm == nullptr || rm->GetArchiveManager() == nullptr) {
        return;
    }
    auto archives = rm->GetArchiveManager();
    const bool check = getenv("PORT_MODS_CHECK") != nullptr;
    std::error_code ec;
    fs::create_directories(fs::u8path(Folder()), ec); // (so players find where mods go)

    // the list: stored, or PORT_MODS for this run
    std::vector<Mod> mods = Scan();
    if (const char* env = getenv("PORT_MODS")) {
        gFromEnvironment = true;
        std::vector<std::string> wanted = strcmp(env, "off") == 0 ? std::vector<std::string>{} : Split(env);
        std::vector<Mod> ordered;
        for (const auto& id : wanted) {
            auto it = std::find_if(mods.begin(), mods.end(), [&](const Mod& m) { return m.id == id; });
            if (it == mods.end()) {
                Log("PORT_MODS names " + id + ", which is not in " + Folder());
                continue;
            }
            it->enabled = true;
            ordered.push_back(*it);
            mods.erase(it);
        }
        for (Mod& m : mods) {
            m.enabled = false;
            ordered.push_back(m);
        }
        mods = ordered;
    }

    // open them all (for the names in the list), and validate the enabled ones (in priority order, highest first)
    std::vector<std::shared_ptr<Ship::Archive>> opened(mods.size());
    std::vector<size_t> candidates;
    for (size_t i = 0; i < mods.size(); i++) {
        Mod& m = mods[i];
        opened[i] = Open(m, rm);
        if (!m.enabled) {
            if (opened[i]->IsInitialized()) {
                ReadManifest(m, *opened[i]); // (for the name in the list only)
            }
            continue;
        }
        if (!opened[i]->IsInitialized()) {
            m.problems.push_back("cannot be opened");
            continue;
        }
        m.problems = ReadManifest(m, *opened[i]);
        if (m.problems.empty()) {
            candidates.push_back(i);
        }
    }
    // an exclusive mod runs alone: the highest one in the list wins, the others stay off
    for (size_t i : candidates) {
        if (mods[i].exclusive) {
            for (size_t k : candidates) {
                if (k != i) {
                    mods[k].problems.push_back(mods[i].Title() + " runs alone (exclusive)");
                }
            }
            candidates = { i };
            break;
        }
    }
    // declared conflicts: of the two, the one lower in the list stays off
    std::vector<size_t> accepted;
    for (size_t i : candidates) {
        std::string clash;
        for (size_t k : accepted) {
            for (const auto& name : mods[i].conflicts) {
                if (Names(mods[k], name)) clash = mods[k].Title();
            }
            for (const auto& name : mods[k].conflicts) {
                if (Names(mods[i], name)) clash = mods[k].Title();
            }
        }
        if (clash.empty()) {
            accepted.push_back(i);
        } else {
            mods[i].problems.push_back("conflicts with " + clash);
        }
    }
    // requirements: repeated until nothing changes (a mod whose requirement stays off stays off too)
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = accepted.begin(); it != accepted.end(); ++it) {
            for (const auto& name : mods[*it].required) {
                bool present = std::any_of(accepted.begin(), accepted.end(), [&](size_t k) { return Names(mods[k], name); });
                if (!present) {
                    mods[*it].problems.push_back("requires " + name);
                    accepted.erase(it);
                    changed = true;
                    break;
                }
            }
            if (changed) {
                break;
            }
        }
    }

    // mount bottom to top: libultraship gives a path to the archive added last
    auto base = archives->GetArchives();
    for (auto it = accepted.rbegin(); it != accepted.rend(); ++it) {
        if (archives->AddArchive(opened[*it]) != nullptr) {
            mods[*it].mounted = true;
        } else {
            mods[*it].problems.push_back("rejected by the archive manager");
        }
    }

    // what each mounted mod overrides, and what overrides it
    std::map<std::string, std::vector<std::string>> conflictLines; // path -> report line (PORT_MODS_CHECK)
    size_t baseReplaced = 0;
    for (size_t i : accepted) {
        Mod& m = mods[i];
        if (!m.mounted) {
            continue;
        }
        std::map<std::string, size_t> over, hiddenBy;
        size_t overBase = 0, added = 0;
        auto files = opened[i]->ListFiles();
        for (const auto& [hash, path] : *files) {
            if (path == "manifest.json" || path.empty() || path.back() == '/') {
                continue;
            }
            m.files++;
            auto winner = archives->GetArchiveFromFile(path);
            if (winner != opened[i]) {
                for (size_t k : accepted) {
                    if (opened[k] == winner) hiddenBy[mods[k].Title()]++;
                }
                continue;
            }
            std::vector<std::string> under;
            for (size_t k : accepted) {
                if (k != i && mods[k].mounted && opened[k]->HasFile(hash)) {
                    over[mods[k].Title()]++;
                    under.push_back(mods[k].id);
                }
            }
            bool inBase = std::any_of(base->begin(), base->end(), [&](const auto& b) { return b->HasFile(hash); });
            if (inBase) {
                overBase++;
                under.push_back("base");
            }
            if (under.empty()) {
                added++;
            } else {
                conflictLines[path] = { m.id };
                conflictLines[path].insert(conflictLines[path].end(), under.begin(), under.end());
            }
        }
        baseReplaced += overBase;
        if (overBase > 0) m.notes.push_back("replaces " + Plural(overBase, "file") + " of the base game");
        for (const auto& [name, n] : over) m.notes.push_back("overrides " + Plural(n, "file") + " of " + name);
        for (const auto& [name, n] : hiddenBy) m.notes.push_back(Plural(n, "file") + " overridden by " + name);
        if (added > 0) m.notes.push_back("adds " + Plural(added, "file"));
    }

    // summary
    std::vector<std::string> active;
    size_t failed = 0;
    for (size_t i : accepted) {
        if (mods[i].mounted) active.push_back(mods[i].id);
    }
    gSignature = Join(active);
    std::string from = gFromEnvironment ? " (PORT_MODS)" : "";
    if (mods.empty()) {
        Log("none in " + Folder());
    } else {
        Log(std::to_string(active.size()) + " of " + std::to_string(mods.size()) + " active" + from +
            (active.empty() ? "" : ": " + Join(active) + " (highest priority first)") + "; " +
            Plural(baseReplaced, "base file") + " replaced");
    }
    for (const Mod& m : mods) {
        if (m.mounted) {
            std::string line = "  + " + m.id + (m.version.empty() ? "" : " " + m.version);
            for (const auto& n : m.notes) line += "; " + n;
            Log(line);
        } else if (!m.problems.empty()) {
            failed++;
            std::string line = "  ! " + m.id + " not loaded:";
            for (const auto& p : m.problems) line += " " + p + ";";
            line.pop_back();
            Log(line);
        }
    }
    if (check) {
        // every path more than one archive holds: the winner first
        for (const auto& [path, owners] : conflictLines) {
            std::string line = "  " + path + ": " + owners[0] + " over";
            for (size_t k = 1; k < owners.size(); k++) line += (k == 1 ? " " : ", ") + owners[k];
            Log(line);
        }
    }
    gMounted = mods;
    if (check) {
        fflush(nullptr);
        _exit(failed > 0 ? 1 : 0);
    }
}

const std::vector<Mod>& Mounted() {
    return gMounted;
}

bool ListFromEnvironment() {
    return gFromEnvironment;
}

void SetCommandLine(int argc, char** argv) {
    gArgs.assign(argv, argv + argc);
}

bool Restart() {
#if defined(__ANDROID__)
    return false;
#else
    std::string exe;
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) exe = buf;
#elif defined(_WIN32)
    char buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buf, sizeof buf) > 0) exe = buf;
#else
    exe = "/proc/self/exe";
#endif
    if (exe.empty() || gArgs.empty()) {
        return false;
    }
    std::vector<char*> argv;
    for (auto& a : gArgs) argv.push_back(a.data());
    argv.push_back(nullptr);
    if (auto logger = spdlog::default_logger()) logger->flush();
    fflush(nullptr);
#if defined(_WIN32)
    // (_execv would let the console's shell think the program ended; a new process and an exit look the same)
    if (_spawnv(_P_NOWAIT, exe.c_str(), argv.data()) == -1) return false;
    _exit(0);
#else
    execv(exe.c_str(), argv.data());
    return false; // (only reached when exec failed)
#endif
#endif
}

} // namespace Port::Mods

extern "C" const char* Port_ModsSignature(void) {
    return Port::Mods::gSignature.c_str();
}
