// The save editor inside the launcher (Debug → Save editor, a Transfer Pak card's Edit save, Files & help on the
// handheld layout): the Stadium save and the Game Boy saves of the Transfer Paks, edited with the C++ save library
// (stadium_save.h). It replaces tools/save_editor's web page, which needed Python.
#pragma once

#include <imgui.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Port {

// how the launcher draws: its scale, fonts and gamepad focus (LauncherWindow::NavItem)
struct SaveEditorStyle {
    float scale = 1.0f;
    ImFont* body = nullptr;
    ImFont* small = nullptr;
    ImFont* game = nullptr;
    ImFont* title = nullptr;
    bool handheld = false;
    std::function<bool()> navItem; // the last ImGui item as a focus target; true when pressed with A
    std::function<void()> setDefaultFocus; // the next item is the page's default focus
    std::function<void()> resetFocus;      // the view changed: back to the default focus next frame
};

struct SaveEditorGb {
    std::string label; // "Player 1"
    std::string save, rom;
};

class SaveEditor {
  public:
    SaveEditor();
    ~SaveEditor();
    // Opens the editor on the Stadium save, with the Game Boy saves next to it; section: -1 the overview, n the
    // Game Boy save n. False (and *error) when the save cannot be read.
    bool Open(const std::string& save, const std::vector<SaveEditorGb>& gbs, int gbSection, std::string* error);
    bool IsOpen() const;
    void Close();
    // draws the editor in the rectangle (screen coordinates); it closes itself with its Close button
    void Draw(const SaveEditorStyle& style, ImVec2 min, ImVec2 max);
    // B / Escape: leaves a picker, the Pokémon dialog or a question first; true when it used the press
    bool Back();
    bool HasUnsavedChanges() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Port
