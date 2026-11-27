#include "stadium_select.h"
#include "src/geo_render.h"
#include "src/model_renderer.h"
#include "src/graphics_textures.h"
#include "src/ui_graphics.h"
#include "src/pokemon_stats.h"
#include "src/save_data.h"
#include "src/game_state.h"
#include "src/session.h"
#include "src/text_system.h"
#include "src/jpeg_stream.h"
#include "src/audio_sfx.h"
#include "src/DDC0.h"
#include "src/matrix.h"
#include "src/geo_layout.h"
#include "src/math_util.h"
#include "src/memory.h"
#include "src/stage_loader.h"
#include "StadionPort.h" // PORT_SEGSYM: images the translated releases keep elsewhere
#include <math.h>        // PORT: sqrtf (the Nintendo Cup '97 icon's flag)

typedef struct MenuItemInfo {
    /* 0x00 */ s16 unk_00;
    /* 0x02 */ s16 unk_02;
    /* 0x04 */ s16 unk_04;
    /* 0x06 */ s16 unk_06;
    /* 0x08 */ s8 unk_08;
    /* 0x09 */ s8 unk_09;
    /* 0x0A */ s8 unk_0A;
    /* 0x0B */ s8 unk_0B;
} MenuItemInfo; // size = 0xC

typedef struct MenuRectAnim {
    /* 0x00 */ s16 unk_00;
    /* 0x02 */ s16 unk_02;
    /* 0x04 */ s16 unk_04;
    /* 0x06 */ s16 unk_06;
    /* 0x08 */ s16 unk_08;
    /* 0x0A */ s16 unk_0A;
} MenuRectAnim; // size = 0xC

#if !defined(VERSION_JP)
static char** sStadiumCupText;
#endif
static BinArchive* D_84103444;
static void* D_84103448;
static void* D_8410344C;
static Vtx* D_84103450;
static GraphNode* D_84103454;
// PORT: 7 with the Nintendo Cups restored (the port's seven-cup layout, below), else 6
static DisplayObject D_84103458[7];
static s16 D_84103CC8;
static DepthBuffer* D_84103CD0[7];
static GraphNode* D_84103CE8;
static GraphNode* D_84103CEC;
static ModeSettings D_84103CF0;

static u8 sBackgroundFileIds[] = {
    0x09, 0x07, 0x08, 0x0C, 0x0A, 0x0B,
};
static s16 gStadiumCupSessionModes[] = { 3, 1, 2, 6, 4, 5 };
static s16 gStadiumCupModeCategories[] = { 3, 1, 2, 6, 4, 5 };

// PORT: restored content (src/port/nintendo_cup.cpp). With a Japanese ROM beside a western one, the screen offers
// seven cups in three rows (Julius's layout): Poké Cup and Nintendo Cup '97; Petit, Prime and Pika Cup; Nintendo Cup
// '98 and '99. The icons are drawn smaller (NC_ICON_SCALE) to fit; '97 and '98 (and '99's) pictures come from the
// Japanese ROM's stadium_select_ui, which keeps them where the western one does.
#define NC_CUP_COUNT 7
#define NC_ICON_SCALE 0.72f
#define NC_JP_STADIUM_SELECT_UI_START 0x524BF0
#define NC_JP_STADIUM_SELECT_UI_END 0x54B7E0
typedef struct NcCupSlot {
    s16 mode;       // session mode and rule category
    s8 is99;        // mode 3 as Nintendo Cup '99
    u8 background;  // backgrounds archive file
    u8 big;         // a four-division icon (152x114) rather than a one-cup icon (128x96)
    s16 x, y;       // the icon's centre on the 640x480 screen
    s8 up, down, left, right;
} NcCupSlot;
static NcCupSlot sNcCupSlots[NC_CUP_COUNT] = {
    { 3, 0, 9, 1, 255, 96, -1, 2, -1, 1 },  // Poké Cup
    { 1, 0, 9, 0, 385, 96, -1, 4, 0, -1 },  // Nintendo Cup '97 (backgrounds file 9: the Japanese '97 picture)
    { 4, 0, 10, 0, 160, 191, 0, 5, -1, 3 }, // Petit Cup
    { 6, 0, 12, 1, 320, 191, 0, 6, 2, 4 },  // Prime Cup
    { 5, 0, 11, 0, 480, 191, 1, 6, 3, -1 }, // Pika Cup
    { 2, 0, 8, 0, 255, 286, 2, -1, -1, 6 }, // Nintendo Cup '98
    { 3, 1, 7, 1, 385, 286, 4, -1, 5, -1 }, // Nintendo Cup '99 (file 7: the Japanese '99 picture)
};
static s32 sNcLayout;     // the seven-cup layout is on
static s16 sCupCount = 6; // cups on the screen (B returns this)
static u8* sNcJpUi;       // the Japanese stadium_select_ui
static MenuItemInfo sNcItemInfo[NC_CUP_COUNT];
// sparkles over the medals: four for each four-division cup (Poké, Prime, '99), one for each other
static MenuRectAnim sNcSparkles[16];
static s32 sNcSparkleCount;
#if defined(VERSION_JP)
static MenuItemInfo sMenuItemInfo[] = {
    { 244, 70, 152, 114, -1, 3, 1, 2 },  { 88, 88, 128, 96, -1, 4, -1, 0 },  { 424, 88, 128, 96, -1, 5, 0, -1 },
    { 244, 212, 152, 114, 0, -1, 4, 5 }, { 88, 212, 128, 96, 1, -1, -1, 3 }, { 424, 212, 128, 96, 2, -1, 3, -1 },
};
#else
static MenuItemInfo sMenuItemInfo[] = {
    {
        244,
        70,
        152,
        114,
        -1,
        3,
        4,
        5,
    },
    {
        88,
        88,
        128,
        96,
        -1,
        -1,
        -1,
        -1,
    },
    {
        424,
        88,
        128,
        96,
        -1,
        -1,
        -1,
        -1,
    },
    {
        244,
        212,
        152,
        114,
        0,
        -1,
        4,
        5,
    },
    {
        88,
        150,
        128,
        96,
        0,
        3,
        -1,
        0,
    },
    {
        424,
        150,
        128,
        96,
        0,
        3,
        3,
        -1,
    },
};
#endif
static MenuRectAnim sMenuRectAnimations[] = {
    {
        0,
        260,
        140,
        32,
        -32768,
        0,
    },
    {
        0,
        298,
        140,
        32,
        16384,
        0,
    },
    {
        0,
        336,
        140,
        32,
        -16384,
        0,
    },
    {
        0,
        374,
        140,
        32,
        0,
        0,
    },
    {
        0,
        188,
        150,
        32,
        28672,
        0,
    },
    {
        0,
        524,
        150,
        32,
        12288,
        0,
    },
    {
        0,
        260,
        282,
        32,
        -24576,
        0,
    },
    {
        0,
        298,
        282,
        32,
        24576,
        0,
    },
    {
        0,
        336,
        282,
        32,
        -8192,
        0,
    },
    {
        0,
        374,
        282,
        32,
        8192,
        0,
    },
#if defined(VERSION_JP)
    { 0, 0xBC, 0x110, 0x20, 0x9000, 0 },
    { 0, 0x20C, 0x110, 0x20, 0xD000, 0 },
#else
    // PORT: entries 10 and 11 were separate s16[6] arrays (D_84102510, D_8410251C) that the 12-entry loops in
    // StadiumSelect_UpdateSparkles / RenderFrame reach as D_84102498[10..11]
    { 0, 0xBC, 0xD2, 0x20, (s16)0x9000, 0 },
    { 0, 0x20C, 0xD2, 0x20, (s16)0xD000, 0 },
#endif
};
#if defined(VERSION_JP)
// JP has built-in cup names and descriptions instead of a string table.
static char* sStadiumCupNames[] = {
    // "Nintendo Cup '99"
    "ニンテンドウカップ‘９９",
    // "Nintendo Cup '97"
    "ニンテンドウカップ‘９７",
    // "Nintendo Cup '98"
    "ニンテンドウカップ‘９８",
    // "Ultra Cup"
    "ウルトラカップ",
    // "Fancy Cup"
    "ファンシーカップ",
    // "Yellow Cup"
    "イエローカップ",
};
static char* sStadiumCupDescriptions[] = {
    // "Battle under the rules of the 3rd Pokémon League in '99. A new kind of battle begins."
    "　９９ねん　だい３かいポケモンリーグルールでの\n　バトル。あたらしい　たたかいの　はじまりだ。",
    // "Battle under the rules of the 1st Pokémon League in '97. Seasoned veterans gather here."
    "　９７ねん　だい１かいポケモンリーグルールでの\n　バトル。れきせんの　つわものが　せいぞろい。",
    // "Battle under the rules of the 2nd Pokémon League in '98. A tough, thoroughly studied cup."
    "　９８ねん　だい２かいポケモンリーグルールでの\n　バトル。けんきゅうしつくされた　てごわいカップ。",
    // "The highest-level cup, for mastering the peak of Pokémon battles. Go all out!"
    "　ポケモンバトルの　ちょうてんを　きわめる　さいだい\n　レベルのカップ。おもいっきり　あばれてみよう。",
    // "There are lots of cute Pokémon, but take them lightly and you'll be in trouble. It may show your true skill."
    "　かわいいポケモンが　おおいからって、ナメてかかると\n　たいへん。キミのじつりょくが　わかるかも。",
    // "The cup you can enter at the lowest level. That makes planning your strategy all the more important."
    "　もっとも　ちいさいレベルで　さんかできるカップ。\n　それだけに　さくせんの　たてかたが　だいじ。",
};

static u8 sMenuOptionRanges[][2] = {
    { 1, 50 }, { 50, 55 }, { 1, 30 }, { 1, 100 }, { 25, 30 }, { 15, 20 },
};
#else
static u8 sMenuOptionRanges[][2] = {
    {
        0x32,
        0x37,
    },
    {
        0x32,
        0x37,
    },
    {
        0x1,
        0x1E,
    },
    {
        0x1,
        0x64,
    },
    {
        0x19,
        0x1E,
    },
    { 0xF, 0x14 },
};
#endif
#if defined(VERSION_DE) || defined(VERSION_IT)
// The German and Italian menu textures before these are larger, so these sit later in segment 3.
extern u8 D_030294E0[];
extern u8 D_0302A260[];
extern u8 D_0302AFE0[];
extern u8 D_0302BD60[];
extern u8 D_0302CAE0[];
extern u8 D_0302DAA0[];
static u8* sMenuOptionTexturePointers[] = { D_030294E0, D_0302A260, D_0302AFE0, D_0302BD60, D_0302CAE0, D_0302DAA0 };
#else
static u8* sMenuOptionTexturePointers[] = { D_03028EC0, D_03029C40, D_0302A9C0, D_0302B740, D_0302C4C0, D_0302D480 };
#endif
// PORT: the German and Italian menu textures before these are larger, so these sit later in segment 3 (the decomp's
// VERSION_DE and VERSION_IT table above: D_030294E0, D_0302A260, D_0302AFE0, D_0302BD60, D_0302CAE0, D_0302DAA0)
static u8* sMenuOptionTexturePointersDeIt[] = { (u8*)0x30294E0, (u8*)0x302A260, (u8*)0x302AFE0,
                                                (u8*)0x302BD60, (u8*)0x302CAE0, (u8*)0x302DAA0 };
#define MENU_OPTION_TEXTURES ((PORT_VERSION_DE || PORT_VERSION_IT) ? sMenuOptionTexturePointersDeIt : sMenuOptionTexturePointers)
static uintptr_t sMenuGeoLayoutRoot[] = {
    // PORT64: holds addresses
    0x0C000000,  0x05000000, 0x0B00002D, 0x00000000, 0x028001E0, 0x00000000, 0xFDBD0000, 0x00000243, 0x05000000,
    0x0D000000,  0x05000000, 0x14000000, 0x00000000, 0xFFFFFF32, 0x16FFFFFF, 0x0F000002, 0x05000000, 0x0A000000,
    &D_800AC840, 0x06000000, 0x06000000, 0x06000000, 0x06000000, 0x01000000, 0x00000000,
};
// PORT: one array (the N64 layout): the game indexes across these symbols
static Vtx sMenuBarVtx0[] = {
    VTX(0, 63, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF),          VTX(0, 43, 0, 0, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 43, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(100, 63, 0, 3200, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, 43, 0, 0, 640, 0xFF, 0xFF, 0xFF, 0xFF),        VTX(0, 23, 0, 0, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 23, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(100, 43, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, 23, 0, 0, 1280, 0xFF, 0xFF, 0xFF, 0xFF),       VTX(0, 3, 0, 0, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 3, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(100, 22, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, 3, 0, 0, 1920, 0xFF, 0xFF, 0xFF, 0xFF),        VTX(0, -17, 0, 0, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -17, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, 3, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, -17, 0, 0, 2560, 0xFF, 0xFF, 0xFF, 0xFF),      VTX(0, -37, 0, 0, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -37, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -17, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, -37, 0, 0, 3200, 0xFF, 0xFF, 0xFF, 0xFF),      VTX(0, -57, 0, 0, 3840, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -57, 0, 3200, 3840, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -37, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, -57, 0, 0, 3840, 0xFF, 0xFF, 0xFF, 0xFF),      VTX(0, -63, 0, 0, 4032, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -63, 0, 3200, 4032, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -57, 0, 3200, 3840, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 63, 0, 3200, 0, 0xFF, 0xFF, 0xFF, 0xFF),     VTX(100, 43, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, 43, 0, 5248, 640, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(164, 63, 0, 5248, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 43, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(100, 23, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, 23, 0, 5248, 1280, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(164, 43, 0, 5248, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 23, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(100, 3, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, 3, 0, 5248, 1920, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(164, 23, 0, 5248, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 3, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(100, -17, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, -17, 0, 5248, 2560, 0xFF, 0xFF, 0xFF, 0xFF), VTX(164, 3, 0, 5248, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -17, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -37, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, -37, 0, 5248, 3200, 0xFF, 0xFF, 0xFF, 0xFF), VTX(164, -17, 0, 5248, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -37, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -57, 0, 3200, 3840, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, -57, 0, 5248, 3840, 0xFF, 0xFF, 0xFF, 0xFF), VTX(164, -37, 0, 5248, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -57, 0, 3200, 3840, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -63, 0, 3200, 4032, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(164, -63, 0, 5248, 4032, 0xFF, 0xFF, 0xFF, 0xFF), VTX(164, -57, 0, 5248, 3840, 0xFF, 0xFF, 0xFF, 0xFF),
};
#define sMenuBarVtx1 (sMenuBarVtx0 + 4)
#define sMenuBarVtx2 (sMenuBarVtx0 + 8)
#define sMenuBarVtx3 (sMenuBarVtx0 + 12)
#define sMenuBarVtx4 (sMenuBarVtx0 + 16)
#define sMenuBarVtx5 (sMenuBarVtx0 + 20)
#define sMenuBarVtx6 (sMenuBarVtx0 + 24)
#define sMenuBarRightVtx0 (sMenuBarVtx0 + 28)
#define sMenuBarRightVtx1 (sMenuBarVtx0 + 32)
#define sMenuBarRightVtx2 (sMenuBarVtx0 + 36)
#define sMenuBarRightVtx3 (sMenuBarVtx0 + 40)
#define sMenuBarRightVtx4 (sMenuBarVtx0 + 44)
#define sMenuBarRightVtx5 (sMenuBarVtx0 + 48)
#define sMenuBarRightVtx6 (sMenuBarVtx0 + 52)
static Gfx sMenuBarBackgroundDL[] = {
    gsSPSetGeometryMode(G_CULL_BACK),
    gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_PASS2),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 0, 99, 19, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx0, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 20, 99, 39, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx1, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 40, 99, 59, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx2, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 60, 99, 79, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx3, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 80, 99, 99, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx4, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 100, 99, 119, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx5, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 0, 120, 99, 125, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarVtx6, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 0, 163, 19, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx0, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 20, 163, 39, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx1, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 40, 163, 59, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx2, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 60, 163, 79, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx3, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 80, 163, 99, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx4, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 100, 163, 119, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx5, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 164, 0, 100, 120, 163, 125, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuBarRightVtx6, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSPEndDisplayList(),
};
static uintptr_t sMenuBarGeoLayout[] = {
    // PORT64: holds addresses
    0x17000000, 0x00000000, 0x00000000, 0x00000000, sMenuBarVtx0,
    0x05000000, 0x22040000, 0x00000000, 0x08000000, StadiumSelect_IconGeoPostCallback,
    0x00000000, 0x06000000, 0x01000000, 0x00000000,
};
// PORT: one array (the N64 layout): the game indexes across these symbols
static Vtx sMenuOptionVtx0[] = {
    VTX(0, 54, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF),          VTX(0, 34, 0, 0, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 34, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(100, 54, 0, 3200, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, 34, 0, 0, 640, 0xFF, 0xFF, 0xFF, 0xFF),        VTX(0, 14, 0, 0, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 14, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(100, 34, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, 14, 0, 0, 1280, 0xFF, 0xFF, 0xFF, 0xFF),       VTX(0, -6, 0, 0, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -6, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(100, 14, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, -6, 0, 0, 1920, 0xFF, 0xFF, 0xFF, 0xFF),       VTX(0, -26, 0, 0, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -26, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -6, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, -26, 0, 0, 2560, 0xFF, 0xFF, 0xFF, 0xFF),      VTX(0, -46, 0, 0, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -46, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -26, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(0, -46, 0, 0, 3200, 0xFF, 0xFF, 0xFF, 0xFF),      VTX(0, -54, 0, 0, 3456, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -54, 0, 3200, 3456, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -46, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 54, 0, 3200, 0, 0xFF, 0xFF, 0xFF, 0xFF),     VTX(100, 34, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(140, 34, 0, 4480, 640, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(140, 54, 0, 4480, 0, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 34, 0, 3200, 640, 0xFF, 0xFF, 0xFF, 0xFF),   VTX(100, 14, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(140, 14, 0, 4480, 1280, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(140, 34, 0, 4480, 640, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, 14, 0, 3200, 1280, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(100, -6, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(140, -6, 0, 4480, 1920, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(140, 14, 0, 4480, 1280, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -6, 0, 3200, 1920, 0xFF, 0xFF, 0xFF, 0xFF),  VTX(100, -26, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(140, -26, 0, 4480, 2560, 0xFF, 0xFF, 0xFF, 0xFF), VTX(140, -6, 0, 4480, 1920, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -26, 0, 3200, 2560, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -46, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(140, -46, 0, 4480, 3200, 0xFF, 0xFF, 0xFF, 0xFF), VTX(140, -26, 0, 4480, 2560, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(100, -46, 0, 3200, 3200, 0xFF, 0xFF, 0xFF, 0xFF), VTX(100, -54, 0, 3200, 3456, 0xFF, 0xFF, 0xFF, 0xFF),
    VTX(140, -54, 0, 4480, 3456, 0xFF, 0xFF, 0xFF, 0xFF), VTX(140, -46, 0, 4480, 3200, 0xFF, 0xFF, 0xFF, 0xFF),
};
#define sMenuOptionVtx1 (sMenuOptionVtx0 + 4)
#define sMenuOptionVtx2 (sMenuOptionVtx0 + 8)
#define sMenuOptionVtx3 (sMenuOptionVtx0 + 12)
#define sMenuOptionVtx4 (sMenuOptionVtx0 + 16)
#define sMenuOptionVtx5 (sMenuOptionVtx0 + 20)
#define sMenuOptionRightVtx0 (sMenuOptionVtx0 + 24)
#define sMenuOptionRightVtx1 (sMenuOptionVtx0 + 28)
#define sMenuOptionRightVtx2 (sMenuOptionVtx0 + 32)
#define sMenuOptionRightVtx3 (sMenuOptionVtx0 + 36)
#define sMenuOptionRightVtx4 (sMenuOptionVtx0 + 40)
#define sMenuOptionRightVtx5 (sMenuOptionVtx0 + 44)
static Gfx sMenuOptionBackgroundDL[] = {
    gsSPSetGeometryMode(G_CULL_BACK),
    gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_PASS2),
    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 0, 0, 99, 19, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionVtx0, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 0, 20, 99, 39, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionVtx1, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 0, 40, 99, 59, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionVtx2, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 0, 60, 99, 79, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionVtx3, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 0, 80, 99, 99, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionVtx4, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 0, 100, 99, 107, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionVtx5, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 100, 0, 139, 19, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD),
    gsSPVertex(sMenuOptionRightVtx0, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 100, 20, 139, 39, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuOptionRightVtx1, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 100, 40, 139, 59, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuOptionRightVtx2, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 100, 60, 139, 79, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuOptionRightVtx3, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 100, 80, 139, 99, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuOptionRightVtx4, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsDPLoadTextureTile(0x0F000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 140, 0, 100, 100, 139, 107, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD),
    gsSPVertex(sMenuOptionRightVtx5, 4, 0),
    gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
    gsSPEndDisplayList(),
};
static uintptr_t sMenuOptionGeoLayout[] = {
    // PORT64: holds addresses
    0x17000000, 0x00000000, 0x00000000, 0x00000000, sMenuOptionVtx0,
    0x05000000, 0x22040000, 0x00000000, 0x08000000, StadiumSelect_IconGeoPostCallback,
    0x00000000, 0x06000000, 0x01000000,
};

void StadiumSelect_AnimateRectangle(s16 arg0, s16 arg1, s16 arg2) {
    s16 sp4;
    s16 sp8;
    s16 spC;
    s16 sp10;
    s16 sp14;

    if (arg2 < 2) {
        return;
    }

    sp4 = 0x8000 / arg2;
    sp8 = (arg0 << 2) - (arg2 << 1);
    spC = (arg1 << 2) - (arg2 << 1);
    sp10 = sp8 + (arg2 << 2);
    sp14 = spC + (arg2 << 2);

    gSPDisplayList(gDisplayListHead++, D_8006F558);
    gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 0, 255);
    gDPLoadTextureBlock(gDisplayListHead++, PORT_SEGSYM(D_302E440), G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(gDisplayListHead++, sp8, spC, sp10, sp14, G_TX_RENDERTILE, 0, 0, sp4, sp4);
    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
}

void StadiumSelect_UpdateSparkles(s16 arg0) {
    s32 i;
    MenuRectAnim* var_s0 = &sMenuRectAnimations[0];

    if (arg0 != 0xA) {
        return;
    }

    // PORT: the seven-cup layout's own sparkles
    if (sNcLayout) {
        var_s0 = sNcSparkles;
        for (i = 0; i < sNcSparkleCount; i++, var_s0++) {
            switch (var_s0->unk_00) {
                case 1:
                    var_s0->unk_06 = (s32)(COSS(var_s0->unk_08) * 12.0f) + 12;
                    StadiumSelect_AnimateRectangle(var_s0->unk_02, var_s0->unk_04, var_s0->unk_06);
                    var_s0->unk_08 += 0x1000;
                    if ((u16)var_s0->unk_08 == 0x8000) {
                        var_s0->unk_00 = 2;
                        var_s0->unk_0A = (MathUtil_Random16() & 7) + 0x14;
                    }
                    break;

                case 2:
                    var_s0->unk_0A--;
                    if (var_s0->unk_0A == 0) {
                        var_s0->unk_00 = 1;
                    }
                    break;
            }
        }
        return;
    }

    for (i = 0; i < 12; i++, var_s0++) {
        switch (i) {
#if !defined(VERSION_JP)
            case 4:
            case 5:
                break;
#endif

            default:
                switch (var_s0->unk_00) {
                    case 1:
                        var_s0->unk_06 = (s32)(COSS(var_s0->unk_08) * 16.0f) + 0x10;
                        StadiumSelect_AnimateRectangle(var_s0->unk_02, var_s0->unk_04, var_s0->unk_06);
                        var_s0->unk_08 += 0x1000;
                        if ((u16)var_s0->unk_08 == 0x8000) {
                            var_s0->unk_00 = 2;
                            var_s0->unk_0A = (MathUtil_Random16() & 7) + 0x14;
                        }
                        break;

                    case 2:
                        var_s0->unk_0A--;
                        if (var_s0->unk_0A == 0) {
                            var_s0->unk_00 = 1;
                        }
                        break;
                }
        }
    }
}

void StadiumSelect_InitSparkleFlags(void) {
    s32 i;

    for (i = 0; i < D_84103CF0.nintendoCupClears; i++) {
        sMenuRectAnimations[i].unk_00 = 1;
    }

    for (i = 0; i < D_84103CF0.primeCupClears; i++) {
        sMenuRectAnimations[i + 6].unk_00 = 1;
    }

#if defined(VERSION_JP)
    if (D_84103CF0.clearedModes & (1 << RULE_CATEGORY_NINTENDO_CUP_97)) {
        sMenuRectAnimations[4].unk_00 = 1;
    }

    if (D_84103CF0.clearedModes & (1 << RULE_CATEGORY_NINTENDO_CUP_98)) {
        sMenuRectAnimations[5].unk_00 = 1;
    }
#endif

    if (D_84103CF0.clearedModes & 0x10) {
        sMenuRectAnimations[10].unk_00 = 1;
    }

    if (D_84103CF0.clearedModes & 0x20) {
        sMenuRectAnimations[11].unk_00 = 1;
    }
}

void StadiumSelect_DrawIconFrame(DepthBuffer* arg0, s16 arg1, s16 arg2) {
    GfxImage_SetRenderTarget(&gDisplayListHead, arg0);
    GfxImage_FillCurrent(&gDisplayListHead, 0);

    gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);
    gDPSetBlendColor(gDisplayListHead++, 255, 255, 255, 128);
    gDPSetAlphaCompare(gDisplayListHead++, G_AC_THRESHOLD);

    Gfx_DrawTextureIa8(-3, -3, 8, 8, D_2000340, 8, 0);
    Gfx_DrawTexturedRectClipped(4, -3, arg1 - 8, 8, 0xE0, 0, 0, 0x400, 0);
    Gfx_DrawTexturedRectClipped(-3, 4, 8, arg2 - 8, 0, 0xE0, 0x400, 0, 0);
    Gfx_DrawTextureIa8(arg1 - 5, -3, 8, 8, D_2000380, 8, 0);
    Gfx_DrawTexturedRectClipped(arg1 - 5, 4, 8, arg2 - 8, 0, 0xE0, 0x400, 0, 0);
    Gfx_DrawTextureIa8(-3, arg2 - 5, 8, 8, D_20002C0, 8, 0);
    Gfx_DrawTexturedRectClipped(4, arg2 - 5, arg1 - 8, 8, 0xE0, 0, 0, 0x400, 0);
    Gfx_DrawTextureIa8(arg1 - 5, arg2 - 5, 8, 8, D_2000300, 8, 0);

    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
}

void StadiumSelect_DrawBlurbBackdrop(s16 arg0, s16 arg1, s16 arg2, s16 arg3, u8 arg4, u8 arg5, u8 arg6, u8 arg7) {
    s16 temp_a2;
    s16 temp_s0_32;
    s16 var_s2;
    s16 var_s3;

    if (arg2 >= 0x10) {
        var_s3 = 8;
    } else {
        var_s3 = arg2 / 2;
    }
    if (arg3 >= 0x10) {
        var_s2 = 8;
    } else {
        var_s2 = arg3 / 2;
    }
    temp_a2 = arg2 - (var_s3 * 2);
    temp_s0_32 = arg3 - (var_s2 * 2);

    gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);
    gDPSetEnvColor(gDisplayListHead++, arg4, arg5, arg6, arg7);

    gDPLoadTextureBlock_4b(gDisplayListHead++, gUiPanelTopLeftTexture, G_IM_FMT_I, 16, 8, 0, G_TX_NOMIRROR | G_TX_WRAP,
                           G_TX_NOMIRROR | G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);

    Gfx_DrawTexturedRectClipped(arg0, arg1, var_s3, var_s2, 0, 0, 0x2000 / var_s3, 0x2000 / var_s2, 0);

    gDPLoadTextureBlock_4b(gDisplayListHead++, gUiPanelTopRightTexture, G_IM_FMT_I, 16, 8, 0, G_TX_NOMIRROR | G_TX_WRAP,
                           G_TX_NOMIRROR | G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);

    Gfx_DrawTexturedRectClipped((arg0 + arg2) - var_s3, arg1, var_s3, var_s2, 0, 0, 0x2000 / var_s3, 0x2000 / var_s2,
                                0);

    gDPLoadTextureBlock_4b(gDisplayListHead++, gUiPanelBottomLeftTexture, G_IM_FMT_I, 16, 8, 0,
                           G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);

    Gfx_DrawTexturedRectClipped(arg0, (arg1 + arg3) - var_s2, var_s3, var_s2, 0, 0, 0x2000 / var_s3, 0x2000 / var_s2,
                                0);

    gDPLoadTextureBlock_4b(gDisplayListHead++, gUiPanelBottomRightTexture, G_IM_FMT_I, 16, 8, 0,
                           G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 5, 5, G_TX_NOLOD, G_TX_NOLOD);

    Gfx_DrawTexturedRectClipped((arg0 + arg2) - var_s3, (arg1 + arg3) - var_s2, var_s3, var_s2, 0, 0, 0x2000 / var_s3,
                                0x2000 / var_s2, 0);

    gSPDisplayList(gDisplayListHead++, gUiTranslucentFillDL);

    if ((temp_a2 > 0) && (temp_s0_32 > 0)) {
        Gfx_FillRectRgba(arg0 + var_s3, arg1, temp_a2, var_s2, arg4, arg5, arg6, arg7);
        Gfx_FillRectRgba(arg0, arg1 + var_s3, arg2, temp_s0_32, arg4, arg5, arg6, arg7);
        Gfx_FillRectRgba(arg0 + var_s3, arg1 + var_s3 + temp_s0_32, temp_a2, var_s2, arg4, arg5, arg6, arg7);
    } else if (temp_a2 > 0) {
        Gfx_FillRectRgba(arg0 + var_s3, arg1, temp_a2, arg3, arg4, arg5, arg6, arg7);
    } else if (temp_s0_32 > 0) {
        Gfx_FillRectRgba(arg0, arg1 + var_s2, arg2, temp_s0_32, arg4, arg5, arg6, arg7);
    }

    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
}

void StadiumSelect_DrawSelectionCorners(s16 arg0, s16 arg1, s16 arg2, s16 arg3, u8 arg4, u8 arg5, u8 arg6, u8 arg7) {
    static s16 D_8410342C = 0;

    s16 sp56 = SINS(D_8410342C) * 2;
    UNUSED s32 pad[2];

    gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);
    gDPSetEnvColor(gDisplayListHead++, arg4, arg5, arg6, arg7);

    Gfx_DrawTextureIa8((arg0 + sp56) - 8, (arg1 + sp56) - 8, 0x10, 0x10, gBottomLeftSelectionCorner, 0x10, 0);
    Gfx_DrawTextureIa8((arg0 + sp56) - 8, ((arg1 + arg3) - sp56) - 8, 0x10, 0x10, gTopLeftSelectionCorner, 0x10, 0);
    Gfx_DrawTextureIa8(((arg0 + arg2) - sp56) - 8, (arg1 + sp56) - 8, 0x10, 0x10, gBottomRightSelectionCorner, 0x10, 0);
    Gfx_DrawTextureIa8(((arg0 + arg2) - sp56) - 8, ((arg1 + arg3) - sp56) - 8, 0x10, 0x10, gTopRightSelectionCorner,
                       0x10, 0);

    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);

    D_8410342C += 0x2000;
}

s32 StadiumSelect_IconGeoPostCallback(s32 arg0, unk_func_80011B94* arg1) {
    if (arg0 == 5) {
        s32 idx = D_8006F09C->node.callbackArg;

        gDPPipeSync(gDisplayListHead++);

        gSPSegment(gDisplayListHead++, 0x0F, (uintptr_t)D_84103CD0[idx]->img_p);

        if ((idx == 0) || (idx == 3) || (sNcLayout && (idx == 6))) {
            gSPDisplayList(gDisplayListHead++, sMenuBarBackgroundDL);
        } else {
            gSPDisplayList(gDisplayListHead++, sMenuOptionBackgroundDL);
        }
        GeoRender_ApplyMaterialState();
    }
    return 0;
}

#if !defined(VERSION_JP)
// PORT: the panel under the seven-cup layout: the western cups' text from the ROM, the Nintendo Cups' from
// translation/jp.csv (Port_NcText)
static void StadiumSelect_DrawNcCupInfo(void) {
    // the western table's entry (name; description + 6; sMenuOptionRanges) for Poké, Petit, Prime and Pika Cup
    static const s8 sWesternEntry[NC_CUP_COUNT] = { 0, -1, 4, 3, 5, -1, -1 };
    static const u8 sNcRanges[NC_CUP_COUNT][2] = { { 50, 55 }, { 50, 55 }, { 0 }, { 0 }, { 0 }, { 1, 30 }, { 1, 50 } };
    static const s8 sNcName[NC_CUP_COUNT] = { -1, PORT_NC_TEXT_NAME_97, -1, -1, -1, PORT_NC_TEXT_NAME_98,
                                              PORT_NC_TEXT_NAME_99 };
    s32 entry = sWesternEntry[D_84103CC8];
    const char* name;
    const char* description;
    s32 low;
    s32 high;

    if (entry >= 0) {
        name = Text_GetString(NULL, 0, sStadiumCupText, entry);
        description = Text_GetString(NULL, 0, sStadiumCupText, entry + 6);
        low = sMenuOptionRanges[entry][0];
        high = sMenuOptionRanges[entry][1];
    } else {
        name = Port_NcText(sNcName[D_84103CC8]);
        description = Port_NcText(sNcName[D_84103CC8] + (PORT_NC_TEXT_DESC_97 - PORT_NC_TEXT_NAME_97));
        low = sNcRanges[D_84103CC8][0];
        high = sNcRanges[D_84103CC8][1];
    }

    Font_BeginTranslucentTextRendering();
    Font_SetActive(0x10, 0);
    Gfx_SetEnvColor(0xFF, 0xFF, 0x64, 0xFF);
    Font_Printf(0x48, 0x160, name);
    Font_Printf(0x18C, 0x160, Text_GetString(NULL, 0, sStadiumCupText, 0xC));
    Font_Printf(Font_MeasureTextExtent(0x10, 0, Text_GetString(NULL, 0, sStadiumCupText, 0xC)) + 0x190, 0x160,
                "%d~%d", low, high);
    Font_SetActive(8, 0);
    Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
    Font_SetLineHeight(0x18);
    Font_Printf(0x48, 0x182, description);
    Font_EndTexturedTextRendering();

    if (gSessionContext.inRound2 == 1) {
        s32 width = Font_MeasureTextExtent(0x10, 0, name);

        gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);
        Gfx_DrawTextureRgba16(width + 0x4C, 0x160, 0x1C, 0x1C, PORT_SEGSYM(&D_302E840), 0x1C, 0);
        gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
    }
}
#endif

void StadiumSelect_DrawCupInfoPanel(s16 arg0) {
    s32 sp28;
    s16 temp_a3;
    s32 temp_v0;

    if (arg0 < 0xB) {
        sp28 = arg0 * 9;
        temp_a3 = (sp28 + 2);
    } else {
        sp28 = arg0 * 9;
        temp_a3 = (0xB6 - sp28);
    }

    if (temp_a3 >= 0xA) {
        StadiumSelect_DrawBlurbBackdrop(0x38, ((0x5C - temp_a3) / 2) + 0x15C, 0x210, temp_a3, 0x1E, 0x1E, 0x82, 0x96);
    }

#if defined(VERSION_JP)
    if ((sp28 == 0x5A) && (D_84103CC8 < 6)) {
        Font_BeginTranslucentTextRendering();
        Font_SetActive(0x10, -4);
        Gfx_SetEnvColor(0xFF, 0xFF, 0x64, 0xFF);
        Font_Printf(0x48, 0x160, sStadiumCupNames[D_84103CC8]);
        // "Level %d-%d"
        Font_Printf(0x18C, 0x160, "レベル%d〜%d", sMenuOptionRanges[D_84103CC8][0], sMenuOptionRanges[D_84103CC8][1]);
        Font_SetActive(8, -2);
        Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
        Font_SetLineHeight(0x18);
        Font_Printf(0x48, 0x182, sStadiumCupDescriptions[D_84103CC8]);
        Font_EndTexturedTextRendering();

        if (gSessionContext.inRound2 == 1) {
            // Each full-width character is 20 pixels wide.
            temp_v0 = (strlen(sStadiumCupNames[D_84103CC8]) / 2) * 20;

            gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);

            Gfx_DrawTextureRgba16(temp_v0 + 0x4C, 0x160, 0x1C, 0x1C, &D_302E840, 0x1C, 0);

            gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
        }
    }
}

#else
    // PORT: the seven-cup layout's names, descriptions and level ranges
    if (sNcLayout && (sp28 == 0x5A) && (D_84103CC8 < NC_CUP_COUNT)) {
        StadiumSelect_DrawNcCupInfo();
        return;
    }
    if ((sp28 == 0x5A) && (D_84103CC8 < 6)) {
        Font_BeginTranslucentTextRendering();
        Font_SetActive(0x10, 0);
        Gfx_SetEnvColor(0xFF, 0xFF, 0x64, 0xFF);
        Font_Printf(0x48, 0x160, Text_GetString(NULL, 0, sStadiumCupText, D_84103CC8));
        Font_Printf(0x18C, 0x160, Text_GetString(NULL, 0, sStadiumCupText, 0xC));
        Font_Printf(Font_MeasureTextExtent(0x10, 0, Text_GetString(NULL, 0, sStadiumCupText, 0xC)) + 0x190, 0x160,
                    "%d~%d", sMenuOptionRanges[D_84103CC8][0], sMenuOptionRanges[D_84103CC8][1]);
        Font_SetActive(8, 0);
        Gfx_SetEnvColor(0xFF, 0xFF, 0xFF, 0xFF);
        Font_SetLineHeight(0x18);
        Font_Printf(0x48, 0x182, Text_GetString(NULL, 0, sStadiumCupText, D_84103CC8 + 6));
        Font_EndTexturedTextRendering();

        if (gSessionContext.inRound2 == 1) {
            temp_v0 = Font_MeasureTextExtent(0x10, 0, Text_GetString(NULL, 0, sStadiumCupText, D_84103CC8));

            gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);

            Gfx_DrawTextureRgba16(temp_v0 + 0x4C, 0x160, 0x1C, 0x1C, PORT_SEGSYM(&D_302E840), 0x1C, 0);

            gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
        }
    }
}
#endif

void StadiumSelect_RenderFrame(s32 arg0, s32 arg1, f32 arg2) {
    s16 i;
    f32 temp_fv0;
    s16 temp_t1;
    s16 temp_v1;
    s16 var_v0;

    // PORT: the seven-cup layout flips its rows alternately (top and bottom one way, middle the other)
    if (sNcLayout) {
        var_v0 = (arg0 < 0xB) ? ((10 - arg0) * -0x5000) / 10 : ((arg0 - 10) * -0x5000) / 10;
        for (i = 0; i < NC_CUP_COUNT; i++) {
            if ((arg0 >= 0xB) && (i == D_84103CC8)) {
                continue;
            }
            D_84103458[i].rotation.x = ((i >= 2) && (i <= 4)) ? -var_v0 : var_v0;
        }
    } else if (arg0 < 0xB) {
        var_v0 = ((10 - arg0) * -0x5000) / 10;
        for (i = 0; i < 3; i++) {
            D_84103458[i].rotation.x = var_v0;
        }

        for (i = 3; i < 6; i++) {
            D_84103458[i].rotation.x = -var_v0;
        }
    } else {
        var_v0 = ((arg0 - 10) * -0x5000) / 10;
        for (i = 0; i < 3; i++) {
            if (i != D_84103CC8) {
                D_84103458[i].rotation.x = var_v0;
            }
        }

        for (i = 3; i < 6; i++) {
            if (i != D_84103CC8) {
                D_84103458[i].rotation.x = -var_v0;
            }
        }
    }

    BgStage_DrawFrame();
    Gfx_DrawTiledRgba16Image(D_84103448);
    Geo_RenderRootNode(D_84103454);

    if (arg2 != 0.0f) {
        MenuItemInfo* ptr = sNcLayout ? &sNcItemInfo[D_84103CC8] : &sMenuItemInfo[D_84103CC8];

        temp_v1 = (ptr->unk_04 / 2) + ptr->unk_00;
        temp_t1 = (ptr->unk_06 / 2) + ptr->unk_02;
        temp_fv0 = ptr->unk_04 / 640.0f;
        Gfx_DrawTiledImageMesh(temp_v1 + ((0x140 - temp_v1) * arg2), temp_t1 + ((0xF0 - temp_t1) * arg2), 0,
                               ((1.0f - temp_fv0) * arg2) + temp_fv0, D_84103450, D_8410344C);
    }

    if (arg1 != 0) {
        MenuItemInfo* ptr = sNcLayout ? &sNcItemInfo[D_84103CC8] : &sMenuItemInfo[D_84103CC8];

        StadiumSelect_DrawSelectionCorners(ptr->unk_00, ptr->unk_02, ptr->unk_04, ptr->unk_06, 0xFF, 0xF0, 0x64, 0xFF);
    }

    StadiumSelect_UpdateSparkles(arg0);
    StadiumSelect_DrawCupInfoPanel(arg0);
    BgStage_AdvanceFrame();
}

// PORT: the division icon with its medal count given (the Nintendo Cups' layout passes '99's own)
static DepthBuffer* StadiumSelect_BuildDivisionIcon(u8* arg0, s32 var_s3);

DepthBuffer* StadiumSelect_BuildDivisionCupIcon(u8* arg0, s16 arg1) {
    return StadiumSelect_BuildDivisionIcon(arg0, (arg1 == 3) ? D_84103CF0.nintendoCupClears : D_84103CF0.primeCupClears);
}

static DepthBuffer* StadiumSelect_BuildDivisionIcon(u8* arg0, s32 var_s3) {
    s16 i;
    DepthBuffer* sp40 = GfxImage_Allocate(0, 2, 0xA4, 0x7E, 1);

    StadiumSelect_DrawIconFrame(sp40, 0xA0, 0x7A);

    gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);

    for (i = 0; i < 9; i++) {
        Gfx_DrawTextureRgba16(4, (i * 0xC) + 4, 0x98, 0xC, (i * 0xE40) + arg0, 0x98, 0);
    }

    Gfx_DrawTextureRgba16(4, (i * 0xC) + 4, 0x98, 6, (i * 0xE40) + arg0, 0x98, 0);

    for (i = 0; i < var_s3; i++) {
        Gfx_DrawTextureRgba16((i * 0x26) + 5, 0x46, 0x24, 0x30, MENU_OPTION_TEXTURES[i], 0x24, 0);
    }

    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);

    return sp40;
}

// PORT: a one-cup icon with its medal given (-1: none); flag draws a small Japanese flag in its top right corner
// (RESTORED_CONTENT.md: Nintendo Cup '97's picture is the Poké Cup's)
static DepthBuffer* StadiumSelect_BuildSimpleIcon(u8* arg0, s32 medal, s32 flag) {
    s16 i;
    DepthBuffer* sp30 = GfxImage_Allocate(0, 2, 0x8C, 0x6C, 1);

    StadiumSelect_DrawIconFrame(sp30, 0x88, 0x68);

    gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);

    for (i = 0; i < 6; i++) {
        Gfx_DrawTextureRgba16(4, (i * 0x10) + 4, 0x80, 0x10, (i << 0xC) + arg0, 0x80, 0);
    }

    if (medal >= 0) {
        Gfx_DrawTextureRgba16(0x54, 0x3A, 0x30, 0x2A, MENU_OPTION_TEXTURES[medal], 0x30, 0);
    }

    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);

    if (flag) {
        // 24x16 with a dark edge: white, a red disc of radius 5 in the middle
        s32 x = 0x88 - 4 - 24 - 3;
        s32 y = 4 + 3;
        s32 dy;

        gSPDisplayList(gDisplayListHead++, gUiTranslucentFillDL);
        Gfx_FillRectRgba(x - 1, y - 1, 26, 18, 0x40, 0x40, 0x40, 0xFF);
        Gfx_FillRectRgba(x, y, 24, 16, 0xFF, 0xFF, 0xFF, 0xFF);
        for (dy = -5; dy <= 4; dy++) {
            f32 fy = dy + 0.5f;
            s32 half = (s32)(sqrtf(25.0f - fy * fy) + 0.5f);

            if (half > 0) {
                Gfx_FillRectRgba(x + 12 - half, y + 8 + dy, half * 2, 1, 0xBC, 0x00, 0x2D, 0xFF);
            }
        }
        gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);
    }

    return sp30;
}

DepthBuffer* StadiumSelect_BuildSimpleCupIcon(u8* arg0, s16 arg1) {
    s16 i;
    DepthBuffer* sp30 = GfxImage_Allocate(0, 2, 0x8C, 0x6C, 1);
    s32 var_v0;

    StadiumSelect_DrawIconFrame(sp30, 0x88, 0x68);

    gSPDisplayList(gDisplayListHead++, gUiEnvTintTextureDL);

    for (i = 0; i < 6; i++) {
        Gfx_DrawTextureRgba16(4, (i * 0x10) + 4, 0x80, 0x10, (i << 0xC) + arg0, 0x80, 0);
    }

    if (D_84103CF0.clearedModes & (1 << arg1)) {
#if defined(VERSION_JP)
        if (arg1 < 4) {
            var_v0 = 5;
        } else {
            var_v0 = 4;
        }
#else
        if (arg1 == 4) {
            var_v0 = 5;
        } else {
            var_v0 = 4;
        }
#endif
        Gfx_DrawTextureRgba16(0x54, 0x3A, 0x30, 0x2A, MENU_OPTION_TEXTURES[var_v0], 0x30, 0);
    }

    gSPDisplayList(gDisplayListHead++, gUiRestoreTextureDL);

    return sp30;
}

// PORT: the seven-cup layout: icons, their places and sizes, the cursor's moves and the medals' sparkles
static void StadiumSelect_BuildNcCupIconGrid(void) {
    s32 i;
    s32 k;
    u8 nc = MODE_SETTINGS_NC_CLEARED(&D_84103CF0);
    u8* ui = (sNcJpUi != NULL) ? sNcJpUi : (u8*)D_3000000;

    D_84103CD0[0] = StadiumSelect_BuildDivisionIcon((u8*)&D_300C000, D_84103CF0.nintendoCupClears);
    D_84103CD0[1] = StadiumSelect_BuildSimpleIcon(ui + 0x0000, (nc & (1 << 1)) ? 5 : -1, 1);
    D_84103CD0[2] = StadiumSelect_BuildSimpleIcon((u8*)&D_3014760, (D_84103CF0.clearedModes & 0x10) ? 5 : -1, 0);
    D_84103CD0[3] = StadiumSelect_BuildDivisionIcon((u8*)&D_3020760, D_84103CF0.primeCupClears);
    D_84103CD0[4] = StadiumSelect_BuildSimpleIcon((u8*)&D_301A760, (D_84103CF0.clearedModes & 0x20) ? 4 : -1, 0);
    D_84103CD0[5] = StadiumSelect_BuildSimpleIcon(ui + 0x6000, (nc & (1 << 2)) ? 5 : -1, 0);
    D_84103CD0[6] = StadiumSelect_BuildDivisionIcon(ui + 0xC000, MODE_SETTINGS_NC99_CLEARS(&D_84103CF0));

    sNcSparkleCount = 0;
    for (i = 0; i < NC_CUP_COUNT; i++) {
        NcCupSlot* slot = &sNcCupSlots[i];
        MenuItemInfo* info = &sNcItemInfo[i];
        f32 width = (slot->big ? 152.0f : 128.0f) * NC_ICON_SCALE;
        f32 height = (slot->big ? 114.0f : 96.0f) * NC_ICON_SCALE;
        s32 medals;
        s32 count;

        info->unk_00 = (s16)(slot->x - width / 2);
        info->unk_02 = (s16)(slot->y - height / 2);
        info->unk_04 = (s16)width;
        info->unk_06 = (s16)height;
        info->unk_08 = slot->up;
        info->unk_09 = slot->down;
        info->unk_0A = slot->left;
        info->unk_0B = slot->right;

        Model_InitDisplayObject(&D_84103458[i], 0, 0, slot->big ? D_84103CE8 : D_84103CEC);
        // the models' left edge is at x, their middle at y: 1 unit is a pixel at this depth
        Vec3f_SetComponentsDuplicate(&D_84103458[i].position,
                                     slot->x - 320.0f - (slot->big ? 82.0f : 70.0f) * NC_ICON_SCALE, 240.0f - slot->y,
                                     -579.0f);
        D_84103458[i].scale.x = D_84103458[i].scale.y = D_84103458[i].scale.z = NC_ICON_SCALE;

        // the medals' sparkles, at the western layout's places on the icon
        if (slot->big) {
            medals = 4;
            count = (i == 0)   ? D_84103CF0.nintendoCupClears
                    : (i == 3) ? D_84103CF0.primeCupClears
                               : MODE_SETTINGS_NC99_CLEARS(&D_84103CF0);
        } else {
            medals = 1;
            count = (i == 1)   ? ((nc >> 1) & 1)
                    : (i == 5) ? ((nc >> 2) & 1)
                    : (i == 2) ? ((D_84103CF0.clearedModes >> 4) & 1)
                               : ((D_84103CF0.clearedModes >> 5) & 1);
        }
        for (k = 0; k < medals; k++) {
            MenuRectAnim* sparkle = &sNcSparkles[sNcSparkleCount++];

            sparkle->unk_00 = (k < count) ? 1 : 0;
            sparkle->unk_02 = info->unk_00 + (s16)((slot->big ? 16 + k * 38 : 100) * NC_ICON_SCALE);
            sparkle->unk_04 = info->unk_02 + (s16)((slot->big ? 70 : 60) * NC_ICON_SCALE);
            sparkle->unk_06 = 0x20;
            sparkle->unk_08 = (s16)(k * 0x2000);
            sparkle->unk_0A = 0;
        }
    }
}

void StadiumSelect_BuildCupIconGrid(void) {
    if (sNcLayout) {
        StadiumSelect_BuildNcCupIconGrid();
        return;
    }
    StadiumSelect_InitSparkleFlags();
    D_84103CD0[0] = StadiumSelect_BuildDivisionCupIcon(&D_300C000, 3);
    D_84103CD0[1] = StadiumSelect_BuildSimpleCupIcon(D_3000000, 1);
    D_84103CD0[2] = StadiumSelect_BuildSimpleCupIcon(D_3006000, 2);
    D_84103CD0[3] = StadiumSelect_BuildDivisionCupIcon(&D_3020760, 6);
    D_84103CD0[4] = StadiumSelect_BuildSimpleCupIcon(&D_3014760, 4);
    D_84103CD0[5] = StadiumSelect_BuildSimpleCupIcon(&D_301A760, 5);
#if defined(VERSION_JP)
    Model_InitDisplayObject(D_84103458, 0, 0, D_84103CE8);
    Model_InitDisplayObject(&D_84103458[1], 0, 0, D_84103CEC);
    Model_InitDisplayObject(&D_84103458[2], 0, 0, D_84103CEC);
    Model_InitDisplayObject(&D_84103458[3], 0, 0, D_84103CE8);
    Model_InitDisplayObject(&D_84103458[4], 0, 0, D_84103CEC);
    Model_InitDisplayObject(&D_84103458[5], 0, 0, D_84103CEC);
    Vec3f_SetComponentsDuplicate(&D_84103458->position, -80.0f, 113.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[1].position, -236.0f, 104.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[2].position, 100.0f, 104.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[3].position, -80.0f, -29.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[4].position, -236.0f, -20.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[5].position, 100.0f, -20.0f, -579.0f);
#else
    Model_InitDisplayObject(D_84103458, 0, 0, D_84103CE8);
    Model_InitDisplayObject(&D_84103458[3], 0, 0, D_84103CE8);
    Model_InitDisplayObject(&D_84103458[4], 0, 0, D_84103CEC);
    Model_InitDisplayObject(&D_84103458[5], 0, 0, D_84103CEC);
    Vec3f_SetComponentsDuplicate(&D_84103458->position, -80.0f, 113.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[3].position, -80.0f, -29.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[4].position, -236.0f, 42.0f, -579.0f);
    Vec3f_SetComponentsDuplicate(&D_84103458[5].position, 100.0f, 42.0f, -579.0f);
#endif
}

s16 StadiumSelect_HandleInput(void) {
    s16 var_a1 = -1;
    s16 var_v1 = -1;

    if (BTN_IS_PRESSED(gPlayer1Controller, BTN_A)) {
        Audio_PlaySoundEffectById(0x26);
        var_v1 = -1;
        var_a1 = D_84103CC8;
    } else if (BTN_IS_PRESSED(gPlayer1Controller, BTN_B)) {
        Audio_PlaySoundEffectById(SFX_MENU_BACK);
        var_v1 = -1;
        var_a1 = sCupCount; // PORT: 7 with the Nintendo Cups (was 6)
    } else if (BTN_IS_PRESSED(gPlayer1Controller, BTN_DUP)) {
        var_v1 = (sNcLayout ? sNcItemInfo : sMenuItemInfo)[D_84103CC8].unk_08;
    } else if (BTN_IS_PRESSED(gPlayer1Controller, BTN_DDOWN)) {
        var_v1 = (sNcLayout ? sNcItemInfo : sMenuItemInfo)[D_84103CC8].unk_09;
    } else if (BTN_IS_PRESSED(gPlayer1Controller, BTN_DLEFT)) {
        var_v1 = (sNcLayout ? sNcItemInfo : sMenuItemInfo)[D_84103CC8].unk_0A;
    } else if (BTN_IS_PRESSED(gPlayer1Controller, BTN_DRIGHT)) {
        var_v1 = (sNcLayout ? sNcItemInfo : sMenuItemInfo)[D_84103CC8].unk_0B;
    }

    if (var_v1 != -1) {
        Audio_PlaySoundEffectById(SFX_MENU_SCROLL);
        D_84103CC8 = var_v1;
    }
    return var_a1;
}

void StadiumSelect_Loop(void) {
    s16 i;
    s16 temp_v0;

    Audio_PlayMusicIfChanged(0x15);
    StadiumSelect_BuildCupIconGrid();
    D_84103CC8 = 0;
    StageFade_StartFromOpaque(0xA);
    while (StageContext_GetFadeMode() != 0) {
        Controller_PollInputs();
        StadiumSelect_RenderFrame(0, 0, 0.0f);
    }

    for (i = 0; i < 10; i++) {
        Controller_PollInputs();
        StadiumSelect_RenderFrame(i, 0, 0.0f);
    }
    do {
        Controller_PollInputs();
        StadiumSelect_RenderFrame(0xA, 1, 0.0f);
        temp_v0 = StadiumSelect_HandleInput();
    } while (temp_v0 == -1);

    D_84103CC8 = temp_v0;

    if (D_84103CC8 < sCupCount) {
        D_8410344C = BinArchive_GetFile(D_84103444, sNcLayout ? sNcCupSlots[D_84103CC8].background
                                                              : sBackgroundFileIds[D_84103CC8]);

        for (i = 11; i < 21; i++) {
            Controller_PollInputs();
            StadiumSelect_RenderFrame(i, 0, 0.0f);
        }

        for (i = 0; i < sCupCount; i++) {
            ModelRenderer_ClearDisplayObject(&D_84103458[i]);
        }

        for (i = 1; i < 10; i++) {
            Controller_PollInputs();
            StadiumSelect_RenderFrame(0, 0, i / 10.0f);
        }

        StadiumSelect_RenderFrame(0, 0, 1.0f);
        StadiumSelect_RenderFrame(0, 0, 1.0f);
    } else {
        StageContext_SetClearColor(0xFFFF);
        Audio_StopMusic(0x14);
        StageFade_StartFromTransparent(0xA);
        while (StageContext_GetFadeMode() != 1) {
            Controller_PollInputs();
            StadiumSelect_RenderFrame(0xA, 0, 0.0f);
        }
        StageLoader_RunFrames(2);
    }
}

void StadiumSelect_InitGeoLayouts(void) {
    s32 i;
    DisplayObject* ptr;
    DisplayObject* ptr2;
    MainPoolState* temp_v0 = MainPool_AllocState(main_pool_get_available(), 0);

    D_84103454 = process_geo_layout(temp_v0, sMenuGeoLayoutRoot);
    D_84103CE8 = process_geo_layout(temp_v0, sMenuBarGeoLayout);
    D_84103CEC = process_geo_layout(temp_v0, sMenuOptionGeoLayout);

    MainPool_FinalizeAllocation(temp_v0);
    ModelRenderer_InitDisplayRoots();

    ptr = &D_84103458[0];
    for (i = 0; i < sCupCount; ptr++, i++) {
        ModelRenderer_AttachDisplayObject(ptr);
        D_84103458[i].node.callbackArg = i;
    }
}

s32 StadiumSelect_ConfirmSelection(void) {
    s32 var_v1 = 0;

    if (sNcLayout && (D_84103CC8 < NC_CUP_COUNT)) {
        // PORT: the seven-cup layout's choice; mode 3 is Nintendo Cup '99 or the Poké Cup
        Session_SetMode(sNcCupSlots[D_84103CC8].mode, sNcCupSlots[D_84103CC8].mode, 0);
        Port_NcSet99(sNcCupSlots[D_84103CC8].is99);
        Team_AddTrainer(0, Trainer_Create(0, 0, "1P", Text_GetPlayerLabel(1)));
        Team_AddTrainer(1, Trainer_Create(-1, 0, "COM", Text_GetPlayerLabel(3)));
        var_v1 = 1;
    } else if (D_84103CC8 < 6) {
        s16 tmp = gStadiumCupSessionModes[D_84103CC8];

        Session_SetMode(tmp, gStadiumCupModeCategories[D_84103CC8], 0);
#if defined(VERSION_JP)
        // "Trainer"
        Team_AddTrainer(0, Trainer_Create(0, 0, "1P", "トレーナー"));
        // "Computer"
        Team_AddTrainer(1, Trainer_Create(-1, 0, "COM", "コンピュータ"));
#else
        Team_AddTrainer(0, Trainer_Create(0, 0, "1P", Text_GetPlayerLabel(1)));
        Team_AddTrainer(1, Trainer_Create(-1, 0, "COM", Text_GetPlayerLabel(3)));
#endif
        var_v1 = 1;
    }
    return var_v1;
}

s32 StadiumSelect_Main(s32 arg0, s32 arg1) {
    BinArchive* temp_v0;

    main_pool_push_state('PSSL');

    Save_EnsureBankLoaded(2);
    Save_GetModeSettings(&D_84103CF0, gSessionContext.inRound2);
    Font_Init(0x18, 0);
    ASSET_LOAD(D_1000000, common_menu1_ui, 0);
    ASSET_LOAD(D_2000000, common_menu2_ui, 0);
    ASSET_LOAD(D_3000000, stadium_select_ui, 0);
    // PORT: the Nintendo Cups (restored content): the seven-cup layout, with the Japanese ROM's pictures
    sNcLayout = Port_NcAvailable();
    sCupCount = sNcLayout ? NC_CUP_COUNT : 6;
    sNcJpUi = NULL;
    if (sNcLayout) {
        sNcJpUi = Asset_LoadCompressed((u8*)(uintptr_t)Port_NcJapanRom(NC_JP_STADIUM_SELECT_UI_START),
                                       (u8*)(uintptr_t)Port_NcJapanRom(NC_JP_STADIUM_SELECT_UI_END), 0, 0);
    }
#if !defined(VERSION_JP)
    Text_InitStringTables();
    sStadiumCupText = Text_GetStringTable(0x13);
#endif
    D_84103450 = func_8001E490();

    D_84103444 = ASSET_LOAD2(backgrounds, 1, 1);

    D_84103448 = BinArchive_GetFile(D_84103444, 3);
    D_84103CC8 = 0;
    StadiumSelect_InitGeoLayouts();
    StageLoader_UpdateSegments();
    StadiumSelect_Loop();
    StageLoader_WaitForRetrace();

    main_pool_pop_state('PSSL');

    return StadiumSelect_ConfirmSelection();
}
