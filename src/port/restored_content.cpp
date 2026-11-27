// Restored content (hippodrome-utils docs/specs/game_specific/1/RESTORED_CONTENT.md): pieces of other Stadium
// releases the player also provides (extra ROMs, rom_version.h) that the loaded ROM lacks or changed.
//
// - Gallery: the French, German, Spanish and Italian cartridges have no Gallery graphics (gallery_ui, album_ui and
//   snap_select_ui are stubs there). With an English ROM (any USA or European English release) also provided, those
//   three pieces are read from it; the Gallery's text stays the loaded ROM's own (its textdata has the Gallery tables
//   translated). Without one the Gallery cannot be entered, as on those cartridges.
// - Nidoqueen's animation: the Japanese release has its own (Pokémon model archive file 30). With a Japanese ROM
//   provided, either one can be chosen; otherwise the loaded ROM's own is used.
// - The Nintendo Cups: with a Japanese ROM provided, Nintendo Cup '97, '98 and '99 beside the Poké Cup
//   (nintendo_cup.cpp).
// - Jynx's colour: Original (USA 1.0 and the Japanese release) or Purple (every later release): model archive file
//   123 and menu icon archive file 123. With a ROM of the other kind provided, either one can be chosen.
//
// Settings: gEnhancements.JynxColor and gEnhancements.NidoqueenAnimation (0: the loaded ROM's own, the default), and
// the extra ROMs in gLauncher.ExtraRom1..3. PORT_EXTRA_ROMS (paths separated by ';') adds extra ROMs too, and
// PORT_JYNX_COLOR=original|purple / PORT_NIDOQUEEN=international|japanese override the settings (scripts, tests).
#include "restored_content.h"
#include "file_io.h"
#include "launcher_files.h"
#include "rom_check.h"
#include "rom_version.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/core/Context.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Rev 2 ROM pieces (generated/abs_syms.S) and archives (display_object_textures.c, poke_icon.c)
constexpr uint32_t kGalleryUi[2] = { 0x697660, 0x69E3A0 };     // gallery_ui
constexpr uint32_t kAlbumUi[2] = { 0x69E3A0, 0x6AAA60 };       // album_ui
constexpr uint32_t kSnapSelectUi[2] = { 0x6AF4E0, 0x6B22C0 };  // snap_select_ui
constexpr uint32_t kModelArchive = 0x919000;                   // Pokémon models, one file per species (dex - 1)
constexpr uint32_t kIconArchive = 0x81D000;                    // menu icons, the same
// where the Japanese release keeps them (it has no layout table: rom_version.h)
constexpr uint32_t kJpModelArchive = 0x920000;
constexpr uint32_t kJpIconArchive = 0x818000;
constexpr int kNidoqueenFile = 31 - 1;
constexpr int kJynxFile = 124 - 1;

int gEnglishSlot = 0;  // an English extra ROM, for the Gallery (0: none)
int gJapanSlot = 0;    // the Japanese extra ROM (0: none)
int gOriginalSlot = -1; // where Jynx's Original colour comes from: 0 the loaded ROM, else a slot (-1: nowhere)
int gPurpleSlot = -1;   // and Purple

bool IsEnglish(PortRomVersion v) {
    return v == PORT_ROM_US || v == PORT_ROM_US_REV1 || v == PORT_ROM_US_REV2 || v == PORT_ROM_EU ||
           v == PORT_ROM_EU_REV1;
}

bool IsTranslated(PortRomVersion v) {
    return v == PORT_ROM_FR || v == PORT_ROM_DE || v == PORT_ROM_ES || v == PORT_ROM_IT;
}

// Jynx is Original in USA 1.0 and the Japanese release, Purple in every later one
bool HasOriginalJynx(PortRomVersion v) {
    return v == PORT_ROM_US || v == PORT_ROM_JP;
}

std::vector<uint8_t> ReadRomFile(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = Port::OpenFileUtf8(path.c_str(), "rb");
    if (f == nullptr) {
        return data;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size == (long)Port::kRomSize) {
        data.resize((size_t)size);
        if (fread(data.data(), 1, data.size(), f) != data.size()) {
            data.clear();
        }
    }
    fclose(f);
    return data;
}

std::vector<std::string> ExtraRomPaths() {
    std::vector<std::string> paths;
    const auto directory = std::filesystem::u8path(Ship::Context::GetAppDirectoryPath());
    for (int i = 1; i <= PORT_ROM_EXTRA_SLOTS; i++) {
        const std::string key = "gLauncher.ExtraRom" + std::to_string(i);
        const std::string path = Port::ResolveLauncherPath(CVarGetString(key.c_str(), ""), directory);
        if (!path.empty()) {
            paths.push_back(path);
        }
    }
    if (const char* env = getenv("PORT_EXTRA_ROMS")) {
        std::string list = env;
        size_t start = 0;
        while (start <= list.size()) {
            const size_t end = std::min(list.find(';', start), list.size());
            if (end > start) {
                paths.push_back(list.substr(start, end - start));
            }
            start = end + 1;
        }
    }
    return paths;
}

int EnvChoice(const char* name, const char* first, const char* second) {
    const char* v = getenv(name);
    if (v == nullptr || *v == '\0') {
        return -1;
    }
    if (strcmp(v, first) == 0) return 1;
    if (strcmp(v, second) == 0) return 2;
    return 0;
}

// the Rev 2 archive's file `index` in the ROM in slot: its marked start and size (false when it cannot be read)
bool ArchiveFileIn(int slot, uint32_t rev2Archive, uint32_t jpArchive, int index, uint32_t* start, uint32_t* size) {
    uint32_t archive;
    if (slot == 0) {
        archive = Port_RomToRaw(rev2Archive);
    } else if (slot == gJapanSlot) {
        archive = Port_RomRawIn(slot, jpArchive);
    } else {
        archive = Port_ExtraRomRaw(slot, rev2Archive);
    }
    if (archive == 0) {
        return false;
    }
    uint8_t head[0x10], entry[0x10];
    if (Port_RomRead(archive, head, sizeof head) != sizeof head ||
        Port_RomRead(archive + 0x10 + (uint32_t)index * 0x10, entry, sizeof entry) != sizeof entry) {
        return false;
    }
    const uint32_t count = ((uint32_t)head[0x0C] << 24) | ((uint32_t)head[0x0D] << 16) | ((uint32_t)head[0x0E] << 8) |
                           head[0x0F];
    if ((uint32_t)index >= count || count > 0x400) {
        return false;
    }
    *start = archive + (((uint32_t)entry[0] << 24) | ((uint32_t)entry[1] << 16) | ((uint32_t)entry[2] << 8) | entry[3]);
    *size = ((uint32_t)entry[4] << 24) | ((uint32_t)entry[5] << 16) | ((uint32_t)entry[6] << 8) | entry[7];
    return *size != 0;
}

} // namespace

namespace Port {

void LoadRestoredContent() {
    ClearExtraRoms();
    ResetNintendoCup();
    gEnglishSlot = gJapanSlot = 0;
    for (const std::string& path : ExtraRomPaths()) {
        std::vector<uint8_t> data = ReadRomFile(path);
        if (data.empty()) {
            fprintf(stderr, "[restored] %s: not a Stadium ROM (cannot read 32 MiB)\n", path.c_str());
            continue;
        }
        const PortRomVersion version = ExtraRomVersionOfSha1(Sha1Hex(data.data(), data.size()));
        if (version == PORT_ROM_VERSION_COUNT) {
            fprintf(stderr, "[restored] %s: not one of the accepted dumps\n", path.c_str());
            continue;
        }
        if (version == LoadedRomVersion()) {
            continue; // the loaded ROM itself
        }
        const int slot = AddExtraRom(std::move(data), version);
        if (slot == 0) {
            fprintf(stderr, "[restored] %s: no room for more extra ROMs\n", path.c_str());
            continue;
        }
        if (version == PORT_ROM_JP) {
            gJapanSlot = slot;
        } else if (IsEnglish(version) && (gEnglishSlot == 0 || version == PORT_ROM_US_REV2)) {
            gEnglishSlot = slot;
        }
    }
    const PortRomVersion loaded = LoadedRomVersion();
    gOriginalSlot = HasOriginalJynx(loaded) ? 0 : -1;
    gPurpleSlot = HasOriginalJynx(loaded) ? -1 : 0;
    for (int slot = 1; slot <= PORT_ROM_EXTRA_SLOTS; slot++) {
        for (int v = 0; v < PORT_ROM_VERSION_COUNT; v++) {
            if (ExtraRomSlot((PortRomVersion)v) != slot) {
                continue;
            }
            if (HasOriginalJynx((PortRomVersion)v)) {
                if (gOriginalSlot < 0) gOriginalSlot = slot;
            } else if (gPurpleSlot < 0) {
                gPurpleSlot = slot;
            }
        }
    }
    if (IsTranslated(loaded) && gEnglishSlot != 0) {
        RedirectRomPiece(kGalleryUi[0], kGalleryUi[1], gEnglishSlot);
        RedirectRomPiece(kAlbumUi[0], kAlbumUi[1], gEnglishSlot);
        RedirectRomPiece(kSnapSelectUi[0], kSnapSelectUi[1], gEnglishSlot);
        fprintf(stderr, "[restored] Gallery graphics from %s\n", RomVersionName(ExtraRomVersion(gEnglishSlot)));
    }
}

int JapanRomSlot() {
    return gJapanSlot;
}

PortRomVersion ExtraRomVersion(int slot) {
    for (int v = 0; v < PORT_ROM_VERSION_COUNT; v++) {
        if (ExtraRomSlot((PortRomVersion)v) == slot && slot != 0) {
            return (PortRomVersion)v;
        }
    }
    return PORT_ROM_VERSION_COUNT;
}

RestoredContentChoices RestoredChoicesFor(PortRomVersion loaded, const std::vector<PortRomVersion>& extras) {
    RestoredContentChoices c;
    bool original = HasOriginalJynx(loaded), purple = !HasOriginalJynx(loaded), english = IsEnglish(loaded);
    for (PortRomVersion v : extras) {
        if (v == PORT_ROM_VERSION_COUNT) continue;
        original |= HasOriginalJynx(v);
        purple |= !HasOriginalJynx(v);
        english |= IsEnglish(v);
        c.nidoqueen |= v == PORT_ROM_JP;
        c.nintendoCup |= v == PORT_ROM_JP && loaded != PORT_ROM_JP;
    }
    c.jynx = original && purple;
    c.jynxOriginalDefault = HasOriginalJynx(loaded);
    c.gallery = !IsTranslated(loaded) || english;
    return c;
}

} // namespace Port

extern "C" {

int Port_GalleryAvailable(void) {
    return !IsTranslated(Port::LoadedRomVersion()) || gEnglishSlot != 0;
}

int Port_Enh_JynxColor(void) {
    const int env = EnvChoice("PORT_JYNX_COLOR", "original", "purple");
    const int c = env >= 0 ? env : CVarGetInteger("gEnhancements.JynxColor", 0);
    return (c == PORT_JYNX_ORIGINAL || c == PORT_JYNX_PURPLE) ? c : PORT_JYNX_ROM;
}

void Port_Enh_SetJynxColor(int color) {
    CVarSetInteger("gEnhancements.JynxColor", color);
    CVarSave();
}

int Port_Enh_NidoqueenAnimation(void) {
    const int env = EnvChoice("PORT_NIDOQUEEN", "international", "japanese");
    const int c = env >= 0 ? env : CVarGetInteger("gEnhancements.NidoqueenAnimation", 0);
    return (c == PORT_NIDOQUEEN_INTERNATIONAL || c == PORT_NIDOQUEEN_JAPANESE) ? c : PORT_NIDOQUEEN_ROM;
}

void Port_Enh_SetNidoqueenAnimation(int animation) {
    CVarSetInteger("gEnhancements.NidoqueenAnimation", animation);
    CVarSave();
}

int Port_RestoredArchiveFile(uint32_t archiveRaw, int index, uint32_t* start, uint32_t* size) {
    const bool models = archiveRaw == Port_RomToRaw(kModelArchive);
    const bool icons = archiveRaw == Port_RomToRaw(kIconArchive);
    int slot = 0;
    if (!models && !icons) {
        return 0;
    }
    if (index == kJynxFile) {
        const int color = Port_Enh_JynxColor();
        if (color == PORT_JYNX_ORIGINAL) slot = gOriginalSlot;
        else if (color == PORT_JYNX_PURPLE) slot = gPurpleSlot;
    } else if (index == kNidoqueenFile && models) {
        if (Port_Enh_NidoqueenAnimation() == PORT_NIDOQUEEN_JAPANESE) slot = gJapanSlot;
    }
    if (slot <= 0) {
        return 0; // the loaded ROM's own
    }
    return ArchiveFileIn(slot, models ? kModelArchive : kIconArchive, models ? kJpModelArchive : kJpIconArchive, index,
                         start, size)
               ? 1
               : 0;
}

} // extern "C"
