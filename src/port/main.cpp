#include "Engine.h"
#include "port_platform.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <csignal>
#if !defined(_WIN32)
#include <sys/ucontext.h>
#include <unistd.h>
#endif

extern "C" void Main(void);
extern "C" uint64_t Port_ViTick();
extern "C" void Port_InputFinish(); // game entry (src/main.c): starts libultra and the idle thread
extern "C" void Port_ReserveLowAddressSpace(void);

#if defined(_WIN32)
// Prints the crash address and the unwound frames plus the image base; resolve with the PDB/map file.
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
    _exit(3);
    return EXCEPTION_EXECUTE_HANDLER;
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
    _exit(128 + sig);
}
#endif

// Installed after Port::Init: libultraship's own crash handler (Linux, Windows) replaces earlier handlers.
static void InstallSignalHandlers() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(CrashHandler);
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

int main(int argc, char** argv) {
    // The port tells N64 addresses (below 4 GB) from host pointers; keep host allocations above 4 GB (Windows).
    Port_ReserveLowAddressSpace();
    const char* romPath = argc > 1 ? argv[1] : "baserom.z64";
    if (!Port::LoadRom(romPath)) {
        fprintf(stderr, "Could not read ROM '%s'\nUsage: pokestadium <path to Pokemon Stadium (USA) .z64>\n", romPath);
        return 1;
    }
    Port_AltSignalStack();
    InstallSignalHandlers();
    if (!Port::Init()) {
        return 1;
    }
    InstallSignalHandlers();
    Main();
    // The window must be driven from the process's main thread; game threads run on host threads.
    while (Port::PumpFrame()) {
        Port::PumpRspTasks();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Port::StopDevices();
    // Window closed: end the process at once, as on SIGTERM. Returning from main ran the static destructors while
    // game threads were still running, and one of them then locked a destroyed mutex ("mutex lock failed: Invalid
    // argument", abort). Saves are already on disk (flash and Transfer Pak writes are written through).
    Port_InputFinish();
    fflush(nullptr);
    _exit(0);
}
