// The save editor inside the launcher. Same pages as tools/save_editor's web page (editor.html) without its raw JSON
// view: overview and repair, progress, options, registered teams, Lab boxes (and moving Pokémon to and from the
// Game Boy games), item boxes, Victory Palace, the saved cup run, the gallery and the Game Boy saves. It edits the save
// the way the page did: a dump of the file ("base") and an edited copy ("cur"); Save rebuilds the file from base and
// commits every record that changed (EncodeEdit), keeping the previous file as .bak.
//
// Everything is plain ImGui in the launcher's look. For the handheld layout every control is a gamepad focus target
// (Style::navItem); lists that would need a drop-down (species, moves, items) open a picker page instead, and the
// Pokémon dialog and questions replace the page, so the focus never lands on something hidden.
#include "save_editor_ui.h"
#include "stadium_save.h"
#include "gamedata.h"

#include <SDL3/SDL.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace Port {

using namespace stadium_save;
namespace gd = stadium_save::gamedata;

namespace {

// Launcher.swift's colours (as launcher.cpp's Theme)
constexpr uint32_t kCard = 0x1A1F27, kBorder = 0x2A313C, kText = 0xE6E9EE, kDim = 0x8B95A3, kGreen = 0x2EA043,
                   kRed = 0xE5484D, kButton = 0x2A313C, kButtonHover = 0x353D4A, kSelected = 0x23406B, kWarn = 0xD29922,
                   kWindow = 0x12161D;

ImU32 Col(uint32_t rgb, float a = 1.0f) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, (int)(a * 255));
}
ImVec4 ColV(uint32_t rgb) {
    return ImGui::ColorConvertU32ToFloat4(Col(rgb));
}

uint32_t TypeColor(const char* t) {
    static const std::map<std::string, uint32_t> colors = {
        { "Normal", 0x9A9A6E }, { "Fighting", 0xC03028 }, { "Flying", 0x8F7AE0 }, { "Poison", 0xA040A0 },
        { "Ground", 0xD6B45A }, { "Rock", 0xB8A038 },     { "Bug", 0x98A820 },    { "Ghost", 0x705898 },
        { "Fire", 0xF08030 },   { "Water", 0x6890F0 },    { "Grass", 0x5CB041 },  { "Electric", 0xE8B800 },
        { "Psychic", 0xF85888 }, { "Ice", 0x6CC8C8 },     { "Dragon", 0x7038F8 },
    };
    if (t == nullptr) return 0x666666;
    auto it = colors.find(t);
    return it != colors.end() ? it->second : 0x888888;
}

// ImGui's built-in font has Latin-1 only: the gender signs as letters, the PK and MN ligatures as their letters
std::string Show(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s.compare(i, 3, "♀") == 0) out += " F", i += 2;
        else if (s.compare(i, 3, "♂") == 0) out += " M", i += 2;
        else if (s.compare(i, 4, "<PK>") == 0) out += "PK", i += 3;
        else if (s.compare(i, 4, "<MN>") == 0) out += "MN", i += 3;
        else out += s[i];
    }
    return out;
}

// A name as it is typed in a text field: the font cannot draw ♂ and ♀, so they are written <M> and <F> (like <PK>,
// <MN>); the codec accepts both spellings.
std::string FieldForm(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s.compare(i, 3, "♀") == 0) out += "<F>", i += 2;
        else if (s.compare(i, 3, "♂") == 0) out += "<M>", i += 2;
        else out += s[i];
    }
    return out;
}

// Characters the Game Boy games (and so the game's names) can hold that a keyboard may not have, for the picker:
// {text inserted, label}. The labels stay in ASCII/Latin-1 for the built-in font.
const std::vector<std::pair<std::string, std::string>>& GbSymbols() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        { "<M>", "<M>  male sign" },     { "<F>", "<F>  female sign" },    { "<PK>", "<PK>  PK" },
        { "<MN>", "<MN>  MN" },          { "é", "é  e acute" },            { "×", "×  multiplication" },
        { "'", "'  apostrophe" },        { "-", "-  hyphen" },             { ".", ".  period" },
        { ",", ",  comma" },             { "/", "/  slash" },              { "?", "?  question mark" },
        { "!", "!  exclamation mark" },  { "(", "(  open parenthesis" },   { ")", ")  close parenthesis" },
        { ":", ":  colon" },             { ";", ";  semicolon" },          { "[", "[  open bracket" },
        { "]", "]  close bracket" },     { "¥", "¥  yen" },                { " ", "(space)" },
        { "à", "à" }, { "è", "è" }, { "ù", "ù" }, { "ç", "ç" }, { "ß", "ß" },
        { "ä", "ä" }, { "ö", "ö" }, { "ü", "ü" }, { "Ä", "Ä" }, { "Ö", "Ö" }, { "Ü", "Ü" },
        { "â", "â" }, { "ê", "ê" }, { "î", "î" }, { "ô", "ô" }, { "û", "û" }, { "ë", "ë" }, { "ï", "ï" },
    };
    return v;
}

// a file's name, as UTF-8
std::string FileNameOf(const std::string& path) {
    const auto u = std::filesystem::u8path(path).filename().u8string();
    return std::string(u.begin(), u.end());
}

std::string Pad(long long v, int width) {
    std::string s = std::to_string(v);
    while ((int)s.size() < width) s = "0" + s;
    return s;
}

int I(const json& v) {
    try {
        return (int)PyInt(v);
    } catch (...) {
        return 0;
    }
}

int SpeciesDex(const json& mon) {
    if (mon.is_object() && mon.contains("species")) {
        try {
            int d = pokemon::SpeciesLookup(mon["species"]);
            return d > 0 && d <= 151 ? d : 0;
        } catch (...) {
        }
    }
    return 0;
}

std::string MonName(const json& m) {
    if (m.contains("species") && m["species"].is_string()) return Show(m["species"].get<std::string>());
    return "Unknown #" + std::to_string(I(m.value("species_index", json(0))));
}

json EmptyList() {
    return json{ { "flags", 0 }, { "name", "" }, { "trainer_id", 0 }, { "entries", json::array() } };
}

struct Cat {
    const char *key, *name, *rule;
    int level;
};
const Cat kCats[] = {
    { "free_battle", "Free Battle", "Any Pokémon at any level.", 50 },
    { "poke_cup", "Poké Cup", "Levels 50-55; the three you send in add up to level 155 at most.", 50 },
    { "petit_cup", "Petit Cup", "Levels 25-30 (three add up to 80 at most), small Pokémon only.", 25 },
    { "pika_cup", "Pika Cup", "Levels 15-20 (three add up to 50 at most).", 15 },
    { "prime_cup", "Prime Cup", "Levels 1-100.", 100 },
    { "gym_leader_castle", "Gym Leader Castle", "Any level up to 100.", 50 },
    { "mewtwo", "Vs. Mewtwo", "Your team for the Mewtwo battle.", 100 },
    { "category_1", "Hidden cup 1", "A Poké Cup copy that the game's menus never show.", 50 },
    { "category_2", "Hidden cup 2", "A Poké Cup copy that the game's menus never show.", 50 },
};
const std::pair<int, const char*> kCupBits[] = { { 3, "Poké Cup" }, { 4, "Petit Cup" }, { 5, "Pika Cup" },
                                                 { 6, "Prime Cup" }, { 7, "Gym Leader Castle" }, { 8, "Beat Mewtwo" } };
const std::pair<int, const char*> kStatueModes[] = { { 3, "Poké Cup" }, { 4, "Petit Cup" }, { 5, "Pika Cup" },
                                                     { 6, "Prime Cup" }, { 7, "Castle" } };
const char* const kStatNames[5] = { "HP", "Attack", "Defense", "Speed", "Special" };

struct Section {
    std::string key, name;
};

} // namespace

struct SaveEditor::Impl {
    struct Gb {
        std::string label, title, kind, path, rom, name;
        json base, cur;
    };
    struct MonCtx {
        std::string title;
        json mon; // null: a new one
        json otName = "RED";
        int otId = 0, level = 50;
        std::function<void(const json&)> onDone;
        std::function<void()> onRemove; // empty: no Remove button
    };
    struct MonDlg {
        MonCtx ctx;
        int tab = 0;
        std::string species;
        json nickname = "";
        int level = 50;
        std::string moves[4];
        int ppUps[4] = {};
        json otName = "RED";
        int otId = 0;
        int dvs[4] = { 15, 15, 15, 15 };
        int se[5] = {};
        json preview;
        std::vector<std::string> warnings;
        std::string error;
    };
    struct Picker {
        std::string title;
        std::vector<std::pair<int, std::string>> items;
        int current = -1;
        std::function<void(int)> onPick;
        char filter[32] = {};
        bool focusFilter = true;
    };
    struct Confirm {
        std::string text, yes;
        std::function<void()> action;
        std::string no = "Cancel";
        std::function<void()> other; // a third button (Close without saving)
        std::string otherLabel;
    };

    bool open = false;
    std::string path, fileName;
    json base, cur;
    std::vector<Gb> gbs;
    bool dirty = false;

    std::string section = "overview";
    std::string cat = "poke_cup";
    int slot = 0, box = 0, ibox = 0, dex = 25, gbTab = 1, gbBox = 0, xg = 0, xstore = -1; // xstore -1: party
    std::optional<MonDlg> dlg;
    std::optional<Picker> picker;
    std::map<ImGuiID, std::string> pendingSymbol; // a Symbols button pick, taken by its name field next frame
    std::optional<Confirm> confirm;
    std::string toast, error;
    uint64_t toastUntil = 0;
    ImGuiID focusField = 0;
    std::map<ImGuiID, std::string> editBuf;
    SaveEditorStyle style;       // the launcher's, as of the last frame
    const SaveEditorStyle* st = &style;
    std::string scrollTo; // reset the content's scroll on the next frame

    // ---- loading and saving ----

    void Load() {
        base = SaveFile::Load(path).ToDict();
        cur = base;
        for (Gb& g : gbs) {
            g.base = gb::GBSave(ReadFile(g.path)).ToDict();
            g.cur = g.base;
        }
        Changed();
    }

    void Changed() {
        dirty = cur != base;
        for (const Gb& g : gbs) dirty |= g.cur != g.base;
    }

    void Toast(const std::string& t) {
        toast = t;
        toastUntil = SDL_GetTicks() + 3000;
    }

    void Save() {
        try {
            std::vector<std::pair<std::string, Bytes>> writes;
            Bytes disk = ReadFile(path);
            if (SaveFile::FromDict(base).data != disk) {
                throw Error(fileName + " changed on disk since it was opened. Discard to load it again.");
            }
            Bytes data = EncodeEdit(base, cur);
            if (data != disk) writes.push_back({ path, data });
            for (const Gb& g : gbs) {
                if (g.cur == g.base) continue;
                Bytes gdisk = ReadFile(g.path);
                if (gb::GBSave(gdisk).ToDict() != g.base) {
                    throw Error(g.name + " changed on disk since it was opened. Discard to load it again.");
                }
                Bytes gdata = gb::ApplyEdit(gdisk, g.base, g.cur);
                if (gdata != gdisk) writes.push_back({ g.path, gdata });
            }
            std::string names;
            for (auto& [p, d] : writes) { // all built first: an error above writes nothing
                WriteWithBackup(p, d);
                names += (names.empty() ? "" : " and ") + FileNameOf(p);
            }
            Load();
            Toast(writes.empty() ? "Nothing to save" : "Saved " + names + " (the previous version is kept as .bak)");
            error.clear();
        } catch (const std::exception& e) {
            error = std::string("The save could not be written: ") + e.what();
        }
    }

    void Repair() {
        try {
            SaveFile s(EncodeEdit(base, cur));
            s.Repair();
            cur = s.ToDict();
            Changed();
            Toast("Repaired. Save to keep it.");
        } catch (const std::exception& e) {
            error = e.what();
        }
    }

    // ---- record helpers (editor.html) ----

    json ListRecord(const json& rec) const {
        return IsValidRecord(rec) ? rec : EmptyList();
    }

    // edit a list record; an unreadable one starts from the game's reset state
    void EditList(json& rec, const std::function<void(json&)>& fn) {
        if (!IsValidRecord(rec)) {
            rec = EmptyList();
            rec["_footer"] = "valid";
        }
        fn(rec);
        rec.erase("count_raw");
        rec.erase("_unused_entries");
        Changed();
    }

    static bool TeamRegistered(const json& t) {
        return IsValidRecord(t) && (I(t["flags"]) & 1) && !t["entries"].empty();
    }

    std::vector<std::pair<std::string, json*>> AllRecords() {
        std::vector<std::pair<std::string, json*>> out;
        for (const RecordRef& ref : Records()) out.push_back({ ref.key(), &GetPath(cur, ref.path) });
        return out;
    }

    int RegisteredTeams() {
        int n = 0;
        for (const char* c : TEAM_CATEGORIES)
            for (const json& t : cur["teams"][c]) n += TeamRegistered(t);
        return n;
    }
    int BoxedCount() {
        int n = 0;
        for (const json& b : cur["boxes"]) n += (int)ListRecord(b)["entries"].size();
        return n;
    }
    int ItemCount() {
        int n = 0;
        for (const json& b : cur["item_boxes"]) n += (int)ListRecord(b)["entries"].size();
        return n;
    }
    int Statues() {
        int n = 0;
        for (const json& e : cur["victory_palace"]["entries"]) n += !e.is_null();
        return n;
    }

    // ---- widgets ----

    float S(float v) const { return v * st->scale; }
    float RowH() const { return st->handheld ? 38.0f : 24.0f; }
    bool Nav() const { return st->navItem ? st->navItem() : false; }
    void ResetFocus() const {
        if (st->resetFocus) st->resetFocus();
    }

    bool Btn(const char* label, float w = 0, float h = 0) {
        bool clicked = ImGui::Button(label, ImVec2(w > 0 ? S(w) : 0, S(h > 0 ? h : RowH())));
        return Nav() || clicked;
    }

    bool ColorBtn(const char* label, uint32_t color, float w = 0, float h = 0) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColV(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ColV(color == kButton ? kButtonHover : color));
        bool c = Btn(label, w, h);
        ImGui::PopStyleColor(2);
        return c;
    }

    bool DangerBtn(const char* label, float w = 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ColV(0xFF8A8D));
        bool c = Btn(label, w);
        ImGui::PopStyleColor();
        return c;
    }

    // a row of buttons, the chosen one highlighted; the index clicked or -1 (launcher.cpp Choice)
    int Seg(const char* id, const std::vector<std::string>& labels, int chosen, float w = 0) {
        int clicked = -1;
        ImGui::PushID(id);
        for (int i = 0; i < (int)labels.size(); i++) {
            ImGui::PushID(i);
            if (i > 0) SameLineWrap(w > 0 ? S(w) : ImGui::CalcTextSize(labels[i].c_str()).x + S(16));
            if (ColorBtn(labels[i].c_str(), i == chosen ? kSelected : kButton, w)) clicked = i;
            ImGui::PopID();
        }
        ImGui::PopID();
        return clicked;
    }

    // SameLine when the next item of this width still fits
    void SameLineWrap(float nextW) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < nextW) ImGui::NewLine();
    }

    bool Check(const char* label, bool* v) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ColV(kButton)); // (fields are darker; a box must stand out)
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ColV(kButtonHover));
        bool changed = ImGui::Checkbox(label, v);
        ImGui::PopStyleColor(2);
        if (Nav()) {
            *v = !*v;
            changed = true;
        }
        return changed;
    }

    void Label(const char* text, float width = 150) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(S(width));
    }

    void Hint(const std::string& text, uint32_t color = kDim) {
        ImGui::PushStyleColor(ImGuiCol_Text, ColV(color));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    void Heading(const std::string& text) {
        if (st->game) ImGui::PushFont(st->game);
        ImGui::TextUnformatted(text.c_str());
        if (st->game) ImGui::PopFont();
    }

    // A text field that commits when it loses focus (or Enter); with the gamepad, A starts typing.
    std::optional<std::string> TextField(const char* id, const std::string& value, float w, bool upper, bool digits = false) {
        ImGuiID wid = ImGui::GetID(id);
        std::string& buf = editBuf[wid];
        if (ImGui::GetActiveID() != wid) buf = value;
        if (focusField == wid) {
            ImGui::SetKeyboardFocusHere();
            focusField = 0;
        }
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s", buf.c_str());
        ImGui::SetNextItemWidth(S(w));
        ImGuiInputTextFlags flags = (upper ? ImGuiInputTextFlags_CharsUppercase : 0) |
                                    (digits ? ImGuiInputTextFlags_CharsDecimal : 0) | ImGuiInputTextFlags_AutoSelectAll;
        ImGui::InputText(id, tmp, digits ? 8 : sizeof(tmp), flags);
        buf = tmp;
        bool commit = ImGui::IsItemDeactivatedAfterEdit();
        if (Nav()) focusField = wid;
        if (commit) return upper ? Upper(buf) : buf;
        return std::nullopt;
    }

    // A Game Boy name (up to 10 characters); refuses what the Game Boy alphabet cannot write. Besides the keyboard, a
    // "Symbols" button picks ♂ ♀ PK MN é × and the rest of the Game Boy character set; the field spells ♂ ♀ as <M> <F>.
    std::optional<std::string> NameField(const char* id, const json& value, size_t size = 11, float w = 150) {
        const std::string current = TextOf(value);
        const ImGuiID wid = ImGui::GetID(id);
        std::optional<std::string> t = TextField(id, FieldForm(current), w, false);
        ImGui::SameLine();
        ImGui::PushID(id);
        if (Btn("Symbols", 64)) {
            std::vector<std::pair<int, std::string>> items;
            for (size_t i = 0; i < GbSymbols().size(); i++) items.push_back({ (int)i, GbSymbols()[i].second });
            OpenPicker("Symbol", std::move(items), -1, [this, wid](int v) { pendingSymbol[wid] = GbSymbols()[v].first; });
        }
        ImGui::PopID();
        auto pend = pendingSymbol.find(wid);
        if (pend != pendingSymbol.end()) {
            t = current + pend->second;
            pendingSymbol.erase(pend);
        }
        if (!t) return std::nullopt;
        try {
            // what the game would read back: <M> -> the sign, e.g. lower case where the alphabet has it
            std::string canon = DecodeGb(EncodeGb(*t, size).data(), size);
            error.clear();
            return canon;
        } catch (const std::exception&) {
            error = "\"" + FieldForm(*t) + "\" cannot be written in the Game Boy alphabet or is too long (" + std::to_string(size - 1) +
                    " characters at most). Letters, digits and the Symbols button's characters work; <PK>, <MN>, <M>, <F> and "
                    "<xx> (a byte in hex) can be typed.";
            return std::nullopt;
        }
    }

    // a number: [-] field [+]; the new value when changed
    std::optional<long long> IntField(const char* id, long long value, long long lo, long long hi, float w = 70, long long step = 1) {
        ImGui::PushID(id);
        std::optional<long long> out;
        if (Btn("-", st->handheld ? 38 : 22)) out = value - step;
        ImGui::SameLine(0, S(2));
        if (std::optional<std::string> t = TextField("##n", std::to_string(value), w, false, true)) {
            try {
                out = std::stoll(t->empty() ? "0" : *t);
            } catch (...) {
            }
        }
        ImGui::SameLine(0, S(2));
        if (Btn("+", st->handheld ? 38 : 22)) out = value + step;
        ImGui::PopID();
        if (out) {
            out = std::clamp(*out, lo, hi);
            if (*out == value) out.reset();
        }
        return out;
    }

    // a button showing the current choice; opens the picker
    void PickButton(const char* id, const std::string& shown, float w, std::function<void()> openPicker) {
        ImGui::PushID(id);
        std::string label = shown + "  v";
        if (Btn(label.c_str(), w)) openPicker();
        ImGui::PopID();
    }

    void OpenPicker(const std::string& title, std::vector<std::pair<int, std::string>> items, int current,
                    std::function<void(int)> onPick) {
        Picker p;
        p.title = title;
        p.items = std::move(items);
        p.current = current;
        p.onPick = std::move(onPick);
        picker = std::move(p);
        ResetFocus();
    }

    static std::vector<std::pair<int, std::string>> SpeciesItems(bool withNone = false) {
        std::vector<std::pair<int, std::string>> v;
        if (withNone) v.push_back({ 0, "None" });
        for (int d = 1; d <= 151; d++) v.push_back({ d, "#" + Pad(d, 3) + " " + Show(gd::SPECIES[d]) });
        return v;
    }
    static std::vector<std::pair<int, std::string>> MoveItems() {
        std::vector<std::pair<int, std::string>> v{ { 0, "- no move -" } };
        std::vector<std::pair<int, std::string>> moves;
        for (int m = 1; m <= 165; m++) {
            const char* t = pokemon::TypeName(gd::MOVE_DATA[m].type);
            std::string s = gd::MOVES[m];
            s += std::string("   ") + (t ? t : "") + (gd::MOVE_DATA[m].power ? " " + std::to_string(gd::MOVE_DATA[m].power) : "") +
                 "  " + std::to_string(gd::MOVE_DATA[m].pp) + " PP";
            moves.push_back({ m, s });
        }
        std::sort(moves.begin(), moves.end(), [](auto& a, auto& b) { return a.second < b.second; });
        v.insert(v.end(), moves.begin(), moves.end());
        return v;
    }
    static std::vector<std::pair<int, std::string>> ItemItems() {
        std::vector<std::pair<int, std::string>> v;
        for (int i = 1; i < gd::kItemCount; i++)
            if (gd::ITEMS[i][0]) v.push_back({ i, gd::ITEMS[i] });
        return v;
    }
    static std::string ItemName(int id) {
        return id >= 0 && id < gd::kItemCount && gd::ITEMS[id][0] ? gd::ITEMS[id] : "Unknown item " + std::to_string(id);
    }

    void Ball(ImDrawList* dl, ImVec2 c, float r, const json& mon) {
        int d = SpeciesDex(mon);
        uint32_t c1 = d ? TypeColor(pokemon::TypeName(gd::BASE[d].type1)) : 0x555555;
        uint32_t c2 = d ? TypeColor(pokemon::TypeName(gd::BASE[d].type2)) : 0x555555;
        dl->PathArcTo(c, r, IM_PI, 2 * IM_PI);
        dl->PathFillConvex(Col(c1));
        dl->PathArcTo(c, r, 0, IM_PI);
        dl->PathFillConvex(Col(c2));
        dl->AddCircle(c, r, Col(0x0B0E13), 0, S(1.5f));
    }

    // a Pokémon: ball, nickname, species and level; true when clicked
    bool MonCard(const char* id, const json& m, float w) {
        ImGui::PushID(id);
        const float h = st->handheld ? 52 : 44;
        bool clicked = ImGui::InvisibleButton("card", ImVec2(S(w), S(h)));
        bool hov = ImGui::IsItemHovered();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        clicked |= Nav();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, Col(hov ? kButtonHover : kButton), S(6));
        Ball(dl, ImVec2(a.x + S(20), (a.y + b.y) / 2), S(11), m);
        std::string nick = Show(TextOf(m["nickname"]));
        std::string name = nick.empty() ? MonName(m) : nick;
        std::string sub = (!nick.empty() && nick != MonName(m) ? MonName(m) + " · " : "") + "Lv " + std::to_string(I(m["level"]));
        const float fs = ImGui::GetFontSize();
        dl->PushClipRect(a, b, true);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + S(38), a.y + (b.y - a.y) / 2 - fs - S(1)), Col(kText), name.c_str());
        dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + S(38), a.y + (b.y - a.y) / 2 + S(1)), Col(kDim), sub.c_str());
        dl->PopClipRect();
        ImGui::PopID();
        return clicked;
    }

    bool EmptyCard(const char* id, const char* label, float w, bool enabled = true) {
        ImGui::PushID(id);
        const float h = st->handheld ? 52 : 44;
        bool clicked = false;
        if (enabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ColV(kCard));
            ImGui::PushStyleColor(ImGuiCol_Border, ColV(kBorder));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S(1));
            clicked = Btn(label, w, h);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        } else {
            ImGui::Dummy(ImVec2(S(w), S(h)));
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(a, b, Col(kBorder, 0.5f), S(6));
        }
        ImGui::PopID();
        return clicked;
    }

    // a clickable summary card (overview)
    bool InfoCard(const char* id, const std::string& title, const std::string& value, const std::string& sub, float w) {
        ImGui::PushID(id);
        bool clicked = ImGui::InvisibleButton("c", ImVec2(S(w), S(70)));
        bool hov = ImGui::IsItemHovered();
        clicked |= Nav();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, Col(hov ? kButtonHover : kButton), S(6));
        const float fs = ImGui::GetFontSize();
        dl->PushClipRect(a, b, true);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + S(10), a.y + S(7)), Col(kDim), title.c_str());
        ImFont* big = st->game ? st->game : ImGui::GetFont();
        dl->AddText(big, big->FontSize * big->Scale * st->scale, ImVec2(a.x + S(10), a.y + S(24)), Col(kText), value.c_str());
        dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + S(10), a.y + S(50)), Col(kDim), sub.c_str());
        dl->PopClipRect();
        ImGui::PopID();
        return clicked;
    }

    // a grid of cells of width w: SameLine unless the row is full
    void GridNext(int i, float w) {
        if (i == 0) return;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < S(w)) ImGui::NewLine();
    }

    // ---- the Pokémon dialog (editor.html openMon) ----

    void OpenMon(MonCtx ctx) {
        MonDlg d;
        const json& m = ctx.mon;
        if (!m.is_null()) {
            int dex = SpeciesDex(m);
            d.species = dex ? gd::SPECIES[dex] : "";
            d.nickname = m["nickname"];
            d.level = I(m["level"]);
            for (int i = 0; i < 4; i++) {
                const json& mv = m["moves"][i];
                if (mv.is_null()) continue;
                int id = 0;
                try {
                    id = pokemon::MoveLookup(mv["move"]);
                } catch (...) {
                }
                d.moves[i] = id > 0 && id <= 165 ? gd::MOVES[id] : "";
                d.ppUps[i] = I(mv["pp_up"]);
            }
            d.otName = m["ot_name"];
            d.otId = I(m["ot_id"]);
            const char* dvk[4] = { "atk", "def", "spd", "spc" };
            for (int i = 0; i < 4; i++) d.dvs[i] = I(m["dvs"][dvk[i]]);
            for (int i = 0; i < 5; i++) d.se[i] = I(m["stat_exp"][pokemon::STAT_KEYS[i]]);
            d.preview = m;
        } else {
            d.level = ctx.level;
            d.otName = ctx.otName;
            d.otId = ctx.otId;
        }
        d.ctx = std::move(ctx);
        dlg = std::move(d);
        ResetFocus();
        if (!dlg->species.empty()) RefreshPreview();
    }

    json MonRequest() const {
        const MonDlg& d = *dlg;
        json moves = json::array();
        for (int i = 0; i < 4; i++)
            if (!d.moves[i].empty()) moves.push_back({ { "move", d.moves[i] }, { "pp_up", d.ppUps[i] } });
        json dvs = { { "atk", d.dvs[0] }, { "def", d.dvs[1] }, { "spd", d.dvs[2] }, { "spc", d.dvs[3] } };
        json se = json::object();
        for (int i = 0; i < 5; i++) se[pokemon::STAT_KEYS[i]] = d.se[i];
        return json{ { "species", d.species }, { "level", d.level },
                     { "nickname", TextOf(d.nickname).empty() ? json(nullptr) : d.nickname }, { "moves", moves },
                     { "ot_name", d.otName }, { "ot_id", d.otId }, { "dvs", dvs }, { "stat_exp", se } };
    }

    void RefreshPreview() {
        MonDlg& d = *dlg;
        if (d.species.empty()) {
            d.preview = nullptr;
            return;
        }
        try {
            json r = MakeMonRequest(MonRequest());
            d.preview = r["mon"];
            d.warnings.clear();
            for (const json& w : r["warnings"]) d.warnings.push_back(w.get<std::string>());
            d.error.clear();
        } catch (const std::exception& e) {
            d.error = e.what();
        }
    }

    void SetSpecies(int dex) {
        MonDlg& d = *dlg;
        std::string old = d.species, name = gd::SPECIES[dex];
        if (TextOf(d.nickname).empty() || TextOf(d.nickname) == old) d.nickname = name;
        d.species = name;
        bool any = false;
        for (const std::string& m : d.moves) any |= !m.empty();
        if (!any) StartingMoves(dex);
        RefreshPreview();
    }

    void StartingMoves(int dex) {
        MonDlg& d = *dlg;
        for (int i = 0; i < 4; i++) {
            int m = gd::BASE[dex].moves[i];
            d.moves[i] = m ? gd::MOVES[m] : "";
            d.ppUps[i] = 0;
        }
    }

    void DrawMonDialog() {
        MonDlg& d = *dlg;
        int dex = 0;
        for (int i = 1; i <= 151; i++)
            if (d.species == gd::SPECIES[i]) dex = i;
        // header: ball, name, species · level, types
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        Ball(dl, ImVec2(p.x + S(16), p.y + S(18)), S(15), dex ? json{ { "species", d.species } } : json::object());
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(40), p.y));
        ImGui::BeginGroup();
        std::string title = dex ? (TextOf(d.nickname).empty() ? Show(d.species) : Show(TextOf(d.nickname)))
                                : (d.ctx.mon.is_null() ? "New Pokémon" : MonName(d.ctx.mon));
        Heading(title);
        if (dex) {
            const char* t1 = pokemon::TypeName(gd::BASE[dex].type1);
            const char* t2 = pokemon::TypeName(gd::BASE[dex].type2);
            std::string types = std::string(t1 ? t1 : "") + (t2 && t2 != t1 ? std::string(" / ") + t2 : "");
            Hint("#" + Pad(dex, 3) + " " + Show(d.species) + " · Lv " + std::to_string(d.level) + " · " + types + "   (" +
                 d.ctx.title + ")");
        } else {
            Hint(d.ctx.title);
        }
        ImGui::EndGroup();
        ImGui::Spacing();
        if (st->setDefaultFocus) st->setDefaultFocus(); // (the gamepad starts on the tabs)
        int t = Seg("tabs", { "Pokémon", "Moves", "Stats" }, d.tab, 110);
        if (t >= 0 && t != d.tab) {
            d.tab = t;
            ResetFocus();
        }
        ImGui::Separator();
        ImGui::Spacing();

        bool changed = false;
        if (d.tab == 0) {
            Label("Species");
            PickButton("species", dex ? "#" + Pad(dex, 3) + " " + Show(d.species) : "Choose a species", 240, [this, dex] {
                OpenPicker("Species", SpeciesItems(), dex, [this](int v) { SetSpecies(v); });
            });
            Label("Nickname");
            if (auto v = NameField("##nick", d.nickname)) {
                d.nickname = v->empty() ? json(d.species) : json(*v);
                changed = true;
            }
            Label("Level (1-100)");
            if (auto v = IntField("level", d.level, 1, 100)) d.level = (int)*v, changed = true;
            Label("Original trainer");
            if (auto v = NameField("##ot", d.otName)) d.otName = *v, changed = true;
            Label("Trainer ID");
            if (auto v = IntField("otid", d.otId, 0, 65535, 80)) d.otId = (int)*v, changed = true;
            ImGui::Spacing();
            Hint("Names use the Game Boy alphabet: up to 10 capital letters, digits and a few symbols. Experience, HP and "
                 "PP are filled in to match the level, as for a freshly caught Pokémon.");
        } else if (d.tab == 1) {
            for (int i = 0; i < 4; i++) {
                ImGui::PushID(i);
                int id = 0;
                for (int m = 1; m <= 165; m++)
                    if (d.moves[i] == gd::MOVES[m]) id = m;
                PickButton("move", id ? gd::MOVES[id] : "- no move -", 190, [this, i, id] {
                    OpenPicker("Move " + std::to_string(i + 1), MoveItems(), id, [this, i](int v) {
                        dlg->moves[i] = v ? gd::MOVES[v] : "";
                        if (!v) dlg->ppUps[i] = 0;
                        RefreshPreview();
                    });
                });
                ImGui::SameLine();
                if (id) {
                    const char* tn = pokemon::TypeName(gd::MOVE_DATA[id].type);
                    ImGui::PushStyleColor(ImGuiCol_Text, ColV(TypeColor(tn)));
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%-8s", tn ? tn : "");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(S(330));
                    std::vector<std::string> ups;
                    for (int u = 0; u < 4; u++) {
                        int pp = pokemon::MaxPp(id, u);
                        ups.push_back(std::to_string(pp) + " PP" + (u ? " (+" + std::to_string(u) + ")" : ""));
                    }
                    int c = Seg("ppup", ups, d.ppUps[i], 86);
                    if (c >= 0 && c != d.ppUps[i]) d.ppUps[i] = c, changed = true;
                } else {
                    ImGui::NewLine();
                }
                ImGui::PopID();
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(!dex);
            if (Btn("Use starting moves")) {
                StartingMoves(dex);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            Hint("Any move can be chosen; the game does not check whether the species can learn it. PP Ups add a fifth of the base PP each.");
        } else {
            const json* stats = d.preview.is_object() && d.preview.contains("_info") ? &d.preview["_info"]["stats"] : nullptr;
            const int hpDv = ((d.dvs[0] & 1) << 3) | ((d.dvs[1] & 1) << 2) | ((d.dvs[2] & 1) << 1) | (d.dvs[3] & 1);
            if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(80));
                ImGui::TableSetupColumn("DV (0-15)", ImGuiTableColumnFlags_WidthFixed, S(st->handheld ? 170 : 130));
                ImGui::TableSetupColumn("Stat Exp (0-65535)", ImGuiTableColumnFlags_WidthFixed, S(st->handheld ? 200 : 160));
                ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthFixed, S(60));
                ImGui::TableHeadersRow();
                for (int k = 0; k < 5; k++) {
                    ImGui::PushID(k);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(kStatNames[k]);
                    ImGui::TableNextColumn();
                    if (k == 0) {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("%d (auto)", hpDv);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The HP DV comes from the other four");
                    } else if (auto v = IntField("dv", d.dvs[k - 1], 0, 15, 40)) {
                        d.dvs[k - 1] = (int)*v;
                        changed = true;
                    }
                    ImGui::TableNextColumn();
                    if (auto v = IntField("se", d.se[k], 0, 65535, 70, 256)) d.se[k] = (int)*v, changed = true;
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (stats) ImGui::Text("%d", I((*stats)[pokemon::STAT_KEYS[k]]));
                    else ImGui::TextUnformatted("-");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (Btn("Perfect stats")) {
                for (int& v : d.dvs) v = 15;
                for (int& v : d.se) v = 65535;
                changed = true;
            }
            ImGui::SameLine();
            if (Btn("No training")) {
                for (int& v : d.se) v = 0;
                changed = true;
            }
            ImGui::SameLine();
            Hint("DVs are Gen 1 \"individual values\"; Stat Exp is the training from battles.");
        }
        if (changed) RefreshPreview();
        ImGui::Spacing();
        if (!d.error.empty()) Hint(d.error, kRed);
        for (const std::string& w : d.warnings) Hint("! " + w, kWarn);
        ImGui::Separator();
        ImGui::Spacing();
        if (d.ctx.onRemove) {
            if (DangerBtn("Remove", 90)) {
                auto fn = d.ctx.onRemove;
                dlg.reset();
                ResetFocus();
                fn();
                return;
            }
            ImGui::SameLine();
        }
        if (Btn("Cancel", 90)) {
            dlg.reset();
            ResetFocus();
            return;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!dex || !d.error.empty());
        if (ColorBtn(d.ctx.mon.is_null() ? "Add" : "Done", kGreen, 90)) {
            try {
                json mon = MakeMonRequest(MonRequest())["mon"];
                auto fn = d.ctx.onDone;
                dlg.reset();
                ResetFocus();
                fn(mon);
            } catch (const std::exception& e) {
                d.error = e.what();
            }
        }
        ImGui::EndDisabled();
    }

    // ---- picker and questions (replace the page) ----

    void DrawPicker() {
        Picker& p = *picker;
        Heading(p.title);
        ImGui::SameLine();
        if (st->setDefaultFocus) st->setDefaultFocus(); // (the current choice below takes it when there is one)
        if (Btn("Cancel", 80)) {
            picker.reset();
            ResetFocus();
            return;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Search");
        ImGui::SameLine();
        if (p.focusFilter && !st->handheld) {
            ImGui::SetKeyboardFocusHere();
            p.focusFilter = false;
        }
        ImGui::SetNextItemWidth(S(220));
        ImGui::InputText("##filter", p.filter, sizeof(p.filter), ImGuiInputTextFlags_CharsUppercase);
        if (Nav()) focusField = ImGui::GetItemID();
        if (focusField == ImGui::GetItemID()) {
            ImGui::ActivateItemByID(focusField);
            focusField = 0;
        }
        ImGui::Spacing();
        ImGui::BeginChild("list", ImVec2(0, 0), false);
        ImGui::SetWindowFontScale(st->scale);
        std::string f = Upper(p.filter);
        const float w = st->handheld ? 220 : 200;
        int n = 0;
        for (auto& [id, name] : p.items) {
            if (!f.empty() && Upper(name).find(f) == std::string::npos) continue;
            ImGui::PushID(id);
            GridNext(n++, w);
            if (id == p.current && st->setDefaultFocus) st->setDefaultFocus();
            if (ColorBtn(name.c_str(), id == p.current ? kSelected : kButton, w)) {
                auto fn = p.onPick;
                int v = id;
                picker.reset();
                ResetFocus();
                ImGui::PopID();
                ImGui::EndChild();
                fn(v);
                return;
            }
            if (id == p.current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY(0.3f);
            ImGui::PopID();
        }
        if (n == 0) Hint("Nothing matches.");
        ImGui::EndChild();
    }

    void Ask(const std::string& text, const std::string& yes, std::function<void()> action) {
        confirm = Confirm{ text, yes, std::move(action) };
        ResetFocus();
    }

    void DrawConfirm() {
        Confirm& c = *confirm;
        ImGui::Spacing();
        Heading(c.text);
        ImGui::Spacing();
        if (st->setDefaultFocus) st->setDefaultFocus();
        if (Btn(c.no.c_str(), 140)) {
            confirm.reset();
            ResetFocus();
            return;
        }
        ImGui::SameLine();
        if (c.other) {
            if (Btn(c.otherLabel.c_str(), 180)) {
                auto fn = c.other;
                confirm.reset();
                ResetFocus();
                fn();
                return;
            }
            ImGui::SameLine();
        }
        if (ColorBtn(c.yes.c_str(), kSelected, 140)) {
            auto fn = c.action;
            confirm.reset();
            ResetFocus();
            fn();
        }
    }

    // ---- pages ----

    static std::string RoundState(const json& p) {
        int c = I(p["cleared_modes"]);
        if ((c & 0x1F8) == 0x1F8) return "Complete";
        if (I(p["mewtwo_unlocked"])) return "Mewtwo unlocked";
        return c || I(p["poke_cup"]) || I(p["prime_cup"]) || I(p["gym_leader_castle"]) ? "In progress" : "Not started";
    }
    static std::string ProgressLine(const json& p) {
        std::string s;
        for (auto [bit, name] : kCupBits)
            if (I(p["cleared_modes"]) & (1 << bit)) s += (s.empty() ? "" : ", ") + std::string(name);
        return s.empty() ? "nothing cleared yet" : s;
    }
    static std::string RunTitle(const json& se) {
        int mode = I(se["mode"]);
        const char* mn = ModeName(mode);
        std::string s = mn ? mn : "Mode " + std::to_string(mode);
        int sel = I(se["mode_selection"]);
        if (mode == 3 || mode == 6) s += std::string(" · ") + CUP_DIVISIONS[sel & 3] + " division";
        if (mode == 7) s += " · " + (sel >= 0 && sel < 10 ? std::string(GLC_STAGES[sel]) : "stage " + std::to_string(sel));
        s += " · battle " + std::to_string(I(se["battle"]));
        if (I(se["round2"])) s += " · Round 2";
        return s;
    }

    void UnlockAll(bool round2) {
        for (int r = 0; r < (round2 ? 2 : 1); r++) ClearRound(cur["options"]["progress"][r]);
        Changed();
    }
    static void ClearRound(json& p) {
        p["cleared_modes"] = I(p["cleared_modes"]) | 0xF8;
        p["gym_leader_castle"] = 8;
        p["poke_cup"] = 4;
        p["prime_cup"] = 4;
        p["mewtwo_unlocked"] = 1;
    }

    void Go(const std::string& sec) {
        section = sec;
        scrollTo = sec;
        ResetFocus();
    }

    void PageOverview() {
        int bad = 0, total = 0;
        for (auto& [key, rec] : AllRecords()) {
            total++;
            bad += !IsValidRecord(*rec) || rec->contains("_backup");
        }
        Heading("Overview");
        Hint("What is in this save. Pick a card to edit that part.");
        ImGui::Spacing();
        if (bad == 0) {
            Hint("This save is healthy: every part of it is readable and both copies agree.", kGreen);
        } else {
            Hint(std::to_string(bad) + " of " + std::to_string(total) +
                     " parts of this save are unreadable or out of sync. That is normal for parts the game has never "
                     "written (unused team slots, for example): the game treats them as empty and rebuilds them when it "
                     "loads the save. Editing a part fixes it; Repair fixes all of them now, the way the game would.",
                 kWarn);
            if (Btn("Repair", 100)) Repair();
        }
        ImGui::Spacing();
        const json& o = cur["options"];
        const float w = 210;
        int i = 0;
        GridNext(i++, w);
        if (InfoCard("r1", "Round 1", RoundState(o["progress"][0]), ProgressLine(o["progress"][0]), w)) Go("progress");
        GridNext(i++, w);
        if (InfoCard("r2", "Round 2", RoundState(o["progress"][1]), ProgressLine(o["progress"][1]), w)) Go("progress");
        GridNext(i++, w);
        if (InfoCard("teams", "Registered teams", std::to_string(RegisteredTeams()), "across all cups and modes", w)) Go("teams");
        GridNext(i++, w);
        if (InfoCard("lab", "Pokémon in the Lab", std::to_string(BoxedCount()), "in 12 boxes of 20", w)) Go("boxes");
        GridNext(i++, w);
        if (InfoCard("palace", "Victory Palace statues", std::to_string(Statues()) + " / 151", "Pokémon that won a cup", w)) Go("palace");
        const json& c = cur["continue"];
        GridNext(i++, w);
        if (InfoCard("run", "Saved cup run", (I(c["flags"]) & 1) ? "Yes" : "None",
                     (I(c["flags"]) & 1) ? RunTitle(c["session"]) : "nothing to continue", w)) Go("run");
        for (size_t g = 0; g < gbs.size(); g++) {
            const json& gc = gbs[g].cur;
            int boxed = 0;
            for (const json& b : gc["boxes"]) boxed += (int)b.size();
            GridNext(i++, w);
            ImGui::PushID((int)g);
            if (InfoCard("gb", gbs[g].label + " · " + gbs[g].title, Show(TextOf(gc["trainer"]["name"])),
                         std::to_string(gc["party"].size()) + " in the party · " + std::to_string(boxed) + " in PC boxes", w)) {
                Go("gb" + std::to_string(g));
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Spacing();
        Heading("Quick actions");
        if (Btn("Clear Round 1 & unlock Mewtwo")) {
            UnlockAll(false);
            Toast("Round 1 fully cleared, Mewtwo unlocked.");
        }
        ImGui::SameLine();
        if (Btn("Clear both rounds")) {
            UnlockAll(true);
            Toast("Both rounds fully cleared.");
        }
        ImGui::SameLine();
        if (Btn("GB Tower: fastest speed")) {
            cur["options"]["gb_tower_speed"] = 2;
            Changed();
            Toast("GB Tower now runs at Dodrio speed.");
        }
        Hint("Changes are only written when you press Save.");
    }

    void PageProgress() {
        Heading("Progress & unlocks");
        Hint("Which cups you have cleared. Clearing all four cups and the Castle unlocks the Mewtwo battle.");
        json& o = cur["options"];
        for (int r = 0; r < 2; r++) {
            ImGui::PushID(r);
            json& p = o["progress"][r];
            ImGui::Spacing();
            ImGui::Separator();
            Heading(r == 0 ? "Round 1" : "Round 2");
            Label("Cleared", 120);
            int n = 0;
            for (auto [bit, name] : kCupBits) {
                if (n++) SameLineWrap(ImGui::CalcTextSize(name).x + S(40));
                bool on = I(p["cleared_modes"]) & (1 << bit);
                if (Check(name, &on)) {
                    p["cleared_modes"] = on ? I(p["cleared_modes"]) | (1 << bit) : I(p["cleared_modes"]) & ~(1 << bit);
                    Changed();
                }
            }
            const std::vector<std::string> divs = { "None", "Poké Ball", "Great Ball", "Ultra Ball", "Master Ball" };
            Label("Poké Cup divisions", 160);
            if (int c = Seg("poke", divs, I(p["poke_cup"])); c >= 0) p["poke_cup"] = c, Changed();
            Label("Prime Cup divisions", 160);
            if (int c = Seg("prime", divs, I(p["prime_cup"])); c >= 0) p["prime_cup"] = c, Changed();
            Label("Castle gyms cleared", 160);
            std::vector<std::string> gyms;
            for (int g = 0; g <= 8; g++) gyms.push_back(std::to_string(g));
            int castle = I(p["gym_leader_castle"]);
            if (int c = Seg("castle", gyms, castle, st->handheld ? 38 : 26); c >= 0) p["gym_leader_castle"] = c, Changed();
            ImGui::SameLine();
            Hint(castle == 0 ? "no gym" : castle >= 8 ? "all 8 gyms" : std::string("up to ") + GLC_STAGES[std::clamp(castle, 1, 8) - 1]);
            bool mew = I(p["mewtwo_unlocked"]) != 0;
            if (Check("Mewtwo unlocked (night sky over the Castle)", &mew)) p["mewtwo_unlocked"] = mew ? 1 : 0, Changed();
            if (Btn("Clear everything")) ClearRound(p), Changed();
            ImGui::SameLine();
            if (Btn("Reset")) {
                p["cleared_modes"] = 0;
                p["gym_leader_castle"] = 0;
                p["poke_cup"] = 0;
                p["prime_cup"] = 0;
                p["mewtwo_unlocked"] = 0;
                Changed();
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        Heading("GB Tower");
        Label("Game Boy speed", 160);
        if (int c = Seg("tower", { "Normal", "Doduo (x2)", "Dodrio (x3)" }, I(o["gb_tower_speed"])); c >= 0) {
            o["gb_tower_speed"] = c;
            Changed();
        }
        Hint("An unlock from clearing cups.");
    }

    void PageOptions() {
        json& o = cur["options"];
        Heading("Options");
        Hint("The settings of the game's Options menu, and a few things the game remembers.");
        ImGui::Spacing();
        Label("Sound", 180);
        if (int c = Seg("snd", { "Stereo", "Mono" }, I(o["sound"]) & 1, 90); c >= 0) o["sound"] = (I(o["sound"]) & ~1) | c, Changed();
        Label("Commentator voice", 180);
        if (int c = Seg("voice", { "On", "Off" }, (I(o["sound"]) >> 1) & 1, 90); c >= 0) {
            o["sound"] = (I(o["sound"]) & ~2) | (c << 1);
            Changed();
        }
        Label("Kids Club", 180);
        bool hyper = I(o["kids_club"]) & 1;
        if (Check("Hyper difficulty unlocked", &hyper)) o["kids_club"] = (I(o["kids_club"]) & ~1) | (hyper ? 1 : 0), Changed();
        ImGui::Spacing();
        ImGui::Separator();
        Heading("Bonus Pokémon");
        Hint("A gift Pokémon waiting at the Lab PC, collected from the Pokémon Lab with a Game Boy cartridge in the "
             "Transfer Pak.");
        int bonus = I(o["bonus_pokemon"]) & 0xFF;
        Label("Pokémon waiting", 180);
        PickButton("bonus", bonus > 0 && bonus <= 151 ? "#" + Pad(bonus, 3) + " " + Show(gd::SPECIES[bonus]) : (bonus ? "#" + std::to_string(bonus) : "None"),
                   220, [this, bonus] {
                       OpenPicker("Bonus Pokémon", SpeciesItems(true), bonus, [this](int v) {
                           json& op = cur["options"];
                           op["bonus_pokemon"] = v ? (I(op["bonus_pokemon"]) & 0x100) | v : 0;
                           Changed();
                       });
                   });
        if (bonus) {
            ImGui::SameLine();
            bool r2 = I(o["bonus_pokemon"]) & 0x100;
            if (Check("From Round 2", &r2)) o["bonus_pokemon"] = (I(o["bonus_pokemon"]) & 0xFF) | (r2 ? 0x100 : 0), Changed();
        }
    }

    void PageTeams() {
        const Cat* c = &kCats[0];
        for (const Cat& k : kCats)
            if (cat == k.key) c = &k;
        json& slots = cur["teams"][c->key];
        Heading("Registered teams");
        Hint("Teams saved with Register in each cup. Every cup has 10 slots.");
        ImGui::Spacing();
        int n = 0;
        for (const Cat& k : kCats) {
            int count = 0;
            for (const json& t : cur["teams"][k.key]) count += TeamRegistered(t);
            std::string label = std::string(k.name) + (count ? "  " + std::to_string(count) : "");
            ImGui::PushID(k.key);
            if (n++) SameLineWrap(ImGui::CalcTextSize(label.c_str()).x + S(16));
            if (ColorBtn(label.c_str(), cat == k.key ? kSelected : kButton)) {
                cat = k.key;
                slot = 0;
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        for (int i = 0; i < 10; i++) {
            const json& s = slots[i];
            json r = ListRecord(s);
            std::string label = std::to_string(i + 1) + "  " +
                                (TeamRegistered(s) ? (TextOf(r["name"]).empty() ? "(no name)" : Show(TextOf(r["name"]))) +
                                                         " · " + std::to_string(r["entries"].size())
                                                   : std::string("empty"));
            ImGui::PushID(i);
            if (i % 5) ImGui::SameLine();
            if (ColorBtn(label.c_str(), i == slot ? kSelected : kButton, 132)) slot = i;
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        json& t = slots[slot];
        json tl = ListRecord(t);
        auto editTeam = [this, &t](const std::function<void(json&)>& fn) {
            EditList(t, [&](json& r) {
                fn(r);
                r["flags"] = !r["entries"].empty() ? (I(r["flags"]) | 1) & ~2 : 0;
            });
        };
        Label("Team name (trainer)", 160);
        if (auto v = NameField("##tname", tl["name"])) editTeam([&](json& r) { r["name"] = *v; });
        ImGui::SameLine();
        ImGui::TextUnformatted("  ID");
        ImGui::SameLine();
        if (auto v = IntField("tid", I(tl["trainer_id"]), 0, 65535, 70)) editTeam([&](json& r) { r["trainer_id"] = *v; });
        if (TeamRegistered(t)) {
            ImGui::SameLine();
            if (DangerBtn("Delete team")) {
                Ask("Delete this team?", "Delete", [this, key = std::string(c->key), s = slot] {
                    json& team = cur["teams"][key][s];
                    EditList(team, [](json& r) {
                        for (auto it = r.begin(); it != r.end();) {
                            if (it.key().rfind("_", 0) == 0 && it.key() != "_footer") it = r.erase(it);
                            else ++it;
                        }
                        r["flags"] = 0;
                        r["name"] = "";
                        r["trainer_id"] = 0;
                        r["entries"] = json::array();
                    });
                });
            }
        }
        ImGui::Spacing();
        MonCtx ctx;
        ctx.title = std::string(c->name) + " team";
        ctx.otName = TextOf(tl["name"]).empty() ? json("RED") : tl["name"];
        ctx.otId = I(tl["trainer_id"]);
        ctx.level = c->level;
        const float w = 210;
        const json entries = tl["entries"];
        const std::string key = c->key;
        const int s = slot;
        for (int i = 0; i < (int)entries.size(); i++) {
            GridNext(i, w);
            if (MonCard(std::to_string(i).c_str(), entries[i], w)) {
                MonCtx m = ctx;
                m.mon = entries[i];
                m.onDone = [this, key, s, i](const json& nm) {
                    EditTeamAt(key, s, [&](json& r) { r["entries"][i] = nm; });
                };
                m.onRemove = [this, key, s, i] { EditTeamAt(key, s, [&](json& r) { r["entries"].erase((size_t)i); }); };
                OpenMon(m);
            }
        }
        if (entries.size() < 6) {
            GridNext((int)entries.size(), w);
            if (EmptyCard("add", "+ Add Pokémon", w)) {
                MonCtx m = ctx;
                m.onDone = [this, key, s](const json& nm) {
                    EditTeamAt(key, s, [&](json& r) {
                        r["entries"].push_back(nm);
                        if (TextOf(r["name"]).empty()) r["name"] = "RED";
                    });
                };
                OpenMon(m);
            }
        }
        ImGui::Spacing();
        Hint(std::string(c->rule) + " Up to 6 Pokémon per team.");
    }

    void EditTeamAt(const std::string& key, int s, const std::function<void(json&)>& fn) {
        json& t = cur["teams"][key][s];
        EditList(t, [&](json& r) {
            fn(r);
            r["flags"] = !r["entries"].empty() ? (I(r["flags"]) | 1) & ~2 : 0;
        });
    }

    // a list of Pokémon (Lab box, GB party or box) as cards with an Add card; edit(fn) changes the list
    void BoxCells(const char* id, const json& list, int cap, const MonCtx& ctx, bool canRemoveLast,
                  const std::function<void(const std::function<void(json&)>&)>& edit,
                  const std::function<bool(int, const json&)>& action = nullptr, float w = 190) {
        ImGui::PushID(id);
        for (int i = 0; i < cap; i++) {
            ImGui::PushID(i);
            GridNext(i, w + (action ? 34 : 0));
            ImGui::BeginGroup(); // a cell: the card, and the move arrow (or room for it)
            bool moved = false;
            if (i < (int)list.size()) {
                if (MonCard("m", list[i], w)) {
                    MonCtx m = ctx;
                    m.mon = list[i];
                    m.onDone = [edit, i](const json& nm) { edit([&](json& l) { l[i] = nm; }); };
                    if (canRemoveLast || list.size() > 1) m.onRemove = [edit, i] { edit([&](json& l) { l.erase((size_t)i); }); };
                    OpenMon(m);
                }
                if (action) {
                    ImGui::SameLine(0, S(4));
                    moved = action(i, list[i]);
                }
            } else {
                if (i == (int)list.size()) {
                    if (EmptyCard("add", "+ Add", w)) {
                        MonCtx m = ctx;
                        m.onDone = [edit](const json& nm) { edit([&](json& l) { l.push_back(nm); }); };
                        OpenMon(m);
                    }
                } else {
                    EmptyCard("x", "", w, false);
                }
                if (action) {
                    ImGui::SameLine(0, S(4));
                    ImGui::Dummy(ImVec2(S(30), S(1)));
                }
            }
            ImGui::EndGroup();
            ImGui::PopID();
            if (moved) break; // (the list changed)
        }
        ImGui::PopID();
    }

    // [<] Box n (k/20) [>]
    void BoxNav(const char* id, int count, int* cur_, const std::function<std::string(int)>& labelOf) {
        ImGui::PushID(id);
        if (Btn("<", st->handheld ? 44 : 28)) *cur_ = (*cur_ + count - 1) % count;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        std::string label = labelOf(*cur_);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (Btn(">", st->handheld ? 44 : 28)) *cur_ = (*cur_ + 1) % count;
        ImGui::PopID();
    }

    static json ToStadium(const json& m) { // the Lab PC re-encodes the nickname (zero padding), the OT name is copied
        json c = m;
        if (c["nickname"].is_object()) c["nickname"] = c["nickname"]["text"];
        return c;
    }

    void PageBoxes() {
        Heading("Pokémon Lab boxes");
        json bl = ListRecord(cur["boxes"][box]);
        const int bi = box;
        auto edit = [this, bi](const std::function<void(json&)>& fn) {
            EditList(cur["boxes"][bi], [&](json& r) { fn(r["entries"]); });
        };
        MonCtx ctx;
        ctx.title = "Box " + std::to_string(box + 1);
        ctx.otName = bl["entries"].empty() ? json("RED") : json(TextOf(bl["entries"][0]["ot_name"]));
        ctx.otId = bl["entries"].empty() ? 0 : I(bl["entries"][0]["ot_id"]);
        auto boxLabel = [this](int i) {
            return "Box " + std::to_string(i + 1) + "  (" + std::to_string(ListRecord(cur["boxes"][i])["entries"].size()) + "/20)";
        };
        if (gbs.empty()) {
            Hint("The Pokémon stored in the Pokémon Lab's PC: 12 boxes with room for 20 each.");
            ImGui::Spacing();
            BoxNav("box", 12, &box, boxLabel);
            if (!bl["entries"].empty()) {
                ImGui::SameLine();
                if (DangerBtn("Empty this box")) {
                    Ask("Empty box " + std::to_string(bi + 1) + "?", "Empty", [edit] { edit([](json& l) { l = json::array(); }); });
                }
            }
            ImGui::Spacing();
            BoxCells("lab", bl["entries"], 20, ctx, true, edit);
            return;
        }
        // with Game Boy games in the Transfer Paks: move Pokémon between the Lab and a cartridge, as the Lab PC does
        Hint("Use the arrow next to a Pokémon to move it between the Lab and a Game Boy game in a Transfer Pak, as the "
             "Lab PC does.");
        xg = std::min(xg, (int)gbs.size() - 1);
        Gb& g = gbs[xg];
        json& gl = xstore < 0 ? g.cur["party"] : g.cur["boxes"][xstore];
        const int cap = xstore < 0 ? 6 : 20;
        const int gi = xg, store = xstore;
        auto gEdit = [this, gi, store](const std::function<void(json&)>& fn) {
            Gb& gg = gbs[gi];
            fn(store < 0 ? gg.cur["party"] : gg.cur["boxes"][store]);
            Changed();
        };
        const std::string storeName = xstore < 0 ? "party" : "box " + std::to_string(xstore + 1);
        {
            const float paneW = (ImGui::GetContentRegionAvail().x - S(12)) / 2;
            const float paneH = std::max(S(200), ImGui::GetContentRegionAvail().y);
            ImGui::BeginChild("lab-pane", ImVec2(paneW, paneH), false);
            ImGui::SetWindowFontScale(st->scale);
            Heading("Pokémon Lab");
            BoxNav("box", 12, &box, boxLabel);
            ImGui::Spacing();
            const json labList = bl["entries"];
            BoxCells("lab", labList, 20, ctx, true, edit, [&](int i, const json& m) {
                ImGui::BeginDisabled((int)gl.size() >= cap);
                bool c = Btn("->", 30, st->handheld ? 52 : 44);
                ImGui::EndDisabled();
                if (c) {
                    json mon = m;
                    edit([i](json& l) { l.erase((size_t)i); });
                    gEdit([&](json& l) { l.push_back(mon); });
                    Toast(Show(TextOf(mon["nickname"])) + " moved to " + g.label + "'s " + storeName);
                }
                return c;
            }, 150);
            ImGui::EndChild();
            ImGui::SameLine(0, S(12));
            ImGui::BeginChild("gb-pane", ImVec2(paneW, paneH), false);
            ImGui::SetWindowFontScale(st->scale);
            Heading("Game Boy");
            if (gbs.size() > 1) {
                std::vector<std::string> names;
                for (const Gb& x : gbs) names.push_back(x.label);
                if (int c = Seg("gbsel", names, xg); c >= 0) xg = c, xstore = -1;
            } else {
                Hint(g.label + " · " + Show(TextOf(g.cur["trainer"]["name"])));
            }
            const std::string partyLabel = "Party (" + std::to_string(g.cur["party"].size()) + "/6)";
            if (ColorBtn(partyLabel.c_str(), xstore < 0 ? kSelected : kButton)) xstore = -1;
            ImGui::SameLine();
            if (ColorBtn("PC boxes", xstore >= 0 ? kSelected : kButton)) xstore = std::max(0, gbBox);
            if (xstore >= 0) {
                BoxNav("gbbox", 12, &xstore, [&](int i) { return "Box " + std::to_string(i + 1) + " (" + std::to_string(g.cur["boxes"][i].size()) + "/20)"; });
                gbBox = xstore;
            } else {
                Hint("The party always keeps at least one Pokémon.");
            }
            MonCtx gctx;
            gctx.title = g.label + " · " + storeName;
            gctx.otName = TextOf(g.cur["trainer"]["name"]);
            gctx.otId = I(g.cur["trainer"]["id"]);
            gctx.level = 5;
            const json gList = gl;
            BoxCells("gb", gList, cap, gctx, xstore >= 0, gEdit, [&](int i, const json& m) {
                ImGui::BeginDisabled((int)ListRecord(cur["boxes"][bi])["entries"].size() >= 20 || (store < 0 && gList.size() <= 1));
                bool c = Btn("<-", 30, st->handheld ? 52 : 44);
                ImGui::EndDisabled();
                if (c) {
                    json lm = ToStadium(m);
                    gEdit([i](json& l) { l.erase((size_t)i); });
                    edit([&](json& l) { l.push_back(lm); });
                    Toast(Show(TextOf(m["nickname"])) + " moved to Lab box " + std::to_string(bi + 1));
                }
                return c;
            }, 150);
            ImGui::EndChild();
        }
    }

    // item rows: item (picker), quantity, remove
    void ItemRows(const char* id, const json& entries, int cap, const std::function<void(const std::function<void(json&)>&)>& mutate,
                  const char* emptyText) {
        ImGui::PushID(id);
        if (entries.empty()) Hint(emptyText);
        for (int i = 0; i < (int)entries.size(); i++) {
            ImGui::PushID(i);
            int item = I(entries[i]["item"]);
            PickButton("item", ItemName(item), 200, [this, i, item, mutate] {
                OpenPicker("Item", ItemItems(), item, [i, mutate](int v) {
                    mutate([&](json& l) { l[i] = json{ { "item", v }, { "quantity", l[i]["quantity"] } }; });
                });
            });
            ImGui::SameLine();
            if (auto v = IntField("q", I(entries[i]["quantity"]), 1, 99, 40)) {
                long long q = *v;
                mutate([&](json& l) { l[i] = json{ { "item", l[i]["item"] }, { "quantity", q } }; });
            }
            ImGui::SameLine();
            if (Btn("x", st->handheld ? 38 : 24)) {
                mutate([i](json& l) { l.erase((size_t)i); });
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::BeginDisabled((int)entries.size() >= cap);
        if (Btn("+ Add item")) mutate([](json& l) { l.push_back(json{ { "item", 40 }, { "quantity", 1 } }); });
        ImGui::EndDisabled();
        ImGui::SameLine();
        Hint(std::to_string(entries.size()) + " of " + std::to_string(cap));
        ImGui::PopID();
    }

    void PageItems() {
        Heading("Item boxes");
        Hint("Items stored at the Pokémon Lab, to send to a Game Boy game through the Transfer Pak.");
        ImGui::Spacing();
        for (int i = 0; i < 4; i++) {
            size_t n = ListRecord(cur["item_boxes"][i])["entries"].size();
            std::string label = "Item box " + std::to_string(i + 1) + (n ? "  " + std::to_string(n) : "");
            ImGui::PushID(i);
            if (i) ImGui::SameLine();
            if (ColorBtn(label.c_str(), i == ibox ? kSelected : kButton)) ibox = i;
            ImGui::PopID();
        }
        ImGui::Spacing();
        const int bi = ibox;
        json bl = ListRecord(cur["item_boxes"][bi]);
        auto edit = [this, bi](const std::function<void(json&)>& fn) {
            EditList(cur["item_boxes"][bi], [&](json& r) {
                fn(r);
                if (!r["entries"].empty()) r["flags"] = I(r["flags"]) | 1;
            });
        };
        Label("Owner name", 120);
        if (auto v = NameField("##owner", bl["name"])) edit([&](json& r) { r["name"] = *v; });
        ImGui::SameLine();
        ImGui::TextUnformatted("  ID");
        ImGui::SameLine();
        if (auto v = IntField("oid", I(bl["trainer_id"]), 0, 65535, 70)) edit([&](json& r) { r["trainer_id"] = *v; });
        ImGui::Spacing();
        ItemRows("rows", bl["entries"], 100, [edit](const std::function<void(json&)>& fn) {
            edit([&](json& r) {
                fn(r["entries"]);
                if (TextOf(r["name"]).empty() && !r["entries"].empty()) r["name"] = "RED";
            });
        }, "This box is empty.");
        if (!bl["entries"].empty()) {
            ImGui::SameLine();
            if (DangerBtn("Empty this box")) {
                Ask("Empty item box " + std::to_string(bi + 1) + "?", "Empty", [this, bi] {
                    EditList(cur["item_boxes"][bi], [](json& r) {
                        r["flags"] = 0;
                        r["name"] = "";
                        r["trainer_id"] = 0;
                        r["entries"] = json::array();
                    });
                });
            }
        }
    }

    static json Statue(int d) {
        return json{ { "mode", 3 }, { "level", 50 }, { "ot_id", 0 }, { "nickname", gd::SPECIES[d] }, { "ot_name", "RED" } };
    }

    void PagePalace() {
        json& ents = cur["victory_palace"]["entries"];
        Heading("Victory Palace");
        Hint(std::to_string(Statues()) + " of 151 Pokémon have a statue. Pick a Pokémon to add, change or remove its statue.");
        ImGui::Spacing();
        json& e = ents[dex - 1];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        Ball(dl, ImVec2(p.x + S(14), p.y + S(14)), S(12), json{ { "species", gd::SPECIES[dex] } });
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(34), p.y));
        Heading("#" + Pad(dex, 3) + " " + Show(gd::SPECIES[dex]) + (e.is_null() ? "   (no statue)" : ""));
        ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(ImGui::GetCursorScreenPos().y, p.y + S(30))));
        if (e.is_null()) {
            if (ColorBtn("Add statue", kGreen)) ents[dex - 1] = Statue(dex), Changed();
        } else {
            Label("Nickname", 110);
            if (auto v = NameField("##nick", e["nickname"])) e["nickname"] = v->empty() ? std::string(gd::SPECIES[dex]) : *v, Changed();
            ImGui::SameLine();
            ImGui::TextUnformatted("  Level");
            ImGui::SameLine();
            if (auto v = IntField("lv", I(e["level"]), 1, 100, 40)) e["level"] = *v, Changed();
            Label("Won in", 110);
            std::vector<std::string> modes;
            int chosen = -1;
            for (int i = 0; i < 5; i++) {
                modes.push_back(kStatueModes[i].second);
                if ((I(e["mode"]) & 0x7F) == kStatueModes[i].first) chosen = i;
            }
            if (int c = Seg("mode", modes, chosen); c >= 0) e["mode"] = (I(e["mode"]) & 0x80) | kStatueModes[c].first, Changed();
            ImGui::SameLine();
            bool r2 = I(e["mode"]) & 0x80;
            if (Check("Round 2", &r2)) e["mode"] = (I(e["mode"]) & 0x7F) | (r2 ? 0x80 : 0), Changed();
            Label("Trainer", 110);
            if (auto v = NameField("##ot", e["ot_name"])) e["ot_name"] = *v, Changed();
            ImGui::SameLine();
            ImGui::TextUnformatted("  ID");
            ImGui::SameLine();
            if (auto v = IntField("id", I(e["ot_id"]), 0, 65535, 70)) e["ot_id"] = *v, Changed();
            if (DangerBtn("Remove statue")) ents[dex - 1] = nullptr, Changed();
        }
        ImGui::Spacing();
        if (Btn("Give every Pokémon a statue")) {
            for (int d = 1; d <= 151; d++)
                if (ents[d - 1].is_null()) ents[d - 1] = Statue(d);
            Changed();
        }
        ImGui::SameLine();
        if (DangerBtn("Remove all")) {
            Ask("Remove all statues?", "Remove all", [this] {
                for (json& x : cur["victory_palace"]["entries"]) x = nullptr;
                Changed();
            });
        }
        ImGui::Spacing();
        const float w = st->handheld ? 150 : 128;
        for (int d = 1; d <= 151; d++) {
            ImGui::PushID(d);
            GridNext(d - 1, w);
            std::string label = Pad(d, 3) + " " + Show(gd::SPECIES[d]);
            uint32_t col = d == dex ? kSelected : !ents[d - 1].is_null() ? 0x1F4D2C : kButton;
            if (ColorBtn(label.c_str(), col, w)) dex = d;
            ImGui::PopID();
        }
    }

    void PageRun() {
        json& c = cur["continue"];
        json& se = c["session"];
        Heading("Saved cup run");
        const bool has = (I(c["flags"]) & 1) == 1;
        auto setMons = [this](const json& list) {
            json& cc = cur["continue"];
            json p = list;
            while (p.size() < 6) p.push_back("fill:00*55");
            cc["pokemon"] = p;
            cc["count"] = list.size();
            Changed();
        };
        if (!has) {
            Hint("When you quit in the middle of a cup or the Castle, the game saves the run so you can CONTINUE it. "
                 "There is no saved run in this save.");
            if (Btn("Create a saved run")) {
                c["flags"] = 1;
                se["mode"] = 3;
                se["mode_selection"] = 0;
                se["battle"] = 1;
                se["round2"] = 0;
                se["continues"] = 0;
                se["trainer_id"] = 0;
                se["player_name"] = "RED";
                setMons(json::array());
            }
            return;
        }
        Hint(RunTitle(se));
        ImGui::Spacing();
        const std::pair<int, const char*> modes[] = { { 3, "Poké Cup" }, { 4, "Petit Cup" }, { 5, "Pika Cup" }, { 6, "Prime Cup" }, { 7, "Castle" } };
        std::vector<std::string> labels;
        int chosen = -1;
        for (int i = 0; i < 5; i++) {
            labels.push_back(modes[i].second);
            if (I(se["mode"]) == modes[i].first) chosen = i;
        }
        Label("Where", 150);
        if (int k = Seg("mode", labels, chosen); k >= 0) se["mode"] = modes[k].first, se["mode_selection"] = 0, Changed();
        int mode = I(se["mode"]);
        if (mode == 7 || mode == 3 || mode == 6) {
            std::vector<std::string> sel;
            if (mode == 7) {
                for (const char* g : GLC_STAGES) sel.push_back(g);
            } else {
                for (const char* d : CUP_DIVISIONS) sel.push_back(d);
            }
            Label(mode == 7 ? "Stage" : "Division", 150);
            if (int k = Seg("sel", sel, I(se["mode_selection"])); k >= 0) se["mode_selection"] = k, Changed();
        }
        Label("Next battle", 150);
        if (auto v = IntField("battle", I(se["battle"]), 1, 8, 40)) se["battle"] = *v, Changed();
        Label("Continues used", 150);
        if (auto v = IntField("cont", I(se["continues"]), 0, 255, 40)) se["continues"] = *v, Changed();
        Label("Player name", 150);
        if (auto v = NameField("##pname", se["player_name"], 16)) se["player_name"] = *v, Changed();
        Label("Trainer ID", 150);
        if (auto v = IntField("tid", I(se["trainer_id"]), 0, 65535, 70)) se["trainer_id"] = *v, Changed();
        Label("Round", 150);
        bool r2 = I(se["round2"]) != 0;
        if (Check("Round 2", &r2)) se["round2"] = r2 ? 1 : 0, Changed();
        ImGui::Spacing();
        Heading("Team in this run");
        json mons = json::array();
        for (int i = 0; i < std::clamp(I(c["count"]), 0, 6); i++)
            if (c["pokemon"][i].is_object()) mons.push_back(c["pokemon"][i]);
        MonCtx ctx;
        ctx.title = "Saved run";
        ctx.otName = TextOf(se["player_name"]).empty() ? json("RED") : json(TextOf(se["player_name"]));
        ctx.otId = I(se["trainer_id"]);
        BoxCells("run", mons, (int)std::min<size_t>(6, mons.size() + 1), ctx, true, [mons, setMons](const std::function<void(json&)>& fn) {
            json l = mons;
            fn(l);
            setMons(l);
        });
        if (mons.empty()) Hint("A saved run needs at least one Pokémon.", kWarn);
        ImGui::Spacing();
        if (DangerBtn("Delete saved run")) {
            Ask("Delete the saved run?", "Delete", [this] {
                json& cc = cur["continue"];
                json& s = cc["session"];
                cc["flags"] = 0;
                cc["count"] = 0;
                s["mode"] = 0;
                s["mode_selection"] = 0;
                s["battle"] = 0;
                s["round2"] = 0;
                s["continues"] = 0;
                s["opponent_history"] = json::array({ 0, 0, 0, 0, 0, 0, 0, 0 });
                s["gb_starter_choice"] = 0;
                s["trainer_id"] = 0;
                s["player_name"] = "";
                cc["pokemon"] = json::array({ "fill:00*55", "fill:00*55", "fill:00*55", "fill:00*55", "fill:00*55", "fill:00*55" });
                cc["battle_byte_52"] = json::array({ 0, 0, 0, 0, 0, 0 });
                cc["battle_byte_53"] = json::array({ 0, 0, 0, 0, 0, 0 });
                Changed();
            });
        }
    }

    void PageGallery() {
        Heading("Gallery");
        Hint("Photos taken in the Gallery. The editor can only count and delete them.");
        ImGui::Spacing();
        for (auto [key, name, total] : { std::tuple<const char*, const char*, int>{ "album", "Photo album", 36 },
                                         { "photos", "Current photo set", 21 } }) {
            int n = 0;
            for (const json& e : cur[key]["entries"]) n += !e.is_null();
            ImGui::PushID(key);
            Label(name, 180);
            ImGui::Text("%d / %d", n, total);
            if (n) {
                ImGui::SameLine();
                if (DangerBtn("Delete all")) {
                    Ask(std::string("Delete all photos of the ") + (key == std::string("album") ? "album?" : "current photo set?"), "Delete",
                        [this, k = std::string(key)] {
                            for (json& e : cur[k]["entries"]) e = nullptr;
                            Changed();
                        });
                }
            }
            ImGui::PopID();
        }
    }

    void PageGb(int gi) {
        Gb& g = gbs[gi];
        json& c = g.cur;
        json& t = c["trainer"];
        Heading(g.label + " · " + g.title);
        Hint(Show(TextOf(t["name"])) + " (ID " + Pad(I(t["id"]), 5) + ") · " + g.name);
        if (!c["valid"].get<bool>()) {
            Hint("The Game Boy game will report this save as corrupted: its checksum does not match. Saving here rewrites "
                 "the checksum.", kWarn);
        }
        ImGui::Spacing();
        int boxed = 0;
        for (const json& b : c["boxes"]) boxed += (int)b.size();
        const std::vector<std::string> tabs = { "Trainer", "Party (" + std::to_string(c["party"].size()) + ")",
                                                "PC boxes (" + std::to_string(boxed) + ")", "Items",
                                                "Pokédex (" + std::to_string(c["pokedex"]["owned"].size()) + ")" };
        if (int k = Seg("tabs", tabs, gbTab); k >= 0 && k != gbTab) gbTab = k, ResetFocus();
        ImGui::Separator();
        ImGui::Spacing();
        auto mut = [this](const std::function<void()>& fn) {
            fn();
            Changed();
        };
        MonCtx ctx;
        ctx.title = g.label;
        ctx.otName = TextOf(t["name"]);
        ctx.otId = I(t["id"]);
        ctx.level = 5;
        if (gbTab == 0) {
            Label("Player name", 160);
            if (auto v = NameField("##name", t["name"], 8)) mut([&] { t["name"] = *v; });
            Label("Rival name", 160);
            if (auto v = NameField("##rival", t["rival"], 8)) mut([&] { t["rival"] = *v; });
            Label("Trainer ID", 160);
            if (auto v = IntField("id", I(t["id"]), 0, 65535, 70)) mut([&] { t["id"] = *v; });
            Label("Money", 160);
            if (auto v = IntField("money", I(t["money"]), 0, 999999, 80, 1000)) mut([&] { t["money"] = *v; });
            if (g.kind == "yellow") {
                Label("Pikachu's friendship", 160);
                if (auto v = IntField("pika", I(t["pikachu_happiness"]), 0, 255, 40, 10)) mut([&] { t["pikachu_happiness"] = *v; });
            }
            Label("Play time", 160);
            const json& pt = t["playtime"];
            ImGui::Text("%d:%s:%s", I(pt["hours"]), Pad(I(pt["minutes"]), 2).c_str(), Pad(I(pt["seconds"]), 2).c_str());
            Hint("Changing the trainer ID makes the game treat your own Pokémon as traded ones (they obey only with the right badges).");
            ImGui::Spacing();
            Heading("Badges");
            for (int i = 0; i < 8; i++) {
                ImGui::PushID(i);
                if (i) SameLineWrap(S(130));
                bool on = I(t["badges"]) & (1 << i);
                std::string label = std::string(gb::BADGE_NAMES[i]) + " Badge";
                if (Check(label.c_str(), &on)) mut([&] { t["badges"] = on ? I(t["badges"]) | (1 << i) : I(t["badges"]) & ~(1 << i); });
                ImGui::PopID();
            }
            if (Btn("All badges")) mut([&] { t["badges"] = 255; });
            ImGui::SameLine();
            if (Btn("No badges")) mut([&] { t["badges"] = 0; });
        } else if (gbTab == 1) {
            BoxCells("party", c["party"], 6, ctx, false, [this, gi](const std::function<void(json&)>& fn) {
                fn(gbs[gi].cur["party"]);
                Changed();
            });
            Hint("Up to 6 Pokémon; the party always keeps at least one. Level and stats are recalculated when saving.");
        } else if (gbTab == 2) {
            BoxNav("box", 12, &gbBox, [&](int i) {
                return "Box " + std::to_string(i + 1) + "  (" + std::to_string(c["boxes"][i].size()) + "/20)" +
                       (i == I(c["current_box"]) ? "  · current" : "");
            });
            const int n = gbBox;
            if (!c["boxes"][n].empty()) {
                ImGui::SameLine();
                if (DangerBtn("Empty this box")) {
                    Ask("Empty PC box " + std::to_string(n + 1) + "?", "Empty", [this, gi, n] {
                        gbs[gi].cur["boxes"][n] = json::array();
                        Changed();
                    });
                }
            }
            ImGui::Spacing();
            BoxCells("box", c["boxes"][n], 20, ctx, true, [this, gi, n](const std::function<void(json&)>& fn) {
                fn(gbs[gi].cur["boxes"][n]);
                Changed();
            });
        } else if (gbTab == 3) {
            Heading("Bag");
            ItemRows("bag", c["bag"], 20, [this, gi](const std::function<void(json&)>& fn) {
                fn(gbs[gi].cur["bag"]);
                Changed();
            }, "The bag is empty.");
            ImGui::Spacing();
            Heading("PC items");
            ItemRows("pc", c["pc_items"], 50, [this, gi](const std::function<void(json&)>& fn) {
                fn(gbs[gi].cur["pc_items"]);
                Changed();
            }, "No items stored in the PC.");
        } else {
            std::set<int> owned, seen;
            for (const json& x : c["pokedex"]["owned"]) owned.insert(I(x));
            for (const json& x : c["pokedex"]["seen"]) seen.insert(I(x));
            Hint(std::to_string(owned.size()) + " caught · " + std::to_string(seen.size()) +
                 " seen · pick a Pokémon to change it (not seen -> seen -> caught)");
            if (Btn("Mark all caught")) {
                json all = json::array();
                for (int d = 1; d <= 151; d++) all.push_back(d);
                mut([&] {
                    c["pokedex"]["owned"] = all;
                    c["pokedex"]["seen"] = all;
                });
            }
            ImGui::SameLine();
            if (DangerBtn("Clear Pokédex")) {
                Ask("Clear the whole Pokédex?", "Clear", [this, gi] {
                    gbs[gi].cur["pokedex"]["owned"] = json::array();
                    gbs[gi].cur["pokedex"]["seen"] = json::array();
                    Changed();
                });
            }
            ImGui::Spacing();
            const float w = st->handheld ? 150 : 128;
            for (int d = 1; d <= 151; d++) {
                ImGui::PushID(d);
                GridNext(d - 1, w);
                std::string label = Pad(d, 3) + " " + Show(gd::SPECIES[d]);
                uint32_t col = owned.count(d) ? 0x1F4D2C : seen.count(d) ? 0x3D3A1E : kButton;
                if (ColorBtn(label.c_str(), col, w)) {
                    if (owned.count(d)) owned.erase(d), seen.erase(d);
                    else if (seen.count(d)) owned.insert(d);
                    else seen.insert(d);
                    mut([&] {
                        c["pokedex"]["owned"] = json(std::vector<int>(owned.begin(), owned.end()));
                        c["pokedex"]["seen"] = json(std::vector<int>(seen.begin(), seen.end()));
                    });
                }
                ImGui::PopID();
            }
        }
    }

    // ---- frame ----

    std::vector<Section> Sections() {
        std::vector<Section> s = { { "overview", "Overview" }, { "progress", "Progress & unlocks" }, { "options", "Options" },
                                   { "teams", "Registered teams" }, { "boxes", "Lab boxes" }, { "items", "Item boxes" },
                                   { "palace", "Victory Palace" }, { "run", "Saved cup run" }, { "gallery", "Gallery" } };
        for (size_t i = 0; i < gbs.size(); i++) s.push_back({ "gb" + std::to_string(i), gbs[i].label + " · " + gbs[i].title });
        return s;
    }

    std::string Count(const std::string& key) {
        if (key == "teams") return std::to_string(RegisteredTeams());
        if (key == "boxes") return std::to_string(BoxedCount());
        if (key == "items") return std::to_string(ItemCount());
        if (key == "palace") return std::to_string(Statues());
        if (key == "run") return (I(cur["continue"]["flags"]) & 1) ? "1" : "";
        if (key.rfind("gb", 0) == 0) {
            const Gb& g = gbs[std::stoi(key.substr(2))];
            return g.cur != g.base ? "*" : "";
        }
        return "";
    }

    void RequestClose() {
        if (!dirty) {
            open = false;
            return;
        }
        Confirm c{ "Save the changes before closing?", "Save and close", [this] {
                      Save();
                      if (error.empty()) open = false;
                  } };
        c.other = [this] { open = false; };
        c.otherLabel = "Close without saving";
        confirm = std::move(c);
        ResetFocus();
    }

    void Draw(ImVec2 min, ImVec2 max) {
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        pdl->AddRectFilled(min, max, Col(kCard), S(8));
        pdl->AddRect(min, max, Col(kBorder), S(8));
        ImGui::SetCursorScreenPos(ImVec2(min.x + S(12), min.y + S(8)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Header, ColV(kSelected));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ColV(kButton));
        ImGui::PushStyleColor(ImGuiCol_Separator, ColV(kBorder));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ColV(kWindow));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ColV(0x0E1117));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6), S(st->handheld ? 8 : 6)));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(st->handheld ? 9 : 4)));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(st->handheld ? 18 : 12));
        const ImVec2 size(max.x - min.x - S(24), max.y - min.y - S(16));
        ImGui::BeginChild("save-editor", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetWindowFontScale(st->scale);

        // header: title, file, status, Discard / Save / Close
        if (st->game) ImGui::PushFont(st->game);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("SAVE EDITOR");
        if (st->game) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ColV(kDim), "%s", fileName.c_str());
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (!toast.empty() && SDL_GetTicks() < toastUntil) ImGui::TextColored(ColV(kGreen), "%s", toast.c_str());
        else ImGui::TextColored(ColV(dirty ? kWarn : kDim), "%s", dirty ? "Unsaved changes" : "All changes saved");
        const float bw = st->handheld ? 110 : 84;
        ImGui::SameLine(size.x - S(bw * 3 + 16));
        const bool modal = dlg || picker || confirm;
        ImGui::BeginDisabled(!dirty || modal);
        if (Btn("Discard", bw)) {
            Ask("Throw away all changes since the last save?", "Discard", [this] {
                try {
                    Load();
                    error.clear();
                } catch (const std::exception& e) {
                    error = e.what();
                }
            });
        }
        ImGui::SameLine();
        if (ColorBtn("Save", dirty ? kGreen : kButton, bw)) Save();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(modal);
        if (Btn("Close", bw)) RequestClose();
        ImGui::EndDisabled();
        if (!error.empty()) {
            Hint(error, kRed);
            ImGui::SameLine();
            if (ImGui::SmallButton("OK")) error.clear();
        }
        ImGui::Separator();

        // sections (left) and the page (right)
        const float navW = st->handheld ? 200 : 178;
        const float h = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("sections", ImVec2(S(navW), h), false);
        ImGui::SetWindowFontScale(st->scale);
        ImGui::BeginDisabled(modal);
        for (const Section& s : Sections()) {
            ImGui::PushID(s.key.c_str());
            std::string count = Count(s.key);
            std::string label = s.name + (count.empty() ? "" : "  (" + count + ")");
            if (!modal && section == s.key && st->setDefaultFocus) st->setDefaultFocus();
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
            if (ColorBtn(label.c_str(), section == s.key ? kSelected : kButton, navW - 8)) Go(s.key);
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("page", ImVec2(0, h), false);
        ImGui::SetWindowFontScale(st->scale);
        if (!scrollTo.empty()) {
            ImGui::SetScrollY(0);
            scrollTo.clear();
        }
        if (confirm) {
            DrawConfirm();
        } else if (picker) {
            DrawPicker();
        } else if (dlg) {
            DrawMonDialog();
        } else {
            ImGui::PushID(section.c_str());
            if (section == "overview") PageOverview();
            else if (section == "progress") PageProgress();
            else if (section == "options") PageOptions();
            else if (section == "teams") PageTeams();
            else if (section == "boxes") PageBoxes();
            else if (section == "items") PageItems();
            else if (section == "palace") PagePalace();
            else if (section == "run") PageRun();
            else if (section == "gallery") PageGallery();
            else if (section.rfind("gb", 0) == 0 && std::stoi(section.substr(2)) < (int)gbs.size()) PageGb(std::stoi(section.substr(2)));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(7);

        // keyboard: Ctrl/Cmd+S saves, Escape goes back
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_S, false) && dirty && !modal) Save();
            if (!st->handheld && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Back();
        }
    }

    bool Back() {
        if (confirm) confirm.reset();
        else if (picker) picker.reset();
        else if (dlg) dlg.reset();
        else {
            RequestClose();
            return true;
        }
        ResetFocus();
        return true;
    }
};

SaveEditor::SaveEditor() : mImpl(std::make_unique<Impl>()) {}
SaveEditor::~SaveEditor() = default;

bool SaveEditor::Open(const std::string& save, const std::vector<SaveEditorGb>& gbs, int gbSection, std::string* error) {
    Impl& m = *mImpl;
    Impl fresh;
    try {
        fresh.path = save;
        fresh.fileName = FileNameOf(save);
        for (const SaveEditorGb& g : gbs) {
            std::error_code ec;
            if (g.save.empty() || !std::filesystem::exists(std::filesystem::u8path(g.save), ec)) continue;
            Impl::Gb e;
            e.label = g.label;
            e.path = g.save;
            e.rom = g.rom;
            e.name = FileNameOf(g.save);
            json info = gb::GameInfo(g.rom, g.save);
            e.title = info["title"].get<std::string>();
            e.kind = info["kind"].get<std::string>();
            try {
                gb::GBSave(ReadFile(g.save)); // a Game Boy save that is not one (wrong size) is left out
            } catch (const std::exception&) {
                continue;
            }
            fresh.gbs.push_back(e);
        }
        fresh.Load();
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return false;
    }
    m.open = true;
    m.path = fresh.path;
    m.fileName = fresh.fileName;
    m.gbs = std::move(fresh.gbs);
    m.base = std::move(fresh.base);
    m.cur = std::move(fresh.cur);
    m.dirty = false;
    m.dlg.reset();
    m.picker.reset();
    m.confirm.reset();
    m.error.clear();
    m.toast.clear();
    m.section = "overview";
    if (gbSection >= 0) {
        for (size_t i = 0; i < m.gbs.size(); i++) // the Game Boy save of that slot
            if (m.gbs[i].path == gbs[gbSection].save) m.section = "gb" + std::to_string(i);
    }
    m.scrollTo = m.section;
    return true;
}

bool SaveEditor::IsOpen() const {
    return mImpl->open;
}

void SaveEditor::Close() {
    mImpl->open = false;
}

void SaveEditor::Draw(const SaveEditorStyle& style, ImVec2 min, ImVec2 max) {
    mImpl->style = style;
    mImpl->Draw(min, max);
}

bool SaveEditor::Back() {
    if (!mImpl->open) return false;
    return mImpl->Back();
}

bool SaveEditor::HasUnsavedChanges() const {
    return mImpl->open && mImpl->dirty;
}

} // namespace Port
