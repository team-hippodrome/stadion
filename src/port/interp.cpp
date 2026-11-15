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
#include "StadionPort.h"

extern "C" double Port_Speed();
extern "C" bool Port_RspTaskPending();
extern "C" int64_t Port_ClockLagNs();

namespace {
using Replacements = std::unordered_map<Mtx*, MtxF>;

struct Tag {
    const void* mtx;
    uint64_t group; // call site and scene-graph node (Port_InterpTagMtx)
};

// One matrix of a finished frame: where the display list reads it, its group, its number in the group, its value
struct Mat {
    const void* mtx;
    uint64_t group;
    uint32_t index;
    MtxF v;
};

struct Frame {
    const void* dl = nullptr;
    uint64_t serial = 0;                                        // build order (frames the game submits again)
    std::vector<Tag> tags;                                      // game thread, while the frame is built
    std::vector<Mat> mats;                                      // when finished
    std::unordered_map<uint64_t, std::vector<uint32_t>> groups; // group -> indices into mats, in drawing order
};

// What the in-between pictures of one game frame draw: for every recorded matrix of the newer frame its partner in the
// older one (matched once per frame, see Plan()), or none (drawn as the game built it)
struct PlanEntry {
    Mtx* mtx;
    MtxF a, b;
    bool blend;
};

std::mutex gLock;
std::shared_ptr<Frame> gBuilding;                 // game thread
std::deque<std::shared_ptr<Frame>> gFinished;     // waiting for their task
const void* gNode = nullptr;
uint64_t gSerial = 0;
std::atomic<bool> gRecording{ false };
std::atomic<uint64_t> gMapGen{ 0 };

// window thread
std::shared_ptr<Frame> gPrev;
uint64_t gPrevTick = 0;
struct Pending {
    void* dl = nullptr;
    std::shared_ptr<Frame> a, b;
    std::vector<PlanEntry> plan;
    int k = 0, next = 0;
    int64_t start = 0, period = 0, vblank = 0;
    bool grid = false;
    bool restore = false; // drawn from a copy of the previous picture (a menu): the exact drawing must happen
    uint64_t mapGen = 0, tick = 0;
    int64_t frameDue = 0; // when the next game frame is expected (steady clock)
} gPending;
bool gHavePending = false;

// How many pictures a game frame may get (window thread). Every in-between picture is a full draw; on a GPU that
// needs ~25 ms per picture (an Intel HD 520), two pictures per 33 ms game frame do not fit, game time falls behind,
// the catch-up (PumpRspTasks) skips whole game frames, and the screen freezes and then jumps several frames ahead
// ("fast-forward"; measured with PORT_DEBUG_DRAW_MS=25: 92 freezes over 50 ms and 37 jumps in 15 s, none without
// Smooth motion). So the pictures are budgeted: when a series overran (a picture finished after the next game frame
// was due, the first picture alone took two refreshes, or pictures were dropped in 3 frames in a row), the next frames
// get one picture
// each, exactly as without Smooth motion, for a hold time that doubles on each new overrun (2 s up to 5 min) and halves
// again after long calm stretches; then in-between pictures are tried again.
struct Budget {
    int maxPictures = 8;
    uint64_t hold = 0;     // game frames left at one picture
    uint64_t backoff = 60; // the next hold
    uint64_t calm = 0;     // game frames at full budget without an overrun
    int dropsInRow = 0;    // game frames in a row whose last pictures were dropped
    bool overran = false;
    uint64_t overruns = 0;
} gBudget;

bool BudgetDisabled() {
    static const bool v = getenv("PORT_INTERP_IMMEDIATE") != nullptr ||
                          (getenv("PORT_INTERP_BUDGET") && atoi(getenv("PORT_INTERP_BUDGET")) == 0);
    return v;
}

void NoteOverrun(const char* why);

// A late picture counts as an overrun only when another came within the last second of game time: a GPU too slow for
// the pictures is late on every frame, while a lone late picture (a scene loading, a shader built, the host busy for
// a moment) put the menus on 30 pictures per second for 2 s and doubled the next hold.
void NoteLatePicture(const char* why) {
    static uint64_t sLastLate = 0;
    const uint64_t tick = Port_ViTick();
    static const bool sOld = getenv("PORT_INTERP_OLD_BUDGET") != nullptr; // the rules before 2026-11-13 (comparisons)
    const bool repeated = sOld || (sLastLate != 0 && tick >= sLastLate && tick - sLastLate <= 60);
    sLastLate = tick;
    if (repeated) {
        NoteOverrun(why);
    } else if (getenv("PORT_INTERP_LOG")) {
        fprintf(stderr, "[interp] late picture at frame %llu (%s): the first in a second, not counted\n",
                (unsigned long long)tick, why);
    }
}

void NoteOverrun(const char* why) {
    if (BudgetDisabled()) return;
    if (!gBudget.overran && getenv("PORT_INTERP_LOG")) {
        fprintf(stderr, "[interp] overrun at frame %llu: %s\n", (unsigned long long)Port_ViTick(), why);
    }
    gBudget.overran = true;
}

// Once per game frame, before its pictures are planned: the number of pictures it may have.
int PicturesAllowed() {
    if (BudgetDisabled()) return 8;
    Budget& b = gBudget;
    // (a refresh, as in PumpInterpolation: drawing itself delays the next retrace by a few ms, and with 4 ms every
    // frame of a slow GPU renewed the hold below, so the budget never left one picture per game frame: an Intel HD 520
    // showed the game's own 20 per second in the title, where v0.0.12 showed 60)
    static const bool sOld = getenv("PORT_INTERP_OLD_BUDGET") != nullptr;
    bool lagging = Port_ClockLagNs() > (sOld ? 4000000 : 16666667);
    if (b.overran) { // (a lag alone is not ours: loading stalls the game too; dropped pictures count, FinishPending)
        if (b.maxPictures > 1) {
            b.hold = b.backoff;
            b.backoff = std::min<uint64_t>(b.backoff * 2, 9000);
            b.overruns++;
            if (getenv("PORT_INTERP_LOG")) {
                fprintf(stderr, "[interp] budget: one picture per game frame for %llu frames (overrun %llu)\n",
                        (unsigned long long)b.hold, (unsigned long long)b.overruns);
            }
        }
        b.maxPictures = 1;
        b.overran = false;
        b.calm = 0;
        b.dropsInRow = 0;
    } else if (b.maxPictures == 1) {
        if (lagging) {
            b.hold = std::max(b.hold, (uint64_t)30); // the game lags without in-between pictures too: wait
        } else if (b.hold > 0) {
            b.hold--;
        } else {
            b.maxPictures = 8; // try again
        }
    } else if (++b.calm >= 1800) { // a minute without an overrun
        b.calm = 0;
        b.backoff = std::max<uint64_t>(b.backoff / 2, 60);
    }
    return b.maxPictures;
}

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
    std::unordered_map<uint64_t, uint32_t> counts;
    f.mats.reserve(f.tags.size());
    for (auto& t : f.tags) {
        uint32_t n = counts[t.group]++;
        f.groups[t.group].push_back((uint32_t)f.mats.size());
        f.mats.push_back({ t.mtx, t.group, n, ReadFixed(t.mtx) });
    }
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
    int mats = 0, matched = 0, blended = 0, cuts = 0, regrouped = 0, reordered = 0, ambiguous = 0, misindexed = 0;
    float maxMove = 0;
    int cutTurn = 0, cutMove = 0, cutProj = 0;
    bool sceneCut = false;
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

float Dist(const MtxF& x, const MtxF& y) {
    float d = 0;
    for (int c = 0; c < 3; c++) d += (x.mf[3][c] - y.mf[3][c]) * (x.mf[3][c] - y.mf[3][c]);
    return std::sqrt(d);
}

// Pairs one group's matrices (same call site and node) of the newer frame b with the older frame a's. partner[i]: the
// index into ga for gb[i], or -1. The n-th matrix of a site is the same object in both frames for models and most
// effects, but not for effects that recycle their slots (sleep Zs, flames, sparkles: the oldest one goes, a new one
// comes, and everything after it moves up a place) or whose number changes. So the n-th pairing is kept only when
// every matrix's own partner is its nearest one in the older frame; otherwise each one takes its nearest partner, and
// only when that is unambiguous (clearly nearer than any other candidate, and nearer than half the spacing between
// the candidates). Anything else is drawn as built: a wrong partner blends one object towards another's place.
void MatchGroup(const Frame& a, const Frame& b, const std::vector<uint32_t>& ga, const std::vector<uint32_t>& gb,
                std::vector<int>& partner, Stats* st) {
    size_t na = ga.size(), nb = gb.size();
    partner.assign(nb, -1);
    // PORT_INTERP_MATCH=index: the rule before 2026-11 (n-th pairing, groups whose size changed not at all), to compare
    static const bool legacy = getenv("PORT_INTERP_MATCH") && strcmp(getenv("PORT_INTERP_MATCH"), "index") == 0;
    if (legacy) {
        if (na == nb)
            for (size_t i = 0; i < nb; i++) partner[i] = (int)i;
        return;
    }
    if (na == 1 && nb == 1) {
        partner[0] = 0;
        return;
    }
    if (!Affine(b.mats[gb[0]].v)) { // projections: by number, and only when nothing came or went
        if (na == nb)
            for (size_t i = 0; i < nb; i++) partner[i] = (int)i;
        return;
    }
    const size_t kMax = 96; // larger groups (a crowd, confetti): by number only, and only when nothing came or went
    if (na > kMax || nb > kMax) {
        if (na == nb)
            for (size_t i = 0; i < nb; i++) partner[i] = (int)i;
        return;
    }
    // the n-th pairing, when nothing came or went and each one's nearest candidate is its own (or as near)
    if (na == nb) {
        bool ok = true;
        for (size_t i = 0; i < nb && ok; i++) {
            const MtxF& m = b.mats[gb[i]].v;
            float own = Dist(m, a.mats[ga[i]].v), best = own;
            for (size_t j = 0; j < na; j++) best = std::min(best, Dist(m, a.mats[ga[j]].v));
            if (own > best * 1.25f + 0.5f) ok = false;
        }
        if (ok) {
            for (size_t i = 0; i < nb; i++) partner[i] = (int)i;
            return;
        }
    }
    st->reordered++;
    static const bool logGroups = getenv("PORT_INTERP_LOG") && atoi(getenv("PORT_INTERP_LOG")) >= 2;
    if (logGroups) {
        fprintf(stderr, "[interp]   group %016llx: %zu -> %zu matrices, not in the same order:", (unsigned long long)b.mats[gb[0]].group, na, nb);
        for (size_t i = 0; i < std::min<size_t>(nb, 6); i++) {
            const MtxF& m = b.mats[gb[i]].v;
            fprintf(stderr, " (%.0f %.0f %.0f)", m.mf[3][0], m.mf[3][1], m.mf[3][2]);
        }
        fputs(" was", stderr);
        for (size_t j = 0; j < std::min<size_t>(na, 6); j++) {
            const MtxF& m = a.mats[ga[j]].v;
            fprintf(stderr, " (%.0f %.0f %.0f)", m.mf[3][0], m.mf[3][1], m.mf[3][2]);
        }
        fputc('\n', stderr);
    }
    // nearest unambiguous partners
    auto spacing = [](const Frame& f, const std::vector<uint32_t>& g, size_t i) {
        float s = 1e30f;
        for (size_t j = 0; j < g.size(); j++)
            if (j != i) s = std::min(s, Dist(f.mats[g[i]].v, f.mats[g[j]].v));
        return s;
    };
    std::vector<float> spaceA(na), spaceB(nb);
    for (size_t j = 0; j < na; j++) spaceA[j] = spacing(a, ga, j);
    for (size_t i = 0; i < nb; i++) spaceB[i] = spacing(b, gb, i);
    for (size_t i = 0; i < nb; i++) {
        const MtxF& m = b.mats[gb[i]].v;
        int bestJ = -1;
        float best = 1e30f, second = 1e30f;
        for (size_t j = 0; j < na; j++) {
            float d = Dist(m, a.mats[ga[j]].v);
            if (d < best) {
                second = best;
                best = d;
                bestJ = (int)j;
            } else if (d < second) {
                second = d;
            }
        }
        if (bestJ < 0) continue;
        if (best * 2.0f < std::min(spaceA[bestJ], spaceB[i]) && best * 2.0f < second) {
            partner[i] = bestJ;
        } else {
            st->ambiguous++;
        }
    }
    // a partner taken twice belongs to neither
    std::vector<int> uses(na, 0);
    for (int p : partner)
        if (p >= 0) uses[p]++;
    for (int& p : partner)
        if (p >= 0 && uses[p] > 1) p = -1;
    // (statistics: matrices the n-th pairing, the design before this one, would have blended with another object)
    if (na == nb) {
        Stats tmp;
        for (size_t i = 0; i < nb; i++) {
            MtxF m;
            if (partner[i] != (int)i && Blend(a.mats[ga[i]].v, b.mats[gb[i]].v, 0.5f, &m, &tmp)) st->misindexed++;
        }
    }
}

// Matches the newer frame's matrices with the older frame's and decides once per game frame which ones are blended.
// The whole frame is shown as built (no in-between movement at all) at a scene cut: a projection that jumped (another
// camera), or a third or more of the matched matrices cut, or most of the matrices new. Blending the few that
// survive a cut (small objects near the camera) would draw them half-way into the old scene.
std::vector<PlanEntry> Plan(const Frame& a, const Frame& b, Stats* st) {
    std::vector<PlanEntry> plan;
    plan.reserve(b.mats.size());
    std::vector<int> partner;
    std::unordered_map<uint32_t, int> partnerOf; // index into b.mats -> index into a.mats
    partnerOf.reserve(b.mats.size());
    for (auto& [group, gb] : b.groups) {
        auto ia = a.groups.find(group);
        if (ia == a.groups.end()) continue;
        if (ia->second.size() != gb.size()) st->regrouped++;
        MatchGroup(a, b, ia->second, gb, partner, st);
        for (size_t i = 0; i < gb.size(); i++)
            if (partner[i] >= 0) partnerOf[gb[i]] = (int)ia->second[partner[i]];
    }
    int cuts = 0;
    for (uint32_t i = 0; i < b.mats.size(); i++) {
        const Mat& m = b.mats[i];
        st->mats++;
        PlanEntry e{ (Mtx*)m.mtx, m.v, m.v, false };
        auto it = partnerOf.find(i);
        if (it != partnerOf.end()) {
            st->matched++;
            e.a = a.mats[it->second].v;
            int before = st->cuts;
            MtxF tmp;
            e.blend = Blend(e.a, e.b, 0.5f, &tmp, st);
            if (st->cuts != before) cuts++;
        }
        plan.push_back(e);
    }
    st->sceneCut = st->cutProj > 0 || (st->matched >= 4 && cuts * 3 >= st->matched) ||
                   (st->mats >= 8 && st->matched * 2 < st->mats);
    for (auto& e : plan) {
        if (st->sceneCut) e.blend = false;
        if (e.blend) st->blended++;
    }
    return plan;
}

// The matrices for the picture at t (0 < t <= 1): blended ones between the two frames, every other recorded one with
// the value the game built. All of them, not just the blended ones: the in-between and final pictures are drawn after
// the game was told this frame is done, and a matrix the game keeps outside the display-list buffer may already hold
// the next frame's value (the first picture, drawn before that, reads the same values).
void Build(const std::vector<PlanEntry>& plan, float t, Replacements& out) {
    Stats unused;
    out.reserve(plan.size());
    for (auto& e : plan) {
        MtxF m;
        if (e.blend && t < 1.0f && Blend(e.a, e.b, t, &m, &unused)) {
            out[e.mtx] = m;
        } else {
            out[e.mtx] = e.b;
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
    // PORT_INTERP_LOG=3: every picture shown (game frame, its number in the series, wall clock): what a real-time run
    // puts on screen, and when
    static const bool logPictures = getenv("PORT_INTERP_LOG") && atoi(getenv("PORT_INTERP_LOG")) >= 3;
    if (logPictures) {
        fprintf(stderr, "[picture] frame %llu %d/%d at %.2f ms\n", (unsigned long long)tick, i + 1, k,
                Port_SteadyNs() / 1e6);
    }
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
// A drop counts against the budget only when the picture dropped was already due: we were late drawing it. A game
// frame that comes before an in-between picture's refresh (the game's own uneven pace: start-up logos, loading) is
// not a lack of time. Before 2026-11-13 every drop counted, and the count was reset only by a drop-free series that
// was still pending when the next game frame came, which a completed series never is (DrawNext ends it): any three
// drops since the last hold, seconds apart, started a new one (PORT_INTERP_OLD_BUDGET=1: those rules).
bool gForcedFinish = false;

void FinishPending(bool countDrop = true, bool behind = false) {
    static const bool sOld = getenv("PORT_INTERP_OLD_BUDGET") != nullptr;
    const bool late = sOld || behind || (gHavePending && Port_SteadyNs() > Due(gPending, gPending.next));
    if (gHavePending && gPending.next < gPending.k && countDrop && late) {
        if (++gBudget.dropsInRow >= 3) NoteOverrun("pictures dropped in 3 game frames in a row");
    } else if (gHavePending) {
        gBudget.dropsInRow = 0;
    }
    if (gHavePending && gPending.restore && gPending.next < gPending.k && gMapGen.load() == gPending.mapGen) {
        gPending.next = gPending.k - 1;
        gForcedFinish = true;
        DrawNext(); // (ends the pending frame)
        gForcedFinish = false;
        return;
    }
    EndPending();
}

void DrawNext() {
    Pending& p = gPending;
    int j = p.next++;
    Replacements r;
    Build(p.plan, j + 1 < p.k ? (float)(j + 1) / p.k : 1.0f, r);
    if (auto interp = Interp()) {
        interp->ReplayPassStart();
        interp->mReplayT = j + 1 < p.k ? (float)(j + 1) / p.k : 1.0f; // (2D rectangles: interpreter.cpp)
    }
    Port::RunFrameWith(p.dl, &r, true, ShotWanted(p.tick));
    Shot(p.tick, j, p.k);
    if (Port_SteadyNs() > p.frameDue + 2000000) NoteLatePicture("a picture ended after the next game frame was due");
    if (p.next >= p.k) {
        static const bool sOld = getenv("PORT_INTERP_OLD_BUDGET") != nullptr;
        if (!gForcedFinish && !sOld) gBudget.dropsInRow = 0; // every picture of the series shown
        EndPending();
    }
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
    if (gBuilding) {
        gBuilding->dl = dlStart;
        gBuilding->serial = ++gSerial;
    }
    gNode = nullptr;
}

extern "C" void Port_InterpNode(const void* node) {
    gNode = node;
}

extern "C" void Port_InterpTagMtx(const void* mtx, const void* site) {
    if (!gRecording.load(std::memory_order_relaxed) || (uintptr_t)mtx < 0x100000000ull) return;
    std::lock_guard<std::mutex> g(gLock);
    if (!gBuilding) return;
    gBuilding->tags.push_back({ mtx, Mix(Mix(0, (uint64_t)(uintptr_t)site), (uint64_t)(uintptr_t)gNode) });
}

extern "C" void Port_InterpMapsChanged(void) {
    gMapGen.fetch_add(1, std::memory_order_relaxed);
}

namespace Port {
void CancelInterpolation() {
    FinishPending(false); // (something else is shown: not a drop for lack of time)
    gPrev = nullptr;
}

void RunGameFrame(void* gfxCommands, bool exact) {
    bool on = Enabled();
    gRecording = on;
    // PORT_INTERP_IMMEDIATE=late (test): the in-between pictures are drawn only now, after the game has built this
    // newer frame (what a real-time run does when they come late in the game frame)
    static const bool late = getenv("PORT_INTERP_IMMEDIATE") && strcmp(getenv("PORT_INTERP_IMMEDIATE"), "late") == 0;
    if (late && gHavePending && gMapGen.load() == gPending.mapGen) {
        while (gHavePending) DrawNext();
    }
    if (Logging() && gHavePending && gPending.next < gPending.k) {
        fprintf(stderr, "[interp] drop at frame %llu picture %d/%d: newer frame (%.1f ms after the first)\n",
                (unsigned long long)gPending.tick, gPending.next + 1, gPending.k, (Port_SteadyNs() - gPending.start) / 1e6);
    }
    FinishPending(); // a newer frame: in-between frames still due are dropped
    if (!on) {
        gPrev = nullptr;
        if (ShotWanted(Port_ViTick())) { // (the same pictures as with it on, for comparisons)
            RunFrameWith(gfxCommands, nullptr, false, true);
        } else {
            RunFrame(gfxCommands);
        }
        Shot(Port_ViTick(), 0, 1);
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
    // (a frame the game submits again, or an older one, is not blended "backwards" either)
    if (cur && gPrev && !exact && cur->serial > gPrev->serial && tick > gPrevTick && tick - gPrevTick <= kMaxGap) {
        k = (int)std::min<uint64_t>((tick - gPrevTick) * perRetrace, 8);
    }
    const uint64_t gap = tick > gPrevTick ? tick - gPrevTick : 0;
    if (on && !exact) k = std::min(k, PicturesAllowed());
    Stats st;
    Replacements r;
    std::vector<PlanEntry> plan;
    bool repaint = false, restore = false;
    auto interpreter = Interp();
    if (k >= 2 && interpreter) {
        repaint = interpreter->FrameRepaintsScreen((Gfx*)gfxCommands);
        // a frame that draws over the previous picture (menus) is drawn again from a copy of that picture
        restore = !repaint && interpreter->CanRestorePicture();
    }
    if (k >= 2 && (repaint || restore)) {
        plan = Plan(*gPrev, *cur, &st);
        Build(plan, 1.0f / k, r);
    }
    if (Logging()) {
        fprintf(stderr, "[interp] frame %llu: %s k %d repaint %d mats %d matched %d regrouped %d reordered %d ambiguous %d "
                "misindexed %d blended %d cuts %d (turn %d move %d projection %d)%s max move %.1f\n",
                (unsigned long long)tick, cur ? "recorded" : "no record", k, (int)repaint, st.mats, st.matched,
                st.regrouped, st.reordered, st.ambiguous, st.misindexed, st.blended, st.cuts, st.cutTurn, st.cutMove, st.cutProj,
                st.sceneCut ? " SCENE CUT" : "", st.maxMove);
    }
    if (k >= 2 && (repaint || restore)) {
        gPending.dl = gfxCommands;
        gPending.a = gPrev;
        gPending.b = cur;
        gPending.plan = std::move(plan);
        gPending.k = k;
        gPending.next = 1;
        gPending.start = Port_SteadyNs();
        gPending.period = grid ? period : 16666667;
        gPending.vblank = vblank;
        gPending.grid = grid;
        gPending.mapGen = gMapGen.load();
        gPending.tick = tick;
        gPending.restore = restore;
        gPending.frameDue = gPending.start + (int64_t)gap * 16666667;
        interpreter->Interp2DPrepare((Gfx*)gfxCommands);
        interpreter->ReplayBegin(restore);
        interpreter->mReplayT = 1.0f / k;
        RunFrameWith(gfxCommands, &r, false);
        Shot(tick, 0, k);
        // the first picture alone took two refreshes (a present that waits for the vblank takes up to one): no room
        if (Port_SteadyNs() - gPending.start > gPending.period * 2) NoteLatePicture("the first picture took two refreshes");
        gHavePending = true;
        if (immediate && !late) {
            while (gHavePending) DrawNext();
        }
    } else {
        if (interpreter) interpreter->mReplayT = 1.0f;
        RunFrame(gfxCommands);
        Shot(tick, 0, 1);
    }
    if (!cur || !gPrev || cur->serial > gPrev->serial) { // (a frame submitted again keeps the newer one as reference)
        gPrev = cur;
        gPrevTick = tick;
    }
}

void PumpInterpolation() {
    if (!gHavePending) return;
    if (gMapGen.load() != gPending.mapGen) { // the game unloaded or moved data the display list may use
        EndPending();
        return;
    }
    // An in-between frame never holds up the game: when the game already waits for its next frame to be drawn, or
    // game time lags behind real time (a slow GPU), the in-between frames still due are dropped. The lag allowed is a
    // refresh: drawing a picture delays the window thread's RSP tasks (the game's audio, its next frame), so each
    // retrace after a long picture comes a few ms late and is made up at once. With 4 ms, pictures of 9-10 ms (3840 x
    // 2160 on an M1/M2's GPU), two of which fit in a 30 fps frame, were dropped on most frames, and the budget then
    // held the menus at 30 pictures per second for up to minutes (PORT_DEBUG_DRAW_MS=9: 37 pictures/s, 5 holds in 45 s).
    static const int64_t sLagNs = getenv("PORT_INTERP_LAG_MS") ? (int64_t)(atof(getenv("PORT_INTERP_LAG_MS")) * 1e6)
                                  : getenv("PORT_INTERP_OLD_BUDGET") ? 4000000 : 16666667;
    if (Port_RspTaskPending() || Port_ClockLagNs() > sLagNs) {
        if (Logging() && gPending.next < gPending.k) {
            fprintf(stderr, "[interp] drop at frame %llu picture %d/%d: %s (lag %.1f ms, %.1f ms after the first)\n",
                    (unsigned long long)gPending.tick, gPending.next + 1, gPending.k,
                    Port_RspTaskPending() ? "next game frame waiting" : "clock lag", Port_ClockLagNs() / 1e6,
                    (Port_SteadyNs() - gPending.start) / 1e6);
        }
        FinishPending(true, Port_ClockLagNs() > sLagNs); // (game time behind: drawing could not keep up)
        return;
    }
    if (Port_SteadyNs() < Due(gPending, gPending.next)) return;
    DrawNext();
}
} // namespace Port
