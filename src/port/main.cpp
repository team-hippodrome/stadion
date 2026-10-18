#include "Engine.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <csignal>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <sys/ucontext.h>
#include <unistd.h>

extern "C" void Main(void);
extern "C" uint64_t Port_ViTick();
extern "C" void Port_InputFinish(); // game entry (src/main.c): starts libultra and the idle thread

// Prints the crashing thread's stack as raw addresses plus the image slide; resolve with
// tools/symbolize.sh (atos). Game threads are host threads, so this is the only crash report there is.
static void CrashHandler(int sig, siginfo_t* info, void* uctx) {
    void* frames[48];
    int n = backtrace(frames, 48);
    dprintf(2, "\n*** crash: signal %d, fault address %p, image slide 0x%lx, game frame %llu\n", sig, info ? info->si_addr : nullptr, (unsigned long)_dyld_get_image_vmaddr_slide(0), (unsigned long long)Port_ViTick());
    Port_InputFinish(); // the input recording now ends at the crash: replay it with PORT_REPLAY
    auto* uc = static_cast<ucontext_t*>(uctx);
    if (uc) {
        // the faulting pc first, then the link register, then the unwound frames
        dprintf(2, "  0x%llx\n  0x%llx\n", (unsigned long long)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss),
                (unsigned long long)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss));
    }
    for (int i = 0; i < n; i++) {
        dprintf(2, "  %p\n", frames[i]);
    }
    _exit(128 + sig);
}

int main(int argc, char** argv) {
    struct sigaction sa = {};
    sa.sa_sigaction = CrashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGTRAP, &sa, nullptr); // __builtin_trap (clang emits it for some undefined behaviour)
    sigaction(SIGILL, &sa, nullptr);
    // A plain SIGTERM (scripts, Ctrl-C) ends the process at once: normal teardown would destroy state that game threads still use.
    signal(SIGTERM, [](int) { _exit(0); });
    signal(SIGINT, [](int) { _exit(0); });
    const char* romPath = argc > 1 ? argv[1] : "baserom.z64";
    if (!Port::LoadRom(romPath)) {
        fprintf(stderr, "Could not read ROM '%s'\nUsage: pokestadium <path to Pokemon Stadium (USA) .z64>\n", romPath);
        return 1;
    }
    if (!Port::Init()) {
        return 1;
    }
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
