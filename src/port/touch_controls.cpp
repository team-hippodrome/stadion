// On-screen N64 controller for touch screens: shown on Android and in the web build on touch screens while no gamepad
// is connected (PORT_TOUCH_CONTROLS=1 shows it always, also on desktop for testing; =0 never). Analog stick bottom left
// with the D-pad above it, A, B and the C buttons on the right, L and Z top left, R top right, Start at the bottom
// centre. Fingers are read from SDL's touch state every frame (several at once: the stick and a button together) and
// act as controller 1, on top of whatever the ControlDeck read; the overlay is drawn translucent over the game with
// ImGui.
#include "touch_controls.h"

#include <SDL3/SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <imgui.h>
#include <libultraship/libultraship.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

namespace {

enum Shape { kRound, kPill };

struct Control {
    const char* label;
    uint16_t button; // 0: the D-pad or the C buttons as a whole (four directions)
    float x, y;      // centre, in screen heights from the left (negative: from the right) and top
    float r;         // radius, in screen heights
    uint32_t color;  // RGB
    Shape shape;
};

// four-way clusters: up, down, left, right
const uint16_t kDpad[4] = { CONT_UP, CONT_DOWN, CONT_LEFT, CONT_RIGHT };
const uint16_t kCButtons[4] = { CONT_E, CONT_D, CONT_C, CONT_F };

const Control kControls[] = {
    { "A", CONT_A, -0.17f, 0.80f, 0.075f, 0x3B5BDB, kRound },
    { "B", CONT_B, -0.32f, 0.72f, 0.060f, 0x2F9E44, kRound },
    { "C", 0, -0.20f, 0.50f, 0.115f, 0xF2C94C, kRound }, // the C buttons
    { "D", 0, 0.17f, 0.42f, 0.100f, 0x868E96, kRound },  // the D-pad
    { "L", CONT_L, 0.12f, 0.08f, 0.055f, 0xADB5BD, kPill },
    { "Z", CONT_G, 0.30f, 0.08f, 0.055f, 0xADB5BD, kPill },
    { "R", CONT_R, -0.12f, 0.08f, 0.055f, 0xADB5BD, kPill },
    { "START", CONT_START, 0.5f, 0.93f, 0.045f, 0xE5484D, kPill }, // (x: half the width, see Centre)
};
constexpr float kStickX = 0.20f, kStickY = 0.76f, kStickR = 0.13f; // the stick's base
constexpr int kStickMax = 80;                                     // an N64 stick's reach

struct State {
    bool visible = false;
    uint16_t buttons = 0;
    float stickDx = 0, stickDy = 0; // knob offset, -1..1
    bool stickHeld = false;
    SDL_FingerID stickFinger = 0;
    float width = 1, height = 1; // the window, in ImGui points (fingers come normalized to it)
} gState;

bool Enabled() {
    static const int forced = getenv("PORT_TOUCH_CONTROLS") ? atoi(getenv("PORT_TOUCH_CONTROLS")) : -1;
    if (forced >= 0) {
        return forced != 0;
    }
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#if defined(__EMSCRIPTEN__)
    // web build: on touch-first devices (phones, tablets: the primary pointer is coarse), as on Android
    static const bool touchScreen = EM_ASM_INT({ return matchMedia("(pointer: coarse)").matches ? 1 : 0; }) != 0;
    if (!touchScreen) {
        return false;
    }
#endif
    int n = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&n);
    SDL_free(pads);
    return n == 0;
#else
    return false;
#endif
}

ImVec2 Centre(const Control& c) {
    const float h = gState.height;
    float x = c.x < 0 ? gState.width + c.x * h : c.x * h;
    if (c.button == CONT_START) {
        x = gState.width * c.x;
    }
    return ImVec2(x, c.y * h);
}

ImVec2 StickCentre() {
    return ImVec2(kStickX * gState.height, kStickY * gState.height);
}

// the button(s) a finger at p presses (clusters: the direction from their centre; diagonals press two)
uint16_t Hit(ImVec2 p) {
    uint16_t pressed = 0;
    for (const Control& c : kControls) {
        ImVec2 o = Centre(c);
        float dx = p.x - o.x, dy = p.y - o.y, r = c.r * gState.height;
        if (c.shape == kPill) {
            if (std::fabs(dx) < r * 1.9f && std::fabs(dy) < r * 1.3f) pressed |= c.button;
            continue;
        }
        float d = std::sqrt(dx * dx + dy * dy);
        if (d > r * 1.25f) continue;
        if (c.button != 0) {
            pressed |= c.button;
            continue;
        }
        const uint16_t* dirs = c.label[0] == 'C' ? kCButtons : kDpad;
        if (d < r * 0.25f) continue; // the centre: no direction
        if (dy < -0.4f * d) pressed |= dirs[0];
        if (dy > 0.4f * d) pressed |= dirs[1];
        if (dx < -0.4f * d) pressed |= dirs[2];
        if (dx > 0.4f * d) pressed |= dirs[3];
    }
    return pressed;
}

// Taps shorter than a frame (the finger is up again before the next poll) would be lost: finger-down events are
// latched as they arrive (an event watch, on the thread that delivers them) and their buttons held for a few frames.
std::mutex gTapLock;
struct Tap {
    float x, y; // normalized
    std::chrono::steady_clock::time_point until;
};
std::vector<Tap> gTaps;

bool OnEvent(void*, SDL_Event* e) {
    if (e->type == SDL_EVENT_FINGER_DOWN && gState.visible) {
        std::lock_guard<std::mutex> g(gTapLock);
        gTaps.push_back({ e->tfinger.x, e->tfinger.y, std::chrono::steady_clock::now() + std::chrono::milliseconds(70) });
    }
    return true;
}

uint32_t Col(uint32_t rgb, float a) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(a * 255));
}

// draws the overlay over everything (ImGui's foreground list) while it is active
class TouchControlsWindow : public Ship::GuiWindow {
  public:
    TouchControlsWindow() : GuiWindow("gTouchControlsOpen", "Touch controls") {
    }
    void Draw() override {
        if (!gState.visible) return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float h = gState.height;
        for (const Control& c : kControls) {
            ImVec2 o = Centre(c);
            float r = c.r * h;
            if (c.shape == kPill) {
                bool on = (gState.buttons & c.button) != 0;
                ImVec2 a(o.x - r * 1.7f, o.y - r * 0.8f), b(o.x + r * 1.7f, o.y + r * 0.8f);
                dl->AddRectFilled(a, b, Col(c.color, on ? 0.55f : 0.25f), r);
                dl->AddRect(a, b, Col(0xFFFFFF, 0.35f), r, 0, 2);
                Label(dl, o, c.label, r * 0.7f);
                continue;
            }
            if (c.button != 0) {
                bool on = (gState.buttons & c.button) != 0;
                dl->AddCircleFilled(o, r, Col(c.color, on ? 0.6f : 0.3f));
                dl->AddCircle(o, r, Col(0xFFFFFF, 0.35f), 0, 2);
                Label(dl, o, c.label, r * 0.9f);
                continue;
            }
            // a cluster: four small buttons (C) or a cross (D-pad)
            const bool cb = c.label[0] == 'C';
            const uint16_t* dirs = cb ? kCButtons : kDpad;
            const ImVec2 offs[4] = { ImVec2(0, -1), ImVec2(0, 1), ImVec2(-1, 0), ImVec2(1, 0) };
            for (int i = 0; i < 4; i++) {
                bool on = (gState.buttons & dirs[i]) != 0;
                ImVec2 p(o.x + offs[i].x * r * 0.62f, o.y + offs[i].y * r * 0.62f);
                if (cb) {
                    dl->AddCircleFilled(p, r * 0.36f, Col(c.color, on ? 0.65f : 0.3f));
                    dl->AddCircle(p, r * 0.36f, Col(0xFFFFFF, 0.3f), 0, 2);
                } else {
                    float s = r * 0.34f;
                    dl->AddRectFilled(ImVec2(p.x - s, p.y - s), ImVec2(p.x + s, p.y + s), Col(c.color, on ? 0.65f : 0.3f), 4);
                }
            }
            if (cb) Label(dl, o, "C", r * 0.35f);
        }
        // the stick: base and knob
        ImVec2 s = StickCentre();
        float r = kStickR * h;
        dl->AddCircleFilled(s, r, Col(0x495057, 0.25f));
        dl->AddCircle(s, r, Col(0xFFFFFF, 0.3f), 0, 2);
        ImVec2 k(s.x + gState.stickDx * r, s.y + gState.stickDy * r);
        dl->AddCircleFilled(k, r * 0.45f, Col(0xCED4DA, gState.stickHeld ? 0.6f : 0.35f));
    }

  protected:
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

  private:
    static void Label(ImDrawList* dl, ImVec2 o, const char* text, float size) {
        ImFont* f = ImGui::GetFont();
        ImVec2 ts = f->CalcTextSizeA(size, 1e9f, 0, text);
        dl->AddText(f, size, ImVec2(o.x - ts.x / 2, o.y - ts.y / 2), Col(0xFFFFFF, 0.8f), text);
    }
};

} // namespace

void Port_TouchControlsApply(void* pad0, void* gui) {
    static bool added = false;
    if (!added && gui != nullptr) {
        added = true;
        auto w = std::make_shared<TouchControlsWindow>();
        static_cast<Ship::Gui*>(gui)->AddGuiWindow(w);
        w->Show();
        SDL_AddEventWatch(OnEvent, nullptr);
    }
    static int checkIn = 0;
    if (checkIn-- <= 0) { // a gamepad connected or gone: twice a second is soon enough
        checkIn = 30;
        gState.visible = Enabled();
    }
    if (!gState.visible) {
        gState.buttons = 0;
        gState.stickHeld = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    gState.width = std::max(1.0f, io.DisplaySize.x);
    gState.height = std::max(1.0f, io.DisplaySize.y);

    uint16_t buttons = 0;
    bool stickSeen = false;
    float sdx = 0, sdy = 0;
    const ImVec2 sc = StickCentre();
    const float sr = kStickR * gState.height;
    int nd = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&nd);
    for (int d = 0; d < nd; d++) {
        int nf = 0;
        SDL_Finger** fingers = SDL_GetTouchFingers(devices[d], &nf);
        for (int i = 0; i < nf; i++) {
            const SDL_Finger* f = fingers[i];
            ImVec2 p(f->x * gState.width, f->y * gState.height);
            float dx = p.x - sc.x, dy = p.y - sc.y;
            bool onStick = gState.stickHeld && f->id == gState.stickFinger;
            if (!onStick && !stickSeen && std::sqrt(dx * dx + dy * dy) < sr * 1.4f) {
                onStick = true; // a finger that comes down on the stick drives it until it lifts
                gState.stickFinger = f->id;
            }
            if (onStick) {
                stickSeen = true;
                float len = std::sqrt(dx * dx + dy * dy);
                float k = len > sr ? sr / len : 1.0f;
                sdx = dx * k / sr;
                sdy = dy * k / sr;
            } else {
                buttons |= Hit(p);
            }
        }
        SDL_free(fingers);
    }
    SDL_free(devices);
    {
        std::lock_guard<std::mutex> g(gTapLock);
        auto now = std::chrono::steady_clock::now();
        gTaps.erase(std::remove_if(gTaps.begin(), gTaps.end(), [&](const Tap& t) { return t.until < now; }), gTaps.end());
        for (const Tap& t : gTaps) {
            buttons |= Hit(ImVec2(t.x * gState.width, t.y * gState.height));
        }
    }
    gState.stickHeld = stickSeen;
    gState.stickDx = stickSeen ? sdx : 0;
    gState.stickDy = stickSeen ? sdy : 0;
    gState.buttons = buttons;

    OSContPad* pad = static_cast<OSContPad*>(pad0);
    pad->button |= buttons;
    if (stickSeen) {
        pad->stick_x = (int8_t)std::lround(sdx * kStickMax);
        pad->stick_y = (int8_t)std::lround(-sdy * kStickMax); // (screen y grows downwards)
    }
}
