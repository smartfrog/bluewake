// cos_sdk_smoke: the "host_alloc" test. cos_sdk's own bookkeeping (the alarm queue, the thread
// side table, HostThread and launch records, adopted host threads) must never call the global
// operator new/delete: on PC those are the game's (JKRHeap.cpp), which allocate from the current
// JKRHeap and free under its OSMutex, and the alarm thread may not block on a mutex. This program
// replaces the global forms with counting ones; while the test runs alarms (one-shot, periodic,
// cancelled, cancelled by tag), creates, runs and joins an OS thread and lets a plain host thread
// adopt an OSThread, they must be called zero times.
//
// One allocation is not cos_sdk's: libstdc++'s __cxa_thread_atexit builds the per-thread TLS
// teardown node (three pointers: destructor, object, next) with the nothrow form of operator new,
// and the pthread key destructor frees it with the sized delete. GCC's emulated TLS initialises
// the OSThread.cpp block (tHostRef and tForeign) as a whole, so every created or adopted thread
// registers both destructors. Only a nothrow allocation of exactly 3 * sizeof(void*) is left out
// of the count, and only those pointers are recognised on free (CountedFree); a nothrow
// allocation of any other size is counted, and its delete is counted too, so a cos_sdk
// new (std::nothrow) still fails the test. The residual gap is a cos_sdk nothrow allocation of
// exactly three pointers: it would be taken for the runtime's node. The failure report names the
// caller of each counted call. Evidence: the Vita ELF's __cxa_thread_atexit calls
// _ZnwjRKSt9nothrow_t with 12 bytes (three 4-byte pointers, the 32-bit Vita), and the key
// destructor's run() calls _ZdlPvj.
//
// Not built into cos_sdk_smoke_tsan: ThreadSanitizer intercepts operator new/delete itself.
#include "smoke.h"

#include <dolphin/os.h>

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#if defined(__NEWLIB__)
#include <malloc.h> // memalign, where std::aligned_alloc is missing (E1, the Vita)
#endif
#include <new>
#include <thread>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define COS_SMOKE_TSAN 1
#endif
#endif

#ifndef COS_SMOKE_TSAN

#if defined(__GNUC__) || defined(__clang__)
#define COS_SMOKE_CALLER() __builtin_extract_return_addr(__builtin_return_address(0))
#elif defined(_MSC_VER)
#include <intrin.h>
#define COS_SMOKE_CALLER() _ReturnAddress()
#else
#define COS_SMOKE_CALLER() nullptr
#endif

namespace {

std::atomic<bool> sCounting{false};
std::atomic<long> sGlobalCalls{0};

// The caller of each counted call, for the failure report (at most kMaxSites lines).
constexpr int kMaxSites = 16;
std::atomic<void*> sSites[kMaxSites];
std::atomic<int> sSiteCount{0};

// The runtime's 3-pointer TLS teardown node: the only nothrow allocation left out of the count,
// and only its pointers are recognised on free.
constexpr int kMaxQuiet = 64;
std::atomic<void*> sQuiet[kMaxQuiet];

void Count(void* caller) {
    if (sCounting.load(std::memory_order_relaxed)) {
        sGlobalCalls.fetch_add(1, std::memory_order_relaxed);
        const int index = sSiteCount.fetch_add(1, std::memory_order_relaxed);
        if (index < kMaxSites) {
            sSites[index].store(caller, std::memory_order_relaxed);
        }
    }
}

bool RememberQuiet(void* p) {
    for (auto& slot : sQuiet) {
        void* expected = nullptr;
        if (slot.compare_exchange_strong(expected, p, std::memory_order_acq_rel)) {
            return true;
        }
    }
    return false;
}

bool TakeQuiet(void* p) {
    for (auto& slot : sQuiet) {
        if (slot.load(std::memory_order_acquire) == p) {
            slot.store(nullptr, std::memory_order_release);
            return true;
        }
    }
    return false;
}

void* RawAlloc(std::size_t size, std::size_t align) noexcept {
    if (size == 0) {
        size = 1;
    }
    if (align <= alignof(std::max_align_t)) {
        return std::malloc(size);
    }
#if defined(__NEWLIB__)
    // Newlib has no std::aligned_alloc (C11); memalign is the one that links, as host_alloc.h
    // does on this target.
    return memalign(align, (size + align - 1) & ~(align - 1));
#else
    return std::aligned_alloc(align, (size + align - 1) & ~(align - 1));
#endif
}

void* CountedAlloc(std::size_t size, std::size_t align, void* caller) {
    Count(caller);
    void* p = RawAlloc(size, align);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}

// The nothrow form: only libstdc++'s __cxa_thread_atexit node (three pointers) is left out of the
// count. Another size counts, and is not remembered, so its delete counts too: a cos_sdk
// new (std::nothrow) cannot hide. The caller is recorded for the failure report.
void* QuietAlloc(std::size_t size, std::size_t align, void* caller) noexcept {
    if (size != 3 * sizeof(void*)) {
        void* p = RawAlloc(size, align);
        if (p != nullptr) {
            Count(caller);
        }
        return p;
    }
    void* p = RawAlloc(size, align);
    if (p != nullptr && !RememberQuiet(p)) {
        Count(caller); // the table is full: fail loudly, with the caller, instead of hiding it
    }
    return p;
}

void CountedFree(void* p, void* caller) noexcept {
    if (p != nullptr) {
        if (!TakeQuiet(p)) {
            Count(caller);
        }
        std::free(p);
    }
}

} // namespace

void* operator new(std::size_t size) {
    return CountedAlloc(size, alignof(std::max_align_t), COS_SMOKE_CALLER());
}
void* operator new[](std::size_t size) {
    return CountedAlloc(size, alignof(std::max_align_t), COS_SMOKE_CALLER());
}
void* operator new(std::size_t size, std::align_val_t align) {
    return CountedAlloc(size, static_cast<std::size_t>(align), COS_SMOKE_CALLER());
}
void* operator new[](std::size_t size, std::align_val_t align) {
    return CountedAlloc(size, static_cast<std::size_t>(align), COS_SMOKE_CALLER());
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return QuietAlloc(size, alignof(std::max_align_t), COS_SMOKE_CALLER());
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return QuietAlloc(size, alignof(std::max_align_t), COS_SMOKE_CALLER());
}
// The matching deletes of a nothrow new-expression whose constructor throws: the test never
// constructs through the nothrow form, but naming them leaves no nothrow path uncounted.
void operator delete(void* p, const std::nothrow_t&) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete(void* p) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete[](void* p) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete(void* p, std::size_t) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete[](void* p, std::size_t) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete(void* p, std::align_val_t) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete[](void* p, std::align_val_t) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    CountedFree(p, COS_SMOKE_CALLER());
}

namespace {

constexpr u32 kStackSize = 16 * 1024;
alignas(32) u8 sWorkerStack[kStackSize];
OSThread sWorker;

OSMessageQueue sQueue;
OSMessage sSlots[16];

void SendTag(OSAlarm* alarm, OSContext*) {
    OSSendMessage(&sQueue, reinterpret_cast<void*>(static_cast<std::uintptr_t>(alarm->tag)),
                  OS_MESSAGE_NOBLOCK);
}

void Never(OSAlarm*, OSContext*) {
    std::abort();
}

void* Worker(void* param) {
    return param;
}

void* Foreign(void*) {
    // The first SDK call of a plain host thread adopts it (an OSThread of its own); its record is
    // dropped when the thread ends.
    return OSGetCurrentThread() != nullptr ? reinterpret_cast<void*>(1) : nullptr;
}

} // namespace

COS_SMOKE_TEST(host_alloc) {
    OSInitMessageQueue(&sQueue, sSlots, 16);
    OSMessage msg = nullptr;
    OSAlarm once, periodic, cancelled, tagged;

    sGlobalCalls.store(0);
    sSiteCount.store(0);
    sCounting.store(true);

    OSCreateAlarm(&once);
    OSCreateAlarm(&periodic);
    OSCreateAlarm(&cancelled);
    OSCreateAlarm(&tagged);
    OSSetAlarmTag(&once, 1);
    OSSetAlarmTag(&periodic, 2);
    OSSetAlarmTag(&tagged, 0x77);
    OSSetAlarm(&cancelled, OSMillisecondsToTicks(20), Never);
    OSSetAlarm(&tagged, OSMillisecondsToTicks(20), Never);
    OSSetAlarm(&once, OSMillisecondsToTicks(2), SendTag);
    OSSetPeriodicAlarm(&periodic, OSGetTime(), OSMillisecondsToTicks(2), SendTag);
    OSCancelAlarm(&cancelled);
    OSCancelAlarms(0x77);
    int onceSeen = 0, periodicSeen = 0;
    while (onceSeen < 1 || periodicSeen < 4) {
        COS_SMOKE_CHECK(OSReceiveMessage(&sQueue, &msg, OS_MESSAGE_BLOCK));
        (reinterpret_cast<std::uintptr_t>(msg) == 1 ? onceSeen : periodicSeen)++;
    }
    OSCancelAlarm(&periodic);

    COS_SMOKE_CHECK(OSCreateThread(&sWorker, Worker, reinterpret_cast<void*>(0x42),
                                   sWorkerStack + kStackSize, kStackSize, 16, 0));
    OSResumeThread(&sWorker);
    void* result = nullptr;
    COS_SMOKE_CHECK(OSJoinThread(&sWorker, &result));
    COS_SMOKE_CHECK(result == reinterpret_cast<void*>(0x42));

    pthread_t foreign;
    COS_SMOKE_CHECK(pthread_create(&foreign, nullptr, Foreign, nullptr) == 0);
    void* adopted = nullptr;
    COS_SMOKE_CHECK(pthread_join(foreign, &adopted) == 0);
    COS_SMOKE_CHECK(adopted != nullptr);

    // The worker's host thread drops its HostThread reference in its thread-local destructors,
    // after OSJoinThread has returned: give it time to end.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sCounting.store(false);
    const long calls = sGlobalCalls.load();
    if (calls != 0) {
        std::fprintf(stderr, "host_alloc: cos_sdk called the global operator new/delete %ld times\n",
                     calls);
        const int sites = sSiteCount.load(std::memory_order_relaxed);
        for (int i = 0; i < sites && i < kMaxSites; i++) {
            std::fprintf(stderr, "host_alloc: call site %d: %p\n", i,
                         sSites[i].load(std::memory_order_relaxed));
        }
        std::fprintf(stderr,
                     "host_alloc: addr2line -e <binary> <address> (non-PIE, e.g. the Vita: the "
                     "address is already link-time; a PIE host: subtract the load base)\n");
    }
    COS_SMOKE_CHECK(calls == 0);
    return true;
}

#endif // COS_SMOKE_TSAN
