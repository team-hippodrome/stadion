// Frame interpolation: scenes the game draws at 30 or 20 fps are shown at the display's refresh rate.
//
// The game keeps its own rate; nothing here changes game state or the lockstep clock. While the game builds a frame,
// every matrix it puts into the display list (gSPMatrix) is recorded with an id that is stable from frame to frame: the
// call site, the scene-graph node being drawn (geo_render.c) and how many matrices that site/node pair produced before
// in the frame. When the frame's display list reaches the window thread it is drawn k times, once per display refresh
// until the next game frame is due (k = retraces between game frames x refreshes per retrace): the first k-1 times
// with matrices blended between the previous game frame and this one (Fast3D's matrix replacement map, as in Ship of
// Harkinian), the last time exactly as built. The picture is therefore one game frame behind, like SoH's.
//
// Drawn again later: only display lists that repaint the whole screen (a full-screen fill rectangle; menus that draw
// over the previous picture would pile up), and only while the game's memory maps stay as they were (a scene that
// unloads its data). The display list itself is safe: the game builds the next frame in its other buffer and waits for
// that frame's task before reusing this one. Render targets drawn by the list (UI text, portraits) use the game's own
// matrices, and the in-between passes write nothing back to game memory (Interpreter::mReplayPass).
//
// PORT_INTERPOLATE=1/0 overrides the launcher setting (gPort.Interpolate). Debug: PORT_INTERP_LOG=1 prints per-frame
// matching statistics; PORT_INTERP_IMMEDIATE=1 draws the in-between frames right after the frame instead of on later
// refreshes (hidden-window and max-speed runs); PORT_INTERP_SHOTS=<first>-<last> saves every frame drawn for those game
// frames as interp_<frame>_<i>of<k>.ppm in PORT_SCREENSHOT_DIR.
#include "interp.h"
#include "Engine.h"
#include "pacing.h"

#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" uint64_t Port_ViTick();
extern "C" double Port_Speed();
extern "C" bool Port_RspTaskPending();
extern "C" int64_t Port_ClockLagNs();

namespace {
using Replacements = std::unordered_map<Mtx*, MtxF>;

struct Tag {
    const void* mtx;
    uint64_t id, group; // group: call site and node; id: group and the matrix's number in it
};

struct Frame {
    const void* dl = nullptr;
    std::vector<Tag> tags;                       // game thread, while the frame is built
    std::unordered_map<uint64_t, MtxF> byId;     // when finished: the matrices' values
    std::unordered_map<uint64_t, uint32_t> sizes; // matrices per group
    std::vector<Tag> mats;
};

std::mutex gLock;
std::shared_ptr<Frame> gBuilding;                 // game thread
std::deque<std::shared_ptr<Frame>> gFinished;     // waiting for their task
std::unordered_map<uint64_t, uint32_t> gSiteUses; // game thread: matrices per (site, node) in this frame
const void* gNode = nullptr;
std::atomic<bool> gRecording{ false };
std::atomic<uint64_t> gMapGen{ 0 };

// window thread
std::shared_ptr<Frame> gPrev;
uint64_t gPrevTick = 0;
struct Pending {
    void* dl = nullptr;
    std::shared_ptr<Frame> a, b;
    int k = 0, next = 0;
    int64_t start = 0, period = 0, vblank = 0;
    bool grid = false;
    bool restore = false; // drawn from a copy of the previous picture (a menu): the exact drawing must happen
    uint64_t mapGen = 0, tick = 0;
} gPending;
bool gHavePending = false;

uint64_t Mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

// The game's matrices in the port's layout (MtxF_ToFixed, guMtxF2L): 16.16 fixed point, 32-bit words holding two
// integer parts, then two fraction parts (as Interpreter::GfxSpMatrix reads them).
MtxF ReadFixed(const void* p) {
    const int32_t* addr = (const int32_t*)p;
    MtxF m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t intPart = addr[i * 2 + j / 2];
            uint32_t fracPart = (uint32_t)addr[8 + i * 2 + j / 2];
            m.mf[i][j] = (int32_t)(((uint32_t)intPart & 0xffff0000u) | (fracPart >> 16)) / 65536.0f;
            m.mf[i][j + 1] = (int32_t)(((uint32_t)intPart << 16) | (fracPart & 0xffff)) / 65536.0f;
        }
    }
    return m;
}

void Finish(Frame& f) {
    f.byId.reserve(f.tags.size());
    for (auto& t : f.tags) {
        f.byId[t.id] = ReadFixed(t.mtx);
        f.sizes[t.group]++;
    }
    f.mats = std::move(f.tags);
    f.tags.clear();
}

// The finished record of the frame whose display list starts at dl (window thread).
std::shared_ptr<Frame> Take(const void* dl) {
    std::lock_guard<std::mutex> g(gLock);
    if (gBuilding && gBuilding->dl == dl) {
        auto f = gBuilding;
        gBuilding = nullptr;
        Finish(*f);
        gFinished.clear();
        return f;
    }
    for (auto it = gFinished.rbegin(); it != gFinished.rend(); ++it) {
        if ((*it)->dl == dl) {
            auto f = *it;
            gFinished.erase(gFinished.begin(), it.base()); // this one and older ones
            return f;
        }
    }
    return nullptr;
}

bool Affine(const MtxF& m) {
    return std::fabs(m.mf[0][3]) < 1e-4f && std::fabs(m.mf[1][3]) < 1e-4f && std::fabs(m.mf[2][3]) < 1e-4f &&
           std::fabs(m.mf[3][3] - 1.0f) < 1e-4f;
}

struct Stats {
    int mats = 0, matched = 0, blended = 0, cuts = 0, regrouped = 0;
    float maxMove = 0;
    int cutTurn = 0, cutMove = 0, cutProj = 0;
};

float JumpLimit() {
    static const float v = getenv("PORT_INTERP_JUMP") ? (float)atof(getenv("PORT_INTERP_JUMP")) : 300.0f;
    return v;
}

// a blended between b's previous value a and b; false: leave b as it is (no change, or a jump not to be smoothed)
bool Blend(const MtxF& a, const MtxF& b, float t, MtxF* out, Stats* st) {
    if (memcmp(&a, &b, sizeof(MtxF)) == 0) return false;
    bool affA = Affine(a), affB = Affine(b);
    if (affA != affB) return false;
    if (!affA) { // a projection: element-wise, unless it changed a lot (another camera)
        for (int i = 0; i < 2; i++) {
            float x = a.mf[i][i], y = b.mf[i][i];
            if (std::fabs(x - y) > 0.5f * std::max(std::fabs(x), std::fabs(y))) {
                st->cuts++;
                st->cutProj++;
                return false;
            }
        }
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) out->mf[i][j] = a.mf[i][j] + (b.mf[i][j] - a.mf[i][j]) * t;
        return true;
    }
    // rows 0-2 are the images of the axes (direction and length blended separately, so a turning object keeps its
    // size); row 3 is the translation
    for (int r = 0; r < 3; r++) {
        float la = std::sqrt(a.mf[r][0] * a.mf[r][0] + a.mf[r][1] * a.mf[r][1] + a.mf[r][2] * a.mf[r][2]);
        float lb = std::sqrt(b.mf[r][0] * b.mf[r][0] + b.mf[r][1] * b.mf[r][1] + b.mf[r][2] * b.mf[r][2]);
        if (la < 1e-6f || lb < 1e-6f) {
            for (int c = 0; c < 3; c++) out->mf[r][c] = a.mf[r][c] + (b.mf[r][c] - a.mf[r][c]) * t;
            continue;
        }
        float d[3], dot = 0;
        for (int c = 0; c < 3; c++) dot += (a.mf[r][c] / la) * (b.mf[r][c] / lb);
        if (dot < 0.5f) { // turned by more than 60 degrees in one game frame: a cut or a reused node
            st->cuts++;
            st->cutTurn++;
            return false;
        }
        float n = 0;
        for (int c = 0; c < 3; c++) {
            d[c] = a.mf[r][c] / la + (b.mf[r][c] / lb - a.mf[r][c] / la) * t;
            n += d[c] * d[c];
        }
        n = std::sqrt(n);
        float l = la + (lb - la) * t;
        for (int c = 0; c < 3; c++) out->mf[r][c] = d[c] / n * l;
    }
    float move = 0, ta = 0, tb = 0;
    for (int c = 0; c < 3; c++) {
        float dlt = b.mf[3][c] - a.mf[3][c];
        move += dlt * dlt;
        ta += a.mf[3][c] * a.mf[3][c];
        tb += b.mf[3][c] * b.mf[3][c];
    }
    move = std::sqrt(move);
    st->maxMove = std::max(st->maxMove, move);
    if (move > JumpLimit() && move > 0.5f * std::sqrt(std::max(ta, tb))) { // moved far, relative to where it is
        st->cuts++;
        st->cutMove++;
        static const bool logJumps = getenv("PORT_INTERP_LOG") && atoi(getenv("PORT_INTERP_LOG")) >= 2;
        if (logJumps) {
            fprintf(stderr, "[interp]   jump %.1f from %.1f %.1f %.1f to %.1f %.1f %.1f\n", move, a.mf[3][0], a.mf[3][1],
                    a.mf[3][2], b.mf[3][0], b.mf[3][1], b.mf[3][2]);
        }
        return false;
    }
    for (int c = 0; c < 3; c++) out->mf[3][c] = a.mf[3][c] + (b.mf[3][c] - a.mf[3][c]) * t;
    for (int r = 0; r < 4; r++) out->mf[r][3] = b.mf[r][3];
    return true;
}

void Build(const Frame& a, const Frame& b, float t, Replacements& out, Stats* st) {
    Stats local;
    if (st == nullptr) st = &local;
    for (auto& tag : b.mats) {
        st->mats++;
        auto ia = a.byId.find(tag.id);
        if (ia == a.byId.end()) continue;
        st->matched++;
        // a site drawing a different number of matrices (particles coming and going): the n-th one may be another
        // object than in the previous frame
        auto sa = a.sizes.find(tag.group);
        if (sa == a.sizes.end() || sa->second != b.sizes.at(tag.group)) {
            st->regrouped++;
            continue;
        }
        MtxF m;
        if (Blend(ia->second, b.byId.at(tag.id), t, &m, st)) {
            out[(Mtx*)tag.mtx] = m;
            st->blended++;
        }
    }
}

bool Enabled() {
    static const char* env = getenv("PORT_INTERPOLATE");
    bool on = env ? atoi(env) != 0 : CVarGetInteger("gPort.Interpolate", 0) != 0;
    // only at normal speed (fast runs draw every n-th frame), unless the in-between frames are drawn at once
    static const bool immediate = getenv("PORT_INTERP_IMMEDIATE") != nullptr;
    return on && (Port_Speed() == 1.0 || immediate);
}

bool Logging() {
    static const bool v = getenv("PORT_INTERP_LOG") != nullptr;
    return v;
}

const char* ShotDir() {
    static const char* dir = getenv("PORT_SCREENSHOT_DIR");
    return dir;
}

bool ShotWanted(uint64_t tick) {
    static uint64_t first = 1, last = 0;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char* e = getenv("PORT_INTERP_SHOTS")) {
            unsigned long long a = 0, b = 0;
            if (sscanf(e, "%llu-%llu", &a, &b) == 2) {
                first = a;
                last = b;
            }
        }
    }
    return tick >= first && tick <= last && ShotDir() != nullptr;
}

void Shot(uint64_t tick, int i, int k) {
    if (!ShotWanted(tick)) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/interp_%05llu_%dof%d.ppm", ShotDir(), (unsigned long long)tick,
             i + 1, k);
    Port::SaveScreen(path);
}

int64_t Due(const Pending& p, int j) {
    if (!p.grid) return p.start + j * p.period;
    // after the vblank j refreshes after the one the frame was drawn in (1 ms in, like the retraces: 0.5 ms)
    int64_t r = (p.start - p.vblank) / p.period - ((p.start - p.vblank) % p.period < 0 ? 1 : 0);
    return p.vblank + (r + j) * p.period + 1000000;
}

std::shared_ptr<Fast::Interpreter> Interp() {
    return Port::GetWindow()->GetInterpreterWeak().lock();
}

// No more in-between passes of the pending frame: the renderer gets the state the frame's first drawing left.
void EndPending() {
    if (gHavePending) {
        if (auto interp = Interp()) interp->ReplayEnd();
    }
    gHavePending = false;
    gPending = Pending();
}

void DrawNext();

// Drops the in-between frames still due. A frame drawn over the previous picture (Pending::restore) has its exact
// drawing done first: the game frame buffer would otherwise keep a blended picture that later frames draw over. Not
// when the game has unloaded data the display list may use (the blended picture stays, as for other frames).
void FinishPending() {
    if (gHavePending && gPending.restore && gPending.next < gPending.k && gMapGen.load() == gPending.mapGen) {
        gPending.next = gPending.k - 1;
        DrawNext(); // (ends the pending frame)
        return;
    }
    EndPending();
}

void DrawNext() {
    Pending& p = gPending;
    int j = p.next++;
    Replacements r;
    if (j + 1 < p.k) Build(*p.a, *p.b, (float)(j + 1) / p.k, r, nullptr);
    if (auto interp = Interp()) {
        interp->ReplayPassStart();
        interp->mReplayT = j + 1 < p.k ? (float)(j + 1) / p.k : 1.0f; // (2D rectangles: interpreter.cpp)
    }
    Port::RunFrameWith(p.dl, &r, true, ShotWanted(p.tick));
    Shot(p.tick, j, p.k);
    if (p.next >= p.k) EndPending();
}
} // namespace

extern "C" void Port_InterpBeginFrame(const void* dlStart) {
    bool on = gRecording.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> g(gLock);
    if (gBuilding && !gBuilding->tags.empty()) {
        Finish(*gBuilding);
        gFinished.push_back(gBuilding);
        while (gFinished.size() > 4) gFinished.pop_front();
    }
    gBuilding = on ? std::make_shared<Frame>() : nullptr;
    if (gBuilding) gBuilding->dl = dlStart;
    gSiteUses.clear();
    gNode = nullptr;
}

extern "C" void Port_InterpNode(const void* node) {
    gNode = node;
}

extern "C" void Port_InterpTagMtx(const void* mtx, const void* site) {
    if (!gRecording.load(std::memory_order_relaxed) || (uintptr_t)mtx < 0x100000000ull) return;
    std::lock_guard<std::mutex> g(gLock);
    if (!gBuilding) return;
    uint64_t key = Mix(Mix(0, (uint64_t)(uintptr_t)site), (uint64_t)(uintptr_t)gNode);
    uint32_t n = gSiteUses[key]++;
    gBuilding->tags.push_back({ mtx, Mix(key, n), key });
}

extern "C" void Port_InterpMapsChanged(void) {
    gMapGen.fetch_add(1, std::memory_order_relaxed);
}

namespace Port {
void CancelInterpolation() {
    FinishPending();
    gPrev = nullptr;
}

void RunGameFrame(void* gfxCommands, bool exact) {
    bool on = Enabled();
    gRecording = on;
    FinishPending(); // a newer frame: in-between frames still due are dropped
    if (!on) {
        gPrev = nullptr;
        RunFrame(gfxCommands);
        return;
    }
    static const bool immediate = getenv("PORT_INTERP_IMMEDIATE") != nullptr;
    auto cur = Take(gfxCommands);
    uint64_t tick = Port_ViTick();
    int64_t period = 0, vblank = 0;
    bool grid = !immediate && Port_DisplayVblank(&period, &vblank) && period > 1000000;
    int perRetrace = grid ? std::max(1, (int)std::lround(1e9 / 60.0 / period)) : 1;
    int k = 0;
    // The game draws every 2nd or 3rd retrace. A longer gap (a hitch, or a fast run drawing every n-th frame) is not
    // blended: over it many of a model's matrices turn too far and are cut while the ones skinned to them still blend,
    // and limbs stretch across the screen (Clefairy in the title's fall, blended from 20 retraces back).
    const uint64_t kMaxGap = 4;
    if (cur && gPrev && !exact && tick > gPrevTick && tick - gPrevTick <= kMaxGap) {
        k = (int)std::min<uint64_t>((tick - gPrevTick) * perRetrace, 8);
    }
    Stats st;
    Replacements r;
    bool repaint = false, restore = false;
    auto interpreter = Interp();
    if (k >= 2 && interpreter) {
        repaint = interpreter->FrameRepaintsScreen((Gfx*)gfxCommands);
        // a frame that draws over the previous picture (menus) is drawn again from a copy of that picture
        restore = !repaint && interpreter->CanRestorePicture();
    }
    if (k >= 2 && (repaint || restore)) {
        Build(*gPrev, *cur, 1.0f / k, r, &st);
    }
    if (Logging()) {
        fprintf(stderr, "[interp] frame %llu: %s k %d repaint %d mats %d matched %d regrouped %d blended %d cuts %d (turn %d "
                "move %d projection %d) max move %.1f\n",
                (unsigned long long)tick, cur ? "recorded" : "no record", k, (int)repaint, st.mats, st.matched,
                st.regrouped, st.blended, st.cuts, st.cutTurn, st.cutMove, st.cutProj, st.maxMove);
    }
    if (k >= 2 && (repaint || restore)) {
        gPending.dl = gfxCommands;
        gPending.a = gPrev;
        gPending.b = cur;
        gPending.k = k;
        gPending.next = 1;
        gPending.start = Port_SteadyNs();
        gPending.period = grid ? period : 16666667;
        gPending.vblank = vblank;
        gPending.grid = grid;
        gPending.mapGen = gMapGen.load();
        gPending.tick = tick;
        gPending.restore = restore;
        interpreter->Interp2DPrepare((Gfx*)gfxCommands);
        interpreter->ReplayBegin(restore);
        interpreter->mReplayT = 1.0f / k;
        RunFrameWith(gfxCommands, &r, false);
        Shot(tick, 0, k);
        gHavePending = true;
        if (immediate) {
            while (gHavePending) DrawNext();
        }
    } else {
        if (interpreter) interpreter->mReplayT = 1.0f;
        RunFrame(gfxCommands);
        Shot(tick, 0, 1);
    }
    gPrev = cur;
    gPrevTick = tick;
}

void PumpInterpolation() {
    if (!gHavePending) return;
    if (gMapGen.load() != gPending.mapGen) { // the game unloaded or moved data the display list may use
        EndPending();
        return;
    }
    // An in-between frame never holds up the game: when the game already waits for its next frame to be drawn, or
    // game time lags behind real time (a slow GPU), the in-between frames still due are dropped.
    if (Port_RspTaskPending() || Port_ClockLagNs() > 4000000) {
        FinishPending();
        return;
    }
    if (Port_SteadyNs() < Due(gPending, gPending.next)) return;
    DrawNext();
}
} // namespace Port
