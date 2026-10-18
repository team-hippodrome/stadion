// ares_save — tools/ares_ref/ares_ref.cpp plus cartridge FlashRAM load/store, for checking edited Pokémon Stadium
// saves in the real game (tools/save_editor). Everything else is identical to ares_ref.
//
// Extra options / script lines:
//   --flash-in <file>             128 KiB FlashRAM image to insert (big-endian bytes as the N64 reads them); without
//                                 it the cartridge gets an erased (0xFF) flash chip
//   --flash-out <file>            write the FlashRAM image here when the script ends
//   flashsave <frame> <file>      write the FlashRAM image at that frame
// Paths given to these options must not contain spaces (nall rewrites such arguments).
//
// Original ares_ref description:
// ares_ref — headless ares (N64 core + paraLLEl-RDP) driver that produces reference frames for the port.
//
// Boot logic (paks, CIC detection, power-on) follows michiiik/N64Recomp ares-bridge/src/ares_core_glue.cpp.
// Adds what the bridge does not have yet: scripted controller input, VI frame capture and save states.
//
// Usage: ARES_REF_FIRMWARE=<ares>/mia/Firmware/Nintendo\ 64 ares_ref <rom.z64> <script> [--load state.bin]
// Script lines (frames are emulated VI frames, counted from boot or from the loaded state):
//   hold <from> <to> <BUTTONS>   hold buttons (A, B, Z, START, L, R, UP, DOWN, LEFT, RIGHT, CU, CD, CL, CR; joined
//                                with +) for frames from..to inclusive
//   tap <frame> <BUTTONS>         hold for 2 frames
//   shot <frame> <file.ppm>       save the picture shown at that frame
//   every <n> <dir>               save a picture every n frames as <dir>/f_<frame>.ppm (for finding a moment)
//   save <frame> <state.bin>      save the emulator state
//   dump <frame> <file>           write RDRAM (8 MB, big-endian byte order) to a file
//   fill <from> <to> <addr> <bytes> <u16 hex>   before each of those frames, fill RDRAM [addr, addr+bytes) with a
//                                halfword (experiments: what does the RDP read from there?)
//   end <frame>                   stop
#include <ares/ares.hpp>
#include <n64/n64.hpp>
#include <nall/main.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace ares;

namespace {

// ---- boot helpers (from ares-bridge) ----
uint64_t ipl2checksum(uint32_t seed, const uint8_t* rom) {
    auto rotl = [](uint32_t v, uint32_t s) -> uint32_t { return (v << s) | (v >> ((-s) & 31)); };
    auto rotr = [](uint32_t v, uint32_t s) -> uint32_t { return (v >> s) | (v << ((-s) & 31)); };
    auto csum = [](uint32_t a0, uint32_t a1, uint32_t a2) -> uint32_t {
        if (a1 == 0) a1 = a2;
        uint64_t prod = (uint64_t)a0 * a1;
        uint32_t diff = (uint32_t)(prod >> 32) - (uint32_t)prod;
        return diff ? diff : a0;
    };
    size_t cursor = 0;
    auto readm = [&]() -> uint32_t {
        uint32_t v = rom[cursor] << 24 | rom[cursor + 1] << 16 | rom[cursor + 2] << 8 | rom[cursor + 3];
        cursor += 4;
        return v;
    };
    uint32_t init = 0x6c078965 * (seed & 0xff) + 1;
    uint32_t data = readm();
    init ^= data;
    uint32_t state[16];
    for (auto& s : state) s = init;
    uint32_t dataNext = data, dataLast, loop = 0;
    while (true) {
        loop++;
        dataLast = data;
        data = dataNext;
        state[0] += csum(1007 - loop, data, loop);
        state[1] = csum(state[1], data, loop);
        state[2] ^= data;
        state[3] += csum(data + 5, 0x6c078965, loop);
        state[9] = (dataLast < data) ? csum(state[9], data, loop) : state[9] + data;
        state[4] += rotr(data, dataLast & 0x1f);
        state[7] = csum(state[7], rotl(data, dataLast & 0x1f), loop);
        state[6] = (data < state[6]) ? (state[3] + state[6]) ^ (data + loop) : (state[4] + data) ^ state[6];
        state[5] += rotl(data, dataLast >> 27);
        state[8] = csum(state[8], rotr(data, dataLast >> 27), loop);
        if (loop == 1008) break;
        dataNext = readm();
        state[15] = csum(csum(state[15], rotl(data, dataLast >> 27), loop), rotl(dataNext, data >> 27), loop);
        state[14] = csum(csum(state[14], rotr(data, dataLast & 0x1f), loop), rotr(dataNext, data & 0x1f), loop);
        state[13] += rotr(data, data & 0x1f) + rotr(dataNext, dataNext & 0x1f);
        state[10] = csum(state[10] + data, dataNext, loop);
        state[11] = csum(state[11] ^ data, dataNext, loop);
        state[12] += state[8] ^ data;
    }
    uint32_t buf[4];
    for (auto& b : buf) b = state[0];
    for (loop = 0; loop < 16; loop++) {
        uint32_t d = state[loop];
        uint32_t tmp = buf[0] + rotr(d, d & 0x1f);
        buf[0] = tmp;
        buf[1] = d < tmp ? buf[1] + d : csum(buf[1], d, loop);
        uint32_t tb = (d & 2) >> 1, t2 = d & 1;
        buf[2] = tb == t2 ? buf[2] + d : csum(buf[2], d, loop);
        buf[3] = t2 == 1 ? buf[3] ^ d : csum(buf[3], d, loop);
    }
    uint64_t checksum = (uint64_t)csum(buf[0], buf[1], 16) << 32;
    checksum |= buf[3] ^ buf[2];
    return checksum & 0xffffffffffffull;
}

std::string detectCic(const uint8_t* ipl3) {
    switch (ipl2checksum(0x3f, ipl3)) {
        case 0x45cc73ee317aull: return "CIC-NUS-6101";
        case 0x44160ec5d9afull: return "CIC-NUS-7102";
        case 0xa536c0f1d859ull: return "CIC-NUS-6102";
    }
    if (ipl2checksum(0x78, ipl3) == 0x586fd4709867ull) return "CIC-NUS-6103";
    if (ipl2checksum(0x91, ipl3) == 0x8618a45bc2d3ull) return "CIC-NUS-6105";
    if (ipl2checksum(0x85, ipl3) == 0x2bbad4e6eb74ull) return "CIC-NUS-6106";
    return "CIC-NUS-6102";
}

// ---- input / video ----
std::atomic<uint32_t> gButtons{ 0 };
enum : uint32_t { bA = 1, bB = 2, bZ = 4, bSTART = 8, bL = 16, bR = 32, bUP = 64, bDOWN = 128, bLEFT = 256,
                  bRIGHT = 512, bCU = 1024, bCD = 2048, bCL = 4096, bCR = 8192 };
const std::map<std::string, uint32_t> kButtonNames = {
    { "A", bA }, { "B", bB }, { "Z", bZ }, { "START", bSTART }, { "L", bL }, { "R", bR }, { "UP", bUP },
    { "DOWN", bDOWN }, { "LEFT", bLEFT }, { "RIGHT", bRIGHT }, { "CU", bCU }, { "CD", bCD }, { "CL", bCL },
    { "CR", bCR } };
const std::map<std::string, uint32_t> kNodeNames = {
    { "A", bA }, { "B", bB }, { "Z", bZ }, { "Start", bSTART }, { "L", bL }, { "R", bR }, { "Up", bUP },
    { "Down", bDOWN }, { "Left", bLEFT }, { "Right", bRIGHT }, { "C-Up", bCU }, { "C-Down", bCD },
    { "C-Left", bCL }, { "C-Right", bCR } };

std::mutex gVideoMutex;
std::vector<uint32_t> gPicture;
uint32_t gPicW = 0, gPicH = 0;
std::atomic<uint64_t> gPictures{ 0 };

struct RefPlatform : ares::Platform {
    std::shared_ptr<vfs::directory> systemPak, cartridgePak;
    auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
        if (!node) return {};
        string name = node->name();
        if (name == "Nintendo 64") return systemPak;
        if (name.endsWith("Cartridge")) return cartridgePak;
        return {};
    }
    auto status(string_view message) -> void override { fprintf(stderr, "[ares] %s\n", string{ message }.data()); }
    auto video(Node::Video::Screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
        std::lock_guard<std::mutex> lock(gVideoMutex);
        gPicture.resize((size_t)width * height);
        for (u32 y = 0; y < height; y++) {
            memcpy(&gPicture[(size_t)y * width], (const uint8_t*)data + (size_t)y * pitch, width * 4);
        }
        gPicW = width;
        gPicH = height;
        gPictures++;
    }
    auto input(Node::Input::Input node) -> void override {
        // only controller port 1's gamepad is driven
        if (auto button = node->cast<Node::Input::Button>()) {
            auto parent = node->parent().lock();
            auto port = parent ? parent->parent().lock() : nullptr;
            if (!port || string{ port->name() } != "Controller Port 1") return;
            auto it = kNodeNames.find(std::string{ string{ node->name() }.data() });
            button->setValue(it != kNodeNames.end() && (gButtons.load() & it->second));
        } else if (auto axis = node->cast<Node::Input::Axis>()) {
            axis->setValue(0);
        }
    }
} gPlatform;

bool savePicture(const std::string& path) {
    std::lock_guard<std::mutex> lock(gVideoMutex);
    if (gPicture.empty()) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "P6\n%u %u\n255\n", gPicW, gPicH);
    for (uint32_t p : gPicture) {
        uint8_t rgb[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

uint32_t parseButtons(const std::string& s) {
    uint32_t v = 0;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '+')) {
        auto it = kButtonNames.find(tok);
        if (it == kButtonNames.end()) {
            fprintf(stderr, "unknown button '%s'\n", tok.c_str());
            exit(2);
        }
        v |= it->second;
    }
    return v;
}

struct Hold { uint64_t from, to; uint32_t buttons; };
struct Fill { uint64_t from, to; uint32_t addr, bytes; uint16_t value; };
struct Action { std::string kind, arg; uint64_t n; };

} // namespace

static int realMain(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: ares_save <rom.z64> <script> [--load state.bin] [--flash-in f] [--flash-out f]\n");
        return 2;
    }
    std::string loadState, firmwareDir, flashIn, flashOut;
    for (int i = 3; i + 1 < argc; i++) {
        if (std::string{ argv[i] } == "--load") loadState = argv[i + 1];
        if (std::string{ argv[i] } == "--flash-in") flashIn = argv[i + 1];
        if (std::string{ argv[i] } == "--flash-out") flashOut = argv[i + 1];
    }
    // (an environment variable: nall rewrites path arguments)
    if (const char* fw = getenv("ARES_REF_FIRMWARE")) firmwareDir = fw;

    std::vector<Hold> holds;
    std::vector<Fill> fills;
    std::multimap<uint64_t, Action> actions;
    uint64_t endFrame = 0, everyN = 0;
    std::string everyDir;
    {
        std::ifstream in(argv[2]);
        std::string line;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::string cmd;
            if (!(ss >> cmd) || cmd[0] == '#') continue;
            if (cmd == "hold") { Hold h; std::string b; ss >> h.from >> h.to >> b; h.buttons = parseButtons(b); holds.push_back(h); }
            else if (cmd == "tap") { Hold h; std::string b; ss >> h.from >> b; h.to = h.from + 1; h.buttons = parseButtons(b); holds.push_back(h); }
            else if (cmd == "shot" || cmd == "save" || cmd == "dump" || cmd == "flashsave") { Action a; a.kind = cmd; ss >> a.n >> a.arg; actions.insert({ a.n, a }); }
            else if (cmd == "fill") { Fill f; std::string a, v; ss >> f.from >> f.to >> a >> f.bytes >> v; f.addr = std::stoul(a, nullptr, 16) & 0x7fffff; f.value = (uint16_t)std::stoul(v, nullptr, 16); fills.push_back(f); }
            else if (cmd == "every") { ss >> everyN >> everyDir; }
            else if (cmd == "end") { ss >> endFrame; }
            else { fprintf(stderr, "bad script line: %s\n", line.c_str()); return 2; }
        }
    }

    std::ifstream romIn(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(romIn)), std::istreambuf_iterator<char>());
    if (rom.size() < 0x1000 || rom[0] != 0x80) {
        fprintf(stderr, "need a big-endian .z64 ROM\n");
        return 2;
    }

    ares::platform = &gPlatform;
    gPlatform.systemPak = std::make_shared<vfs::directory>();
    // PIF boot firmware (ares ships it in mia/Firmware/Nintendo 64); without it the game never starts
    for (const char* name : { "pif.ntsc.rom", "pif.pal.rom", "pif.sm5.rom" }) {
        std::ifstream fin(firmwareDir + "/" + name, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            fprintf(stderr, "missing firmware %s/%s (set ARES_REF_FIRMWARE=<ares>/mia/Firmware/Nintendo 64)\n", firmwareDir.c_str(), name);
            return 2;
        }
        gPlatform.systemPak->append(name, bytes);
    }
    auto cart = std::make_shared<vfs::directory>();
    std::string title((const char*)&rom[0x20], 20);
    while (!title.empty() && (title.back() == ' ' || title.back() == 0)) title.pop_back();
    cart->setAttribute("title", string{ title.c_str() });
    cart->setAttribute("region", string{ "NTSC" });
    cart->setAttribute("cic", string{ detectCic(&rom[0x40]).c_str() });
    cart->append("program.rom", rom);
    {
        // Pokémon Stadium uses a 1 Mbit (128 KiB) FlashRAM; an erased chip reads 0xFF
        std::vector<uint8_t> flash(128 * 1024, 0xff);
        if (!flashIn.empty()) {
            std::ifstream fin(flashIn, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
            if (bytes.size() != flash.size()) {
                fprintf(stderr, "flash image %s must be %zu bytes (got %zu)\n", flashIn.c_str(), flash.size(), bytes.size());
                return 2;
            }
            flash = bytes;
            fprintf(stderr, "[ref] flash <- %s\n", flashIn.c_str());
        }
        cart->append("save.flash", flash);
    }
    gPlatform.cartridgePak = cart;
    auto writeFlash = [&](const std::string& path) {
        Nintendo64::cartridge.save();
        auto fp = cart->read("save.flash");
        if (!fp) { fprintf(stderr, "[ref] no flash\n"); return; }
        std::ofstream out(path, std::ios::binary);
        out.write((const char*)fp->data(), fp->size());
        fprintf(stderr, "[ref] flash -> %s\n", path.c_str());
    };

    Nintendo64::option("Enable GPU acceleration", "true");
    Nintendo64::option("Deterministic Entropy", "true");
    Nintendo64::option("Expansion Pak", "true");
    Nintendo64::option("Quality", "SD");
    Nintendo64::option("Recompiler", "true");
    Node::System root;
    if (!Nintendo64::load(root, "[Nintendo] Nintendo 64 (NTSC)")) {
        fprintf(stderr, "Nintendo64::load failed\n");
        return 1;
    }
    if (auto port = root->find<Node::Port>("Cartridge Slot")) { port->allocate(); port->connect(); }
    if (auto port = root->find<Node::Port>("Controller Port 1")) { port->allocate("Gamepad"); port->connect(); }
    Nintendo64::system.power(false);

    if (!loadState.empty()) {
        std::ifstream sin(loadState, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(sin)), std::istreambuf_iterator<char>());
        serializer s(data.data(), (u32)data.size());
        if (!Nintendo64::system.unserialize(s)) {
            fprintf(stderr, "could not load state %s\n", loadState.c_str());
            return 1;
        }
        fprintf(stderr, "[ref] loaded state %s\n", loadState.c_str());
    }

    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t frame = 1; !endFrame || frame <= endFrame; frame++) {
        uint32_t b = 0;
        for (auto& h : holds) {
            if (frame >= h.from && frame <= h.to) b |= h.buttons;
        }
        gButtons = b;
        for (auto& f : fills) {
            if (frame < f.from || frame > f.to) continue;
            auto& ram = Nintendo64::rdram.ram;
            for (u32 i = 0; i + 2 <= f.bytes; i += 2) {
                u32 a = (f.addr + i) & ~3u;
                u32 w = ram.Nintendo64::Memory::Writable::read<Nintendo64::Word>(a);
                if (((f.addr + i) & 2) == 0) w = (w & 0xffff) | ((u32)f.value << 16);
                else w = (w & 0xffff0000) | f.value;
                ram.Nintendo64::Memory::Writable::write<Nintendo64::Word>(a, w);
            }
        }
        uint64_t before = gPictures.load();
        Nintendo64::system.run();
        bool wantShot = actions.count(frame) || (everyN && frame % everyN == 0);
        if (getenv("REF_DEBUG") && frame % 30 == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); fprintf(stderr, "[ref] frame %llu pictures %llu\n", (unsigned long long)frame, (unsigned long long)gPictures.load()); }
        if (wantShot) { // the screen thread delivers the picture shortly after the frame
            for (int i = 0; i < 200 && gPictures.load() == before; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (everyN && frame % everyN == 0) {
            char name[64];
            snprintf(name, sizeof(name), "/f_%06llu.ppm", (unsigned long long)frame);
            savePicture(everyDir + name);
        }
        auto range = actions.equal_range(frame);
        for (auto it = range.first; it != range.second; ++it) {
            const Action& a = it->second;
            if (a.kind == "shot") {
                fprintf(stderr, "[ref] frame %llu -> %s (%s)\n", (unsigned long long)frame, a.arg.c_str(), savePicture(a.arg) ? "ok" : "no picture");
            } else if (a.kind == "dump") {
                auto& ram = Nintendo64::rdram.ram;
                std::vector<uint8_t> buf(ram.size);
                for (u32 i = 0; i + 4 <= ram.size; i += 4) {
                    u32 w = ram.Nintendo64::Memory::Writable::read<Nintendo64::Word>(i);
                    buf[i] = w >> 24; buf[i + 1] = w >> 16; buf[i + 2] = w >> 8; buf[i + 3] = w;
                }
                std::ofstream out(a.arg, std::ios::binary);
                out.write((const char*)buf.data(), buf.size());
                fprintf(stderr, "[ref] frame %llu RDRAM -> %s\n", (unsigned long long)frame, a.arg.c_str());
            } else if (a.kind == "flashsave") {
                writeFlash(a.arg);
            } else if (a.kind == "save") {
                serializer s = Nintendo64::system.serialize();
                std::ofstream out(a.arg, std::ios::binary);
                out.write((const char*)s.data(), s.size());
                fprintf(stderr, "[ref] frame %llu state -> %s (%u bytes)\n", (unsigned long long)frame, a.arg.c_str(), (unsigned)s.size());
            }
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "[ref] %llu frames in %.1f s, %llu pictures\n", (unsigned long long)endFrame, secs, (unsigned long long)gPictures.load());
    if (!flashOut.empty()) writeFlash(flashOut);
    Nintendo64::system.unload();
    return 0;
}

// nall provides the process entry point and calls nall::main with the arguments (without the program path).
auto nall::main(nall::Arguments arguments) -> void {
    std::vector<std::string> args{ "ares_ref" };
    for (auto& a : arguments) args.push_back(a.data());
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    exit(realMain((int)argv.size(), argv.data()));
}
