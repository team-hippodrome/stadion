#pragma once
// Which physical controller plays which N64 player (pad_assign.cpp).
//
// libultraship hands every gamepad to player 1 and tells players 2-4 to ignore it, so a second controller drove
// player 1 too and players 2-4 could never get one. The port assigns the connected gamepads to players in the order
// they were connected (the first one, the Thor's built-in controls on an AYN Thor, is player 1), keeps a player the
// launcher's Controls panel chose for a controller (gPort.PadPlayer.<GUID>), and re-assigns when one connects or
// disconnects. The keyboard stays player 1's.
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Gamepad;

namespace Port {

struct PadInfo {
    int32_t id = 0;       // SDL joystick instance id
    std::string name;     // as SDL names it
    std::string guid;     // the model's GUID (two identical controllers share it)
    int port = -1;        // 0-3, -1: none (more than four connected)
    SDL_Gamepad* pad = nullptr;
};

// Applies the assignment when the connected controllers or the chosen players changed (cheap otherwise). Call it on
// the thread that handles window events, each frame.
void UpdatePadAssignment();
// the connected gamepads, by player (unassigned last)
std::vector<PadInfo> ConnectedPads();
// the players that have a controller, as N64 controller bits (bit 0 = player 1); safe from any thread
int PadPortBits();
// puts controller `id` on player `port` (the controller that was there takes this one's old player); remembered
void SetPadPort(int32_t id, int port);
// true while any button of the player's controller is held or a stick is pushed (the Controls panel's light)
bool PadActive(int port);
// libultraship's Xbox names for a gamepad input ("A", "LB", "View", "LT"...) as the player's controller labels them
// ("Cross", "L1", "Share", "L2" on a PlayStation pad; "B", "L", "-", "ZL" on a Nintendo one)
std::string PadInputLabel(int port, const std::string& xboxName);

// joysticks SDL does not know as a gamepad (no mapping): the launcher offers to set them up
struct UnknownJoystick {
    int32_t id = 0;
    std::string name, guid;
};
std::vector<UnknownJoystick> UnknownJoysticks();
// adds an SDL mapping ("<guid>,<name>,a:b0,...") for this session and to gamecontrollerdb.txt in the app folder (read
// at the next start); false with `error` set when SDL refuses it or the file cannot be written
bool AddPadMapping(const std::string& mapping, std::string& error);

// test hook (PORT_TEST_UNKNOWN_PAD=1, launcher scripts "vpad <button>|-"): a virtual joystick SDL has no mapping for,
// to walk through the setup without such a controller; PressTestPad(-1) releases everything
void AttachTestPad();
void PressTestPad(int button);

} // namespace Port
