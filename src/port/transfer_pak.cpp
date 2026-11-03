#include "file_io.h"
#include "launcher_reliability.h"
#include "launcher_files.h"
// Transfer Pak (64GB-Pak) emulation: the libultra osGbpak* calls, served by a Game Boy cartridge held in memory.
//
// Configuration (environment):
//   PORT_GB_ROM=<file.gb>       cartridge in the Transfer Pak of controller 1 (PORT_GB_ROM2..4 for controllers 2-4)
//   PORT_GB_SAVE=<file.sav>     its battery RAM (default: the ROM path with .sav); written back after every write
//   PORT_GB_REGION_COMPAT=0     off: present the cartridge header as it is. On (default): European Pokémon
//                               Red/Blue/Yellow are presented under their US title ("POKEMON YELLOW" ...), which the
//                               US Stadium requires (on hardware it only accepts the US/English carts).
//
// The game reaches the cartridge through osGbpakReadWrite with Game Boy bus addresses: 0x0000-0x7FFF read ROM through
// the cartridge's mapper and write mapper registers, 0xA000-0xBFFF read/write the battery RAM. The mapper (MBC1, MBC3
// or MBC5, from the header) is emulated here.
#include <ultra64.h>
#include <port_compat.h>
#include <PR/os_gbpak.h>

#include "port_platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Cart {
    bool present = false;
    bool power = false;
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram;
    std::string savePath;
    int mbc = 0; // 0 none, 1 MBC1, 3 MBC3, 5 MBC5
    bool ramEnabled = false;
    uint32_t romBank = 1;
    uint32_t ramBank = 0;
    uint32_t mbc1Mode = 0;
    bool ramDirty = false;
};

std::mutex gLock;
Cart gCarts[4];
bool gLoaded = false;

void PatchRegion(std::vector<uint8_t>& rom) {
    const char* e = getenv("PORT_GB_REGION_COMPAT");
    if (e != nullptr && strcmp(e, "0") == 0) {
        return;
    }
    if (rom.size() < 0x150 || rom[0x14A] != 1) {
        return;
    }
    // European Gen 1 titles: "POKEMON RED" / "POKEMON BLUE" + manufacturer code, "POKEMON YEL" + "APSx"
    const char* title = (const char*)&rom[0x134];
    const char* us = nullptr;
    if (memcmp(title, "POKEMON YEL", 11) == 0 && memcmp(title, "POKEMON YELLOW", 14) != 0) {
        us = "POKEMON YELLOW";
    } else if (memcmp(title, "POKEMON RED", 11) == 0 && title[11] != 0) {
        us = "POKEMON RED";
    } else if (memcmp(title, "POKEMON BLUE", 12) == 0 && title[12] != 0) {
        us = "POKEMON BLUE";
    }
    if (us == nullptr) {
        return;
    }
    memset(&rom[0x134], 0, 15); // keep 0x143 (CGB flag)
    memcpy(&rom[0x134], us, strlen(us));
    uint8_t sum = 0; // header checksum over 0x134-0x14C
    for (int i = 0x134; i <= 0x14C; i++) {
        sum = sum - rom[i] - 1;
    }
    rom[0x14D] = sum;
    fprintf(stderr, "[gbpak] presenting the cartridge as \"%s\" (PORT_GB_REGION_COMPAT=0 to turn off)\n", us);
}

void LoadCarts() {
    if (gLoaded) {
        return;
    }
    gLoaded = true;
    static const char* romVars[4] = { "PORT_GB_ROM", "PORT_GB_ROM2", "PORT_GB_ROM3", "PORT_GB_ROM4" };
    static const char* saveVars[4] = { "PORT_GB_SAVE", "PORT_GB_SAVE2", "PORT_GB_SAVE3", "PORT_GB_SAVE4" };
    for (int ch = 0; ch < 4; ch++) {
        const char* romPath = getenv(romVars[ch]);
        if (romPath == nullptr || *romPath == 0) {
            continue;
        }
        FILE* f = Port::OpenFileUtf8(romPath, "rb");
        if (f == nullptr) {
            fprintf(stderr, "[gbpak] cannot open %s\n", romPath);
            continue;
        }
        Cart& c = gCarts[ch];
        fseek(f, 0, SEEK_END);
        c.rom.resize((size_t)ftell(f));
        fseek(f, 0, SEEK_SET);
        if (fread(c.rom.data(), 1, c.rom.size(), f) != c.rom.size() || c.rom.size() < 0x8000) {
            fclose(f);
            fprintf(stderr, "[gbpak] %s is not a Game Boy ROM\n", romPath);
            c.rom.clear();
            continue;
        }
        fclose(f);
        switch (c.rom[0x147]) {
            case 0x01: case 0x02: case 0x03: c.mbc = 1; break;
            case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: c.mbc = 3; break;
            case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: c.mbc = 5; break;
            default: c.mbc = 0; break;
        }
        static const uint32_t ramSizes[] = { 0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000 };
        uint8_t rs = c.rom[0x149];
        c.ram.assign(rs < 6 ? ramSizes[rs] : 0, 0xFF);
        const char* sp = getenv(saveVars[ch]);
        if (sp != nullptr && *sp) {
            c.savePath = sp;
        } else {
            c.savePath = romPath;
            size_t dot = c.savePath.find_last_of('.');
            c.savePath = (dot == std::string::npos ? c.savePath : c.savePath.substr(0, dot)) + ".sav";
        }
        if (FILE* s = Port::OpenFileUtf8(c.savePath.c_str(), "rb")) {
            size_t n = fread(c.ram.data(), 1, c.ram.size(), s);
            fclose(s);
            fprintf(stderr, "[gbpak] controller %d: save %s (%zu bytes)\n", ch + 1, c.savePath.c_str(), n);
        } else {
            fprintf(stderr, "[gbpak] controller %d: no save file %s (empty battery RAM)\n", ch + 1, c.savePath.c_str());
        }
        PatchRegion(c.rom);
        c.present = true;
        fprintf(stderr, "[gbpak] controller %d: %s, MBC%d, ROM %zu KB, RAM %zu KB\n", ch + 1, romPath, c.mbc,
                c.rom.size() / 1024, c.ram.size() / 1024);
    }
}

void FlushSave(Cart& c) {
    if (!c.ramDirty || c.savePath.empty()) {
        return;
    }
    std::string error;
    if (Port::WriteProtectedSave(std::filesystem::u8path(c.savePath), c.ram.data(), c.ram.size(), error)) {
        c.ramDirty = false;
        Port_SaveWritten();
    } else {
        Port::ReportSaveFailure(error);
        fprintf(stderr, "[gbpak] SAVE FAILED: %s\n", error.c_str());
    }
}

uint8_t BusRead(Cart& c, uint32_t a) {
    if (a < 0x4000) {
        return c.rom[a % c.rom.size()];
    }
    if (a < 0x8000) {
        size_t off = (size_t)c.romBank * 0x4000 + (a - 0x4000);
        return c.rom[off % c.rom.size()];
    }
    if (a >= 0xA000 && a < 0xC000) {
        if (!c.ramEnabled || c.ram.empty()) {
            return 0xFF;
        }
        size_t off = (size_t)c.ramBank * 0x2000 + (a - 0xA000);
        return c.ram[off % c.ram.size()];
    }
    return 0xFF;
}

void BusWrite(Cart& c, uint32_t a, uint8_t v) {
    if (a < 0x2000) {
        c.ramEnabled = (v & 0x0F) == 0x0A;
    } else if (a < 0x4000) {
        if (c.mbc == 5) {
            if (a < 0x3000) {
                c.romBank = (c.romBank & 0x100) | v;
            } else {
                c.romBank = (c.romBank & 0xFF) | ((uint32_t)(v & 1) << 8);
            }
        } else if (c.mbc == 3) {
            c.romBank = (v & 0x7F) ? (v & 0x7F) : 1;
        } else if (c.mbc == 1) {
            c.romBank = (c.romBank & 0x60) | ((v & 0x1F) ? (v & 0x1F) : 1);
        }
    } else if (a < 0x6000) {
        if (c.mbc == 5) {
            c.ramBank = v & 0x0F;
        } else if (c.mbc == 3) {
            c.ramBank = v & 0x03; // (0x08-0x0C would select the RTC registers)
        } else if (c.mbc == 1) {
            if (c.mbc1Mode) {
                c.ramBank = v & 3;
            } else {
                c.romBank = (c.romBank & 0x1F) | ((uint32_t)(v & 3) << 5);
            }
        }
    } else if (a < 0x8000) {
        if (c.mbc == 1) {
            c.mbc1Mode = v & 1;
        }
    } else if (a >= 0xA000 && a < 0xC000) {
        if (c.ramEnabled && !c.ram.empty()) {
            size_t off = ((size_t)c.ramBank * 0x2000 + (a - 0xA000)) % c.ram.size();
            if (c.ram[off] != v) {
                c.ram[off] = v;
                c.ramDirty = true;
            }
        }
    }
}

Cart* CartFor(OSPfs* pfs) {
    if (pfs == nullptr || pfs->channel < 0 || pfs->channel > 3) {
        return nullptr;
    }
    Cart& c = gCarts[pfs->channel];
    return c.present ? &c : nullptr;
}

} // namespace

extern "C" {

s32 osPfsIsPlug(OSMesgQueue*, u8* pattern) {
    std::lock_guard<std::mutex> g(gLock);
    LoadCarts();
    *pattern = 0;
    for (int ch = 0; ch < 4; ch++) {
        if (gCarts[ch].present) {
            *pattern |= (u8)(1 << ch);
        }
    }
    return 0;
}

s32 osGbpakInit(OSMesgQueue* mq, OSPfs* pfs, int channel) {
    std::lock_guard<std::mutex> g(gLock);
    LoadCarts();
    pfs->queue = mq;
    pfs->channel = channel;
    pfs->status = 0;
    pfs->activebank = 0;
    if (channel < 0 || channel > 3 || !gCarts[channel].present) {
        return PFS_ERR_NOPACK;
    }
    pfs->status = PFS_GBPAK_INITIALIZED;
    return 0;
}

s32 osGbpakPower(OSPfs* pfs, s32 flag) {
    std::lock_guard<std::mutex> g(gLock);
    Cart* c = CartFor(pfs);
    if (c == nullptr) {
        return PFS_ERR_NOPACK;
    }
    if (getenv("PORT_TRACE_GBPAK")) fprintf(stderr, "[gbpak-trace] power channel=%d flag=%d\n", pfs->channel, (int)flag);
    c->power = flag != 0;
    if (!c->power) {
        FlushSave(*c);
    }
    return 0;
}

s32 osGbpakGetStatus(OSPfs* pfs, u8* status) {
    std::lock_guard<std::mutex> g(gLock);
    Cart* c = CartFor(pfs);
    if (c == nullptr) {
        *status = 0;
        return PFS_ERR_NOPACK;
    }
    *status = OS_GBPAK_GBCART_ON | OS_GBPAK_RSTB_STATUS | (c->power ? OS_GBPAK_POWER : 0);
    return 0;
}

s32 osGbpakReadWrite(OSPfs* pfs, u16 flag, u16 address, u8* buffer, u16 size) {
    std::lock_guard<std::mutex> g(gLock);
    Cart* c = CartFor(pfs);
    if (c == nullptr) {
        return PFS_ERR_NOPACK;
    }
    for (u32 i = 0; i < size; i++) {
        if (flag == OS_READ) {
            buffer[i] = BusRead(*c, (u32)address + i);
        } else {
            BusWrite(*c, (u32)address + i, buffer[i]);
        }
    }
    if (getenv("PORT_TRACE_GBPAK") && flag == OS_READ && (address < 0x8000 || size >= 0x100)) {
        unsigned sum = 0;
        for (unsigned i = 0; i < size; i++) sum += buffer[i];
        fprintf(stderr, "[gbpak-trace] read channel=%d addr=%04x size=%04x rombank=%u rambank=%u sum=%x\n",
                pfs->channel, address, size, c->romBank, c->ramBank, sum);
    }
    if (flag != OS_READ) {
        FlushSave(*c);
    }
    return 0;
}

s32 osGbpakReadId(OSPfs* pfs, OSGbpakId* id, u8* status) {
    std::lock_guard<std::mutex> g(gLock);
    Cart* c = CartFor(pfs);
    if (c == nullptr) {
        *status = 0;
        return PFS_ERR_NOPACK;
    }
    c->power = true; // libultra powers the cartridge on to read the header
    *status = OS_GBPAK_GBCART_ON | OS_GBPAK_RSTB_STATUS | OS_GBPAK_POWER;
    const uint8_t* h = &c->rom[0x100];
    auto be16 = [](const uint8_t* p) { return (u16)((p[0] << 8) | p[1]); };
    id->fixed1 = be16(h + 0x00);
    id->start_address = be16(h + 0x02);
    memcpy(id->nintendo_chr, h + 0x04, sizeof(id->nintendo_chr));
    memcpy(id->game_title, h + 0x34, sizeof(id->game_title));
    id->company_code = be16(h + 0x44);
    id->body_code = h[0x46];
    id->cart_type = h[0x47];
    id->rom_size = h[0x48];
    id->ram_size = h[0x49];
    id->country_code = h[0x4A];
    id->fixed2 = h[0x4B];
    id->version = h[0x4C];
    id->isum = h[0x4D];
    id->sum = be16(h + 0x4E);
    return 0;
}

s32 osGbpakCheckConnector(OSPfs* pfs, u8* status) {
    std::lock_guard<std::mutex> g(gLock);
    Cart* c = CartFor(pfs);
    if (c == nullptr) {
        *status = 0;
        return PFS_ERR_NOPACK;
    }
    c->power = true; // libultra powers the cartridge on to read and compare the header through the connector
    *status = OS_GBPAK_GBCART_ON | OS_GBPAK_RSTB_STATUS | OS_GBPAK_POWER;
    return 0;
}

// Direct access for a host-side Game Boy core (GB Tower): the cartridge bus of controller 'channel'.
int Port_GbCartPresent(int channel) {
    std::lock_guard<std::mutex> g(gLock);
    LoadCarts();
    return channel >= 0 && channel < 4 && gCarts[channel].present;
}
uint8_t Port_GbCartRead(int channel, uint32_t address) {
    std::lock_guard<std::mutex> g(gLock);
    return gCarts[channel].present ? BusRead(gCarts[channel], address) : 0xFF;
}
void Port_GbCartWrite(int channel, uint32_t address, uint8_t value) {
    std::lock_guard<std::mutex> g(gLock);
    if (gCarts[channel].present) {
        BusWrite(gCarts[channel], address, value);
    }
}
void Port_GbCartFlush(int channel) {
    std::lock_guard<std::mutex> g(gLock);
    if (gCarts[channel].present) {
        FlushSave(gCarts[channel]);
    }
}

} // extern "C"
