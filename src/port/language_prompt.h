// The first-boot language prompt (hippodrome-utils docs/specs/TRANSLATION.md, "Language Selection Procedure"): when
// the system language is a supported language other than the ROM's, the launcher offers the AI translation of the
// ROM's text into it, in both languages. The prompt's own strings are translation/port.csv (packed into
// pokestadium.o2r); the choice is the port setting gPort.TextLanguage ("" = the ROM's own text) and the prompt is
// shown once (gPort.LanguagePrompted).
#pragma once

#include <string>
#include <vector>

namespace Port {

// The supported languages, in the tables' column order: en, es, jp, pt, de, ko, fr, it.
extern const char* const kLanguageCodes[8];
bool IsOfficialLanguage(const std::string& code); // has its own Stadium ROM (not pt or ko)
// The language's name in its own language (LANGUAGE_NATIVE_*: "Deutsch", "日本語").
std::string NativeLanguageName(const std::string& code);

// The system's preferred language as a code above ("" when it is none of them). PORT_LOCALE=<code or locale such as
// ja_JP> overrides it (a test aid).
std::string SystemLanguage();
// The language of the Stadium ROM at `path` from its header's country byte ("" when it is not a Stadium ROM).
std::string RomLanguage(const std::string& path);
// The same from a ROM's first 0x40 bytes.
std::string RomLanguageFromHeader(const unsigned char* header);

// A file in pokestadium.o2r as text ("" when it is missing), and RFC 4180 CSV text as rows of cells.
std::string ArchiveText(const char* path);
std::vector<std::vector<std::string>> ParseCsv(const std::string& text);

// translation/port.csv's cell for `id` in `lang`, with {rom} / {locale} (and English {a_rom}: "a"/"an" + the name)
// replaced by those languages' names in `lang`. The English cell when `lang`'s is empty; `id` itself when missing.
std::string PortText(const char* id, const std::string& lang, const std::string& rom = "",
                     const std::string& locale = "");
// Every character port.csv's cells in `langs` hold, as UTF-8 (the launcher adds their glyphs to its fonts).
std::string PortTextCharacters(const std::vector<std::string>& langs);

// The language in-game text is drawn in: the chosen text language (gPort.TextLanguage), else the loaded ROM's own
// (src/port/text_lookup.cpp). A UTF-8 string in the game's encoding (wide codes for characters the ROM font lacks),
// and a UTF-8 string with its ASCII and Latin-1 letters in capitals.
std::string TextTargetLanguage();
std::string TextEncode(const std::string& utf8);
std::string TextUpper(const std::string& utf8);

struct LanguagePrompt {
    std::string rom;    // the ROM's language
    std::string locale; // the system's
    bool official;      // the system's language has its own official ROM (the longer prompt)
};
// Whether the prompt is due before the ROM at `path` starts, and for which languages.
bool LanguagePromptDue(const std::string& path, LanguagePrompt* out);
// Whether it could be due for some ROM: the launcher then loads the fonts it needs.
bool LanguagePromptPossible();
// The player's answer: `language` is the locale's code (the AI translation) or "" (the ROM's own text).
void LanguagePromptAnswered(const std::string& language);

} // namespace Port
