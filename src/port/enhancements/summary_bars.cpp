// The side summary bars (hippodrome-utils docs/specs/PARTY_SUMMARY_BARS.md): a column of cards on each side of the
// game's picture during a battle, the left trainer's party on the left and the right one's on the right. They are
// drawn by ImGui in the window's room beside the picture, never over it: a 4:3 picture in a wider window, or a 16:9
// picture on an ultrawide screen. When the window has no such room there are no bars; the game's own picture and HUD
// are never moved or covered. The battle order screen shows them too (both parties, before the battle): a 2D screen,
// so in a widescreen picture they go beside its 4:3 frame, over its background continued into the sides.
//
// The battle code (battle_enhancements.c) publishes what the cards show each frame; this file only draws it. A card's
// picture is its own animated copy of the Pokémon's 3D model (card_models.c draws them into one off-screen picture per
// side, which this file creates), tinted and animated the way the spec's model look says (hu_card_model_look). If a
// model could not be loaded, the card shows the game's menu icon of the Pokémon instead.
#include "enhancements.h"
#include "summary_bars.h"
#include "overlay.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>


namespace Port {
std::shared_ptr<Fast::Fast3dWindow> GetWindow(); // Engine.cpp
} // namespace Port

namespace PortOverlay {

// the cards' pictures, uploaded once each on the render thread
static std::map<uint32_t, ImTextureID> gTextures;

ImTextureID IconTexture(uint32_t key) {
    if (key == 0) return nullptr;
    auto it = gTextures.find(key);
    if (it != gTextures.end()) return it->second;
    const uint8_t* rgba = Port_Enh_CardIcon(key);
    auto window = Port::GetWindow();
    auto interp = window ? window->GetInterpreterWeak().lock() : nullptr;
    if (rgba == nullptr || !interp) return nullptr;
    auto* api = interp->GetCurrentRenderingAPI();
    const uint32_t id = api->NewTexture();
    api->SelectTexture(0, id);
    api->SetSamplerParameters(0, false, 0, 0);
    api->UploadTexture(rgba, PORT_ENH_ICON, PORT_ENH_ICON);
    ImTextureID tex = api->GetTextureById((int)id);
    gTextures[key] = tex;
    return tex;
}

static std::map<int32_t, ImTextureID> gPortraitTextures;

ImTextureID PortraitTexture(int32_t index) {
    if (index < 0) return nullptr;
    auto it = gPortraitTextures.find(index);
    if (it != gPortraitTextures.end()) return it->second;
    const uint8_t* rgba = Port_Enh_Portrait((uint32_t)index);
    auto window = Port::GetWindow();
    auto interp = window ? window->GetInterpreterWeak().lock() : nullptr;
    if (rgba == nullptr || !interp) return nullptr;
    auto* api = interp->GetCurrentRenderingAPI();
    const uint32_t id = api->NewTexture();
    api->SelectTexture(0, id);
    api->SetSamplerParameters(0, false, 0, 0);
    api->UploadTexture(rgba, PORT_ENH_PORTRAIT, PORT_ENH_PORTRAIT);
    ImTextureID tex = api->GetTextureById((int)id);
    gPortraitTextures[index] = tex;
    return tex;
}

ImU32 Rgba(int r, int g, int b, int a) {
    return IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), std::clamp(a, 0, 255));
}

const char* Str(const char* id) {
    // (drawn on the render thread: kept per id, and read again when the chosen language changes)
    static std::map<std::string, std::string> cache;
    static std::string lang;
    const char* now = CVarGetString("gPort.TextLanguage", "");
    if (now == nullptr) now = "";
    if (lang != now) {
        cache.clear();
        lang = now;
    }
    auto it = cache.find(id);
    if (it == cache.end()) {
        char buf[96];
        Port_TextToUtf8(Port_TextPortString(id), buf, sizeof buf);
        it = cache.emplace(id, buf).first;
    }
    return it->second.c_str();
}

void Text(ImDrawList* dl, ImFont* f, float x, float y, float size, float maxw, const char* s, ImU32 col) {
    if (s == nullptr || *s == '\0') return;
    float w = f->CalcTextSizeA(size, FLT_MAX, 0, s).x;
    if (maxw <= 0) { // no width limit (a clip rectangle 0 wide would hide the whole text)
        dl->AddText(f, size, ImVec2(x, y), col, s);
        return;
    }
    if (w > maxw) size = std::max(size * 0.7f, size * maxw / w);
    dl->PushClipRect(ImVec2(x, y - size), ImVec2(x + maxw, y + size * 2), true);
    dl->AddText(f, size, ImVec2(x, y), col, s);
    dl->PopClipRect();
}

} // namespace PortOverlay

namespace {

// the cards' model pictures (card_models.c draws into them): created on the render thread, read by the game thread.
// One picture per side, a row of cells each.
std::atomic<int> gCardFb[2] = { 0, 0 };
ImTextureID gCardTexture[2] = { 0, 0 };

void EnsureCardPictures() {
    auto window = Port::GetWindow();
    auto interp = window ? window->GetInterpreterWeak().lock() : nullptr;
    if (!interp) return;
    for (int s = 0; s < 2; s++) {
        if (gCardFb[s].load() <= 0) {
            // fixed size, no widescreen adjustment: the game's coordinates are the picture's pixels
            const int fb = gfx_create_framebuffer(PORT_ENH_CARD_ATLAS_W, PORT_ENH_CARD_CELL, PORT_ENH_CARD_ATLAS_W,
                                                  PORT_ENH_CARD_CELL, 0, false);
            if (fb > 0) gCardFb[s].store(fb);
        }
        if (gCardFb[s].load() > 0) {
            gCardTexture[s] = (ImTextureID)interp->GetCurrentRenderingAPI()->GetFramebufferTextureId(gCardFb[s].load());
        }
    }
}

// Large Pokémon: the part of the cell with the face, while it has not fainted (the spec's "Model").
void FaceFrame(int species, float* zoom, float* cx, float* cy) {
    struct Frame {
        int species;
        float zoom, cx, cy;
    };
    static const Frame kFrames[] = {
        { 95, 1.9f, 0.62f, 0.36f },  // Onix
        { 103, 1.6f, 0.50f, 0.36f }, // Exeggutor
        { 131, 1.5f, 0.48f, 0.34f }, // Lapras
    };
    for (const Frame& f : kFrames) {
        if (f.species == species) {
            *zoom = f.zoom;
            *cx = f.cx;
            *cy = f.cy;
            return;
        }
    }
}

using PortOverlay::IconTexture;
using PortOverlay::Rgba;
using PortOverlay::Str;
using PortOverlay::Text;

// the cards need at least this much room beside the picture (a fraction of the window's height): a 4:3 picture in
// a 16:9 window leaves 0.22, a 16:9 picture in a 21:9 window 0.29, a 4:3 picture in a 16:10 window only 0.13 (no bars)
constexpr float kMinSide = 0.18f;

void DrawMoveName(ImDrawList* dl, ImFont* font, float x, float y, float row, float width, const char* name,
                  ImU32 color) {
    if (name == nullptr || *name == '\0' || width <= 0) return;
    const float size = row * 0.86f;
    const float textWidth = font->CalcTextSizeA(size, FLT_MAX, 0, name).x;
    const float overflow = textWidth - width;
    float offset = 0;
    float drawSize = size;
    float spaceTrim = 0;

    if (overflow > width * 0.10f) {
        // Pause at each end so both the start and the end of the name can be read.
        const double hold = 0.75;
        const double travel = overflow / (size * 2.0);
        const double leg = hold + travel;
        const double phase = std::fmod(ImGui::GetTime(), 2 * leg);
        const double progress = std::clamp((std::fmod(phase, leg) - hold) / travel, 0.0, 1.0);
        offset = overflow * (float)(phase < leg ? progress : 1.0 - progress);
    } else if (overflow > 0) {
        const auto spaces = std::count(name, name + std::char_traits<char>::length(name), ' ');
        const float spaceWidth = font->CalcTextSizeA(size, FLT_MAX, 0, " ").x;
        // Prefer slightly tighter word spacing to changing the text size. Unspaced names
        // can still fit small overflows with a size adjustment of less than ten percent.
        if (spaces > 0 && overflow <= spaces * spaceWidth * 0.5f) {
            spaceTrim = overflow / spaces;
        } else {
            drawSize *= width / textWidth;
        }
    }

    const float textY = y + (row - drawSize) / 2;
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + row), true);
    if (spaceTrim > 0) {
        float wordX = x;
        const float advance = font->CalcTextSizeA(size, FLT_MAX, 0, " ").x - spaceTrim;
        for (const char* word = name; *word;) {
            const char* end = word;
            while (*end && *end != ' ') ++end; // ASCII space never splits a UTF-8 codepoint.
            dl->AddText(font, size, ImVec2(wordX, textY), color, word, end);
            wordX += font->CalcTextSizeA(size, FLT_MAX, 0, word, end).x;
            word = *end ? end + 1 : end;
            if (*end) wordX += advance;
        }
    } else {
        dl->AddText(font, drawSize, ImVec2(x - offset, textY), color, name);
    }
    dl->PopClipRect();
}

// moveRows: the rows the moves take, HU_CARD_MOVES but for Mewtwo's card under Harder vs Mewtwo, which grows (the
// card's height p1.y - p0.y includes them; fractional while it grows)
void DrawCard(ImDrawList* dl, ImFont* f, const PortEnhBattleView& v, int s, int i, ImVec2 p0, ImVec2 p1,
              float moveRows = HU_CARD_MOVES) {
    const hu_card& c = v.sides[s].cards[i];
    const hu_card_state state = hu_card_get_state(&c);
    const bool own = (s == v.viewer);
    const bool big = v.bigRows > 0 && s == v.bigSide && i == v.bigCard;
    const float w = p1.x - p0.x, fullH = p1.y - p0.y;
    const float h = fullH * (HU_CARD_MOVES + 3) / (moveRows + 3); // a regular card's height
    const float pad = std::max(2.0f, h * 0.06f);
    const bool active = (c.flags & HU_CARD_F_ACTIVE) != 0;
    const int alpha = (state == HU_CARD_NOT_PICKED || state == HU_CARD_FAINTED) ? 150 : 255;

    dl->AddRectFilled(p0, p1, active ? Rgba(0x28, 0x34, 0x58, 0xF0) : Rgba(0x14, 0x18, 0x28, 0xE8), h * 0.12f);
    if (active) dl->AddRect(p0, p1, Rgba(0xF8, 0xD8, 0x30, 0xFF), h * 0.12f, 0, std::max(1.0f, h * 0.025f));

    // the picture, on the card's outer side (the left trainer's on the left, the right trainer's on the right), the
    // text column beside it
    const bool right = (s == 1);
    const float m = std::min(h - 2 * pad, w * 0.34f);
    const ImVec2 m0(right ? p1.x - pad - m : p0.x + pad, p0.y + (fullH - m) / 2), m1(m0.x + m, m0.y + m);
    dl->AddRectFilled(m0, m1, Rgba(0x30, 0x3C, 0x60, 0xFF), m * 0.18f);
    if (v.modelReady[s][i] && gCardTexture[s]) {
        float zoom = 1.0f, cx = 0.5f, cy = 0.5f;
        if (state != HU_CARD_FAINTED) FaceFrame(v.modelSpecies[s][i], &zoom, &cx, &cy);
        const float half = 0.5f / zoom;
        const float x0 = std::clamp(cx - half, 0.0f, 1.0f - 2 * half), y0 = std::clamp(cy - half, 0.0f, 1.0f - 2 * half);
        const float cu = 1.0f / HU_SIDE_MAX;
        // the right trainer's cards show their models mirrored, so they face the left trainer's (the card itself is a
        // mirror image of the left trainer's); the tint is the model's own (card_models.c)
        const float u0 = (i + x0) * cu, u1 = (i + x0 + 2 * half) * cu;
        const ImVec2 uv0(right ? u1 : u0, y0), uv1(right ? u0 : u1, y0 + 2 * half);
        dl->AddImageRounded(gCardTexture[s], m0, m1, uv0, uv1, IM_COL32_WHITE, m * 0.18f);
    } else if (ImTextureID tex = IconTexture(v.iconKey[s][i])) {
        // the look: the status colour or grey multiplied in by its amount, darker while its pick is not known
        const hu_model_look look = hu_card_model_look(&c, 40);
        hu_rgb tint = look.tint;
        float amount = look.tint_amount;
        if (state == HU_CARD_FAINTED) {
            tint = { 0x80, 0x80, 0x80 };
            amount = 0.8f;
        }
        auto mix = [&](int ch) { return (int)(255 * (1 - amount) + ch * amount); };
        const ImU32 col = Rgba(mix(tint.r), mix(tint.g), mix(tint.b), 0xFF);
        // the icons face left; the right trainer's are mirrored, so they face the left trainer's
        const ImVec2 uv0(right ? 1.0f : 0.0f, 0.0f), uv1(right ? 0.0f : 1.0f, 1.0f);
        dl->AddImageRounded(tex, m0, m1, uv0, uv1, col, m * 0.18f);
    } else {
        char dex[8];
        snprintf(dex, sizeof dex, "%u", c.model_species);
        const float ts = m * 0.32f;
        const ImVec2 d = f->CalcTextSizeA(ts, FLT_MAX, 0, dex);
        dl->AddText(f, ts, ImVec2((m0.x + m1.x - d.x) / 2, (m0.y + m1.y - d.y) / 2), Rgba(0xFF, 0xFF, 0xFF, 0xB0), dex);
    }
    if (c.flags & HU_CARD_F_SUBSTITUTE) {
        dl->AddRect(m0, m1, Rgba(0xFF, 0xFF, 0xFF, 0xC0), m * 0.18f, 0, std::max(1.0f, m * 0.05f));
    }

    // The text column: name, HP, one move per row, then Speed.
    const float x = right ? p0.x + pad : m1.x + pad, tw = (right ? m0.x : p1.x) - pad - x;
    const float row = (h - 2 * pad) / (HU_CARD_MOVES + 3), ts = row * 0.86f;
    float y = p0.y + pad;
    const ImU32 white = Rgba(0xFF, 0xFF, 0xFF, alpha);
    {
        char lv[8];
        snprintf(lv, sizeof lv, "L%u", c.level);
        const float lw = f->CalcTextSizeA(ts * 0.8f, FLT_MAX, 0, lv).x;
        Text(dl, f, x, y, ts, tw - lw - pad, v.names[s][i], white);
        dl->AddText(f, ts * 0.8f, ImVec2(x + tw - lw, y + ts * 0.15f), Rgba(0xD0, 0xD0, 0xD0, alpha), lv);
    }
    y += row;
    // HP: the Game Boy's bar colours; the numbers only for the side whose HP the player may know
    {
        const float bh = row * 0.42f;
        char hp[16];
        snprintf(hp, sizeof hp, "%u/%u", c.hp, c.max_hp);
        const float hs = ts * 0.75f;
        const float hw = f->CalcTextSizeA(hs, FLT_MAX, 0, hp).x;
        const float bw = own ? std::max(tw * 0.3f, tw - hw - bh * 2.2f) : tw - bh * 2.2f;
        const ImVec2 b0(x, y + (row - bh) / 2), b1(x + bw, b0.y + bh);
        dl->AddRectFilled(b0, b1, Rgba(0x30, 0x30, 0x30, alpha), bh / 2);
        const int fill = hu_hp_fill(c.hp, c.max_hp, 480);
        if (fill > 0) {
            static const ImU32 bands[3] = { IM_COL32(0x48, 0xD8, 0x60, 0xFF), IM_COL32(0xF8, 0xD0, 0x30, 0xFF),
                                            IM_COL32(0xF0, 0x48, 0x38, 0xFF) };
            dl->AddRectFilled(b0, ImVec2(b0.x + bw * fill / 480.0f, b1.y), bands[hu_hp_band_of(c.hp, c.max_hp)],
                              bh / 2);
        }
        if (c.status != HU_STATUS_NONE) { // the status colour as a dot after the bar
            const hu_rgb sc = hu_status_color((hu_status)c.status);
            dl->AddCircleFilled(ImVec2(b1.x + bh * 0.9f, (b0.y + b1.y) / 2), bh * 0.6f, Rgba(sc.r, sc.g, sc.b, 0xFF));
        }
        if (own) {
            Text(dl, f, x + tw - hw, y + (row - hs) / 2, hs, hw + 1, hp, white);
        }
    }
    y += row;
    if (big) {
        // in the order they were revealed; the row being added fades in as the card grows
        dl->PushClipRect(ImVec2(p0.x, y), ImVec2(p1.x, y + moveRows * row), true);
        for (int k = 0; k < (int)std::ceil(moveRows) && k < PORT_ENH_BIG_MOVES; k++) {
            const float moveY = y + k * row;
            if (k < v.bigShown) {
                const hu_rgb tc = hu_type_text_color((hu_type)v.bigTypes[k]);
                DrawMoveName(dl, f, x, moveY, row, tw, v.bigMoves[k], Rgba(tc.r, tc.g, tc.b, alpha));
            } else {
                DrawMoveName(dl, f, x, moveY, row, tw, "-", Rgba(0xFF, 0xFF, 0xFF, 0x70));
            }
        }
        dl->PopClipRect();
    }
    for (int k = 0; k < HU_CARD_MOVES && !big; k++) {
        const float moveY = y + k * row;
        if (hu_card_move_visible(&c, k)) {
            if (c.moves[k] == 0) continue; // an empty slot
            const hu_rgb tc = hu_type_text_color((hu_type)v.moveTypes[s][i][k]);
            DrawMoveName(dl, f, x, moveY, row, tw, v.moves[s][i][k], Rgba(tc.r, tc.g, tc.b, alpha));
        } else if (c.moves[k] != 0 || !own) {
            DrawMoveName(dl, f, x, moveY, row, tw, "-", Rgba(0xFF, 0xFF, 0xFF, 0x70));
        }
    }
    y += moveRows * row;
    {
        const hu_speed_range r = v.speed[s][i];
        char sp[64];
        const char* label = Str("PORT_SUMMARY_SPEED");
        if (r.min == r.max) snprintf(sp, sizeof sp, "%s %u", label, r.min);
        else snprintf(sp, sizeof sp, "%s %u-%u", label, r.min, r.max);
        Text(dl, f, x, y, ts * 0.85f, tw, sp, Rgba(0x90, 0xD8, 0xFF, alpha));
    }
}

class SummaryBarsWindow : public Ship::GuiWindow {
  public:
    SummaryBarsWindow() : GuiWindow("gEnhancements.SummaryBarsOverlay", "Summary bars") {
    }
    void Draw() override {
        if (!Port_Enh_SummaryBarsEnabled()) return;
        EnsureCardPictures();
        PortEnhBattleView v;
        if (!Port_Enh_LatestBattle(&v)) return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float W = vp->Size.x, H = vp->Size.y;
        if (W <= 0 || H <= 0) return;
        // the bars go beside the picture. The battle order screen is a 2D screen, kept in its 4:3 frame even in a
        // widescreen picture (whose sides then show its background continued, PortEnh_OrderSelectBackdrop): its bars go
        // beside that frame, unless the window is wide enough to hold them beside the whole 16:9 picture (ultrawide)
        const bool wide = Port::AspectSetting() == 1;
        float picW = std::min(W, H * (wide ? 16.0f / 9.0f : 4.0f / 3.0f));
        if (wide && v.orderSelect && (W - picW) / 2 < kMinSide * H) picW = std::min(W, H * 4.0f / 3.0f);
        const float side = (W - picW) / 2;
        if (side < kMinSide * H) return; // no room beside the picture: no bars

        // a window of its own, kept just above libultraship's game view ("Main Game") and under every other window
        // (menus): the cards beside the order screen's 4:3 frame lie over the picture, which would cover a background
        // draw list's
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("##summarybars", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar(2);
        // (the game view is docked: its dock host is what sits in the display order)
        // (the game view is docked in "Main - Deck", which is what sits in the display order and holds the menu bar:
        // the cards stay under that bar)
        ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
        float top = vp->Pos.y;
        if (ImGuiWindow* game = ImGui::FindWindowByName("Main Game")) {
            ImGuiWindow* deck = game->RootWindowDockTree ? game->RootWindowDockTree : game;
            ImGui::BringWindowToDisplayBack(deck);
            if (deck->Flags & ImGuiWindowFlags_MenuBar) top = std::max(top, deck->MenuBarRect().Max.y);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(vp->Pos.x, top), ImVec2(vp->Pos.x + W, vp->Pos.y + H), false);
        const float margin = H * 0.012f, gap = H * 0.008f;
        const float cw = std::min(side - 2 * margin, H * 0.34f);
        const float ch = (H - 2 * margin - (HU_SIDE_MAX - 1) * gap) / HU_SIDE_MAX;
        ImFont* f = Port::CardFont(ch / (HU_CARD_MOVES + 3));
        if (f == nullptr) f = ImGui::GetFont();
        for (int s = 0; s < 2; s++) {
            const hu_side& sd = v.sides[s];
            // each column sits in the middle of its band beside the picture
            const float x0 = vp->Pos.x + (s == 0 ? (side - cw) / 2 : W - side + (side - cw) / 2);
            for (int i = 0; i < sd.count && i < HU_SIDE_MAX; i++) {
                const float y0 = vp->Pos.y + margin + i * (ch + gap);
                if (v.bigRows > 0 && s == v.bigSide && i == v.bigCard) {
                    // Mewtwo's card (Harder vs Mewtwo) grows a row for each move revealed past the fourth; its side
                    // has one card, so it grows into the room below
                    const float target = v.bigRows;
                    if (mBigRows < HU_CARD_MOVES || mBigRows > target) mBigRows = std::min<float>(target, std::max<float>(mBigRows, HU_CARD_MOVES));
                    mBigRows = std::min(target, mBigRows + ImGui::GetIO().DeltaTime * 2.5f);
                    const float bh = ch * (mBigRows + 3) / (HU_CARD_MOVES + 3);
                    DrawCard(dl, f, v, s, i, ImVec2(x0, y0), ImVec2(x0 + cw, y0 + bh), mBigRows);
                    continue;
                }
                DrawCard(dl, f, v, s, i, ImVec2(x0, y0), ImVec2(x0 + cw, y0 + ch));
            }
        }
        dl->PopClipRect();
        ImGui::End();
    }

  private:
    float mBigRows = HU_CARD_MOVES; // Mewtwo's card's move rows as drawn (it grows toward v.bigRows)

  protected:
    void DrawElement() override {
    }
    void UpdateElement() override {
    }
};

} // namespace

extern "C" int Port_Enh_CardModelFramebuffer(int side) {
    return (side == 0 || side == 1) ? gCardFb[side].load() : 0;
}

void Port_Enh_InstallOverlay(void* gui) {
    static bool added = false;
    if (added || gui == nullptr) return;
    added = true;
    auto w = std::make_shared<SummaryBarsWindow>();
    static_cast<Ship::Gui*>(gui)->AddGuiWindow(w);
    w->Show();
    PortOverlay::InstallMemoryScreen(gui); // memory_screen.cpp
}
