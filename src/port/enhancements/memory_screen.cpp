// The trainer memory screen's window (memory_screen.c keeps its state and publishes what it shows): drawn by ImGui over
// the team select screen, in the game's own 640 x 480 stage coordinates scaled to the picture. Closed, only a hint
// shows in the bottom right corner; open, a window lists the trainers on the left and the highlighted one's team on
// the right: each Pokémon's icon, name, level, speed estimate and the moves seen. It expands from its middle, then its
// contents fade in (hu_window_anim, as the game's windows do).
#include "enhancements.h"
#include "overlay.h"

#include <libultraship/libultraship.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <memory>

using PortOverlay::IconTexture;
using PortOverlay::Rgba;
using PortOverlay::Str;
using PortOverlay::Text;

namespace {

// the window on the stage: under the team select's header (it ends near y 72), above the hint's line
constexpr float kWinX = 24.0f, kWinY = 76.0f, kWinW = 592.0f, kWinH = 372.0f;
constexpr float kListW = 196.0f;

class MemoryScreenWindow : public Ship::GuiWindow {
  public:
    MemoryScreenWindow() : GuiWindow("gEnhancements.TrainerMemoryOverlay", "Trainer memory") {
    }

    void Draw() override {
        PortEnhMemoryView v;
        if (!Port_Enh_TrainerMemoryEnabled() || !Port_Enh_LatestMemory(&v)) return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float W = vp->Size.x, H = vp->Size.y;
        if (W <= 0 || H <= 0) return;
        // the picture, then the 4:3 stage the menus are laid out on, in its middle
        const float aspect = Port::AspectSetting() == 1 ? 16.0f / 9.0f : 4.0f / 3.0f;
        const float picW = std::min(W, H * aspect), picH = picW / aspect;
        mScale = picH / 480.0f;
        mX0 = vp->Pos.x + (W - picH * 4.0f / 3.0f) / 2;
        mY0 = vp->Pos.y + (H - picH) / 2;
        mDl = ImGui::GetForegroundDrawList(); // over the game's picture
        mFont = Port::CardFont(14.0f * mScale);
        if (mFont == nullptr) mFont = ImGui::GetFont();

        if (!v.open) {
            DrawHint();
            return;
        }
        // the frame grows from its middle, then the contents fade in
        const float w = kWinW * v.size, h = kWinH * v.size;
        Rect(kWinX + (kWinW - w) / 2, kWinY + (kWinH - h) / 2, w, h, Rgba(0x08, 0x0C, 0x18, 0xF0), 6.0f);
        if (v.size >= 1.0f) {
            Rect(kWinX, kWinY, kWinW, 2.0f, Rgba(0xF8, 0xD8, 0x30, 0xFF), 0.0f);
        }
        if (v.fill <= 0.0f) return;
        mFade = v.fill;
        DrawContents(v);
        mFade = 1.0f;
    }

  protected:
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

  private:
    ImDrawList* mDl = nullptr;
    ImFont* mFont = nullptr;
    float mScale = 1.0f, mX0 = 0.0f, mY0 = 0.0f, mFade = 1.0f;

    ImVec2 P(float x, float y) const {
        return ImVec2(mX0 + x * mScale, mY0 + y * mScale);
    }
    ImU32 Col(int r, int g, int b, int a) const {
        return Rgba(r, g, b, (int)(a * mFade));
    }
    void Rect(float x, float y, float w, float h, ImU32 col, float round) {
        mDl->AddRectFilled(P(x, y), P(x + w, y + h), col, round * mScale);
    }
    void Txt(float x, float y, float size, float maxw, const char* s, ImU32 col) {
        Text(mDl, mFont, P(x, y).x, P(x, y).y, size * mScale, maxw * mScale, s, col);
    }
    float TextW(float size, const char* s) const {
        return mFont->CalcTextSizeA(size * mScale, FLT_MAX, 0, s).x / mScale;
    }

    void DrawHint() {
        const char* hint = Str("PORT_MEMORY_HINT");
        const float w = TextW(14.0f, hint);
        Rect(620.0f - w - 12.0f, 452.0f, w + 12.0f, 22.0f, Rgba(0, 0, 0, 0xA0), 4.0f);
        Txt(620.0f - w - 6.0f, 455.0f, 14.0f, 0, hint, Rgba(0xFF, 0xFF, 0xFF, 0xFF));
    }

    void DrawContents(const PortEnhMemoryView& v) {
        Txt(kWinX + 16.0f, kWinY + 10.0f, 18.0f, 400.0f, Str("PORT_MEMORY_TITLE"), Col(0xFF, 0xFF, 0xFF, 0xFF));
        const char* back = Str("PORT_MEMORY_BACK");
        Txt(kWinX + kWinW - 16.0f - TextW(14.0f, back), kWinY + 14.0f, 14.0f, 0, back, Col(0xC0, 0xC0, 0xC0, 0xFF));

        // the trainers
        const float listY = kWinY + 42.0f, rowH = std::min(40.0f, (kWinH - 52.0f) / std::max(1, v.rows));
        for (int i = 0; i < v.rows && i < PORT_MEM_ROWS; i++) {
            const float ry = listY + i * rowH;
            const int a = v.reached[i] ? 0xFF : 0x90;
            if (i == v.cursor) {
                Rect(kWinX + 8.0f, ry - 3.0f, kListW, rowH - 4.0f, Col(0x30, 0x50, 0x90, 0xFF), 4.0f);
            }
            char num[8];
            snprintf(num, sizeof num, "%d", i + 1);
            Txt(kWinX + 14.0f, ry + 2.0f, 14.0f, 0, num, Col(0xC0, 0xC0, 0xC0, a));
            Txt(kWinX + 38.0f, ry + 2.0f, 14.0f, kListW - 34.0f,
                v.reached[i] ? v.rowNames[i] : Str("PORT_MEMORY_NOT_MET"), Col(0xFF, 0xFF, 0xFF, a));
        }
        Rect(kWinX + kListW + 16.0f, listY - 4.0f, 2.0f, kWinH - 50.0f, Col(0x40, 0x48, 0x60, 0xFF), 0.0f);

        // the highlighted trainer's team
        const float x = kWinX + kListW + 30.0f, w = kWinX + kWinW - 12.0f - x;
        float y = listY;
        if (v.cursor >= 0 && v.cursor < PORT_MEM_ROWS && v.reached[v.cursor]) {
            Txt(x, y, 18.0f, w, v.rowNames[v.cursor], Col(0xFF, 0xFF, 0xFF, 0xFF));
        }
        y += 30.0f;
        if (v.teamCount == 0) {
            Txt(x, y, 14.0f, w, Str("PORT_MEMORY_TEAM_UNKNOWN"), Col(0xA0, 0xA0, 0xA0, 0xFF));
            return;
        }
        const float monH = std::min(48.0f, (kWinY + kWinH - 8.0f - y) / v.teamCount);
        const float icon = std::min(40.0f, monH - 6.0f);
        const float textX = x + icon + 8.0f, movesX = x + 170.0f, mw = (x + w - movesX) / 2;
        for (int i = 0; i < v.teamCount && i < HU_TM_TEAM_MAX; i++) {
            const PortEnhMemoryMon& m = v.team[i];
            const float ry = y + i * monH;
            Rect(x - 4.0f, ry - 2.0f, w + 8.0f, monH - 4.0f, Col(0x20, 0x28, 0x40, 0xC0), 4.0f);
            Rect(x, ry, icon, icon, Col(0x30, 0x3C, 0x60, 0xFF), 6.0f);
            if (ImTextureID tex = IconTexture(m.iconKey)) {
                mDl->AddImageRounded(tex, P(x, ry), P(x + icon, ry + icon), ImVec2(0, 0), ImVec2(1, 1),
                                     Col(0xFF, 0xFF, 0xFF, 0xFF), 6.0f * mScale);
            }
            Txt(textX, ry, 14.0f, movesX - textX - 6.0f, m.name, Col(0xFF, 0xFF, 0xFF, 0xFF));
            char buf[48];
            if (m.level != 0) {
                snprintf(buf, sizeof buf, "L%u", m.level);
                Txt(textX, ry + 15.0f, 12.0f, 0, buf, Col(0xE0, 0xE0, 0xE0, 0xFF));
            }
            if (m.hasSpeed) {
                const char* label = Str("PORT_SUMMARY_SPEED");
                if (m.speed.min == m.speed.max) snprintf(buf, sizeof buf, "%s %u", label, m.speed.min);
                else snprintf(buf, sizeof buf, "%s %u-%u", label, m.speed.min, m.speed.max);
                Txt(textX, ry + 28.0f, 12.0f, movesX - textX - 6.0f, buf, Col(0x90, 0xD8, 0xFF, 0xFF));
            }
            // the moves seen, two by two; the others as dashes
            for (int k = 0; k < HU_TM_MOVES; k++) {
                if (m.moves[k][0] == '\0') continue; // an empty slot
                const float kx = movesX + (k & 1) * mw, ky = ry + (k >> 1) * 19.0f + 3.0f;
                const bool seen = !(m.moves[k][0] == '-' && m.moves[k][1] == '\0');
                const hu_rgb c = seen ? hu_type_text_color((hu_type)m.moveTypes[k]) : hu_rgb{ 0xFF, 0xFF, 0xFF };
                Txt(kx, ky, 14.0f, mw - 6.0f, m.moves[k], Col(c.r, c.g, c.b, seen ? 0xFF : 0x80));
            }
        }
    }
};

} // namespace

void PortOverlay::InstallMemoryScreen(void* gui) {
    auto w = std::make_shared<MemoryScreenWindow>();
    static_cast<Ship::Gui*>(gui)->AddGuiWindow(w);
    w->Show();
}
