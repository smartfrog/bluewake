/* The adopted guest MEM1 base and the out-of-line repair path
 * (guest_ram_guard.h). */
#include "guest_ram_guard.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

u8* g_bw_guest_ram;
u32 g_bw_guest_ram_size;
CPUState* g_bw_guest_cpu;

static BwGuestRamGuardStats g_bw_guest_ram_guard;

/* A corrupted CPUState would otherwise flood the log: #22's aftermath was
 * 36,341 invalid accesses at PC 0. The first reports carry the evidence; the
 * counters and the shutdown line carry the rest. */
#define BW_GUEST_RAM_GUARD_REPORTS 8ul
#define BW_GUEST_RAM_GUARD_REPORT_MASK 0xFFFul

static void bw_guest_ram_guard_note(const char* what, CPUState* cpu, const void* bad,
                                    const void* good) {
    g_bw_guest_ram_guard.reports++;
    if (g_bw_guest_ram_guard.reports <= BW_GUEST_RAM_GUARD_REPORTS ||
        (g_bw_guest_ram_guard.reports & BW_GUEST_RAM_GUARD_REPORT_MASK) == 0u)
        fprintf(stderr, "[ram-guard] pc=%08X bad=%p good=%p what=%s repairs=%lu redirects=%lu\n",
                cpu != NULL ? cpu->pc : 0u, bad, good, what,
                g_bw_guest_ram_guard.ram_repairs, g_bw_guest_ram_guard.cpu_redirects);
}

void bw_guest_ram_guard_adopt(CPUState* cpu) {
    const uintptr_t base = cpu != NULL ? (uintptr_t)cpu->ram : 0u;
    if (cpu == NULL || cpu->ram == NULL || (base & 7u) != 0u || cpu->ram_size < 4u ||
        base > UINTPTR_MAX - cpu->ram_size) {
        fprintf(stderr, "[ram-guard] adopt rejected cpu=%p ram=%p size=%u\n", (void*)cpu,
                cpu != NULL ? (void*)cpu->ram : NULL, cpu != NULL ? cpu->ram_size : 0u);
        abort();
    }
    g_bw_guest_cpu = cpu;
    g_bw_guest_ram = cpu->ram;
    g_bw_guest_ram_size = cpu->ram_size;
}

void bw_guest_ram_guard_forget(void) {
    g_bw_guest_cpu = NULL;
    g_bw_guest_ram = NULL;
    g_bw_guest_ram_size = 0u;
}

BwGuestRamGuardStats bw_guest_ram_guard_stats(void) {
    return g_bw_guest_ram_guard;
}

void bw_guest_ram_guard_report(FILE* out) {
    if (out == NULL)
        return;
    fprintf(out, "[ram-guard] repairs=%lu redirects=%lu reports=%lu\n",
            g_bw_guest_ram_guard.ram_repairs, g_bw_guest_ram_guard.cpu_redirects,
            g_bw_guest_ram_guard.reports);
}

CPUState* bw_guest_ram_guard_slow(CPUState* cpu) {
    if (cpu != g_bw_guest_cpu) {
        g_bw_guest_ram_guard.cpu_redirects++;
        bw_guest_ram_guard_note("ctx", g_bw_guest_cpu, cpu, g_bw_guest_cpu);
        cpu = g_bw_guest_cpu;
    }
    if (cpu != NULL && cpu->ram != g_bw_guest_ram) {
        u8* const bad = cpu->ram;
        g_bw_guest_ram_guard.ram_repairs++;
        bw_guest_ram_guard_note("ram", cpu, bad, g_bw_guest_ram);
        cpu->ram = g_bw_guest_ram;
        cpu->ram_size = g_bw_guest_ram_size;
    }
    return cpu;
}

#if defined(_WIN32)
#define BW_GUEST_RAM_GUARD_EXPORT __declspec(dllexport)
#else
#define BW_GUEST_RAM_GUARD_EXPORT __attribute__((visibility("default")))
#endif

/* The adopting entry point for a host that loads the module (runtime/host's
 * main.c resolves it by name; the Vita host calls the plain symbol above). */
BW_GUEST_RAM_GUARD_EXPORT void bluewake_composite_ram_guard_adopt(CPUState* cpu) {
    bw_guest_ram_guard_adopt(cpu);
}
