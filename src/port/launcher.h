#pragma once
#include <memory>
#include <string>

namespace Fast {
class Fast3dWindow;
}

namespace Port {
// The launcher (launcher.cpp) in the game window: returns the chosen ROM after setting the save and Transfer Pak
// environment, or "" when the user quit. Shows nothing when "start directly" is on (unless Shift is held).
std::string RunLauncher(const std::shared_ptr<Fast::Fast3dWindow>& window, bool& restart);
// Default recordings and saves follow the same data folder as the config on every platform.
// Desktop data lives beside the launcher artifact; Android/iOS use their writable app-files directory.
// SHIP_HOME explicitly overrides that directory.
void UseShipHomeDefaults();
} // namespace Port
