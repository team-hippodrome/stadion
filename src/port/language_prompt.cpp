// The first-boot language prompt's logic and strings (language_prompt.h); the launcher draws it (launcher.cpp).
#include "language_prompt.h"
#include "file_io.h"

#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace Port {

const char* const kLanguageCodes[8] = { "en", "es", "jp", "pt", "de", "ko", "fr", "it" };

namespace {

// a code above for an ISO 639-1 language ("ja" is the tables' "jp"), "" for any other
std::string FromIso(std::string lang) {
    std::transform(lang.begin(), lang.end(), lang.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    lang = lang.substr(0, lang.find_first_of("_-.@"));
    if (lang == "ja" || lang == "jp") {
        return "jp";
    }
    for (const char* code : kLanguageCodes) {
        if (lang == code) {
            return lang;
        }
    }
    return "";
}

// translation/port.csv: id -> language -> cell (RFC 4180)
using Table = std::map<std::string, std::map<std::string, std::string>>;

std::vector<std::vector<std::string>> ParseCsvRows(const std::string& text) {
    std::vector<std::vector<std::string>> rows(1);
    std::string cell;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                cell += '"';
                i++;
            } else if (c == '"') {
                quoted = false;
            } else {
                cell += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            rows.back().push_back(cell);
            cell.clear();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                i++;
            }
            rows.back().push_back(cell);
            cell.clear();
            rows.emplace_back();
        } else {
            cell += c;
        }
    }
    if (!cell.empty() || !rows.back().empty()) {
        rows.back().push_back(cell);
    }
    if (rows.back().empty()) {
        rows.pop_back();
    }
    return rows;
}

const Table& Strings() {
    static Table table;
    static bool loaded = false;
    if (loaded) {
        return table;
    }
    loaded = true;
    std::string text = ArchiveText("translation/port.csv");
    if (text.empty()) {
        SDL_Log("[language] translation/port.csv is not in pokestadium.o2r: the language prompt is off");
        return table;
    }
    auto rows = ParseCsvRows(text);
    if (rows.empty()) {
        return table;
    }
    const auto& head = rows[0];
    for (size_t r = 1; r < rows.size(); r++) {
        if (rows[r].empty()) continue;
        auto& entry = table[rows[r][0]];
        for (size_t c = 1; c < head.size() && c < rows[r].size(); c++) {
            entry[head[c]] = rows[r][c];
        }
    }
    return table;
}

std::string Cell(const char* id, const std::string& lang) {
    const Table& t = Strings();
    auto it = t.find(id);
    if (it == t.end()) {
        return id;
    }
    auto cell = it->second.find(lang);
    if (cell != it->second.end() && !cell->second.empty()) {
        return cell->second;
    }
    cell = it->second.find("en");
    return cell != it->second.end() ? cell->second : std::string(id);
}

std::string LanguageName(const std::string& code, const std::string& in) {
    std::string upper = code;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return Cell(("LANGUAGE_NAME_" + upper).c_str(), in);
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
        s.replace(at, from.size(), to);
    }
}

} // namespace

std::vector<std::vector<std::string>> ParseCsv(const std::string& text) {
    return ParseCsvRows(text);
}

std::string ArchiveText(const char* path) {
    std::string text;
    auto rm = ResourceGetResourceManager();
    if (rm != nullptr && rm->GetArchiveManager() != nullptr) {
        if (auto file = rm->GetArchiveManager()->LoadFile(path); file && file->Buffer) {
            text.assign(file->Buffer->begin(), file->Buffer->end());
        }
    }
    return text;
}

std::string NativeLanguageName(const std::string& code) {
    std::string upper = code;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return Cell(("LANGUAGE_NATIVE_" + upper).c_str(), code);
}

bool IsOfficialLanguage(const std::string& code) {
    return !code.empty() && code != "pt" && code != "ko" && FromIso(code) == code;
}

std::string SystemLanguage() {
    if (const char* forced = getenv("PORT_LOCALE")) {
        return FromIso(forced);
    }
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    std::string lang;
    if (locales != nullptr && count > 0 && locales[0] != nullptr && locales[0]->language != nullptr) {
        lang = FromIso(locales[0]->language);
    }
    SDL_free(locales);
    return lang;
}

std::string RomLanguage(const std::string& path) {
    FILE* f = OpenFileUtf8(path.c_str(), "rb");
    if (f == nullptr) {
        return "";
    }
    unsigned char h[0x40] = {};
    const size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    return n < sizeof h ? std::string() : RomLanguageFromHeader(h);
}

std::string RomLanguageFromHeader(const unsigned char* h) {
    if (h == nullptr || h[0] != 0x80 || h[1] != 0x37) {
        return "";
    }
    // the game code: NPO + region for the Western releases, CP2J for Japan's ポケモンスタジアム２
    if (memcmp(h + 0x3B, "CP2J", 4) == 0) {
        return "jp";
    }
    if (memcmp(h + 0x3B, "NPO", 3) != 0) {
        return "";
    }
    switch (h[0x3E]) {
        case 'E': // USA
        case 'P': // Europe (English)
            return "en";
        case 'F': return "fr";
        case 'D': return "de";
        case 'S': return "es";
        case 'I': return "it";
        default: return "";
    }
}

std::string PortText(const char* id, const std::string& lang, const std::string& rom, const std::string& locale) {
    std::string s = Cell(id, lang);
    if (!rom.empty()) {
        const std::string name = LanguageName(rom, lang);
        if (s.find("{a_rom}") != std::string::npos) {
            const bool vowel = !name.empty() && strchr("AEIOUaeiou", name[0]) != nullptr;
            ReplaceAll(s, "{a_rom}", (vowel ? "an " : "a ") + name);
        }
        ReplaceAll(s, "{rom}", name);
    }
    if (!locale.empty()) {
        ReplaceAll(s, "{locale}", LanguageName(locale, lang));
    }
    return s;
}

std::string PortTextCharacters(const std::vector<std::string>& langs) {
    std::string all;
    for (const auto& [id, cells] : Strings()) {
        for (const std::string& lang : langs) {
            auto cell = cells.find(lang);
            if (cell != cells.end()) all += cell->second;
        }
    }
    return all;
}

bool LanguagePromptPossible() {
    return CVarGetInteger("gPort.LanguagePrompted", 0) == 0 && !SystemLanguage().empty() && !Strings().empty();
}

bool LanguagePromptDue(const std::string& path, LanguagePrompt* out) {
    if (!LanguagePromptPossible()) {
        return false;
    }
    LanguagePrompt p;
    p.rom = RomLanguage(path);
    p.locale = SystemLanguage();
    p.official = IsOfficialLanguage(p.locale);
    if (p.rom.empty() || p.locale.empty() || p.rom == p.locale) {
        return false;
    }
    if (out != nullptr) {
        *out = p;
    }
    return true;
}

void LanguagePromptAnswered(const std::string& language) {
    CVarSetString("gPort.TextLanguage", language.c_str());
    CVarSetInteger("gPort.LanguagePrompted", 1);
    CVarSave();
}

} // namespace Port
