#define _POSIX_C_SOURCE 200809L
#include "boot_host.h"

#ifdef __vita__
#include <psp2/kernel/threadmgr/thread.h>
#else
#include <time.h>
#endif

const char* ww_host_wall_us(u64* us) {
    if (us == NULL)
        return "wall clock requires an output";
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
    const char* error = ww_host_wall_us(&us);
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
    const u64 previous_us = host->frame_timing.present.us;
    const char* error = stamp(&host->frame_timing.present, presented, "[frame-present] frame=%llu us=%llu\n");
    if (error != NULL)
        return error;
    const u64 now_us = host->frame_timing.present.us;
    WwFrameTiming* timing = &host->frame_timing;
    const u64 leap_cycles = ww_host_idle_leap_cycles();
    const u64 leap_wall_us = ww_host_idle_leap_wall_us();
    const u64 irq500 = ww_host_irq500_count();
    const u64 irq900 = ww_host_irq900_count();
    WwRendererStats stats = {0};
    if (host->renderer != NULL)
        ww_renderer_stats(host->renderer, &stats);
    if (previous_us != 0u && now_us > previous_us) {
        const u64 wall_us = now_us - previous_us;
        const u64 cycles = host->cycles.absolute_cycles - timing->work_cycles;
        const u64 leap_delta = leap_cycles - timing->work_leap_cycles;
        const u64 leap_wall_delta = leap_wall_us - timing->work_leap_wall_us;
        const u64 post_leap_cycles = cycles > leap_delta ? cycles - leap_delta : 0u;
        const u64 draws = stats.draws - timing->work_draws;
        const u64 gpu_finish = host->gpu_finish_calls - timing->work_gpu_finish_calls;
        const u64 present_us = host->renderer_present_us - timing->work_present_us;
        const u64 finish_us = host->renderer_finish_us - timing->work_finish_us;
        const u64 irq500_delta = irq500 - timing->work_irq500;
        const u64 irq900_delta = irq900 - timing->work_irq900;
        if (fprintf(stderr,
                    "[frame-work] frame=%llu wall_us=%llu cycles=%llu turns=%llu leap_cycles=%llu leap_wall_us=%llu post_leap_cycles=%llu draws=%llu gpu_finish=%llu present_us=%llu finish_us=%llu irq500=%llu irq900=%llu\n",
                    (unsigned long long)timing->present.index,
                    (unsigned long long)wall_us,
                    (unsigned long long)cycles,
                    (unsigned long long)(host->turns - timing->work_turns),
                    (unsigned long long)leap_delta,
                    (unsigned long long)leap_wall_delta,
                    (unsigned long long)post_leap_cycles,
                    (unsigned long long)draws,
                    (unsigned long long)gpu_finish,
                    (unsigned long long)present_us,
                    (unsigned long long)finish_us,
                    (unsigned long long)irq500_delta,
                    (unsigned long long)irq900_delta) < 0)
            return "frame work trace write failed";
    }
    timing->work_us = now_us;
    timing->work_cycles = host->cycles.absolute_cycles;
    timing->work_turns = host->turns;
    timing->work_leap_cycles = leap_cycles;
    timing->work_leap_wall_us = leap_wall_us;
    timing->work_draws = stats.draws;
    timing->work_gpu_finish_calls = host->gpu_finish_calls;
    timing->work_present_us = host->renderer_present_us;
    timing->work_finish_us = host->renderer_finish_us;
    timing->work_irq500 = irq500;
    timing->work_irq900 = irq900;
    timing->present_turns = host->turns;
    return NULL;
}

const char* ww_host_frame_retrace(WwHost* host) {
    if (host == NULL)
        return "frame retrace marker requires a host";
    return stamp(&host->frame_timing.retrace, host->vi_retraces, "[frame-timing] retrace=%llu us=%llu\n");
}
