// Subtitles for the announcer (hippodrome-utils docs/specs/SUBTITLES.md): while a voice line plays, and for 45 frames
// after it ends, its text in the player's language is drawn in a translucent box above the battle message box.
//
// The line comes from src/port/announcer.cpp (Port_AnnouncerClip publishes each line the announcer starts on stream
// channel 1; Port_SubtitleText is its text, or NULL when the setting hides it). This side keeps the timing, wraps the
// text to the box and draws it at the end of the frame (StageLoader_SetupFrame), with the game's own font: the
// screen's loaded font set closest to 12 pixels high, the characters it lacks from Unifont or Pop (wide codes, as for
// all translated text).
#include "global.h"
#include "variables.h"
#include "src/graphics_textures.h"
#include "src/stage_loader.h"
#include "src/audio_stream.h"

#include "StadionPort.h"

// 320x240 coordinates, doubled on a 640x480 stage. The box sits just above Stadium 1's battle message box (x 50-270,
// from y 180), left-aligned with it and narrower, so it stays clear of the portrait in the command screen's corner.
#define SUBTITLE_BOX_X 50
#define SUBTITLE_BOX_W 204
#define SUBTITLE_BOX_BOTTOM 176
#define SUBTITLE_PAD 6
#define SUBTITLE_MAX_LINES 6
#define SUBTITLE_HOLD_FRAMES 45
#define SUBTITLE_MAX_CODES 512

static s32 sSerial = -1;
static s32 sHold;
static s32 sShowing; // a line is current (playing, or within its hold time)

static s32 sCodes[SUBTITLE_MAX_CODES];
static s32 sLineStart[SUBTITLE_MAX_LINES];
static s32 sLineEnd[SUBTITLE_MAX_LINES];

// The loaded font set to draw with: 12 pixels high if the screen has it, else the nearest (smaller first), or -1.
static s32 Subtitles_PickFontSet(void) {
    static const s32 kOrder[] = { 1, 0, 2, 3, 4, 5 }; // heights 12, 10, 16, 20, 24, 32
    s32 i;

    if (gFontContext == NULL) {
        return -1;
    }
    for (i = 0; i < ARRAY_COUNT(kOrder); i++) {
        FontSet* set = &gFontContext->unk_00[kOrder[i]];
        if (set->glyphWidths != NULL && set->texture != NULL && set->height > 0) {
            return kOrder[i];
        }
    }
    return -1;
}

static u32 Subtitles_CodePoint(s32 code) {
    if (code & FONT_WIDE_CODE) {
        return Port_TextWideCodePoint(code & 0xFFFF);
    }
    return (u32)(code & 0xFF); // the ROM font's bytes are ASCII and Latin-1
}

// CJK characters (Japanese has no spaces) break anywhere; Latin and Hangul break at spaces only.
static s32 Subtitles_IsCjk(u32 cp) {
    return cp >= 0x2E80 && !(cp >= 0xAC00 && cp <= 0xD7A3);
}

// No line starts with closing punctuation, the long-vowel mark or a small kana.
static s32 Subtitles_NoBreakBefore(u32 cp) {
    static const u16 kClosing[] = { 0x3001, 0x3002, 0xFF01, 0xFF1F, 0xFF0C, 0xFF0E, 0x2026, 0x300D, 0x300F, 0xFF09,
                                    0x30FC, 0x3063, 0x3083, 0x3085, 0x3087, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049,
                                    0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9 };
    s32 i;

    if (cp == '!' || cp == '?' || cp == '.' || cp == ',') {
        return 1;
    }
    for (i = 0; i < ARRAY_COUNT(kClosing); i++) {
        if (cp == kClosing[i]) {
            return 1;
        }
    }
    return 0;
}

static s32 Subtitles_Advance(FontSet* set, s32 setIndex, s32 code) {
    return Font_CodeAdvance(set, setIndex, code) - ((setIndex >= 2) ? 2 : 1);
}

// `text` (the game's encoding) broken into lines no wider than maxWidth: the number of lines (at most
// SUBTITLE_MAX_LINES; the rest are dropped).
static s32 Subtitles_Wrap(const char* text, FontSet* set, s32 setIndex, s32 maxWidth) {
    const u8* p = (const u8*)text;
    s32 count = 0;
    s32 lines = 0;
    s32 start = 0;
    s32 code;

    while ((code = Font_NextCode(&p)) != 0 && count < SUBTITLE_MAX_CODES) {
        sCodes[count++] = code;
    }

    while (start < count && lines < SUBTITLE_MAX_LINES) {
        s32 width = 0;
        s32 lastBreak = -1; // a line may end before this code
        s32 i = start;
        s32 end;
        s32 next;

        while (start < count && sCodes[start] == ' ') { // a line's leading spaces are dropped
            start++;
        }
        if (start >= count) {
            break;
        }
        for (i = start; i < count; i++) {
            u32 cp = Subtitles_CodePoint(sCodes[i]);

            if (sCodes[i] == '\n') {
                break;
            }
            if (i > start) {
                u32 prev = Subtitles_CodePoint(sCodes[i - 1]);

                if (sCodes[i] == ' ' ||
                    ((sCodes[i - 1] == ' ' || Subtitles_IsCjk(cp) || Subtitles_IsCjk(prev)) && !Subtitles_NoBreakBefore(cp))) {
                    lastBreak = i;
                }
            }
            width += Subtitles_Advance(set, setIndex, sCodes[i]);
            if (width > maxWidth && i > start) {
                break;
            }
        }
        if (i >= count || sCodes[i] == '\n') {
            end = i;
            next = i + 1;
        } else if (lastBreak > start) {
            end = lastBreak;
            next = lastBreak;
        } else { // one word wider than the box: broken where it overflows
            end = i;
            next = i;
        }
        while (end > start && sCodes[end - 1] == ' ') { // and its trailing spaces
            end--;
        }
        sLineStart[lines] = start;
        sLineEnd[lines] = end;
        lines++;
        start = next;
    }
    return lines;
}

// One glyph at x, y, `scale` times its size: the ROM font's, or a wide code's from Unifont/Pop.
static s32 Subtitles_DrawCode(FontSet* set, s32 setIndex, s32 x, s32 y, s32 code, s32 scale) {
    const u8* tex;
    s32 texWidth;
    s32 drawWidth;
    s32 height;
    s32 advance;

    if (code & FONT_WIDE_CODE) {
        PortTextGlyph glyph;

        if (!Port_TextGlyph(code & 0xFFFF, set->height, &glyph)) {
            return x;
        }
        tex = glyph.tex;
        texWidth = glyph.tex_width;
        drawWidth = glyph.draw_width;
        height = glyph.height;
        advance = glyph.advance;
    } else {
        tex = set->texture + Font_MapGlyphCode(code & 0xFF) * set->width * set->height;
        texWidth = set->width;
        advance = Font_GetGlyphAdvance(set, code & 0xFF);
        drawWidth = advance;
        height = set->height;
    }
    if (tex != gFontContext->unk_4C) {
        gFontContext->unk_4C = (u8*)tex;
        gDPLoadTextureBlock(gDisplayListHead++, (uintptr_t)tex, G_IM_FMT_IA, G_IM_SIZ_8b, texWidth, height, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
    }
    Gfx_DrawTexturedRectClipped(x, y, drawWidth * scale, height * scale, 0, 0, 0x400 / scale, 0x400 / scale, 0);
    return x + (advance - ((setIndex >= 2) ? 2 : 1)) * scale;
}

void Port_SubtitlesDraw(void) {
    s32 serial = Port_SubtitleSerial();
    const char* text;
    s32 setIndex;
    FontSet* set;
    s32 scale;
    s32 lines;
    s32 lineHeight;
    s32 boxHeight;
    s32 boxY;
    s32 i;
    s32 j;
    // the font state the screen set up and prints with every frame
    u8 savedSet, savedLineHeight, savedFlags;
    s8 savedSpacing;

    // the timing runs whatever the setting, so turning subtitles off mid-line keeps it in step
    if (serial != sSerial) {
        sSerial = serial;
        sShowing = serial > 0;
        sHold = SUBTITLE_HOLD_FRAMES;
    } else if (sShowing && !Port_SubtitleTestActive() && !AudioStream_IsPlaying(1)) {
        if (--sHold <= 0) {
            sShowing = 0;
        }
    }
    if (!sShowing || (text = Port_SubtitleText()) == NULL || *text == '\0') {
        return;
    }
    setIndex = Subtitles_PickFontSet();
    if (setIndex < 0) {
        return;
    }
    set = &gFontContext->unk_00[setIndex];
    scale = StageContext_IsHighResolution() ? 2 : 1;

    lines = Subtitles_Wrap(text, set, setIndex, SUBTITLE_BOX_W - 2 * SUBTITLE_PAD);
    if (lines == 0) {
        return;
    }
    lineHeight = set->height + 1;
    boxHeight = lines * lineHeight + SUBTITLE_PAD + 2;
    boxY = SUBTITLE_BOX_BOTTOM - boxHeight;

    savedSet = gFontContext->setIndex;
    savedSpacing = gFontContext->letterSpacing;
    savedLineHeight = gFontContext->lineHeight;
    savedFlags = gFontContext->flags;

    Gfx_FillRectWithDisplayState(SUBTITLE_BOX_X * scale, boxY * scale, SUBTITLE_BOX_W * scale, boxHeight * scale, 0, 0,
                                 0, 0xA0);
    Font_BeginTranslucentTextRendering();
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
    for (i = 0; i < lines; i++) {
        s32 x = SUBTITLE_BOX_X + SUBTITLE_PAD;
        s32 y = boxY + SUBTITLE_PAD / 2 + 1 + i * lineHeight;

        x *= scale;
        for (j = sLineStart[i]; j < sLineEnd[i]; j++) {
            x = Subtitles_DrawCode(set, setIndex, x, y * scale, sCodes[j], scale);
        }
    }
    Font_EndTexturedTextRendering();

    gFontContext->setIndex = savedSet;
    gFontContext->letterSpacing = savedSpacing;
    gFontContext->lineHeight = savedLineHeight;
    gFontContext->flags = savedFlags;
    gFontContext->unk_4C = NULL; // the glyph the screen last loaded is no longer in texture memory
}
