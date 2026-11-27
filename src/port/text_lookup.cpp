// Translated in-game text (StadionPort.h "Translated text"; hippodrome-utils docs/specs/TRANSLATION.md).
//
// The loaded ROM's language picks translation/<language>.csv (packed into pokestadium.o2r); gPort.TextLanguage picks
// its column. Text_GetString (game/src/text_system.c) asks for table T<table>_<index> and draws the cell instead of
// the ROM's string, or the ROM's own when the cell is empty. The cells are UTF-8: each character the ROM font has
// (ASCII, Latin-1, the female/male signs) becomes its byte, and any other becomes a two-byte wide code whose glyph
// hippodrome-utils' Unifont (or Pop, gPort.TextFont 1) draws in the game font's style (text_font.h).
#include "StadionPort.h"
#include "Engine.h"
#include "language_prompt.h"

#include <hippodrome/text_font.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <SDL3/SDL.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kWideTrail = 0x80; // trail bytes 0x80-0xFF: 7 bits each
constexpr int kWideCodes = (PORT_TEXT_WIDE_LAST - PORT_TEXT_WIDE_FIRST + 1) * kWideTrail;

struct State {
    std::string source;  // the ROM's language ("" when it has no table here)
    std::vector<std::string> columns; // the table's target languages, in its order
    // language -> (table << 16 | index) -> its cell in the game's encoding (non-empty cells only). A language once
    // loaded stays: the game may hold a string's pointer across a change of language (the options screen).
    std::map<std::string, std::unordered_map<uint32_t, std::string>> cells;
    std::vector<std::vector<std::string>> rows; // the parsed table (all columns), kept to switch languages
    std::vector<std::string> head;
    bool tableRead = false;
} gState;

// wide code -> code point, and back
std::vector<uint32_t> gWide;
std::unordered_map<uint32_t, int> gWideIndex;

hu_unifont* gUnifont = nullptr;
hu_pop* gPop = nullptr;
bool gFontsRead = false;
std::map<uint64_t, std::unique_ptr<hu_glyph, void (*)(hu_glyph*)>> gGlyphs;

uint32_t Key(uint32_t table, uint32_t index) {
    return (table << 16) | index;
}

uint32_t NextCodePoint(const std::string& s, size_t* i) {
    const char* p = s.c_str() + *i;
    const char* start = p;
    const uint32_t cp = hu_utf8_next(&p);
    *i += (size_t)(p - start);
    return cp;
}

int WideCode(uint32_t cp) {
    auto it = gWideIndex.find(cp);
    if (it != gWideIndex.end()) {
        return it->second;
    }
    if ((int)gWide.size() >= kWideCodes) {
        return -1;
    }
    const int code = (int)gWide.size();
    gWide.push_back(cp);
    gWideIndex[cp] = code;
    return code;
}

// A UTF-8 cell in the game's encoding: \n and \xNN escapes, the ROM font's own bytes, wide codes for the rest.
std::string Encode(const std::string& cell) {
    std::string out;
    size_t i = 0;
    while (i < cell.size()) {
        if (cell[i] == '\\' && i + 1 < cell.size() && cell[i + 1] == 'n') {
            out += '\n';
            i += 2;
            continue;
        }
        if (cell[i] == '\\' && i + 3 < cell.size() && cell[i + 1] == 'x' && isxdigit((unsigned char)cell[i + 2]) &&
            isxdigit((unsigned char)cell[i + 3])) {
            out += (char)strtol(cell.substr(i + 2, 2).c_str(), nullptr, 16);
            i += 4;
            continue;
        }
        const uint32_t cp = NextCodePoint(cell, &i);
        if (cp == 0) {
            break;
        }
        if (cp == '\r') {
            continue;
        }
        if (cp < 0x80) {
            out += (char)cp;
            continue;
        }
        if (cp == 0x2640 || cp == 0x2642) { // the font draws ♀ at 0xBE and ♂ at 0xA9
            out += (char)(cp == 0x2640 ? 0xBE : 0xA9);
            continue;
        }
        if (cp >= 0xA0 && cp <= 0xFF && cp != 0xBE && cp != 0xA9 && Font_MapGlyphCode((uint16_t)cp) != 0) {
            out += (char)cp;
            continue;
        }
        const int code = WideCode(cp);
        if (code < 0) {
            out += '?';
            continue;
        }
        out += (char)(PORT_TEXT_WIDE_FIRST + code / kWideTrail);
        out += (char)(0x80 | (code % kWideTrail));
    }
    return out;
}

void ReadTable() {
    if (gState.tableRead) {
        return;
    }
    gState.tableRead = true;
    if (Port::RomSize() < 0x40) {
        return;
    }
    const std::string src = Port::RomLanguageFromHeader(Port::RomData());
    if (src.empty()) {
        return;
    }
    const std::string text = Port::ArchiveText(("translation/" + src + ".csv").c_str());
    if (text.empty()) {
        SDL_Log("[text] translation/%s.csv is not in pokestadium.o2r: the ROM's own text only", src.c_str());
        return;
    }
    gState.rows = Port::ParseCsv(text);
    if (gState.rows.empty()) {
        return;
    }
    gState.head = gState.rows[0];
    gState.rows.erase(gState.rows.begin());
    gState.source = src;
    // id, md5, address, the targets, note
    for (size_t c = 3; c + 1 < gState.head.size(); c++) {
        gState.columns.push_back(gState.head[c]);
    }
}

std::string Chosen() {
    ReadTable();
    std::string lang = CVarGetString("gPort.TextLanguage", "");
    for (const std::string& c : gState.columns) {
        if (c == lang) {
            return lang;
        }
    }
    return "";
}

// `lang`'s cells, read on first use.
const std::unordered_map<uint32_t, std::string>& Load(const std::string& lang) {
    auto found = gState.cells.find(lang);
    if (found != gState.cells.end()) {
        return found->second;
    }
    auto& cells = gState.cells[lang];
    size_t col = 0;
    for (size_t c = 0; c < gState.head.size(); c++) {
        if (gState.head[c] == lang) {
            col = c;
        }
    }
    size_t n = 0;
    for (const auto& row : gState.rows) {
        // T<table hex>_<index decimal>; ROM_ rows are drawn by other code (the GB Tower's menu) and named rows are
        // COM nicknames (<src>_localized.csv), neither through textdata
        if (row.size() <= col || row[0].size() < 6 || row[0][0] != 'T' || row[0][3] != '_' || row[col].empty()) {
            continue;
        }
        const uint32_t table = (uint32_t)strtoul(row[0].substr(1, 2).c_str(), nullptr, 16);
        const uint32_t index = (uint32_t)strtoul(row[0].substr(4).c_str(), nullptr, 10);
        cells[Key(table, index)] = Encode(row[col]);
        n++;
    }
    SDL_Log("[text] %s text from translation/%s.csv: %zu strings, %zu characters outside the ROM font", lang.c_str(),
            gState.source.c_str(), n, gWide.size());
    return cells;
}

void ReadFonts() {
    if (gFontsRead) {
        return;
    }
    gFontsRead = true;
    const std::string hex = Port::ArchiveText("fonts/unifont_subset.hex");
    if (!hex.empty()) {
        gUnifont = hu_unifont_parse(hex.data(), hex.size());
    }
    const std::string ttf = Port::ArchiveText("fonts/pop_subset.ttf");
    if (!ttf.empty()) {
        gPop = hu_pop_load(ttf.data(), ttf.size());
    }
    if (gUnifont == nullptr && gPop == nullptr) {
        SDL_Log("[text] no fonts in pokestadium.o2r: characters outside the ROM font are not drawn");
    }
}

std::string Upper(const std::string& s) {
    // ASCII and Latin-1 letters (the options screen's values are capitals, like the ROM's)
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        const size_t at = i;
        uint32_t cp = NextCodePoint(s, &i);
        if (cp >= 'a' && cp <= 'z') {
            out += (char)(cp - 0x20);
        } else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) {
            cp -= 0x20;
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += s.substr(at, i - at);
        }
    }
    return out;
}

// the languages the options screen offers: "" (the ROM's own) then the table's columns
std::vector<std::string> Choices() {
    ReadTable();
    std::vector<std::string> out = { "" };
    for (const std::string& c : gState.columns) {
        out.push_back(c);
    }
    return out;
}

std::string gLabel;
std::vector<std::string> gNames;
std::string gNamesFor = "\x01";

void BuildLabels() {
    const std::string lang = Chosen();
    if (lang == gNamesFor) {
        return;
    }
    gNamesFor = lang;
    const std::string shown = lang.empty() ? gState.source : lang;
    gLabel = Encode(Upper(Port::PortText("OPTIONS_TEXT_LANGUAGE", shown.empty() ? "en" : shown)));
    gNames.clear();
    for (const std::string& c : Choices()) {
        gNames.push_back(Encode(Upper(Port::NativeLanguageName(c.empty() ? (gState.source.empty() ? "en" : gState.source)
                                                                         : c))));
    }
}

} // namespace

namespace Port {

std::string TextTargetLanguage() {
    std::string lang = Chosen();
    if (lang.empty()) {
        lang = gState.source.empty() ? "en" : gState.source;
    }
    return lang;
}

std::string TextEncode(const std::string& utf8) {
    return Encode(utf8);
}

std::string TextUpper(const std::string& utf8) {
    return Upper(utf8);
}

} // namespace Port

extern "C" {

const char* Port_TextLookup(uint32_t table, uint32_t index) {
    const std::string lang = Chosen();
    if (lang.empty()) {
        return nullptr;
    }
    const auto& cells = Load(lang);
    auto it = cells.find(Key(table, index));
    return it == cells.end() ? nullptr : it->second.c_str();
}

int Port_TextGlyph(uint32_t wide_index, int height, PortTextGlyph* out) {
    if (wide_index >= gWide.size() || height < 1 || height > HU_GLYPH_MAX_HEIGHT || out == nullptr) {
        return 0;
    }
    ReadFonts();
    const bool pop = CVarGetInteger("gPort.TextFont", 0) == 1 && gPop != nullptr;
    const uint64_t key = ((uint64_t)wide_index << 16) | ((uint64_t)height << 1) | (pop ? 1 : 0);
    auto it = gGlyphs.find(key);
    if (it == gGlyphs.end()) {
        const uint32_t cp = gWide[wide_index];
        const hu_glyph_style style = hu_glyph_style_for(height, HU_GLYPH_BOLD);
        std::unique_ptr<hu_glyph, void (*)(hu_glyph*)> g(new hu_glyph{}, [](hu_glyph* x) {
            hu_glyph_free(x);
            delete x;
        });
        int ok = 0;
        if (pop && hu_pop_has(gPop, cp)) {
            ok = hu_pop_glyph(gPop, cp, height, style, g.get());
        }
        if (!ok && gUnifont != nullptr) {
            ok = hu_unifont_glyph(gUnifont, cp, height, style, g.get());
        }
        if (!ok && gPop != nullptr && hu_pop_has(gPop, cp)) {
            ok = hu_pop_glyph(gPop, cp, height, style, g.get());
        }
        if (!ok) {
            g.reset();
        }
        it = gGlyphs.emplace(key, std::move(g)).first;
    }
    if (!it->second) {
        return 0;
    }
    const hu_glyph& g = *it->second;
    out->tex = g.tex;
    out->tex_width = g.tex_width;
    out->draw_width = g.draw_width;
    out->advance = g.advance;
    out->height = g.height;
    return 1;
}

const char* Port_TextLanguageLabel(void) {
    BuildLabels();
    return gLabel.c_str();
}

int Port_TextLanguageCount(void) {
    return (int)Choices().size();
}

const char* Port_TextLanguageName(int choice) {
    BuildLabels();
    return choice >= 0 && choice < (int)gNames.size() ? gNames[choice].c_str() : "";
}

int Port_TextLanguageChoice(void) {
    const std::string lang = Chosen();
    const auto choices = Choices();
    for (size_t i = 0; i < choices.size(); i++) {
        if (choices[i] == lang) {
            return (int)i;
        }
    }
    return 0;
}

void Port_TextLanguageSet(int choice) {
    const auto choices = Choices();
    if (choice < 0 || choice >= (int)choices.size()) {
        return;
    }
    CVarSetString("gPort.TextLanguage", choices[choice].c_str());
    CVarSave();
}

int Port_TextToUtf8(const char* text, char* out, int size) {
    // the reverse of Encode: ASCII, the ROM font's own Latin-1 bytes, the font's ♀ / ♂ and the port's wide codes
    int n = 0;
    auto put = [&](uint32_t cp) {
        char buf[4];
        int len;
        if (cp < 0x80) {
            buf[0] = (char)cp;
            len = 1;
        } else if (cp < 0x800) {
            buf[0] = (char)(0xC0 | (cp >> 6));
            buf[1] = (char)(0x80 | (cp & 0x3F));
            len = 2;
        } else if (cp < 0x10000) {
            buf[0] = (char)(0xE0 | (cp >> 12));
            buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (cp & 0x3F));
            len = 3;
        } else {
            buf[0] = (char)(0xF0 | (cp >> 18));
            buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[3] = (char)(0x80 | (cp & 0x3F));
            len = 4;
        }
        if (n + len >= size) {
            return false;
        }
        memcpy(out + n, buf, (size_t)len);
        n += len;
        return true;
    };
    if (out == nullptr || size <= 0) {
        return 0;
    }
    for (const unsigned char* p = (const unsigned char*)(text != nullptr ? text : ""); *p != 0; p++) {
        uint32_t cp = *p;
        if (cp >= PORT_TEXT_WIDE_FIRST && cp <= PORT_TEXT_WIDE_LAST && (p[1] & 0x80) != 0) {
            const uint32_t code = (cp - PORT_TEXT_WIDE_FIRST) * kWideTrail + (p[1] & 0x7F);
            p++;
            cp = code < gWide.size() ? gWide[code] : '?';
        } else if (cp == 0xBE) {
            cp = 0x2640;
        } else if (cp == 0xA9) {
            cp = 0x2642;
        } else if (cp == '\n') {
            cp = ' ';
        } else if (cp >= 0x80 && cp < 0xA0) {
            cp = '?';
        }
        if (!put(cp)) {
            break;
        }
    }
    out[n] = '\0';
    return n;
}

const char* Port_TextPortString(const char* id) {
    // (language, id) -> the cell in the game's encoding; kept for the whole run, so the pointers stay valid
    static std::map<std::string, std::string> sCache;
    std::string lang = Chosen();
    if (lang.empty()) {
        lang = gState.source.empty() ? "en" : gState.source;
    }
    const std::string key = lang + '\n' + id;
    auto it = sCache.find(key);
    if (it == sCache.end()) {
        it = sCache.emplace(key, Encode(Port::PortText(id, lang))).first;
    }
    return it->second.c_str();
}

} // extern "C"
