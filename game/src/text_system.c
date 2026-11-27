#include <stdio.h>
#include "text_system.h"
#include "src/jpeg_stream.h"
#include "src/memory.h"
#include "StadionPort.h"

#define D_800AF740_NUM_FILES 42

static BinArchive* gTextArchive;
static char** gTextTokenTable;
// PORT: each file's string table as last returned, so Text_GetString knows which table a string is from (its
// translation's id is T<file>_<index>, src/port/text_lookup.cpp)
#define TEXT_MAX_FILES 64
static char** sTextTables[TEXT_MAX_FILES];

void Text_InitStringTables(void) {
    s32 i;

    gTextArchive = BinArchive_Open(textdata_ROM_START, NULL, 1, 0);
    gTextTokenTable = main_pool_alloc(D_800AF740_NUM_FILES * sizeof(char*), 0);

    for (i = 0; i < D_800AF740_NUM_FILES; i++) {
        gTextTokenTable[i] = NULL;
    }
}

// PORT: a text file starts with a big-endian count followed by that many big-endian u32 offsets (relative to the
// file start), swapped in place on first use. Files can be reloaded into a buffer at the same address, so whether a
// table is already swapped is decided from its contents: the first string follows the offset table, so in native
// order table[1] is a small value just past (count + 1) * 4, while an unswapped count reads as a huge number.
char** Text_GetStringTable(s32 file_number) {
    u32* table = (u32*)BinArchive_GetFile(gTextArchive, file_number);

    if (table == NULL) {
        return NULL;
    }
    if (file_number >= 0 && file_number < TEXT_MAX_FILES) {
        s32 t;
        for (t = 0; t < TEXT_MAX_FILES; t++) { // another file loaded at this address before
            if (sTextTables[t] == (char**)table) {
                sTextTables[t] = NULL;
            }
        }
        sTextTables[file_number] = (char**)table;
    }
    if (!(table[0] < 0x10000 && table[1] >= (table[0] + 1) * 4 && table[1] < (table[0] + 1) * 4 + 0x10)) {
        u32 count = Port_Swap32(table[0]);
        u32 j;

        table[0] = count;
        for (j = 1; j <= count; j++) {
            table[j] = Port_Swap32(table[j]);
        }
        if (table[1] < (count + 1) * 4 || table[1] >= (count + 1) * 4 + 0x10) {
            fprintf(stderr, "[text] table %d has unexpected layout (count %u, first offset 0x%x)\n", file_number, count,
                    table[1]);
        }
    }
    return (char**)table;
}

void Text_SetStringToken(u32 token_index, uintptr_t character) {
    if ((token_index >= 0x14) && (token_index < 0x2A)) {
        gTextTokenTable[token_index] = (char*)character;
    }
}

void Text_SetNumberToken(u32 token_index, uintptr_t character) {
    if ((token_index != 0) && (token_index < 0xA)) {
        gTextTokenTable[token_index] = (char*)character;
    }
}

void Text_SubstituteTokens(char* text, u32 text_length, s8* token) {
    u32 i;
    s32 in_replace_mode;
    s32 current_char;
    char text_buffer[12];
    s8* replacement;
    char* sp54;
    u32 written_count;

    in_replace_mode = 0;
    written_count = 0;
    sp54 = replacement;
    replacement = sp54;
    i = 0;

    while (i < text_length - 1u) {
        switch (in_replace_mode) {
            case 0:
                current_char = *token++;
                if (current_char == '\x00') {
                    goto end;
                }

                if (current_char == '#') {
                    current_char = *token++;

                    current_char -= '0';
                    current_char = (*token++ + (current_char * 10)) - '0';
                    if ((current_char > 0) && (current_char < 10)) {
                        sprintf(text_buffer, "%d", gTextTokenTable[current_char]);
                        replacement = text_buffer;
                        in_replace_mode = 1;
                        continue;
                    }

                    if ((current_char >= 0x14) && (current_char < 0x2A)) {
                        if (gTextTokenTable[current_char] != NULL) {
                            replacement = gTextTokenTable[current_char];
                            in_replace_mode = 1;
                        }
                        continue;
                    }
                } else {
                    *text++ = current_char;
                    written_count++;
                    i++;
                }
                break;

            case 1:
                current_char = *replacement++;
                if (current_char == '\x00') {
                    in_replace_mode = 0;
                } else {
                    *text++ = current_char;
                    written_count++;
                    i++;
                }
                break;
        }
    }

end:
    *text++ = '\x00';
}

char* Text_GetString(char* text, s32 text_length, char** string_array, u32 file_number) {
    char* string;
    if (string_array == NULL) {
        return ""; // PORT: text archive not available yet (its tables are big-endian 32-bit offsets)
    }
    if (string_array == sTextTables[0x25] && file_number == PORT_MOVE_HEAL_BELL - 1) {
        // PORT: Heal Bell, the move harder Vs. Mewtwo adds after the ROM's 165 (src/port/enhancements/heal_bell.c)
        const char* name = Port_TextPortString("PORT_MOVE_HEAL_BELL");
        if (text == NULL) {
            return (char*)name;
        }
        Text_SubstituteTokens(text, text_length, (s8*)name);
        return text;
    }
    if (file_number >= ((u32*)string_array)[0]) {
        // PORT: out of range (e.g. Oak's gift Pokémon with an unset id 0xFFFF): the N64 read junk memory here and
        // printed garbage; on the host that is a wild pointer
        if (text != NULL && text_length > 0) {
            text[0] = '\0';
            return text;
        }
        return "";
    }
    char* string_array_lookup = (char*)string_array + ((u32*)string_array)[file_number + 1];
    {
        // PORT: the chosen text language's string, if it has one (`file_number` is the string's index here)
        s32 t;
        for (t = 0; t < TEXT_MAX_FILES; t++) {
            if (sTextTables[t] == string_array) {
                const char* translated = Port_TextLookup((u32)t, file_number);
                if (translated != NULL) {
                    string_array_lookup = (char*)translated;
                }
                break;
            }
        }
    }

    if (text == NULL) {
        string = string_array_lookup;
    } else {
        string = text;
        Text_SubstituteTokens(text, text_length, string_array_lookup);
    }

    return string;
}

s32 Text_CountLines(s8* arg0) {
    s32 last_chr = 0;
    s32 line_count = 0;
    s32 chr = *arg0++;

    while (chr != 0) {
        last_chr = chr;
        if (chr == '\n') {
            line_count++;
        }
        chr = *arg0++;
    }

    if (last_chr != '\n') {
        line_count++;
    }

    return line_count;
}
