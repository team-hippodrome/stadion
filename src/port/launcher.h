#pragma once
#include <memory>
#include <string>

namespace Fast {
class Fast3dWindow;
}

namespace Port {
// The launcher (launcher.cpp) in the game window: returns the chosen ROM after setting the save and Transfer Pak
// environment, or "" when the user quit. Shows nothing when "start directly" is on (unless Shift is held).
std::string RunLauncher(const std::shared_ptr<Fast::Fast3dWindow>& window);
// Packaged builds (the AppImage, the macOS app) set SHIP_HOME, the per-user folder for config, logs and saves: there,
// the files the port writes by default (the input recording, the save of a game started with a ROM argument) go to that
// folder too instead of the current directory. Developer builds keep the current directory.
void UseShipHomeDefaults();
} // namespace Port
