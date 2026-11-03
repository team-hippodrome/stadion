#include "Engine.h"
#include "rom_check.h"
#include "interp.h"
#include "port_platform.h"
#include "launcher.h"
#include "launcher_files.h"
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
#if defined(_WIN32)
#include <exception>
#include <io.h>
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

extern "C" void Main(void);
extern "C" uint64_t Port_ViTick();
extern "C" void Port_InputFinish(); // game entry (src/main.c): starts libultra and the idle thread
extern "C" void Port_ReserveLowAddressSpace(void);

#if defined(_WIN32)
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

static int RunGame(int argc, char** argv) {
#if defined(_WIN32)
    AttachToConsole();
#endif
#if defined(__ANDROID__)
    ForwardStdioToLogcat();
    LoadEnvFile();
#endif
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
    if (!Port::Init()) {
        return 1;
    }
    InstallSignalHandlers();
    if (romPath.empty()) {
        romPath = Port::RunLauncher(Port::GetWindow());
        if (romPath.empty()) {
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
    Main();
    // The window must be driven from the process's main thread; game threads run on host threads.
    while (Port::PumpFrame()) {
        Port::PumpRspTasks();
        Port::PumpInterpolation();
        // up to 1 ms, less when the game queues an RSP task: a scene's 300 JPEG tasks each waited a full loop pass
        // (a 0.35 s pause at the title-to-menu change)
        Port::WaitForRspTasks(std::chrono::milliseconds(1));
    }
    Port::StopDevices();
    // Window closed: end the process at once, as on SIGTERM. Returning from main ran the static destructors while
    // game threads were still running, and one of them then locked a destroyed mutex ("mutex lock failed: Invalid
    // argument", abort). Saves are already on disk (flash and Transfer Pak writes are written through).
    Port_InputFinish();
    fflush(nullptr);
    _exit(0);
}

int main(int argc, char** argv) {
#ifdef ENABLE_VULKAN
    try {
        return RunGame(argc, argv);
    } catch (const Fast::VulkanSwapchainError& e) {
        Port::ExitAfterVulkanFailure(e.what());
    }
#else
    return RunGame(argc, argv);
#endif
}
