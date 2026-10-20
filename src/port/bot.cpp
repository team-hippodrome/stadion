// Autoplay bot channel (tools/autoplay.py): PORT_BOT=<unix socket path> opens a socket the bot connects to. The game
// reports through hooks (port_compat.h) whether a battle runs, what player 1 has to decide and where the team-order
// panel is; the bot reads that with "state" and answers with button presses on controller 1, which are added to the
// live (or replayed) input frame by frame (Port_BotApplyPad from os_devices.cpp). One line per request:
//   state                  -> one JSON line (see BuildState)
//   press <buttons> <n>    -> hold the buttons (N64 bit mask) for n frames, then release them for 2; answers "ok <frame>"
//   wait <n>               -> answers "ok <frame>" after n frames
// Not available on Windows (no unix sockets used there).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

extern "C" unsigned long long Port_ViTick(void);
extern "C" void Port_NoteDecision(void);

namespace {
std::mutex gLock;
bool gEnabled = false;
uint64_t gBattleTick = 0;     // last battle frame
bool gBattleSeen = false;
int gBattleResult = 0;        // the battle's result (Port_BotBattleFrame)
int gOwnSide = -1;            // player 1's side in the battle
struct Choice {
    bool seen = false;
    uint64_t tick = 0;
    std::vector<uint8_t> own, opp;
    std::vector<std::vector<uint8_t>> party;
    int active = 0, menu = 0, sub = 0, forced = 0, side = 0;
} gChoice;
struct Order {
    bool seen = false;
    uint64_t tick = 0;
    int sub = 0, picked = 0;
} gOrder;
std::vector<std::vector<uint8_t>> gOrderOwn, gOrderOpp; // both teams on the team-order screen
bool gRentalSeen = false;
uint64_t gRentalTick = 0;
int gRentalSpecies = 0;
uint64_t gRentalConfirmTick = 0;
bool gRentalConfirmSeen = false;
std::atomic<uint16_t> gPressButtons{ 0 };
std::atomic<uint64_t> gPressUntil{ 0 }; // buttons held while the frame is below this

constexpr size_t kRuntimeSize = 0x64; // BattleMonRuntime (game/src/3D140.h)

std::string Hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

std::string BuildState() {
    std::lock_guard<std::mutex> g(gLock);
    const uint64_t now = Port_ViTick();
    char buf[96];
    std::string s = "{\"tick\":" + std::to_string(now);
    snprintf(buf, sizeof buf, ",\"battle_age\":%lld,\"result\":%d,\"side\":%d",
             gBattleSeen ? (long long)(now - gBattleTick) : -1LL, gBattleResult, gOwnSide);
    s += buf;
    if (gChoice.seen) {
        s += ",\"choice\":{\"age\":" + std::to_string(now - gChoice.tick) + ",\"active\":" + std::to_string(gChoice.active) +
             ",\"menu\":" + std::to_string(gChoice.menu) + ",\"sub\":" + std::to_string(gChoice.sub) +
             ",\"forced\":" + std::to_string(gChoice.forced) + ",\"own\":\"" + Hex(gChoice.own) + "\",\"opp\":\"" +
             Hex(gChoice.opp) + "\",\"party\":[";
        for (size_t i = 0; i < gChoice.party.size(); i++) {
            s += (i ? ",\"" : "\"") + Hex(gChoice.party[i]) + "\"";
        }
        s += "]}";
    }
    if (gRentalSeen) {
        s += ",\"rental_age\":" + std::to_string(now - gRentalTick) + ",\"rental_species\":" +
             std::to_string(gRentalSpecies);
    }
    if (gRentalConfirmSeen) {
        s += ",\"rental_confirm_age\":" + std::to_string(now - gRentalConfirmTick);
    }
    if (gOrder.seen) {
        s += ",\"order\":{\"age\":" + std::to_string(now - gOrder.tick) + ",\"sub\":" + std::to_string(gOrder.sub) +
             ",\"picked\":" + std::to_string(gOrder.picked);
        for (int t = 0; t < 2; t++) {
            const auto& team = t == 0 ? gOrderOwn : gOrderOpp;
            s += t == 0 ? ",\"own\":[" : ",\"opp\":[";
            for (size_t i = 0; i < team.size(); i++) {
                s += (i ? ",\"" : "\"") + Hex(team[i]) + "\"";
            }
            s += "]";
        }
        s += "}";
    }
    s += "}\n";
    return s;
}

void WaitUntil(uint64_t tick) {
    while (Port_ViTick() < tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

#if !defined(_WIN32)
void Serve(std::string path) {
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path.c_str());
    unlink(path.c_str());
    if (srv < 0 || bind(srv, (sockaddr*)&addr, sizeof addr) != 0 || listen(srv, 1) != 0) {
        fprintf(stderr, "[bot] cannot listen on %s\n", path.c_str());
        return;
    }
    fprintf(stderr, "[bot] listening on %s\n", path.c_str());
    for (;;) {
        int c = accept(srv, nullptr, nullptr);
        if (c < 0) {
            continue;
        }
        std::string line;
        char ch;
        while (read(c, &ch, 1) == 1) {
            if (ch != '\n') {
                line += ch;
                continue;
            }
            std::string reply;
            unsigned buttons = 0, n = 0;
            if (line == "state") {
                reply = BuildState();
            } else if (sscanf(line.c_str(), "press %u %u", &buttons, &n) == 2) {
                const uint64_t start = Port_ViTick() + 1;
                gPressButtons = (uint16_t)buttons;
                gPressUntil = start + n;
                WaitUntil(start + n + 2);
                reply = "ok " + std::to_string(Port_ViTick()) + "\n";
            } else if (sscanf(line.c_str(), "wait %u", &n) == 1) {
                WaitUntil(Port_ViTick() + n);
                reply = "ok " + std::to_string(Port_ViTick()) + "\n";
            } else {
                reply = "error unknown request\n";
            }
            if (write(c, reply.data(), reply.size()) < 0) {
                break;
            }
            line.clear();
        }
        close(c);
        gPressUntil = 0;
    }
}
#endif

bool Enabled() {
    static const bool on = [] {
        const char* p = getenv("PORT_BOT");
        if (p == nullptr || *p == '\0') {
            return false;
        }
#if defined(_WIN32)
        fprintf(stderr, "[bot] PORT_BOT is not supported on Windows\n");
        return false;
#else
        std::thread(Serve, std::string(p)).detach();
        return true;
#endif
    }();
    return on;
}
} // namespace

// Adds the bot's held buttons to controller 1's state for this frame (os_devices.cpp, LatchPads).
extern "C" void Port_BotApplyPad(unsigned long long tick, uint16_t* button) {
    if (Enabled() && tick < gPressUntil.load()) {
        *button |= gPressButtons.load();
    }
}

extern "C" void Port_BotBattleFrame(int result) {
    if (!Enabled()) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    gBattleTick = Port_ViTick();
    gBattleSeen = true;
    gBattleResult = result; // kept after the battle: the bot reads it once the battle screen is gone
}

extern "C" void Port_BotBattleChoice(const void* own, const void* opp, const void* party, int partyCount, int partyStride,
                                     int activeSlot, int menu, int sub, int forced, int side) {
    if ((menu == 0 && sub == 0) || forced) {
        Port_NoteDecision(); // the in-game "skip to the next decision" stops here (os_devices.cpp)
    }
    if (!Enabled()) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    gChoice.seen = true;
    gChoice.tick = Port_ViTick();
    gChoice.own.assign((const uint8_t*)own, (const uint8_t*)own + kRuntimeSize);
    gChoice.opp.assign((const uint8_t*)opp, (const uint8_t*)opp + kRuntimeSize);
    gChoice.party.clear();
    for (int i = 0; i < partyCount && i < 6; i++) {
        const uint8_t* p = (const uint8_t*)party + (size_t)i * partyStride;
        gChoice.party.emplace_back(p, p + partyStride);
    }
    gChoice.active = activeSlot;
    gChoice.menu = menu;
    gChoice.sub = sub;
    gChoice.forced = forced;
    gChoice.side = side;
    gOwnSide = side;
}

extern "C" void Port_BotOrderTeams(const void* own, int ownCount, const void* opp, int oppCount, int stride) {
    if (!Enabled()) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    for (int t = 0; t < 2; t++) {
        auto& team = t == 0 ? gOrderOwn : gOrderOpp;
        const uint8_t* base = (const uint8_t*)(t == 0 ? own : opp);
        const int count = t == 0 ? ownCount : oppCount;
        team.clear();
        for (int i = 0; i < count && i < 6; i++) {
            team.emplace_back(base + (size_t)i * stride, base + (size_t)i * stride + stride);
        }
    }
}

extern "C" void Port_BotRentalConfirm(void) {
    if (!Enabled()) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    gRentalConfirmSeen = true;
    gRentalConfirmTick = Port_ViTick();
}

extern "C" void Port_BotRental(int species) {
    static bool told = false;
    if (!told) { // where a route recording for random rental teams ends (tools/autoplay.py --random-team)
        told = true;
        fprintf(stderr, "[bot] rental carousel first waits for input at frame %llu\n", Port_ViTick());
    }
    if (!Enabled()) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    gRentalSeen = true;
    gRentalTick = Port_ViTick();
    gRentalSpecies = species;
}

extern "C" void Port_BotOrderSelect(int sub, int picked) {
    if (sub >= 0) {
        Port_NoteDecision();
    }
    if (!Enabled()) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    gOrder.seen = true;
    gOrder.tick = Port_ViTick();
    gOrder.sub = sub;
    gOrder.picked = picked;
}
