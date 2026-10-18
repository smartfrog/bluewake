/* The adopted guest MEM1 base: ordinary accesses unchanged, a corrupted base
 * or CPU pointer repaired and reported (guest_ram_guard.h). */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guest_ram_guard.h"

/* Covers the #22 store offset 0xA2C50 and a spare block above it. */
#define RAM_BYTES 0x100000u

/* The dispatcher's report is read back from here; stderr is redirected last so
 * that nothing else in this test writes into it. */
static const char* const kCtxLogPath = "bluewake-ctx-canon.log";

static unsigned external_writes;
static unsigned journal_calls;
static u32 journal_offset;
static u32 journal_size;

static void external_write(CPUState* cpu, u32 addr, u64 value, u8 size) {
    (void)cpu;
    (void)addr;
    (void)value;
    (void)size;
    external_writes++;
}

static void journal(u32 offset, u32 size, void* user) {
    (void)user;
    journal_calls++;
    journal_offset = offset;
    journal_size = size;
}

typedef struct {
    u32 address;
    u32 size;
    u64 value;
} Access;

static u64 apply(CPUState* cpu, const Access* access) {
    switch (access->size) {
    case 1:
        mem_write8(cpu, access->address, (u8)access->value);
        return mem_read8(cpu, access->address);
    case 2:
        mem_write16(cpu, access->address, (u16)access->value);
        return mem_read16(cpu, access->address);
    case 4:
        mem_write32(cpu, access->address, (u32)access->value);
        return mem_read32(cpu, access->address);
    default:
        mem_write64(cpu, access->address, access->value);
        return mem_read64(cpu, access->address);
    }
}

int main(void) {
    u8* const ram = (u8*)calloc(1, RAM_BYTES);
    u8* const reference_ram = (u8*)calloc(1, RAM_BYTES);
    assert(ram != NULL && reference_ram != NULL);

    CPUState cpu = {0};
    cpu.ram = ram;
    cpu.ram_size = RAM_BYTES;
    cpu.external_write = external_write;

    /* Not adopted yet: the wrappers are the runtime's own helpers. */
    mem_write32(&cpu, GC_RAM_BASE + 4u, 0x11223344u);
    assert(memcmp(ram + 4, "\x11\x22\x33\x44", 4) == 0);
    mem_write32(&cpu, 0xCC008000u, 1u);
    assert(external_writes == 1);
    BwGuestRamGuardStats stats = bw_guest_ram_guard_stats();
    assert(stats.ram_repairs == 0u && stats.cpu_redirects == 0u && stats.reports == 0u);
    /* ... and the dispatcher keeps the pointer it is given while no guest has
     * been adopted, so the module still works as a plain library. */
    CPUState* const stranger = (CPUState*)(uintptr_t)0x800A29C4u;
    assert(bw_guest_ctx_canonical(stranger, 0x800A16E0u) == stranger);
    assert(bw_guest_ram_guard_stats().ctx_canons == 0u);

    /* An intact CPU keeps the ordinary path: big-endian bytes, journal offset,
     * read-back, and nothing reported. */
    bw_guest_ram_guard_adopt(&cpu);
    assert(g_bw_guest_ram == ram && g_bw_guest_ram_size == RAM_BYTES &&
           g_bw_guest_cpu == &cpu);
    g_mem_write_journal = journal;
    mem_write32(&cpu, GC_RAM_BASE + 0x20u, 0xDEADBEEFu);
    g_mem_write_journal = NULL;
    assert(journal_calls == 1u && journal_offset == 0x20u && journal_size == 4u);
    assert(memcmp(ram + 0x20, "\xDE\xAD\xBE\xEF", 4) == 0);
    assert(mem_read32(&cpu, GC_RAM_BASE + 0x20u) == 0xDEADBEEFu);
    stats = bw_guest_ram_guard_stats();
    assert(stats.ram_repairs == 0u && stats.cpu_redirects == 0u && stats.reports == 0u);

    /* The #22 base is unaligned and outside every mapped region. The store at
     * guest 0x800A2A30 lands at the adopted base's 0xA2C50, and the field is
     * put back before the access. */
    ram[0xA2C50] = 0xFF;
    cpu.ram = (u8*)(uintptr_t)0x0201F025u;
    cpu.ram_size = 0x18u;
    mem_write32(&cpu, GC_RAM_BASE + 0xA2C50u, 0u);
    assert(cpu.ram == ram && cpu.ram_size == RAM_BYTES);
    assert(ram[0xA2C50] == 0x00u);
    stats = bw_guest_ram_guard_stats();
    assert(stats.ram_repairs == 1u && stats.reports == 1u && stats.cpu_redirects == 0u);

    /* Aligned but wrong is repaired too: the adopted base is authoritative,
     * this is not an alignment check. */
    cpu.ram = (u8*)(uintptr_t)0x10000000u;
    cpu.ram_size = 0x1000u;
    mem_write8(&cpu, GC_RAM_BASE + 3u, 0x5Au);
    assert(ram[3] == 0x5Au && cpu.ram == ram && cpu.ram_size == RAM_BYTES);
    assert(bw_guest_ram_guard_stats().ram_repairs == 2u);

    /* A corrupted base does not turn a device access into a RAM store. */
    cpu.ram = (u8*)(uintptr_t)0x0201F025u;
    const unsigned external_before = external_writes;
    mem_write32(&cpu, 0xCC008000u, 0xFFFF0000u);
    assert(external_writes == external_before + 1u);
    assert(cpu.ram == ram && bw_guest_ram_guard_stats().ram_repairs == 3u);

    /* An unaligned guest address keeps the runtime's own handling. */
    mem_write32(&cpu, GC_RAM_BASE + 0x101u, 0xAABBCCDDu);
    assert(ram[0x101] == 0xAAu && ram[0x104] == 0xDDu);

    /* A null or stale CPU pointer is redirected to the adopted CPUState: the
     * #16 null-ctx and #22 foreign-pointer readings. */
    cpu.ram = ram;
    cpu.ram_size = RAM_BYTES;
    const unsigned redirects_before = bw_guest_ram_guard_stats().cpu_redirects;
    mem_write16(NULL, GC_RAM_BASE + 0x30u, 0xBEEFu);
    mem_write16((CPUState*)(uintptr_t)0x800A2C50u, GC_RAM_BASE + 0x32u, 0x1234u);
    assert(memcmp(ram + 0x30, "\xBE\xEF\x12\x34", 4) == 0);
    assert(bw_guest_ram_guard_stats().cpu_redirects == redirects_before + 2u);

    /* Direct users of the helper see the repaired base as well. */
    cpu.ram = (u8*)(uintptr_t)0x0201F025u;
    u32 offset = 0u;
    u8* resolved = get_ram_ptr(&cpu, GC_RAM_BASE + 0x40u, 4u, &offset);
    assert(resolved == ram + 0x40u && offset == 0x40u && cpu.ram == ram);

    /* No behaviour change on valid input: the same accesses produce the same
     * bytes and the same values through the adopted CPU and through a CPU the
     * guard does not know. */
    static const Access accesses[] = {
        {GC_RAM_BASE + 0x80u, 1u, 0xA5u},
        {GC_RAM_BASE + 0x82u, 2u, 0xBEEFu},
        {GC_RAM_BASE + 0x84u, 4u, 0xDEADBEEFu},
        {GC_RAM_BASE + 0x88u, 8u, 0x0123456789ABCDEFull},
        {GC_RAM_BASE + 0x90u, 4u, 0x00000000u},
        {GC_RAM_BASE + 0xFFFCu, 4u, 0xFFFFFFFFu},
        {0xCC008000u, 4u, 0xCAFEBABEu},
        {0xCC002000u, 2u, 0x1234u},
    };
    const unsigned count = (unsigned)(sizeof accesses / sizeof accesses[0]);
    u64 guarded_values[sizeof accesses / sizeof accesses[0]];
    memset(ram, 0, RAM_BYTES);
    memset(reference_ram, 0, RAM_BYTES);
    const unsigned repairs_before = bw_guest_ram_guard_stats().ram_repairs;
    const unsigned writes_before = external_writes;
    for (unsigned i = 0; i < count; ++i)
        guarded_values[i] = apply(&cpu, &accesses[i]);
    stats = bw_guest_ram_guard_stats();
    assert(stats.ram_repairs == repairs_before && stats.cpu_redirects == 2u);
    const unsigned guarded_writes = external_writes - writes_before;

    CPUState reference = {0};
    reference.ram = reference_ram;
    reference.ram_size = RAM_BYTES;
    reference.external_write = external_write;
    bw_guest_ram_guard_forget();
    external_writes = writes_before;
    for (unsigned i = 0; i < count; ++i)
        assert(apply(&reference, &accesses[i]) == guarded_values[i]);
    assert(external_writes - writes_before == guarded_writes);
    assert(memcmp(ram, reference_ram, RAM_BYTES) == 0);

    /* The dispatcher's fast path: the adopted CPU is returned untouched and
     * nothing is counted or logged. Nothing may be adopted beyond this point:
     * the last block redirects stderr and reads the report back. */
    bw_guest_ram_guard_adopt(&cpu);
    cpu.pc = 0x800A16E0u;
    cpu.lr = 0x800A29C4u;
    const unsigned canons_before = bw_guest_ram_guard_stats().ctx_canons;
    const unsigned reports_before = bw_guest_ram_guard_stats().reports;
    const unsigned redirects_before_canon = bw_guest_ram_guard_stats().cpu_redirects;
    assert(bw_guest_ctx_canonical(&cpu, 0x800A16E0u) == &cpu);
    assert(bw_guest_ram_guard_stats().ctx_canons == canons_before);

    /* Trial #24's pointer: the block must run on the adopted state, so that the
     * dispatch writes pc, the gprs and the return address to the real CPU. The
     * first substitution is reported once, with both pointers, the guest pc it
     * was asked to run and the module return address. */
    assert(freopen(kCtxLogPath, "w", stderr) != NULL);
    CPUState* const bogus = (CPUState*)(uintptr_t)0x800A29C4u;
    assert(bw_guest_ctx_canonical(bogus, 0x800A16E0u) == &cpu);
    assert(bw_guest_ctx_canonical(bogus, 0x800A16E4u) == &cpu);
    fflush(stderr);
    FILE* log = fopen(kCtxLogPath, "r");
    assert(log != NULL);
    static char text[4096];
    const size_t size = fread(text, 1, sizeof text - 1, log);
    text[size] = '\0';
    fclose(log);
    remove(kCtxLogPath);
    const char* const report = strstr(text, "[ctx-canon]");
    assert(report != NULL && strstr(report + 1, "[ctx-canon]") == NULL);
    assert(strstr(report, "address=800A16E0 bad=") != NULL);
    assert(strstr(report, " good=") != NULL);
    assert(strstr(report, " pc=800A16E0 lr=800A29C4 caller=") != NULL);
    stats = bw_guest_ram_guard_stats();
    assert(stats.ctx_canons == canons_before + 2u);
    assert(stats.reports == reports_before && stats.cpu_redirects == redirects_before_canon);

    bw_guest_ram_guard_forget();
    free(ram);
    free(reference_ram);
    puts("guest RAM guard contracts passed");
    return 0;
}
