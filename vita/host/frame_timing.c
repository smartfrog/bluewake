#define _POSIX_C_SOURCE 200809L
#include "boot_host.h"

#ifdef __vita__
#include <psp2/kernel/threadmgr/thread.h>
#else
#include <time.h>
#endif

static const char* wall_us(u64* us) {
#ifdef __vita__
    const SceInt64 now = sceKernelGetSystemTimeWide();
    if (now < 0)
        return "Vita system wall clock read failed";
    *us = (u64)now;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return "host monotonic wall clock read failed";
    if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000)
        return "host wall clock returned an invalid timestamp";
    const u64 fraction = (u64)now.tv_nsec / 1000u;
    if ((u64)now.tv_sec > (UINT64_MAX - fraction) / 1000000u)
        return "host wall microsecond timestamp overflow";
    *us = (u64)now.tv_sec * 1000000u + fraction;
#endif
    return NULL;
}

static const char* stamp(WwFrameStamp* series, u64 index, const char* format) {
    if (index == 0u || series->index == UINT64_MAX || index != series->index + 1u)
        return "frame marker requires consecutive actual event indices";
    u64 us;
    const char* error = wall_us(&us);
    if (error != NULL)
        return error;
    if (series->index != 0u && us <= series->us)
        return "frame marker wall time did not advance";
    if (fprintf(stderr, format, (unsigned long long)index, (unsigned long long)us) < 0)
        return "frame marker trace write failed";
    series->index = index;
    series->us = us;
    return NULL;
}

const char* ww_host_frame_present(WwHost* host, u64 presented) {
    if (host == NULL)
        return "frame present marker requires a host";
    return stamp(&host->frame_timing.present, presented, "[frame-present] frame=%llu us=%llu\n");
}

const char* ww_host_frame_retrace(WwHost* host) {
    if (host == NULL)
        return "frame retrace marker requires a host";
    return stamp(&host->frame_timing.retrace, host->vi_retraces, "[frame-timing] retrace=%llu us=%llu\n");
}
