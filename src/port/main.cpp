#include "Engine.h"
#include "rom_check.h"
#include "interp.h"
#include "port_platform.h"
#include "launcher.h"
#include "launcher_files.h"
#include "launcher_reliability.h"
#include <fstream>
#include <nlohmann/json.hpp>
#ifdef ENABLE_VULKAN
#include <fast/backends/gfx_vulkan.h>
#endif

#include <SDL3/SDL_messagebox.h>

#include <string>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <csignal>
#include "StadionPort.h"
#if defined(_WIN32)
#include <exception>
#include <io.h>
#include <SDL3/SDL_process.h>
#include <spdlog/spdlog.h>
#else
#include <sys/ucontext.h>
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <fstream>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <fast/Fast3dWindow.h>
#endif

extern "C" void Main(void);
extern "C" void Port_InputFinish(); // game entry (src/main.c): starts libultra and the idle thread
extern "C" void Port_ReserveLowAddressSpace(void);

#if defined(_WIN32)
static bool RestartWindowsLauncher() {
    wchar_t path[32768];
    const DWORD size = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    if (size == 0 || size >= std::size(path)) {
        SDL_SetError("Could not locate the running executable");
        return false;
    }
    const auto executable = Port::PathUtf8(std::filesystem::path(path));
    const char* args[] = { executable.c_str(), "--launcher", nullptr };
    SDL_Environment* env = SDL_CreateEnvironment(true);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process* process = nullptr;
    if (env && props &&
        SDL_UnsetEnvironmentVariable(env, "PORT_SAFE_MODE") &&
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env) &&
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args)) &&
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true)) {
        process = SDL_CreateProcessWithProperties(props);
    }
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    if (!process) return false;
    SDL_DestroyProcess(process);
    return true;
}

// pokestadium.exe is a window-only program (no console window of its own); stdout/stderr (logs, crash reports) still
// reach a console when there is one. Output redirected to a file or pipe (pokestadium.exe 2> log.txt) is kept as is;
// otherwise the program writes to the console of the shell it was started from, or to a console of its own with
// PORT_CONSOLE=1.
static void AttachToConsole() {
    DWORD type = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) {
        return;
    }
    const char* want = getenv("PORT_CONSOLE");
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !(want != nullptr && strcmp(want, "1") == 0 && AllocConsole())) {
        return;
    }
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    setvbuf(stderr, nullptr, _IONBF, 0);
    // for code that writes to the handles directly (spdlog's console sink)
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(stdout)));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));
}

// libultraship's handler, replaced by CrashHandler below: it writes the registers and a traceback to the log file
static LPTOP_LEVEL_EXCEPTION_FILTER sShipCrashHandler = nullptr;

// Prints the crash address and the unwound frames plus the image base; resolve with the PDB/map file. Then lets
// libultraship's handler write the crash to the log: without a console, stderr goes nowhere.
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    void* frames[48];
    int n = Port_Backtrace(frames, 48);
    fprintf(stderr, "\n*** crash: exception 0x%08lx at %p, image base 0x%lx, game frame %llu\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, Port_ImageSlide(),
            (unsigned long long)Port_ViTick());
    Port_InputFinish();
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "  %p\n", frames[i]);
    }
    fflush(stderr);
    if (sShipCrashHandler != nullptr) {
        sShipCrashHandler(ep);
    }
    _exit(3);
    return EXCEPTION_EXECUTE_HANDLER;
}

// An uncaught C++ exception (a failed D3D call, for one) ends in std::terminate, which no exception filter sees
static void TerminateHandler() {
    const char* what = "unknown exception";
    try {
        if (std::exception_ptr e = std::current_exception()) {
            std::rethrow_exception(e);
        }
    } catch (const std::exception& e) {
        what = e.what();
    } catch (...) {
    }
    fprintf(stderr, "\n*** terminate: %s\n", what);
    SPDLOG_CRITICAL("Uncaught exception: {}", what);
    spdlog::shutdown();
    _exit(3);
}
#else
// Prints the crashing thread's stack as raw addresses plus the image slide; resolve with tools/symbolize.sh.
// Game threads are host threads, so this is the only crash report there is.
static void CrashHandler(int sig, siginfo_t* info, void* uctx) {
    // header first: backtrace() itself can fault when the crash corrupted the heap or stack
    dprintf(2, "\n*** crash: signal %d, fault address %p, image slide 0x%lx, game frame %llu\n", sig,
            info ? info->si_addr : nullptr, Port_ImageSlide(), (unsigned long long)Port_ViTick());
    auto* uc = static_cast<ucontext_t*>(uctx);
    if (uc) {
        // the faulting pc first (and the link register on arm64), then the unwound frames
#if defined(__APPLE__) && defined(__aarch64__)
        dprintf(2, "  0x%llx\n  0x%llx\n", (unsigned long long)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss),
                (unsigned long long)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss));
#elif defined(__APPLE__) && defined(__x86_64__)
        dprintf(2, "  0x%llx\n", (unsigned long long)uc->uc_mcontext->__ss.__rip);
#elif defined(__linux__) && defined(__aarch64__)
        dprintf(2, "  0x%llx\n  0x%llx\n", (unsigned long long)uc->uc_mcontext.pc,
                (unsigned long long)uc->uc_mcontext.regs[30]);
#elif defined(__linux__) && defined(__x86_64__)
        dprintf(2, "  0x%llx\n", (unsigned long long)uc->uc_mcontext.gregs[REG_RIP]);
#endif
    }
    void* frames[48];
    int n = Port_Backtrace(frames, 48);
    for (int i = 0; i < n; i++) {
        dprintf(2, "  %p\n", frames[i]);
    }
    Port_InputFinish(); // the input recording now ends at the crash: replay it with PORT_REPLAY
#if defined(__ANDROID__)
    usleep(300000); // let ForwardStdioToLogcat pass the stack on before the process ends
#endif
    _exit(128 + sig);
}
#endif

// Installed after Port::Init: libultraship's own crash handler (Linux, Windows) replaces earlier handlers. On Windows
// it is kept and called after ours, so crashes still reach the log file.
static void InstallSignalHandlers() {
#if defined(_WIN32)
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(CrashHandler);
    if (previous != CrashHandler) {
        sShipCrashHandler = previous;
    }
    std::set_terminate(TerminateHandler);
#else
    struct sigaction sa = {};
    sa.sa_sigaction = CrashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGTRAP, &sa, nullptr); // __builtin_trap (clang emits it for some undefined behaviour)
    sigaction(SIGILL, &sa, nullptr);
#endif
    // A plain SIGTERM (scripts, Ctrl-C) ends the process at once: normal teardown would destroy state that game threads still use.
    signal(SIGTERM, [](int) { _exit(0); });
    signal(SIGINT, [](int) { _exit(0); });
}

#if defined(__ANDROID__)
// Android drops stdout/stderr: forward them, line by line, to the system log (adb logcat -s pokestadium), so the port's
// messages and the crash handler's stack (written straight to fd 2) can be read
static void ForwardStdioToLogcat() {
    static int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        return;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipeFds[1], 1);
    dup2(pipeFds[1], 2);
    std::thread([] {
        char buf[1024];
        std::string line;
        ssize_t n;
        while ((n = read(pipeFds[0], buf, sizeof buf)) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == '\n') {
                    __android_log_write(ANDROID_LOG_INFO, "pokestadium", line.c_str());
                    line.clear();
                } else {
                    line += buf[i];
                }
            }
        }
    }).detach();
}

// Android apps get no environment from a shell: the debug switches (PORT_TRACE_PACING and the like) come from
// port_env.txt in the app's folder instead, one NAME=VALUE per line (adb push it to
// /sdcard/Android/data/com.teamhippodrome.pokestadium/files/). Read before anything checks them.
static void LoadEnvFile() {
    const char* dir = SDL_GetAndroidExternalStoragePath();
    if (dir == nullptr) {
        return;
    }
    std::ifstream f(std::string(dir) + "/port_env.txt");
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos || eq == 0) {
            continue;
        }
        setenv(line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), 1);
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), line.substr(0, eq).c_str(), line.substr(eq + 1).c_str(), true);
        fprintf(stderr, "port_env.txt: %s\n", line.c_str());
    }
}
#endif

// The loaded ROM must be the original dump (rom_check.h): any other starts and then crashes in the middle of the game.
// The message goes to stderr and, as there is no console on Windows or in the macOS app, to a message box.
static bool RomIsOriginal(const std::string& path) {
    const std::string error = Port::CheckRomDump(Port::RomData(), Port::RomSize());
    if (error.empty()) {
        return true;
    }
    const std::string sha1 = Port::Sha1Hex(Port::RomData(), Port::RomSize());
    char message[1024];
    snprintf(message, sizeof message,
             "%s is not the original Pokemon Stadium (USA) ROM.\n\nSHA-1 %s (%zu bytes)\nexpected %s (%zu bytes, .z64)\n\n"
             "A different revision, a patched or translated ROM or a damaged dump does not work with the port.",
             path.c_str(), sha1.c_str(), Port::RomSize(), Port::kRomSha1, Port::kRomSize);
    fprintf(stderr, "%s\n", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pokémon Stadium: wrong ROM", message, nullptr);
    return false;
}

#if defined(__EMSCRIPTEN__)
// Web build: the browser's main thread must return to its event loop for the canvas to show a frame, so the window loop
// below runs one pass per animation frame (requestAnimationFrame): events, then display lists until a frame was drawn
// or the time for this pass is up. The game threads (Web Workers) run on meanwhile, and so do the audio and JPEG tasks
// (os_devices.cpp).
static void WebLoopPass() {
    static bool started = false;
    if (!started) {
        if (Port::PumpShaderWarmup()) return;
        if (!Port::GetWindow()->IsRunning()) {
            emscripten_cancel_main_loop();
            return;
        }
        Main();
        started = true;
    }
    if (!Port::PumpFrame()) {
        Port::StopDevices();
        Port_InputFinish();
        emscripten_cancel_main_loop();
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    const uint64_t drawn = Port::FramesDrawn();
    do {
        Port::PumpRspTasks();
        Port::PumpInterpolation();
        if (Port::FramesDrawn() != drawn) {
            break;
        }
        Port::WaitForRspTasks(std::chrono::milliseconds(1));
        // (not much longer: the browser's main thread spins while it waits, and the frame must reach the screen in time)
    } while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(6));
}

// The absolute symbols of the web build (tools/gen_wasm_abs_syms.py) only come out right when their object is placed
// first in memory; a wrong placement shows here instead of as wrong N64 addresses everywhere.
extern "C" char port_abs_check[];
static bool CheckAbsSymbols() {
    if ((uintptr_t)port_abs_check != 0x12345670u) {
        fprintf(stderr, "web build: absolute symbols are off (port_abs_check = %p); see tools/gen_wasm_abs_syms.py\n",
                (void*)port_abs_check);
        return false;
    }
    return true;
}
#endif

static int RunGame(int argc, char** argv) {
#if defined(__EMSCRIPTEN__)
    if (!CheckAbsSymbols()) {
        return 1;
    }
#endif
#if defined(_WIN32)
    AttachToConsole();
#endif
#if defined(__ANDROID__)
    ForwardStdioToLogcat();
    LoadEnvFile();
#endif
    auto setOption = [](const char* key) {
#ifdef _WIN32
        _putenv_s(key, "1");
#else
        setenv(key, "1", 1);
#endif
    };
    const bool forceLauncher = argc > 1 && (strcmp(argv[1], "--launcher") == 0 || strcmp(argv[1], "--safe-mode") == 0);
    if (forceLauncher) {
        setOption("PORT_FORCE_LAUNCHER");
        if (strcmp(argv[1], "--safe-mode") == 0) setOption("PORT_SAFE_MODE");
        argc = 1;
    }
    if (argc > 1 && strcmp(argv[1], "--check-launcher") == 0) {
        if (argc != 12) { fprintf(stderr, "Usage: --check-launcher <rom> <save> <rom1> <save1> ... <rom4> <save4>\n"); return 1; }
        std::array<Port::LauncherSlot, 4> slots;
        for (int i = 0; i < 4; ++i) slots[i] = {*argv[4 + i * 2] != 0, argv[4 + i * 2], argv[5 + i * 2]};
        auto result = Port::CheckLauncherFiles(std::filesystem::u8path(Port::AppPath("")), argv[3], slots);
        auto romError = Port::CheckRomDumpFile(argv[2]);
        if (!romError.empty()) result.errors.insert(result.errors.begin(), "Stadium ROM: " + romError);
        for (const auto& error : result.errors) printf("Error: %s\n", error.c_str());
        for (const auto& warning : result.warnings) printf("Notice: %s\n", warning.c_str());
        return result.errors.empty() ? 0 : 1;
    }
    if (argc > 1 && (strcmp(argv[1], "--export-stadium-save") == 0 || strcmp(argv[1], "--launcher-diagnostics") == 0)) {
        const bool diagnostics = strcmp(argv[1], "--launcher-diagnostics") == 0;
        if ((!diagnostics && argc != 3) || (diagnostics && argc != 2)) return 1;
        std::string path, error;
        const auto directory = std::filesystem::u8path(Port::AppPath(""));
        std::string requested = "Unknown";
        if (diagnostics) {
            try {
                std::ifstream config(directory / "pokestadium.cfg.json");
                const auto json = nlohmann::json::parse(config);
                const auto windowId = json.value("Window", nlohmann::json::object()).value("Backend", nlohmann::json::object()).value("Id", 0);
                const auto id = json.value("CVars", nlohmann::json::object()).value("gLauncher", nlohmann::json::object()).value("Renderer", windowId);
                requested = id == 1 ? "DirectX 11" : id == 2 ? "OpenGL" : id == 3 ? "Metal" : id == 4 ? "Vulkan" : "Unknown";
            } catch (...) { /* A broken config can still produce a diagnostic report. */ }
        }
        const bool ok = diagnostics ? Port::ExportLauncherDiagnostics(directory, PORT_BUILD_VERSION,
            requested, "Not running (frontend export; see recent log)", path, error) :
            Port::ExportStadiumSave(std::filesystem::u8path(argv[2]), directory, path, error);
        if (!ok) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
        printf("%s\n", path.c_str()); return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("Pokemon Stadium [ROM.z64]\n--launcher: bypass Auto-play\n--safe-mode: bypass Auto-play and select OpenGL (Metal on macOS)\nLauncher Files & help: check setup, open folders, export saves and diagnostics.\n");
        return 0;
    }
    // The legacy macOS frontend shares the native cartridge-save importer instead of maintaining another format reader.
    if (argc > 1 && strcmp(argv[1], "--import-stadium-save") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Usage: pokestadium --import-stadium-save <raw cartridge save>\n");
            return 1;
        }
        std::string imported, error;
        if (!Port::ImportStadiumSave(std::filesystem::u8path(argv[2]), std::filesystem::u8path(Port::AppPath("")),
                                     imported, error)) {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        printf("%s\n", imported.c_str());
        return 0;
    }
    // The port tells N64 addresses (below 4 GB) from host pointers; keep host allocations above 4 GB (Windows).
    Port_ReserveLowAddressSpace();
    Port_AltSignalStack();
    // Battle boot (src/port/xcheck.cpp): `pokestadium <rom> --battle <case file> [--frames <dir>] [--every <n>]
    // [--res <WxH>] [--max-frames <n>]` starts the battle engine directly; the options set the PORT_BATTLE_* variables.
    for (int i = 2; i + 1 < argc; i += 2) {
        const char* key = nullptr;
        if (strcmp(argv[i], "--battle") == 0) key = "PORT_BATTLE";
        else if (strcmp(argv[i], "--frames") == 0) key = "PORT_BATTLE_FRAMES";
        else if (strcmp(argv[i], "--every") == 0) key = "PORT_BATTLE_EVERY";
        else if (strcmp(argv[i], "--res") == 0) key = "PORT_BATTLE_RES";
        else if (strcmp(argv[i], "--max-frames") == 0) key = "PORT_BATTLE_MAX_FRAMES";
        if (!key) break;
#ifdef _WIN32
        _putenv_s(key, argv[i + 1]);
#else
        setenv(key, argv[i + 1], 1);
#endif
    }
    InstallSignalHandlers();
    Port::UseShipHomeDefaults();
    // With a ROM argument the game starts right away (scripts, replays); without one, the launcher (launcher.cpp)
    // asks for the ROM, the save and the Transfer Pak cartridges in the game window first.
    std::string romPath = argc > 1 ? argv[1] : "";
    if (!romPath.empty() && !Port::LoadRom(romPath.c_str())) {
        fprintf(stderr, "Could not read ROM '%s'\nUsage: pokestadium [path to Pokemon Stadium (USA) .z64]\n", romPath.c_str());
        return 1;
    }
    if (!romPath.empty() && !RomIsOriginal(romPath)) {
        return 1;
    }
    // An interrupted interactive launch comes back to settings. Only a failure while creating the
    // graphics window selects a safe renderer; an interrupted game merely bypasses Auto-play.
    const auto recovery = std::filesystem::u8path(Port::AppPath("launcher-session.txt"));
    const bool interactive = romPath.empty();
    auto clearRecovery = [&] { if (interactive) { std::error_code ec; std::filesystem::remove(recovery, ec); } };
    if (interactive) {
        std::ifstream previous(recovery); std::string stage; std::getline(previous, stage);
        if (!stage.empty()) {
            setOption("PORT_FORCE_LAUNCHER");
            if (stage == "graphics") setOption("PORT_SAFE_MODE");
            fprintf(stderr, "[launcher] Previous launch did not close normally; reopening settings.\n");
        }
        std::ofstream marker(recovery); marker << "graphics\n";
    }
    const auto configPath = std::filesystem::u8path(Port::AppPath("pokestadium.cfg.json"));
    if (std::filesystem::is_regular_file(configPath)) {
        if (getenv("PORT_SAFE_MODE") != nullptr) {
            // Recovery changes only graphics/Auto-play, and keeps an untouched copy of the original JSON.
            std::error_code ec; bool copied = false;
            for (int i = 1; i <= 10000; ++i) {
                const auto copy = std::filesystem::u8path(Port::PathUtf8(configPath) + ".before-recovery-" + std::to_string(i));
                if (std::filesystem::exists(copy)) continue;
                copied = std::filesystem::copy_file(configPath, copy, std::filesystem::copy_options::none, ec); break;
            }
            if (!copied) { fprintf(stderr, "Cannot preserve config before recovery. No settings changed.\n"); clearRecovery(); return 1; }
        } else {
            try {
                std::ifstream input(configPath);
                if (!nlohmann::json::parse(input).is_object()) throw std::runtime_error("Config must be an object");
            } catch (const std::exception&) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot read launcher settings",
                    "The config JSON is invalid and has been preserved. Start with --safe-mode to create working settings and keep a recovery copy.", nullptr);
                clearRecovery(); return 1;
            }
        }
    }
    if (!Port::Init()) { return 1; }
    if (interactive) { std::ofstream marker(recovery); marker << "game\n"; }
    InstallSignalHandlers();
    if (romPath.empty()) {
        bool restart = false;
        do {
            restart = false;
            romPath = Port::RunLauncher(Port::GetWindow(), restart);
#ifdef _WIN32
            if (restart) {
                clearRecovery();
                if (RestartWindowsLauncher()) {
                    fflush(nullptr);
                    _exit(0);
                }
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not restart Pokémon Stadium", SDL_GetError(), nullptr);
            }
#endif
        } while (restart);
        if (romPath.empty()) {
            clearRecovery();
            fflush(nullptr);
            _exit(0);
        }
        if (!Port::LoadRom(romPath.c_str())) {
            fprintf(stderr, "Could not read ROM '%s'\n", romPath.c_str());
            return 1;
        }
        if (!RomIsOriginal(romPath)) { // (the launcher already checked; the file may have changed since)
            return 1;
        }
    }
#if defined(__EMSCRIPTEN__)
    Port::BeginShaderWarmup();
    emscripten_set_main_loop(WebLoopPass, 0, true); // does not return
#else
    if (!Port::RunShaderWarmup()) {
        clearRecovery();
        fflush(nullptr);
        _exit(0);
    }
    Main();
#endif
    // The window must be driven from the process's main thread; game threads run on host threads.
    while (Port::PumpFrame()) {
        // up to 1 ms, less when the game queues an RSP task: a scene's 300 JPEG tasks each waited a full loop pass
        // (a 0.35 s pause at the title-to-menu change). Tasks queued right after one another (those JPEG tasks: the
        // game submits the next when the last is done) are run for up to 4 ms before the window's events are pumped
        // again: the event pump (Cocoa) costs about as much as a task, and paying it 300 times held up the screen
        // changes by 15-30 ms (with or without Smooth motion). Events are still handled every 4 ms or so.
        const auto burstStart = std::chrono::steady_clock::now();
        do {
            Port::PumpRspTasks();
            Port::PumpInterpolation();
        } while (Port::WaitForRspTasks(std::chrono::milliseconds(1)) &&
                 std::chrono::steady_clock::now() - burstStart < std::chrono::milliseconds(4));
    }
    Port::StopDevices();
    // Window closed: end the process at once, as on SIGTERM. Returning from main ran the static destructors while
    // game threads were still running, and one of them then locked a destroyed mutex ("mutex lock failed: Invalid
    // argument", abort). Saves are already on disk (flash and Transfer Pak writes are written through).
    Port_InputFinish();
    clearRecovery();
    fflush(nullptr);
    _exit(0);
}

int main(int argc, char** argv) {
#ifdef ENABLE_VULKAN
    try {
        return RunGame(argc, argv);
    } catch (const Fast::VulkanFatalError& e) { // (a failed swapchain, or a device out of memory or lost)
        Port::ExitAfterVulkanFailure(e.what());
    }
#else
    return RunGame(argc, argv);
#endif
}
