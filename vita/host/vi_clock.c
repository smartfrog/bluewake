#include "boot_host.h"

void ww_host_vi_advance(WwHost* host, u64 cycles) {
    if (!host->vi_clock_enabled || cycles == 0u)
        return;
    dol_vi_clock_advance(&host->vi_clock, cycles);
    while (dol_vi_clock_pop_retrace(&host->vi_clock, NULL)) {
        ++host->vi_retraces;
        dol_interrupts_assert_vi_retrace(&host->interrupts);
        const char* marker_error = ww_host_frame_retrace(host);
        if (marker_error != NULL) {
            ww_host_fail(host, marker_error, DOL_VI_BASE + DOL_VI_DI0_OFF, host->cpu.pc);
            return;
        }
        if (host->vi_retraces <= 16u &&
            fprintf(stderr, "[vi-retrace] retrace=%llu cycles=%llu cause=%08X mask=%08X vi_di0=%04X pc=%08X\n",
                    (unsigned long long)host->vi_retraces, (unsigned long long)host->cycles.absolute_cycles,
                    dol_interrupts_pi_cause(&host->interrupts), dol_interrupts_pi_mask(&host->interrupts),
                    (u16)dol_interrupts_mmio_read(&host->interrupts, DOL_VI_BASE + DOL_VI_DI0_OFF, 2u),
                    host->cpu.pc) < 0) {
            ww_host_fail(host, "VI retrace trace write failed", DOL_VI_BASE + DOL_VI_DI0_OFF, host->cpu.pc);
            return;
        }
    }
}
