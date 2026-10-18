#ifndef BLUEWAKE_GUEST_RAM_GUARD_H
#define BLUEWAKE_GUEST_RAM_GUARD_H

/* Guest MEM1 at the address the host adopted after cpu_init.
 *
 * Trials #16 and #22 lost the guest CPU's own state at the title boundary:
 * #16 called the dispatch with a null CPUState, #22 had a corrupt MEM1 base
 * (CPUState+0xD64 = 0x0201F025, unaligned and outside every mapped region) and
 * the next guest store went to 0x020C1C75, which is cpu->ram + 0xA2C50 for the
 * guest store at 0x800A2A30. Both are host-state corruption: every guest value
 * in the dumps is ordinary (.data addresses, zero stored value).
 *
 * get_ram_ptr and the mem_* accessors read cpu->ram for every access, so a
 * write to that one field turns every later guest store into a wild host store.
 * The base adopted at init cannot be corrupted by the guest the same way: it
 * is a host global, not guest-addressable state. The chunks' memory helpers
 * therefore compare cpu->ram with it, repair the field when it differs, and
 * report the first occurrences, keeping the ordinary inline fast path for an
 * intact CPU. A corrupted CPU pointer (a stale or garbage CPUState) is
 * redirected to the adopted CPUState, which is the only guest CPU in this host.
 * Redirecting one access is not enough for the pointer itself: the chunk keeps
 * its CPUState in a register for the whole block and reaches pc, lr and every
 * register through it, so bw_guest_ctx_canonical runs the whole block on the
 * adopted state, once per dispatch.
 *
 * Compiled into the module only with BW_GUEST_RAM_GUARD_WRAP, which
 * cmake/composite/CMakeLists.txt adds as a forced include; the host includes
 * this header for the declarations alone. The wrappers are transparent until
 * bw_guest_ram_guard_adopt runs, so the host and the tests see the runtime's
 * own helpers unchanged.
 *
 * Not covered: native fast paths that read cpu->ram themselves (their
 * get_ram_ptr calls are wrapped, a cached u8* is not), the runtime's own
 * non-composite users of cpu->ram, and every access when the module is built
 * without BLUEWAKE_RAM_GUARD. The adopting host must call
 * bw_guest_ram_guard_adopt again whenever it replaces cpu->ram (runtime/host's
 * main.c does after its linked-REL expansion; the Vita host's base never
 * moves). */

/* The chunks' generated.h selects the runtime header this way; prepared
 * composites may copy it, so take the same route. */
#if !defined(DOLRECOMP_CPU_HEADER)
#define DOLRECOMP_CPU_HEADER "core/cpu.h"
#endif
#include DOLRECOMP_CPU_HEADER

extern u8* g_bw_guest_ram;
extern u32 g_bw_guest_ram_size;
extern CPUState* g_bw_guest_cpu;

typedef struct BwGuestRamGuardStats {
    unsigned long ram_repairs;
    unsigned long cpu_redirects;
    unsigned long ctx_canons;
    unsigned long reports;
} BwGuestRamGuardStats;

/* Record the guest MEM1 the host just created or resized. Aborts on a CPU, a
 * base or a size that cannot be a live allocation: at that point the process
 * could not run the guest correctly anyway. */
void bw_guest_ram_guard_adopt(CPUState* cpu);

/* Back to the transparent state (tests, and a host that frees its guest). */
void bw_guest_ram_guard_forget(void);

BwGuestRamGuardStats bw_guest_ram_guard_stats(void);
void bw_guest_ram_guard_report(FILE* out);

/* Out of line: the CPU pointer or the MEM1 base is not the adopted one. */
CPUState* bw_guest_ram_guard_slow(CPUState* cpu);

/* Out of line: the dispatcher was handed a CPUState that is not the adopted
 * one. Returns the adopted state, so the block, not just its memory accesses,
 * runs on the real CPU. */
CPUState* bw_guest_ctx_canon(CPUState* cpu, u32 address);

#if defined(BW_GUEST_RAM_GUARD_WRAP)

/* Trial #24 (build-f5/trial24-pc0-diag-72c50764): redirecting the mem_* argument
 * left the chunk's own CPUState in its callee-saved register, so pc, lr, the
 * gprs and the entry switch still read the bogus state and the run ended at
 * PC 0. The dispatcher therefore checks the pointer once, before it writes pc,
 * and runs the block on the adopted state when they differ. */
static inline __attribute__((always_inline)) CPUState* bw_guest_ctx_canonical(CPUState* cpu,
                                                                             u32 address) {
    if (__builtin_expect(g_bw_guest_cpu == NULL || g_bw_guest_cpu == cpu, 1))
        return cpu;
    return bw_guest_ctx_canon(cpu, address);
}

static inline __attribute__((always_inline)) CPUState* bw_guest_ram_guard_access(CPUState* cpu) {
    const bool intact = g_bw_guest_cpu == NULL ||
                        (g_bw_guest_cpu == cpu && cpu->ram == g_bw_guest_ram);
    if (__builtin_expect(intact, 1))
        return cpu;
    return bw_guest_ram_guard_slow(cpu);
}

static inline __attribute__((always_inline)) u8 bw_guest_mem_read8(CPUState* cpu, u32 addr) {
    return mem_read8(bw_guest_ram_guard_access(cpu), addr);
}

static inline __attribute__((always_inline)) u16 bw_guest_mem_read16(CPUState* cpu, u32 addr) {
    return mem_read16(bw_guest_ram_guard_access(cpu), addr);
}

static inline __attribute__((always_inline)) u32 bw_guest_mem_read32(CPUState* cpu, u32 addr) {
    return mem_read32(bw_guest_ram_guard_access(cpu), addr);
}

static inline __attribute__((always_inline)) u64 bw_guest_mem_read64(CPUState* cpu, u32 addr) {
    return mem_read64(bw_guest_ram_guard_access(cpu), addr);
}

static inline __attribute__((always_inline)) void bw_guest_mem_write8(CPUState* cpu, u32 addr, u8 value) {
    mem_write8(bw_guest_ram_guard_access(cpu), addr, value);
}

static inline __attribute__((always_inline)) void bw_guest_mem_write16(CPUState* cpu, u32 addr, u16 value) {
    mem_write16(bw_guest_ram_guard_access(cpu), addr, value);
}

static inline __attribute__((always_inline)) void bw_guest_mem_write32(CPUState* cpu, u32 addr, u32 value) {
    mem_write32(bw_guest_ram_guard_access(cpu), addr, value);
}

static inline __attribute__((always_inline)) void bw_guest_mem_write64(CPUState* cpu, u32 addr, u64 value) {
    mem_write64(bw_guest_ram_guard_access(cpu), addr, value);
}

static inline __attribute__((always_inline)) u8* bw_guest_get_ram_ptr(CPUState* cpu, u32 addr, u32 size,
                                                                     u32* out_offset) {
    return get_ram_ptr(bw_guest_ram_guard_access(cpu), addr, size, out_offset);
}

#define mem_read8 bw_guest_mem_read8
#define mem_read16 bw_guest_mem_read16
#define mem_read32 bw_guest_mem_read32
#define mem_read64 bw_guest_mem_read64
#define mem_write8 bw_guest_mem_write8
#define mem_write16 bw_guest_mem_write16
#define mem_write32 bw_guest_mem_write32
#define mem_write64 bw_guest_mem_write64
#define get_ram_ptr bw_guest_get_ram_ptr

#endif

#endif
