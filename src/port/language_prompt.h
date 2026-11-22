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

// The system's preferred language as a code above ("" when it is none of them). PORT_LOCALE=<code or locale such as
// ja_JP> overrides it (a test aid).
std::string SystemLanguage();
// The language of the Stadium ROM at `path` from its header's country byte ("" when it is not a Stadium ROM).
std::string RomLanguage(const std::string& path);

// translation/port.csv's cell for `id` in `lang`, with {rom} / {locale} (and English {a_rom}: "a"/"an" + the name)
// replaced by those languages' names in `lang`. The English cell when `lang`'s is empty; `id` itself when missing.
std::string PortText(const char* id, const std::string& lang, const std::string& rom = "",
                     const std::string& locale = "");
// Every character port.csv's cells in `langs` hold, as UTF-8 (the launcher adds their glyphs to its fonts).
std::string PortTextCharacters(const std::vector<std::string>& langs);

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
