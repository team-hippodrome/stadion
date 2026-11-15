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

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------------------------------
// Timing and pacing (src/port/os_devices.cpp)
// ---------------------------------------------------------------------------------------------------------------------

// Retraces (N64 vertical blanks) since boot, on the port's clock: virtual in lockstep mode.
uint64_t Port_ViTick(void);
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
int Port_BattleBootNextAction(int side, int consume, int* arg);
void Port_BattleBootPhase(int phase);
void Port_BattleBootFinished(void);

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
// Field offsets of the Game Boy Tower's emulator context, used by src/port/gb_core.c
// (game/src/fragments/gb_tower_emulator/gb_tower_emulator_7F9A0.c).
extern const size_t Port_GbEmuOffsets[9];

#ifdef __cplusplus
}
#endif

#endif // STADION_PORT_H
