// P-A follow-up: the smallest gConfig and harness support pc_milestone.cpp needs on the Vita.
//
// pc_milestone.cpp is the harness's milestone printer (frame count, VI retrace count, elapsed ms)
// and the COS_MILESTONE exit. The PC harness supplies pc::Config gConfig, writef, elapsedMs and
// pc_exit from pc_harness.cpp/pc_crash.cpp; the Vita boot path has no harness, so this unit
// defines only those four, plus the "static-init" milestone request. Nothing else of the harness
// is linked (pc_copydate_loaded and pc_frame_tick are dropped by --gc-sections).
#include "pc/pc_harness.h"
#include "pc_internal.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {

const std::chrono::steady_clock::time_point sStart = std::chrono::steady_clock::now();

} // namespace

namespace pc {

Config gConfig;

uint64_t elapsedMs() {
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - sStart).count());
}

// The harness's writer (pc_crash.cpp): a fixed buffer and write(2), so it works before stdio is
// configured. The crash-handler unit is not linked on the Vita.
void writef(int fd, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    if (length <= 0) {
        return;
    }
    const size_t size = static_cast<size_t>(length) < sizeof buffer ? static_cast<size_t>(length)
                                                                   : sizeof buffer - 1;
    [[maybe_unused]] const ssize_t written = write(fd, buffer, size);
}

} // namespace pc

extern "C" {

// The harness's pc_exit without the perf flush and stdio locks: there are no other threads yet
// and no perf rows. _Exit, not exit: the game's static destructors assume a booted game.
__attribute__((noreturn)) void pc_exit(int code) {
    std::fflush(nullptr);
    std::_Exit(code);
}

__attribute__((noreturn)) void vitaMilestoneStaticInit() {
    pc::gConfig.milestone = "static-init";
    pc_milestone("static-init");
    std::_Exit(PC_EXIT_CHECK_FAILED);
}

} // extern "C"
