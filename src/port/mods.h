#pragma once
#include <string>
#include <vector>

// Mods (docs/MODS_CONCEPT.md, docs/MODDING.md): .o2r/.zip archives and loose folders in <app dir>/mods, mounted into
// libultraship's virtual file system on top of pokestadium.o2r at start. The enabled list is ordered and its FIRST
// entry has the highest priority: where two mods (or a mod and the base archive) hold the same path, the mod higher
// in the list wins. libultraship lets the archive added last win, so the list is mounted bottom to top.
namespace Port::Mods {

struct Mod {
    std::string id;   // the file or folder name in mods/ ("hello-mod.o2r", "my-wip-mod"): the key in the lists
    std::string path; // full path
    bool folder = false;
    bool enabled = false;
    bool mounted = false;
    // manifest.json (optional): name, version, author, description, plus the port's own "pokestadium" object
    std::string name, version, author, description;
    std::vector<std::string> required, conflicts; // other mods, by name or id
    bool exclusive = false;
    std::vector<std::string> problems; // why an enabled mod was not mounted
    std::vector<std::string> notes;    // what it overrides, and what overrides it
    size_t files = 0;
    // shown in the list: the manifest name, else the id
    const std::string& Title() const {
        return name.empty() ? id : name;
    }
};

// <app dir>/mods (build/mods in a developer build, the per-user folder of a packaged one)
std::string Folder();

// The mods in the folder in list order: enabled ones by priority (highest first), then disabled ones. Mods missing
// from both stored lists are new: they are enabled, at the top. Read from the folder and the CVars each time.
std::vector<Mod> Scan();

// Stores the lists (gSettings.Mods.Enabled / .Disabled, "|"-separated, highest priority first) and saves the config.
void StoreLists(const std::vector<std::string>& enabled, const std::vector<std::string>& disabled);

// Mounts the enabled mods after the base archive (Engine.cpp, right after the ResourceManager is set up), validates
// them, and logs a summary and the conflicts. PORT_MODS overrides the stored list for one run: "off" mounts none,
// "a.o2r|b" mounts exactly these (same order rule). PORT_MODS_CHECK=1 prints the report and ends the process.
void Mount();

// The state of the last Mount: every mod in the folder, with mounted/problems/notes filled in
const std::vector<Mod>& Mounted();

// true when PORT_MODS decided this run's list (the stored lists are not what was mounted)
bool ListFromEnvironment();

// The program's arguments, for Restart (main.cpp)
void SetCommandLine(int argc, char** argv);
// Starts the program again with the same arguments (mods are only mounted at start). false where that is not
// possible (Android, web): the player restarts the app.
bool Restart();

} // namespace Port::Mods

// "id1|id2" of the mounted mods, highest priority first ("" for none): recorded in input recordings (os_devices.cpp)
extern "C" const char* Port_ModsSignature(void);
