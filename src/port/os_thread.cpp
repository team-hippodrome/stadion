// Host implementation of the libultra thread and message-queue model used by the game.
//
// The N64 runs one thread at a time, always the highest-priority runnable one, and only switches
// when a thread blocks, yields, changes priorities, or an interrupt makes a higher-priority thread
// runnable. Each OSThread here gets a host thread, but a single "token" decides which one may run:
// all other host threads sleep on their own condition variable. Switches happen at libultra calls
// (and when interrupt-like events post messages while no thread holds the token), so game code never
// runs concurrently with other game code, matching the single-CPU assumptions in the decomp.
#include <ultra64.h>
#include <port_compat.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "port_platform.h"

static bool TraceOn() {
    static const bool on = getenv("PORT_TRACE") != nullptr;
    return on;
}
// PORT_TRACE=2 also records where a thread blocks: raw return addresses plus the image slide, resolved by
// tools/symbolize.sh (looks for lines starting with '@').
static void DumpStack(const char* what, int id) {
    static const bool deep = getenv("PORT_TRACE") && getenv("PORT_TRACE")[0] == '2';
    if (!deep) return;
    void* frames[12];
    int n = Port_Backtrace(frames, 12);
    fprintf(stderr, "@stack %s thread=%d slide=0x%lx:", what, id, (unsigned long)Port_ImageSlide());
    for (int i = 2; i < n; i++) fprintf(stderr, " %p", frames[i]);
    fputc('\n', stderr);
}
extern "C" void port_NotifyMaybeIdle(void); // os_devices.cpp: wakes the lockstep clock

#define TRACE(...) do { if (TraceOn()) { fprintf(stderr, "[os] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

namespace {

enum class State { Stopped, Runnable, Waiting };

struct Thread {
    OSThread* os = nullptr;
    OSId id = 0;
    int pri = 0;
    State state = State::Stopped;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    std::thread host;
    bool hostStarted = false;
    bool dead = false;
    uint64_t seq = 0;          // FIFO order among equal priorities
    std::condition_variable cv;
    OSMesgQueue* waitingOn = nullptr;
    bool waitForRecv = false;
    void* blockedAt[16] = {};   // where the thread last blocked (for the stall report)
    int blockedDepth = 0;
    uint64_t blockCount = 0;    // how often it blocked (rising between stall reports = it is still looping)
};

struct QueueWaiters {
    std::deque<Thread*> recv;
    std::deque<Thread*> send;
};

std::mutex gLock;
std::unordered_map<OSThread*, Thread*> gThreads;
std::unordered_map<OSMesgQueue*, QueueWaiters> gWaiters;
std::unordered_map<OSMesgQueue*, std::vector<void*>> gQueueStorage;
Thread* gCurrent = nullptr;  // token holder; nullptr when every thread is blocked
uint64_t gSeq = 0;
thread_local Thread* tlsSelf = nullptr;
bool gShuttingDown = false;

Thread* Lookup(OSThread* t) {
    if (t == nullptr) {
        return tlsSelf;
    }
    auto it = gThreads.find(t);
    return it == gThreads.end() ? nullptr : it->second;
}

Thread* PickBest() {
    Thread* best = nullptr;
    for (auto& kv : gThreads) {
        Thread* t = kv.second;
        if (t->state != State::Runnable || t->dead) {
            continue;
        }
        if (best == nullptr || t->pri > best->pri || (t->pri == best->pri && t->seq < best->seq)) {
            best = t;
        }
    }
    return best;
}

// Gives the token to the best runnable thread. 'force' also takes it away from a thread that is still
// runnable (used at yield points); otherwise a running thread keeps it until its next libultra call.
void Reschedule(bool force) {
    Thread* best = PickBest();
    if (gCurrent != nullptr && gCurrent->state == State::Runnable && !gCurrent->dead) {
        if (!force || best == nullptr || best->pri <= gCurrent->pri) {
            return;
        }
    }
    gCurrent = best;
    if (best != nullptr) {
        best->cv.notify_all();
    } else {
        port_NotifyMaybeIdle(); // every game thread is blocked: the lockstep clock may go on (os_devices.cpp)
    }
}

// Called with gLock held by the thread 'me' after it changed scheduler state.
void WaitForToken(std::unique_lock<std::mutex>& lk, Thread* me) {
    Reschedule(true);
    while (gCurrent != me && !gShuttingDown) {
        me->cv.wait(lk);
    }
}

void ThreadMain(Thread* t) {
    tlsSelf = t;
    Port_AltSignalStack();
    std::unique_lock<std::mutex> lk(gLock);
    while (gCurrent != t && !gShuttingDown) {
        t->cv.wait(lk);
    }
    if (gShuttingDown) {
        return;
    }
    lk.unlock();
    t->entry(t->arg);
    lk.lock();
    t->dead = true;
    t->state = State::Stopped;
    if (gCurrent == t) {
        gCurrent = nullptr;
    }
    Reschedule(false);
}

void WakeOne(std::deque<Thread*>& q) {
    if (q.empty()) {
        return;
    }
    auto best = q.begin();
    for (auto it = q.begin(); it != q.end(); ++it) {
        if ((*it)->pri > (*best)->pri) {
            best = it;
        }
    }
    Thread* t = *best;
    q.erase(best);
    t->state = State::Runnable;
    t->seq = ++gSeq;
    t->waitingOn = nullptr;
}

OSMesg* Buf(OSMesgQueue* mq) {
    return reinterpret_cast<OSMesg*>(mq->msg);
}

bool PushLocked(OSMesgQueue* mq, OSMesg msg, bool jam) {
    if (mq->validCount >= mq->msgCount) {
        return false;
    }
    if (jam) {
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        Buf(mq)[mq->first] = msg;
    } else {
        Buf(mq)[(mq->first + mq->validCount) % mq->msgCount] = msg;
    }
    mq->validCount++;
    WakeOne(gWaiters[mq].recv);
    return true;
}

} // namespace

extern "C" {

// ---- Threads -----------------------------------------------------------------------------------

void osCreateThread(OSThread* t, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    std::lock_guard<std::mutex> g(gLock);
    auto* th = new Thread();
    th->os = t;
    th->id = id;
    th->pri = pri;
    th->entry = entry;
    th->arg = arg;
    t->priority = pri;
    t->id = id;
    t->state = OS_STATE_STOPPED;
    gThreads[t] = th;
    TRACE("create thread id=%d pri=%d", (int)id, (int)pri);
}

void osStartThread(OSThread* t) {
    std::unique_lock<std::mutex> lk(gLock);
    Thread* th = Lookup(t);
    if (th == nullptr || th->dead) {
        return;
    }
    TRACE("start thread id=%d (from %d)", (int)th->id, tlsSelf ? (int)tlsSelf->id : -1);
    th->state = State::Runnable;
    th->seq = ++gSeq;
    if (!th->hostStarted) {
        th->hostStarted = true;
        th->host = std::thread(ThreadMain, th);
        th->host.detach();
    }
    if (tlsSelf != nullptr) {
        WaitForToken(lk, tlsSelf);
    } else {
        Reschedule(false);
    }
}

void osStopThread(OSThread* t) {
    std::unique_lock<std::mutex> lk(gLock);
    Thread* th = Lookup(t);
    if (th == nullptr) {
        return;
    }
    th->state = State::Stopped;
    if (th == tlsSelf) {
        WaitForToken(lk, th);
    } else if (gCurrent == th) {
        gCurrent = nullptr;
        Reschedule(false);
    }
}

void osDestroyThread(OSThread* t) {
    std::unique_lock<std::mutex> lk(gLock);
    Thread* th = Lookup(t);
    if (th == nullptr) {
        return;
    }
    th->dead = true;
    th->state = State::Stopped;
    if (gCurrent == th) {
        gCurrent = nullptr;
        Reschedule(false);
    }
    // A thread destroying itself (osDestroyThread(NULL), e.g. the GB Tower's Game Boy Pak service thread when the
    // emulator closes) never returns, as on the N64. It used to: the thread then ran on with its message (NULL) and
    // crashed the game. A dead thread is never scheduled again, so its host thread just waits for the process to end.
    if (th == tlsSelf) {
        for (;;) {
            th->cv.wait(lk);
        }
    }
}

OSPri osGetThreadPri(OSThread* t) {
    std::lock_guard<std::mutex> g(gLock);
    Thread* th = Lookup(t);
    return th ? th->pri : 0;
}

void osSetThreadPri(OSThread* t, OSPri pri) {
    std::unique_lock<std::mutex> lk(gLock);
    Thread* th = Lookup(t);
    if (th == nullptr) {
        return;
    }
    th->pri = pri;
    if (th->os) {
        th->os->priority = pri;
    }
    if (th == tlsSelf && pri == OS_PRIORITY_IDLE && th->id == 1) {
        // The original idle thread drops to priority 0 and spins forever. Park it so it cannot hold the
        // token while everything else waits.
        th->state = State::Stopped;
        WaitForToken(lk, th);
        while (!gShuttingDown) {
            th->cv.wait(lk);
        }
        return;
    }
    if (th == tlsSelf) {
        WaitForToken(lk, th);
    } else {
        Reschedule(false);
    }
}

OSThread* __osGetActiveQueue(void) {
    return nullptr;
}

OSId osGetThreadId(OSThread* t) {
    std::lock_guard<std::mutex> g(gLock);
    Thread* th = Lookup(t);
    return th ? th->id : 0;
}

void osYieldThread(void) {
    std::unique_lock<std::mutex> lk(gLock);
    if (tlsSelf == nullptr) {
        return;
    }
    tlsSelf->seq = ++gSeq;
    gCurrent = nullptr;
    WaitForToken(lk, tlsSelf);
}

// ---- Message queues ----------------------------------------------------------------------------

void port_osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count) {
    std::lock_guard<std::mutex> g(gLock);
    mq->mtqueue = nullptr;
    mq->fullqueue = nullptr;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    gWaiters.erase(mq);
    if (count > 1) {
        // The game sizes message buffers for 4-byte OSMesg slots; the host's slots are 8 bytes, so a caller-supplied
        // buffer for several messages would overflow into its neighbours. Own the storage here (keyed by queue
        // address so queues that are re-created on the stack reuse it).
        auto& store = gQueueStorage[mq];
        store.assign((size_t)count, nullptr);
        mq->msg = reinterpret_cast<LUS_OSMesg*>(store.data());
    } else {
        mq->msg = reinterpret_cast<LUS_OSMesg*>(msgBuf);
    }
}

s32 port_osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    std::unique_lock<std::mutex> lk(gLock);
    while (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_NOBLOCK || tlsSelf == nullptr) {
            return -1;
        }
        tlsSelf->state = State::Waiting;
        tlsSelf->waitingOn = mq;
        gWaiters[mq].send.push_back(tlsSelf);
        WaitForToken(lk, tlsSelf);
    }
    static const bool traceOps = getenv("PORT_TRACE_OPS") != nullptr;
    if (traceOps) fprintf(stderr, "[op] t%d send %p\n", tlsSelf ? (int)tlsSelf->id : -1, (void*)mq);
    PushLocked(mq, msg, false);
    if (tlsSelf != nullptr) {
        WaitForToken(lk, tlsSelf);
    } else {
        Reschedule(false);
    }
    return 0;
}

s32 port_osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    std::unique_lock<std::mutex> lk(gLock);
    while (mq->validCount >= mq->msgCount) {
        if (flag == OS_MESG_NOBLOCK || tlsSelf == nullptr) {
            return -1;
        }
        tlsSelf->state = State::Waiting;
        tlsSelf->waitingOn = mq;
        gWaiters[mq].send.push_back(tlsSelf);
        WaitForToken(lk, tlsSelf);
    }
    PushLocked(mq, msg, true);
    if (tlsSelf != nullptr) {
        WaitForToken(lk, tlsSelf);
    } else {
        Reschedule(false);
    }
    return 0;
}

s32 port_osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    std::unique_lock<std::mutex> lk(gLock);
    while (mq->validCount == 0) {
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        if (tlsSelf == nullptr) {
            // Boot context blocking on a queue: poll, nothing else can make progress without the token.
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            lk.lock();
            continue;
        }
        TRACE("thread %d blocks on recv %p", (int)tlsSelf->id, (void*)mq);
        tlsSelf->blockedDepth = Port_BacktraceFast(tlsSelf->blockedAt, 16);
        tlsSelf->blockCount++;
        DumpStack("recv", (int)tlsSelf->id);
        tlsSelf->state = State::Waiting;
        tlsSelf->waitingOn = mq;
        gWaiters[mq].recv.push_back(tlsSelf);
        WaitForToken(lk, tlsSelf);
    }
    static const bool traceOps = getenv("PORT_TRACE_OPS") != nullptr;
    if (traceOps) fprintf(stderr, "[op] t%d recv %p\n", tlsSelf ? (int)tlsSelf->id : -1, (void*)mq);
    if (msg != nullptr) {
        *msg = Buf(mq)[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    WakeOne(gWaiters[mq].send);
    if (tlsSelf != nullptr) {
        WaitForToken(lk, tlsSelf);
    } else {
        Reschedule(false);
    }
    return 0;
}

s32 port_osRecvMesgSz(OSMesgQueue* mq, void* msg, s32 flag, int bytes) {
    if (msg == nullptr || bytes >= 8) {
        return port_osRecvMesg(mq, static_cast<OSMesg*>(msg), flag);
    }
    OSMesg tmp = nullptr;
    s32 r = port_osRecvMesg(mq, &tmp, flag);
    if (r == 0) {
        uintptr_t v = reinterpret_cast<uintptr_t>(tmp);
        switch (bytes) {
            case 4: *static_cast<uint32_t*>(msg) = (uint32_t)v; break;
            case 2: *static_cast<uint16_t*>(msg) = (uint16_t)v; break;
            default: *static_cast<uint8_t*>(msg) = (uint8_t)v; break;
        }
    }
    return r;
}

// Wakes a thread waiting to receive from 'mq' after a message was placed there by code that bypassed
// port_osSendMesg (libultraship's controller functions).
void port_WakeQueue(OSMesgQueue* mq) {
    std::lock_guard<std::mutex> g(gLock);
    if (mq->validCount > 0) {
        WakeOne(gWaiters[mq].recv);
    }
    Reschedule(false);
}

// Posts a message from interrupt-like host code (VI timer, RSP/RDP completion, DMA completion).
// Never blocks; a full queue drops the message like a real interrupt handler would.
void port_PostMesgFromIrq(OSMesgQueue* mq, OSMesg msg) {
    if (mq == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> g(gLock);
    PushLocked(mq, msg, false);
    Reschedule(false);
}

// ---- Event registration (osSetEventMesg / VI) ---------------------------------------------------

void port_osSetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {
    std::lock_guard<std::mutex> g(gLock);
    __osEventStateTab[event].queue = mq;
    __osEventStateTab[event].msg = *reinterpret_cast<LUS_OSMesg*>(&msg);
}

void port_PostEvent(OSEvent event) {
    OSMesgQueue* mq;
    OSMesg msg;
    {
        std::lock_guard<std::mutex> g(gLock);
        mq = __osEventStateTab[event].queue;
        msg = *reinterpret_cast<OSMesg*>(&__osEventStateTab[event].msg);
    }
    port_PostMesgFromIrq(mq, msg);
}

// True when no game thread holds or wants the token: everything is blocked until the next interrupt-like event.
// The lockstep clock only advances virtual time in this state, which makes runs deterministic.
int port_ThreadsIdle(void) {
    std::lock_guard<std::mutex> g(gLock);
    return gCurrent == nullptr && PickBest() == nullptr;
}

void port_DumpThreads(void) {
    std::lock_guard<std::mutex> g(gLock);
    fprintf(stderr, "[threads] token: %d\n", gCurrent ? (int)gCurrent->id : -1);
    for (auto& kv : gThreads) {
        Thread* t = kv.second;
        fprintf(stderr, "  id=%d pri=%d state=%d dead=%d waitingOn=%p", (int)t->id, t->pri, (int)t->state, (int)t->dead,
                (void*)t->waitingOn);
        if (t->waitingOn) fprintf(stderr, " (queue %d/%d)", (int)t->waitingOn->validCount, (int)t->waitingOn->msgCount);
        fprintf(stderr, " blocks=%llu", (unsigned long long)t->blockCount);
        fputc('\n', stderr);
        if (t->state == State::Waiting) {
            fprintf(stderr, "@stack thread=%d slide=0x%lx:", (int)t->id, (unsigned long)Port_ImageSlide());
            for (int i = 2; i < t->blockedDepth; i++) fprintf(stderr, " %p", t->blockedAt[i]);
            fputc('\n', stderr);
        }
    }
}

void port_ShutdownThreads(void) {
    std::lock_guard<std::mutex> g(gLock);
    gShuttingDown = true;
    for (auto& kv : gThreads) {
        kv.second->cv.notify_all();
    }
}

} // extern "C"
