#ifndef STADION_PORT_H
#define STADION_PORT_H

// The boundary between the vendored game (game/) and the port (src/port/, the libultraship fork).
//
// Every function that crosses that boundary is declared here, once, in both directions:
//   - the port's services the game calls (most of this file), and
//   - the hooks the game implements for the port (the last section).
// Game files that use any of it include this header ("StadionPort.h"); the port files that implement it include it
// too, so the compiler checks each definition against the declaration the other side sees.
//
// Rules for this header:
//   - Declare a boundary function here instead of with a local `extern` in a .c/.cpp file.
//   - Use only <stdint.h>/<stddef.h> types: the game sees its SDK shim (src/port/sdk) and the port sees
//     libultraship's headers, and both must be able to include this file.
//   - Port-internal functions (called only between files in src/port/) belong in the port's own headers.
//   - The libultra replacements the game reaches through SDK names (port_osGetTime and the like) stay in
//     src/port/sdk/port_compat.h, next to the #defines that route the SDK names to them.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "rom_layouts.h" // PortRomVersion (plain C, <stdint.h> only)

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Timing and pacing (src/port/os_devices.cpp)
// ---------------------------------------------------------------------------------------------------------------------

// Retraces (N64 vertical blanks) since boot, on the port's clock: virtual in lockstep mode.
uint64_t Port_ViTick(void);
// Display lists the game has submitted (graphics tasks), drawn or not: the window title's frame rate leaves out the
// stretches in which the game submits none (Engine.cpp UpdateFpsTitle).
uint64_t Port_GameFramesSubmitted(void);
// Whether the N64 logo's hold is paced by retraces (PORT_PACED_LOGO_HOLD, or a replayed recording; n64_logo.c).
int Port_PacedLogoHold(void);
// The N64's loading time where the game's timing counts on it (the boot diorama's model loads, which the music is
// timed around), as waits; off when replaying an input recording made before them.
int Port_LoadWaits(void);
// The display worker's frame buffer for the task it submits (dp_intro.c DisplayTask_Submit), so a scene's new frame
// buffer is known before its first swap.
void Port_NoteFrameBuffer(const void* fb);

// ---------------------------------------------------------------------------------------------------------------------
// Audio (src/port/os_devices.cpp, sound_settings.cpp, audio_hle.cpp, gb_audio_out.cpp)
// ---------------------------------------------------------------------------------------------------------------------

// The game's sound engine runs unless switched off (PORT_GAME_AUDIO=0, or replaying an input recording made before
// the engine was ported). Off, its entry points return at once as they did then.
int Port_GameAudio(void);
#define PORT_AUDIO_OFF() (!Port_GameAudio())
// The launcher's Sound volumes for the parts of the mix (src/port/sound_settings.cpp).
#define PORT_SOUND_MUSIC 0
#define PORT_SOUND_EFFECTS 1
#define PORT_SOUND_VOICES 2
// One part's gain in Q15 (32768: as the game plays it).
// Called by the game's mixer. For music and effects it scales the music player's voice volume (which the synthesizer
// squares), for voices the samples: the result is the same amplitude curve, (slider/100)^2.
int Port_SoundPartGainQ15(int part);
// Records an audio command's full host pointer in a side table keyed by the command's slot; returns the 32-bit value
// the command carries (game/src/libnaudio/n_abi.h PORT_ACMD_ADDR).
unsigned int Port_AcmdAddr(void* cmd, uintptr_t addr, int kind);
// One mixed Game Boy Tower buffer: 16-bit stereo samples in host byte order.
void Port_GbAudioSubmit(const void* samples, uint32_t bytes);
// Bytes still queued in the host's Game Boy Tower sound output.
uint32_t Port_GbAudioQueuedBytes(void);

// ---------------------------------------------------------------------------------------------------------------------
// The loaded ROM (src/port/rom_version.cpp): the game is US Rev 2 code whatever the ROM
// ---------------------------------------------------------------------------------------------------------------------

// A link address read from the loaded ROM's own code (a fragment image) as the Rev 2 link address of the same symbol.
// fragmentRom is the Rev 2 ROM offset of the fragment holding it, 0 for the main segment.
uint32_t Port_RomCodeAddrToGame(uint32_t fragmentRom, uint32_t addr);
uint32_t Port_RomToRaw(uint32_t gameOffset);              // src/port/rom_version.h
uint32_t Port_RomRangeEndToRaw(uint32_t gameStart, uint32_t gameEnd);
// The loaded ROM's release, a PortRomVersion (src/port/rom_layouts.h). The game is Rev 2 code for every ROM, except
// where a European release changed the code itself (wider windows for longer words, other credits, quieter sound
// while the battle music plays): there it takes that release's path, chosen with these at run time where the decomp
// chooses with VERSION_FR and the like when it builds.
int Port_RomVersion(void);
#define PORT_VERSION_IS(v) (Port_RomVersion() == (v))
#define PORT_VERSION_EU PORT_VERSION_IS(PORT_ROM_EU)
#define PORT_VERSION_EU_REV1 PORT_VERSION_IS(PORT_ROM_EU_REV1)
#define PORT_VERSION_FR PORT_VERSION_IS(PORT_ROM_FR)
#define PORT_VERSION_DE PORT_VERSION_IS(PORT_ROM_DE)
#define PORT_VERSION_ES PORT_VERSION_IS(PORT_ROM_ES)
#define PORT_VERSION_IT PORT_VERSION_IS(PORT_ROM_IT)
// the French, German, Spanish and Italian releases (the decomp's VERSION_FR || VERSION_DE || ...)
#define PORT_VERSION_TRANSLATED (PORT_VERSION_FR || PORT_VERSION_DE || PORT_VERSION_ES || PORT_VERSION_IT)
// every European release (VERSION_PAL)
#define PORT_VERSION_PAL (PORT_VERSION_EU || PORT_VERSION_EU_REV1 || PORT_VERSION_TRANSLATED)
// A segment address the code names (an image inside a UI archive, such as D_2001A20) as the loaded ROM keeps it: the
// translated releases rearranged some archives (tools/gen_seg_syms.py).
uintptr_t Port_SegSym(uintptr_t rev2);
#define PORT_SEGSYM(addr) ((__typeof__(&*(addr)))Port_SegSym((uintptr_t)(addr)))
// 0 when the Gallery cannot be entered: the French, German, Spanish and Italian cartridges have no Gallery graphics,
// unless an English ROM was also provided (restored content, src/port/restored_content.cpp)
int Port_GalleryAvailable(void);
// Data the translated releases keep in their own copy of a fragment (images that carry text, which the port does not
// keep): size bytes at link address vram, read once out of the loaded ROM's image of the fragment that Rev 2 keeps at
// ROM offset fragmentRom and links at fragmentVram. NULL when the ROM is too short. Port_FragmentRomWords reads count
// big-endian words as host u32s (the way the port's u32 arrays hold them); Port_FragmentRomWord reads one (0 if none).
const void* Port_FragmentRomData(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram, uint32_t size);
const uint32_t* Port_FragmentRomWords(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram, uint32_t count);
uint32_t Port_FragmentRomWord(uintptr_t fragmentRom, uint32_t fragmentVram, uint32_t vram);
// A file of a ROM archive that restored content takes from another release (src/port/restored_content.h): archiveRaw
// is the archive's start as Port_RomToRaw marks it; sets *start (marked) and *size and returns 1, else 0.
int Port_RestoredArchiveFile(uint32_t archiveRaw, int index, uint32_t* start, uint32_t* size);

// The Nintendo Cups (nintendo_cup.cpp). Port_NcAvailable: a Japanese ROM was provided beside a western one, so the
// stadium select screen offers Nintendo Cup '97, '98 and '99 beside the Poké Cup. Session mode 3 is the Poké Cup or,
// while Port_NcIs99() is set, Nintendo Cup '99 (the stadium select screen and a resumed session set it).
int Port_NcAvailable(void);
int Port_NcIs99(void);
void Port_NcSet99(int on);
// A trainer archive file (archiveRaw: the archive's marked start) that the Nintendo Cups take from the Japanese ROM,
// converted to the western layout: its bytes and size, or NULL for the loaded ROM's own.
const void* Port_NcTrainerFile(uint32_t archiveRaw, int index, uint32_t* size);
// The Nintendo Cups' text in the game's encoding and the chosen text language (translation/jp.csv)
#define PORT_NC_TEXT_NAME_97 0
#define PORT_NC_TEXT_NAME_98 1
#define PORT_NC_TEXT_NAME_99 2
#define PORT_NC_TEXT_DESC_97 3
#define PORT_NC_TEXT_DESC_98 4
#define PORT_NC_TEXT_DESC_99 5
#define PORT_NC_TEXT_NO_CHAMPIONS 6
const char* Port_NcText(int id);
// The cup names in the western message tables, for the Nintendo Cups (NULL: the table's own string)
const char* Port_NcTableText(uint32_t table, uint32_t index);
// The backgrounds archive file for a mode's pictures (`file`: the western table's), where the Nintendo Cups differ
int Port_NcBackgroundFile(int mode, int file);
// The marked offset of a Japanese ROM offset (0: no Japanese ROM)
uint32_t Port_NcJapanRom(uint32_t jpOffset);

// ---------------------------------------------------------------------------------------------------------------------
// Input and saves (src/port/os_devices.cpp)
// ---------------------------------------------------------------------------------------------------------------------

// The controllers plugged in, as osContInit's controller bits: 1 always, 2-4 as the launcher's Controls panel,
// PORT_CONTROLLERS or a replayed recording say.
int Port_ControllerBits(void);
// Bracket a multi-page flash write (save_data.c): the save file is written once, at the end, if anything changed.
void Port_BeginFlashBankWrite(void);
void Port_EndFlashBankWrite(void);

// ---------------------------------------------------------------------------------------------------------------------
// Graphics and data translation (src/port/gfx_translate.cpp, interp.cpp, gb_core.c, libultraship fork)
// ---------------------------------------------------------------------------------------------------------------------

// Whether p points into raw ROM-loaded data (N64 byte order, 32-bit addresses) rather than a host-converted copy.
int Port_IsRawData(const void* p);
// A texture/palette descriptor in model data, converted to the host layout (pointer-sized address field).
void* Port_ConvertDesc(void* link);
// Element idx of an array of texture descriptors: 12 bytes apart in ROM data, hostStride apart in host copies.
void* Port_DescAt(void* base, int idx, size_t hostStride);
// The game's segment or fragment maps changed: drop translated display lists, vertices and matrices.
void Port_GfxFlushCache(void);
// A RAM range was freed: forget render targets that lived in it (libultraship fork, interpreter.cpp).
void Port_GfxForgetRenderTargets(const void* start, size_t size);
// Frame interpolation (src/port/interp.cpp): a new frame's display-list buffer, the scene-graph node being drawn.
void Port_InterpBeginFrame(const void* dlStart);
void Port_InterpNode(const void* node);
// Composes a Game Boy Tower frame on the host (the N64 did it in an RSP task; src/port/gb_core.c).
void Port_GbPresent(void* frameBuffer, const void* statusStrip, const void* frameImage, int ox, int oy, int sy,
                    int brightness, int frameBrightness);

// ---------------------------------------------------------------------------------------------------------------------
// Widescreen (src/port/Engine.cpp, os_devices.cpp, libultraship fork interpreter.cpp)
// ---------------------------------------------------------------------------------------------------------------------

// A horizontal view half-angle (binary angle, 0x10000 = 360 degrees) widened for the picture's shape: unchanged at
// 4:3, wider at 16:9. For game checks of what the camera sees, which assume the N64's 4:3 picture.
int16_t Port_WidenViewAngle(int16_t angle);
// Horizontal camera spans follow the projection's width relative to the original 4:3 view.
float Port_WidenViewSpan(float span);
// N64 pixels beside the 4:3 frame on each side of a widescreen picture (0 at 4:3).
int Port_WideMarginN64(void);
// The x of a node of the battle's camera-facing effect grids (Hypnosis' swirl: Trail40, Trail40Variant,
// Trail40GridAlt) for the picture's shape: unchanged at 4:3, wider in widescreen so the grid covers the whole picture.
float Port_WidenEffectGridX(float x);
// The battle's two portrait cameras (CameraNode*): their portraits are anchored like the HUD when drawn
// (battle_engine_main.c Battle_FillActivePokemonThumbnail); set by the battle every frame it draws its HUD; a
// registration older than a few frames no longer counts.
void Port_SetHudAnchorCameras(const void* left, const void* right);
int Port_HudAnchorSide(const void* camera); // -1 left, 1 right, 0 none

// Display-list markers the libultraship fork reads (no-op tags on the N64's microcode). They expand to gDPNoOpTag3,
// so they need the game's GBI headers where they are used.
// Full-screen 2D effects made for the 4:3 picture (the battle's strip wipe): the textured rectangles between these
// markers are stretched over the whole width of a widescreen picture (interpreter.cpp sWideRects).
#define gDPPortWideRectsBegin(pkt) gDPNoOpTag3(pkt, 0x50, 0, 1)
#define gDPPortWideRectsEnd(pkt) gDPNoOpTag3(pkt, 0x50, 0, 0)
// Preserve the CPU pixels of the next colour image before a GPU blend (interpreter.cpp RttInitialize).
#define gDPPortPreserveColorImage(pkt) gDPNoOpTag3(pkt, 0x51, 0, 1)
// The battle HUD in a widescreen picture: what is drawn between these markers moves to the left or right edge of the
// picture (by the margin beside the 4:3 frame) and is not cut at the 4:3 sides (interpreter.cpp sAnchor), so the
// status boxes, portraits and menus slide in from the screen's edges. Nothing changes at 4:3.
#define gDPPortAnchorLeft(pkt) gDPNoOpTag3(pkt, 0x52, 0, 1)
#define gDPPortAnchorRight(pkt) gDPNoOpTag3(pkt, 0x52, 0, 2)
#define gDPPortAnchorEnd(pkt) gDPNoOpTag3(pkt, 0x52, 0, 0)
// Not moved, only not cut at the 4:3 sides (what runs off the N64 screen's edge runs on to the picture's edge); end it
// with gDPPortAnchorEnd.
#define gDPPortNoCut(pkt) gDPNoOpTag3(pkt, 0x52, 0, 3)
// The main picture is a 3D scene's even where 2D drawing comes first: a backdrop of full-screen rectangles drawn
// before any 3D (the Elite Four room's sky, battle_engine_main.c Battle_DrawStageBackdrop) must not make the frame a
// 2D screen, which widescreen keeps inside the 4:3 area (interpreter.cpp s2DRectArea). Nothing changes at 4:3.
#define gDPPort3DScene(pkt) gDPNoOpTag3(pkt, 0x53, 0, 1)

// ---------------------------------------------------------------------------------------------------------------------
// Code and fragments (src/port/port_platform.c, generated by tools/gen_frag_sections.py and gen_fragment_thunks.py)
// ---------------------------------------------------------------------------------------------------------------------

// Web build: whether addr is a WebAssembly function-table index rather than an N64 address.
int Port_IsHostFunction(uintptr_t addr);
// Restores a fragment's .data/.bss as the N64 did on every load (generated frag_sections.c).
void Port_FragmentResetData(void* entry);
// Web build: a thunk of FragmentEntry's type for a fragment entry (generated fragment_thunks.c).
void* Port_FragmentCallable(void* entry);

// ---------------------------------------------------------------------------------------------------------------------
// Debug logging and tracing (src/port/stubs.c, xcheck.cpp); all off unless their PORT_* environment switch is set
// ---------------------------------------------------------------------------------------------------------------------

void Port_LogAlloc(void* p, int n, void* caller);
void Port_LogF2L(float mf[4][4], void* m);
void Port_TraceState(int state);
void Port_TraceJpeg(const char* what, int a, int b, int c);
// Camera log (PORT_CAMLOG): the game frame number, and frames since the current move animation started.
unsigned long long Port_CamlogGameFrameNo(void);
unsigned long long Port_CamlogAnimFrame(void);

// A debug switch from the environment, read once per call site: getenv scans the whole environment, and the port's
// debug checks sit in per-frame and per-draw code (Font_DrawChar, the geo renderer, the battle HUD). The switches
// are set before launch and never change while the game runs. (Statement expression: the game builds with clang.)
#define PORT_GETENV_ONCE(name)                                                  \
    ({                                                                          \
        static const char* sPortEnvValue_ = (const char*)1; /* 1: not read */   \
        if (sPortEnvValue_ == (const char*)1) sPortEnvValue_ = getenv(name);    \
        sPortEnvValue_;                                                         \
    })

// ---------------------------------------------------------------------------------------------------------------------
// Battle cross-check against the original game in ares, and scripted battle boot (src/port/xcheck.cpp,
// tools/battle_xcheck.py); all off unless PORT_XCHECK* / PORT_BATTLE* is set
// ---------------------------------------------------------------------------------------------------------------------

int Port_XcheckEnabled(void);
int Port_XcheckForcingRandom(void);
int Port_XcheckCom(void);
int Port_XcheckRandom(int fn, int arg, int* out, void* caller);
int Port_XcheckHasCase(void);
void Port_XcheckPatchMon(int side, int slot, void* battleMon);
int Port_XcheckWantDraw(void);
int Port_XcheckNoMusic(void);
void Port_XcheckGameFrame(void);
void Port_XcheckSfx(int kind, unsigned a, int b, int c);
unsigned long long Port_XcheckOsGetTime(void);
void Port_XcheckWriteEvent(const char* name, const void* state0, const void* state1, const void* scene, unsigned damage,
                           const void* messageQueues);
void Port_XcheckMoveAnim(int side, int move);
int Port_XcheckLoadWait(void);
int Port_XcheckAnimActive(void);
const char* Port_XcheckFramePath(void);
// guRandom's callers while an animation is captured (PORT_XCHECK_GURANDOM=1).
void Port_XcheckGuRandomCalled(void* caller);

int Port_BattleBootActive(void);
int Port_BattleBootMon(int side, int slot, int* species, int* level, char* nick);
int Port_BattleBootHasField(int side, int slot, const char* name);
const char* Port_BattleBootTrainer(int side);
int Port_BattleBootScripted(int side);
int Port_BattleBootMode(int* rule, int* ai);
int Port_BattleBootThink(void);
int Port_BattleBootArena(void); // the case's arena, or -1
int Port_BattleBootRound2(void); // the case says the battle is Round 2's (Vs. Mewtwo: harder_mewtwo.c)
int Port_BattleBootNextAction(int side, int consume, int* arg);
void Port_BattleBootPhase(int phase);
void Port_BattleBootFinished(void);

// ---------------------------------------------------------------------------------------------------------------------
// Translated text (src/port/text_lookup.cpp; hippodrome-utils docs/specs/TRANSLATION.md)
// ---------------------------------------------------------------------------------------------------------------------

// The text language the player chose (gPort.TextLanguage): translation/<ROM language>.csv's column for it replaces
// the ROM's textdata strings, an empty cell keeping the ROM's own. The strings are in the game's encoding: its
// font's bytes (ASCII, Latin-1, 0xBE/0xA9 for the female/male signs), and two bytes for a character the font lacks:
// lead 0x81-0x9F (PORT_TEXT_WIDE_FIRST..LAST), then 0x80-0xFF (the font draws it from hippodrome-utils' Unifont or Pop).
#define PORT_TEXT_WIDE_FIRST 0x81
#define PORT_TEXT_WIDE_LAST 0x9F
// Table `table`'s string `index` in the chosen language, or NULL for the ROM's own (none chosen, or an empty cell).
// The pointer stays valid until the language changes.
const char* Port_TextLookup(uint32_t table, uint32_t index);
// A wide character's glyph at the font set's height (10 to 32 pixels): an IA8 texture of tex_width (a multiple of 8)
// by height texels, draw_width of them drawn, then the pen moves advance pixels. 0 when there is none.
typedef struct PortTextGlyph {
    const uint8_t* tex;
    int tex_width;
    int draw_width;
    int advance;
    int height;
} PortTextGlyph;
int Port_TextGlyph(uint32_t wide_index, int height, PortTextGlyph* out);
// The options screen's text-language row (game/src/fragments/options/options.c): its label, the choices (0 is the
// ROM's own text, then each language translation/<ROM language>.csv has a column for), the current choice, and
// setting it (saved in the port's settings). Labels are in the game's encoding.
const char* Port_TextLanguageLabel(void);
int Port_TextLanguageCount(void);
const char* Port_TextLanguageName(int choice);
int Port_TextLanguageChoice(void);
void Port_TextLanguageSet(int choice);
// The port's own string `id` (translation/port.csv) in the chosen text language, or the ROM's language when none is
// chosen, in the game's encoding (as Port_TextLookup's); English when that cell is empty, `id` when there is none.
const char* Port_TextPortString(const char* id);
// `text` (the game's encoding) as UTF-8 into out (size bytes, always terminated): for the port's own windows (ImGui).
// Returns the bytes written.
int Port_TextToUtf8(const char* text, char* out, int size);

// ---------------------------------------------------------------------------------------------------------------------
// Enhancements (src/port/enhancements/; hippodrome-utils docs/specs). Each hook does nothing while its option is off.
// ---------------------------------------------------------------------------------------------------------------------

struct BattleMon;
struct TeamRoster;
// Stat Exp difficulty (statexp_difficulty.c): the rental setting on the Pokémon select screen's rental roster as it
// is loaded (fragment61 Rental_LoadRosterForMode); a setting (0 rental, 1 opponent) on a roster's Pokémon (Battle
// Now's preset teams, Team_BuildQuickBattleTeams); the opponent setting on every COM roster as a battle starts
// (BattleScene_Init, before Battle_SelectComRoster).
void PortEnh_StatExpRentals(struct BattleMon* mons, int count);
void PortEnh_StatExpRoster(struct TeamRoster* roster, int group);
void PortEnh_StatExpBattleTeams(void);

struct ActivePokemon;
// A battle starts (BattleScene_Init, once its participants are set up) and, every frame the battle draws its HUD, after
// it (battle_engine_messages.c Battle_RenderHud). battle_enhancements.c.
void PortEnh_BattleBegin(void);
void PortEnh_BattleFrame(void);
// The battle's command menus (battle_menus.c): mid-battle move descriptions and order queueing.
// BattleScene_HandlePlayerMoveSelection, before its C buttons; BattleScene_CollectPlayerAction, first (with the
// command phase's frame count, sElapsedOrderSelectionTime) and last.
void PortEnh_MoveMenuInput(struct ActivePokemon* a);
void PortEnh_PlayerCommandBegin(struct ActivePokemon* a, struct ActivePokemon* foe, float elapsed_frames);
void PortEnh_PlayerCommandEnd(struct ActivePokemon* a);
// The side summary bars and speed estimates (battle_enhancements.c): who acts first, once Battle_StartTurn has the
// order (-1 from BattleScene_InitializeSwitchActions: a side switches, Speed decided nothing); the acting Pokémon
// attempts a move (ProcessBattleTurn, after the inability-to-act checks); the move's presentation starts
// (BattleScene_ActionSetupCameraStep, as its message box opens), which reveals the attempted move on its card.
void PortEnh_TurnOrder(int first_side);
void PortEnh_MoveAttempted(void);
void PortEnh_MoveStart(void);
// The battle order screen (battle_order_select.c): the summary bars show both parties there too. Each frame it draws
// (BattleMenu_UpdateOrderSelect, after the screen), and once it has closed (BattleMenu_RunOrderSelectScreen).
// The frame receives each human side's required pick count and current roster-slot mask; COM sides have count 0.
// PortEnh_OrderSelectBackdrop: in widescreen its tiled background goes on into the sides of the picture (with the
// image the screen tiles), so the bars there sit on the screen's own background; nothing at 4:3.
void PortEnh_OrderSelectFrame(const uint8_t required[2], const uint8_t picked[2]);
void PortEnh_OrderSelectEnd(void);
// More room for display list buffers (Gfx_InitDisplayListBuffers' size) for the summary bars' card models.
uint32_t PortEnh_OrderSelectDisplayListRoom(void);
uint32_t PortEnh_BattleDisplayListRoom(void);
void PortEnh_OrderSelectBackdrop(const void* image);
// Per-card effect state active while card_models.c renders a summary-bar card model (NULL during normal rendering).
// Keeps card models isolated from battle's shared model_callback_library globals and freezes effects when held/fainted.
typedef struct PortEnhCardParticle {
    int16_t active;
    int16_t padding0;
    float position[3];
    float velocity[3];
    float scale[3];
    int16_t age;
    int16_t padding1;
} PortEnhCardParticle;
typedef struct PortEnhCardEffect {
    int32_t frame;
    int16_t scrollOffset;
    uint8_t shouldStep;
    uint8_t particlesSeeded;
    int32_t textureNodeIndex;
    int32_t particleNodeIndex;
    int32_t particleDrawCount;
    uint32_t randomState;
    PortEnhCardParticle particles[10];
} PortEnhCardEffect;
PortEnhCardEffect* PortEnh_ActiveCardEffect(void);
uint32_t PortEnh_CardEffectRandom(PortEnhCardEffect* effect);
// Hue choice (hue_picker.c). PortEnh_HueShift: the hue to build `mon`'s model and icon with, `game_shift` being the
// game's own (Model_ComputeNicknameColorVariant); PortEnh_HueBypass(1) keeps the game's own for the calls until
// PortEnh_HueBypass(0) (a Hall of Fame record, which has no DVs). The team select screens' Pokémon card
// (TeamSelection_Preview): PortEnh_HuePickerInput takes its L / R / Z and stick, 1 when the hue changed (the card then
// rebuilds the model and the icon); PortEnh_HuePickerDraw draws the hue bar over the card's model picture.
int16_t PortEnh_HueShift(struct BattleMon* mon, int16_t game_shift);
void PortEnh_HueBypass(int on);
int PortEnh_HuePickerInput(struct BattleMon* mon, uint16_t pressed, float stick_x);
void PortEnh_HuePickerDraw(int x, int y, int w, int h, struct BattleMon* mon);
// Trainer memory (stadion_trainer_memory.c, memory_screen.c). Team_LoadOpponentPreset has loaded the COM trainer (its
// name and team go into the memory). The screen right before the player picks their Game Boy or rental Pokémon
// (TeamSelection_RunModal, each frame after Controller_PollInputs): the choosing player's C button opens the memory
// screen; while it is open it takes the buttons (it clears them, so the screen under it sees none).
// PortEnh_MemoryScreenEnd: that screen has closed.
void PortEnh_TmTrainerLoaded(void);
void PortEnh_MemoryScreenFrame(void);
void PortEnh_MemoryScreenEnd(void);
// Rival bug fix (rival_fix.c). RegisteredTeam_Register: the source the player picked for the new registered team (0-3
// a Game Boy, 4 the rentals); func_8420DBA0: that team (an unk_D_84229EB0_00024) has been written to the save;
// Team_CommitSelectionToTrainer: a registered team was picked (its gbStarterChoice comes from the side file).
void PortEnh_RivalRegisterSource(int source);
void PortEnh_RivalTeamSaved(void* team);
void PortEnh_RivalTeamPicked(void* team);
// Harder vs Mewtwo (src/port/enhancements/harder_mewtwo.c, heal_bell.c; hippodrome-utils
// docs/specs/game_specific/1/HARDER_VS_MEWTWO.md). The ActivePokemon arguments are void* here so the port's C++ can
// include this header; each hook does nothing outside the harder Vs. Mewtwo battle.
// the moves Mewtwo knows in the harder battle; Heal Bell, the move gMoveData gains after Struggle (165), and its effect
// (the free slot after Disable's 0x56)
#define MEWTWO_MOVES 12
#define PORT_MOVE_HEAL_BELL 166
#define PORT_EFFECT_HEAL_BELL 0x57
void PortEnh_MewtwoBattleBegin(void);
int PortEnh_MewtwoActive(void);
int PortEnh_MewtwoFreezeImmune(void* target);       // BattleEffect_SecondaryStatusChance: Mewtwo can't be frozen
void PortEnh_MewtwoTransformed(void* user_runtime, void* target_active); // Transform on it: the vanilla moveset
void PortEnh_MewtwoLegality(void* active);          // after the illegality mask: Earthquake and Heal Bell are legal
int PortEnh_MewtwoChooseAction(void* cpu, int game_choice); // BattleScene_TriggerCpuChoice: the rule tables' slot
int PortEnh_MewtwoSkipResidual(void);               // Battle_ResolveMonAction: 1 to hold residual damage
void PortEnh_MewtwoActionResolved(void);            // after it
int PortEnh_MewtwoActAgainPending(void);            // where Mewtwo's action ends: its second action is due
int PortEnh_MewtwoActAgain(void);                   // 1 when it resolved the second action (the phase is set)
int PortEnh_MewtwoPassAction(void);                 // ProcessBattleTurn: 1 for the skipped action's caption
void PortEnh_MewtwoRevealPending(void);             // the action's presentation starts
int PortEnh_MewtwoIsCard(int side);
int PortEnh_MewtwoCardMoves(uint16_t* moves, int max);
// Heal Bell (src/port/enhancements/heal_bell.c): its effect (sMoveEffectHandlers' 0x57), the presentation stand-in
// once the move has resolved, the bell (drawn with the battle's effects) and its melody (mixed into the game's sound)
void PortEnh_HealBellEffect(void);
void PortEnh_HealBellResolved(void);
int PortEnh_HealBellPresenting(void* active);
void PortEnh_HealBellStart(void* active);
int PortEnh_HealBellBusy(void);
int PortEnh_HealBellSettled(int row_settled);
void PortEnh_HealBellReset(void);
void PortEnh_HealBellDraw(void);
void PortEnh_HealBellMix(int16_t* samples, int frames, int rate);

// ---------------------------------------------------------------------------------------------------------------------
// Hooks the game implements for the port
// ---------------------------------------------------------------------------------------------------------------------

// The size of the memory-pool block holding data (game/src/memory_main.c).
size_t Port_PoolBlockSize(void* data);
// The host pointer for a virtual N64 address, and whether p is raw ROM-loaded data (game/src/memmap.c).
void* Port_VResolve(uintptr_t addr);
int Port_VIsRaw(const void* p);
// Reseeds guRandom (game/lib/ultralib/src/gu/random.c), for the battle cross-check.
void Port_GuRandomSeed(unsigned int seed);
// Battle cross-check events and the case's parties (game/src/fragments/battle_engine/battle_engine_mechanics.c).
void Port_XcheckBattleEvent(const char* name);
void Port_XcheckApplyCaseParties(void);
// Display-list buffer debugging (game/src/gfx_buffer.c; weak references in the libultraship fork, interpreter.cpp).
void Port_DlBufferWhere(uintptr_t p, int* which, long* off);
int Port_DlBufferActive(void);
uintptr_t Port_DlBufferBase(void);
// The ROM font's glyph for a byte, 0 for none (game/src/graphics_textures.c): text_lookup.cpp keeps a character as
// one byte only where the font draws it.
uint8_t Font_MapGlyphCode(uint16_t index);
// A controller port's menu gradient, top and bottom colours as RGB (battle_engine_hud.c), for the enhancements' windows.
void BattleHud_PortMenuColors(int port, uint8_t start[3], uint8_t end[3]);
// Field offsets of the Game Boy Tower's emulator context, used by src/port/gb_core.c
// (game/src/fragments/gb_tower_emulator/gb_tower_emulator_7F9A0.c).
extern const size_t Port_GbEmuOffsets[9];

#ifdef __cplusplus
}
#endif

#endif // STADION_PORT_H
