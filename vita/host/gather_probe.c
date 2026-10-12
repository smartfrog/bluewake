#include "probe_support.h"
#include "gxruntime/guest_memory_dirty.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && strncmp(format, "[gather-burst]", 14u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static u64 byte_sequence(u8 first, u8 width) {
    u64 value = 0u;
    for (u8 index = 0u; index < width; ++index)
        value = (value << 8u) | (u8)(first + index);
    return value;
}

static void store(WwHost* host, u64 value, u8 width) {
    switch (width) {
    case 1u: mem_write8(&host->cpu, 0xCC008000u, (u8)value); break;
    case 2u: mem_write16(&host->cpu, 0xCC008000u, (u16)value); break;
    case 4u: mem_write32(&host->cpu, 0xCC008000u, (u32)value); break;
    case 8u: mem_write64(&host->cpu, 0xCC008000u, value); break;
    default: host->cpu.external_write(&host->cpu, 0xCC008000u, value, width); break;
    }
}

static void fill(WwHost* host, u8 count, u8 width) {
    for (u8 offset = 0u; offset < count; offset += width) {
        store(host, byte_sequence((u8)(0x80u + offset), width), width);
        assert(host->error == NULL);
    }
}

static void untouched(const WwHost* host) {
    assert(host->frontier.size == 0u && !host->frontier.complete && host->frontier.error == NULL);
    assert(host->gx.fifo.size == 0u && !host->edge_stopped);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    host->cpu.pc = 0x803212CCu;
    host->cpu.gpr[1] = 0x817FFF00u;
    const char* scenario = argv[1];
    const bool unlinked = strncmp(scenario, "unlinked-", 9u) == 0;
    ww_probe_fifo_setup(host, !unlinked);
    const DolInterrupts interrupts = host->interrupts;
    if (strcmp(scenario, "control") == 0) {
        assert(host->cpu.external_read(&host->cpu, 0xCC000002u, 2u) == 0x14u);
        host->cpu.external_write(&host->cpu, 0xCC000002u, 0x14u, 2u);
        assert(host->error == NULL && host->gather.size == 0u && host->gather.transfers == 0u);
        assert(host->pi_fifo_write_pointer == 0x1000u && host->cp_fifo_write_pointer == 0x1000u);
        assert(host->cp_fifo_distance == 0u && host->cpu.pc == 0x803212CCu);
    } else if (strncmp(scenario, "partial-", 8u) == 0) {
        const u8 width = (u8)strtoul(scenario + 8u, NULL, 0);
        fill(host, width, width);
        assert(host->gather.size == width && host->gather.transfers == 0u);
        for (u8 index = 0u; index < width; ++index)
            assert(host->gather.bytes[index] == (u8)(0x80u + index));
        assert(host->pi_fifo_write_pointer == 0x1000u && host->cp_fifo_write_pointer == 0x1000u);
        assert(host->cp_fifo_distance == 0u);
        for (u32 index = 0xFE0u; index < 0x10A0u; ++index)
            assert(host->cpu.ram[index] == 0xA5u);
    } else if (strncmp(scenario, "burst-", 6u) == 0 || strncmp(scenario, "unlinked-burst-", 15u) == 0) {
        const u8 width = (u8)strtoul(scenario + (unlinked ? 15u : 6u), NULL, 0);
        fill(host, 32u, width);
        for (u32 index = 0u; index < 32u; ++index)
            assert(host->cpu.ram[0x1000u + index] == (u8)(0x80u + index));
        assert(host->cpu.ram[0xFFFu] == 0xA5u && host->cpu.ram[0x1020u] == 0xA5u);
        assert(host->gather.size == 0u && host->gather.transfers == 1u);
        assert(host->pi_fifo_write_pointer == 0x1020u);
        assert(host->cp_fifo_write_pointer == (unlinked ? 0x1000u : 0x1020u));
        assert(host->cp_fifo_distance == (unlinked ? 0u : 32u));
        assert(host->cp_fifo_read_pointer == 0x1000u);
        assert(host->cpu.external_read(&host->cpu, 0xCC003014u, 4u) == 0x1020u);
        assert(host->cpu.external_read(&host->cpu, 0xCC000034u, 2u) == host->cp_fifo_write_pointer);
    } else if (strcmp(scenario, "spill") == 0) {
        fill(host, 31u, 1u);
        store(host, byte_sequence(0x9Fu, 8u), 8u);
        assert(host->error == NULL && host->gather.size == 7u && host->gather.transfers == 1u);
        for (u8 index = 0u; index < 32u; ++index)
            assert(host->cpu.ram[0x1000u + index] == (u8)(0x80u + index));
        for (u8 index = 0u; index < 7u; ++index)
            assert(host->gather.bytes[index] == (u8)(0xA0u + index));
        assert(host->pi_fifo_write_pointer == 0x1020u && host->cp_fifo_distance == 32u);
    } else if (strcmp(scenario, "wrap") == 0) {
        host->pi_fifo_write_pointer = host->cp_fifo_write_pointer = host->cp_fifo_read_pointer = 0x1060u;
        fill(host, 32u, 8u);
        assert(host->pi_fifo_write_pointer == 0x1000u && host->cp_fifo_write_pointer == 0x1000u);
        fill(host, 32u, 8u);
        for (u8 index = 0u; index < 32u; ++index) {
            assert(host->cpu.ram[0x1060u + index] == (u8)(0x80u + index));
            assert(host->cpu.ram[0x1000u + index] == (u8)(0x80u + index));
        }
        assert(host->pi_fifo_write_pointer == 0x1020u && host->cp_fifo_write_pointer == 0x1020u);
        assert(host->cp_fifo_distance == 64u && host->cp_fifo_read_pointer == 0x1060u);
    } else if (strcmp(scenario, "no-parse") == 0 || strcmp(scenario, "unlinked-no-parse") == 0) {
        store(host, 0x61u, 1u);
        store(host, 0x52123456u, 4u);
        untouched(host);
        for (u8 index = 5u; index < 32u; ++index)
            store(host, 0u, 1u);
        assert(host->error == NULL && host->gather.transfers == 1u);
        assert(host->cpu.ram[0x1000u] == 0x61u && read_be32(host->cpu.ram + 0x1001u) == 0x52123456u);
        assert(!host->gx.bp_valid[0x52u]);
    } else if (strcmp(scenario, "credit") == 0) {
        fill(host, 24u, 8u);
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount -= 256;
        store(host, byte_sequence(0x98u, 8u), 8u);
        assert(host->error == NULL && host->cycles.absolute_cycles == 256u);
        assert(host->cpu.downcount == 0 && host->cpu.cycle_budget == 0);
        assert(host->cp_fifo_distance == 32u && host->gather.transfers == 1u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
    } else if (strcmp(scenario, "dirty-reservation") == 0) {
        dol_guest_memory_dirty_reset();
        host->cpu.reserve_addr = 0xC0001004u;
        host->cpu.reserve_valid = true;
        fill(host, 24u, 8u);
        assert(host->cpu.reserve_valid);
        fill(host, 8u, 8u);
        u64 epoch;
        assert(!host->cpu.reserve_valid && dol_guest_memory_dirty_epoch(0x80001000u, 32u, &epoch) && epoch != 0u);
    } else if (strcmp(scenario, "sampling") == 0) {
        host->pi_fifo_end = host->cp_fifo_end = 0x9000u;
        for (u32 transfer = 1u; transfer <= 1001u; ++transfer) {
            fill(host, 32u, 8u);
            assert(host->gather.transfers == transfer && host->cp_fifo_distance == transfer * 32u);
        }
    } else {
        fill(host, 24u, 8u);
        if (strcmp(scenario, "pi-range") == 0)
            host->pi_fifo_end = 0xFE0u;
        else if (strcmp(scenario, "pi-pointer") == 0)
            host->pi_fifo_write_pointer = 0x1080u;
        else if (strcmp(scenario, "pi-alignment") == 0)
            host->pi_fifo_write_pointer = 0x1001u;
        else if (strcmp(scenario, "pi-mem1") == 0)
            host->pi_fifo_end = host->cpu.ram_size;
        else if (strcmp(scenario, "cp-base") == 0)
            host->cp_fifo_base = 0xFE0u;
        else if (strcmp(scenario, "cp-end") == 0)
            host->cp_fifo_end = 0x1080u;
        else if (strcmp(scenario, "cp-pointer") == 0)
            host->cp_fifo_write_pointer = 0x1020u;
        else if (strcmp(scenario, "capacity") == 0)
            host->cp_fifo_distance = 96u;
        else if (strcmp(scenario, "distance-overflow") == 0)
            host->cp_fifo_distance = UINT32_MAX;
        else if (strcmp(scenario, "alias") == 0 || strcmp(scenario, "partial-alias") == 0)
            assert(ppc_guest_alias_add(0x80001000u + (strcmp(scenario, "alias") == 0 ? 0u : 12u), 4u, NULL));
        else if (strcmp(scenario, "trace-failure") == 0)
            fail_trace = true;
        else if (strcmp(scenario, "counter-exhausted") == 0)
            host->gather.transfers = UINT64_MAX;
        else if (strcmp(scenario, "prior-failure") == 0)
            ww_host_fail(host, "prior failure", 0xCC000002u, host->cpu.pc);
        else
            assert(strncmp(scenario, "width-", 6u) == 0 || strncmp(scenario, "freeze-", 7u) == 0);
        const WwGatherPipe before = host->gather;
        const u32 pi = host->pi_fifo_write_pointer;
        const u32 cp = host->cp_fifo_write_pointer;
        const u32 distance = host->cp_fifo_distance;
        u32 address = 0xCC008000u;
        u64 value = byte_sequence(0x98u, 8u);
        u8 width = strncmp(scenario, "width-", 6u) == 0 ? (u8)strtoul(scenario + 6u, NULL, 0) : 8u;
        if (strncmp(scenario, "freeze-", 7u) == 0) {
            value = 0u;
            width = 2u;
            const char* reg = scenario + 7u;
            address = strcmp(reg, "distance-low") == 0 ? 0xCC000030u :
                      strcmp(reg, "distance-high") == 0 ? 0xCC000032u :
                      strcmp(reg, "rptr-high") == 0 ? 0xCC00003Au : 0xCC003014u;
            if (address == 0xCC003014u)
                width = 4u;
            host->cpu.external_write(&host->cpu, address, value, width);
        } else {
            store(host, value, width);
        }
        assert(host->error != NULL && host->error_pc == host->cpu.pc);
        if (strcmp(scenario, "prior-failure") == 0) {
            assert(host->error_address == 0xCC000002u && strcmp(host->error, "prior failure") == 0);
        } else {
            assert(host->error_address == address && host->error_width == width && host->error_value == value);
        }
        assert(memcmp(&before, &host->gather, sizeof(before)) == 0);
        assert(host->pi_fifo_write_pointer == pi && host->cp_fifo_write_pointer == cp && host->cp_fifo_distance == distance);
        for (u32 index = 0xFE0u; index < 0x10A0u; ++index)
            assert(host->cpu.ram[index] == 0xA5u);
        if (strcmp(scenario, "counter-exhausted") == 0)
            assert(strcmp(host->error, "gather burst counter exhausted") == 0);
        if (strcmp(scenario, "trace-failure") == 0)
            assert(strcmp(host->error, "gather burst trace write failed") == 0);
    }
    untouched(host);
    assert(memcmp(&interrupts, &host->interrupts, sizeof(interrupts)) == 0);
    if (strcmp(scenario, "credit") != 0)
        assert(host->cycles.absolute_cycles == 0u && host->cpu.timebase == 0u);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
