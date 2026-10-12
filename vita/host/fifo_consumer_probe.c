#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_consume_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_consume_trace && stream == stderr && strncmp(format, "[cp-consume]", 12u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static void enable(WwHost* host) {
    host->cpu.pc = 0x80321270u;
    mem_write16(&host->cpu, 0xCC000002u, 0x15u);
}

static u32 command_bytes(u8* bytes, const char* kind) {
    memset(bytes, 0, WW_FIFO_ORIGIN_BYTES);
    if (strcmp(kind, "bp") == 0) {
        bytes[0] = 0x61u;
        write_be32(bytes + 1u, 0x41123456u);
        return 5u;
    }
    if (strcmp(kind, "cp") == 0) {
        bytes[0] = 0x08u;
        bytes[1] = 0x30u;
        write_be32(bytes + 2u, 0x12345678u);
        return 6u;
    }
    bytes[0] = 0x10u;
    const u32 words = strcmp(kind, "xf-max") == 0 ? 255u : strcmp(kind, "xf-split") == 0 ? 9u : 2u;
    write_be32(bytes + 1u, ((words - 1u) << 16u) | (words == 2u ? 0x1018u : 0u));
    for (u32 index = 0u; index < words; ++index)
        write_be32(bytes + 5u + index * 4u, 0x3F800000u);
    return 5u + words * 4u;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    ww_probe_fifo_setup(host, true);
    host->cp_fifo_high_watermark = 96u;
    const DolInterrupts interrupts = host->interrupts;
    const char* scenario = argv[1];
    u8 bytes[WW_FIFO_ORIGIN_BYTES];
    if (strcmp(scenario, "empty") == 0 || strcmp(scenario, "credit") == 0) {
        const bool credit = strcmp(scenario, "credit") == 0;
        if (credit) {
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            host->cpu.downcount -= 256;
        }
        enable(host);
        assert(host->error == NULL && mem_read16(&host->cpu, 0xCC000002u) == 0x15u);
        assert(host->fifo_capture.read_bytes == 0u && host->fifo_capture.transfers == 0u);
        assert(host->cp_fifo_read_pointer == 0x1000u && host->cp_fifo_distance == 0u);
        assert(!host->frontier.complete && host->gx.fifo.size == 0u);
        assert(host->cycles.absolute_cycles == (credit ? 256u : 0u));
        if (credit) {
            assert(host->cpu.cycle_budget == 0 && host->cpu.timebase == 21u);
            assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
        }
    } else if (strncmp(scenario, "pipeline-", 9u) == 0) {
        const char* kind = scenario + 9u;
        char name[8] = {0};
        const char* width_text = strrchr(kind, '-');
        assert(width_text != NULL && (size_t)(width_text - kind) < sizeof(name));
        memcpy(name, kind, (size_t)(width_text - kind));
        const u8 width = (u8)strtoul(width_text + 1u, NULL, 0);
        const u32 required = command_bytes(bytes, name);
        const u32 total = (required + 31u) & ~31u;
        enable(host);
        assert(host->error == NULL);
        ww_probe_gather_bytes(host, bytes, 0u, total, width);
        assert(host->error == NULL && host->frontier.complete && host->frontier.size == required);
        assert(memcmp(host->frontier.bytes, bytes, required) == 0 && host->gx.fifo.size == required);
        assert(host->frontier.pc == 0x80001000u && host->frontier.sp == 0x817FFF00u);
        assert(host->frontier.completion_pc == 0x80001000u + ((required - 1u) / width) * 4u);
        assert(host->frontier.trailing_bytes == total - required);
        assert(host->fifo_capture.read_bytes == total && host->fifo_capture.transfers == total / 32u);
        const u32 next = 0x1000u + total % 128u;
        assert(host->cp_fifo_read_pointer == next && host->cp_fifo_write_pointer == next);
        assert(host->pi_fifo_write_pointer == next && host->cp_fifo_distance == 0u);
        assert(mem_read16(&host->cpu, 0xCC000038u) == next && mem_read16(&host->cpu, 0xCC00003Au) == 0u);
        assert(mem_read16(&host->cpu, 0xCC000030u) == 0u && mem_read16(&host->cpu, 0xCC000032u) == 0u);
        if (strcmp(name, "bp") == 0)
            assert(host->gx.bp_valid[0x41u] && host->gx.bp_regs[0x41u] == 0x123456u);
        else if (strcmp(name, "cp") == 0)
            assert(host->gx.matrix_index_a == 0x12345678u);
        else
            assert(host->gx.last_xf_count == (required - 5u) / 4u);
        assert(ww_host_edge(host, &host->cpu, host->cpu.pc) && host->edge_stopped);
    } else if (strcmp(scenario, "partial") == 0) {
        command_bytes(bytes, "bp");
        enable(host);
        assert(host->error == NULL);
        ww_probe_gather_bytes(host, bytes, 0u, 8u, 8u);
        assert(host->error == NULL && host->gather.size == 8u && !host->frontier.complete);
        assert(host->frontier.size == 0u && host->gx.fifo.size == 0u && host->fifo_capture.read_bytes == 0u);
        assert(host->cp_fifo_read_pointer == 0x1000u && host->cp_fifo_write_pointer == 0x1000u);
    } else if (strcmp(scenario, "spill") == 0) {
        command_bytes(bytes, "xf-split");
        write_be32(bytes + 1u, 0x00070000u);
        enable(host);
        assert(host->error == NULL);
        ww_probe_gather_bytes(host, bytes, 0u, 31u, 1u);
        host->cpu.pc = 0x80002000u;
        host->cpu.gpr[1] = 0x817FF000u;
        mem_write64(&host->cpu, 0xCC008000u, read_be64(bytes + 31u));
        assert(host->error == NULL && host->gather.size == 7u && host->frontier.size == 32u);
        assert(!host->frontier.complete && host->fifo_capture.read_bytes == 32u);
        ww_probe_gather_bytes(host, bytes, 39u, 64u, 1u);
        assert(host->error == NULL && host->frontier.complete && host->frontier.size == 37u);
        assert(memcmp(host->frontier.bytes, bytes, 37u) == 0);
        assert(host->frontier.pc == 0x80001000u && host->frontier.sp == 0x817FFF00u);
        assert(host->frontier.completion_pc == 0x80002000u && host->frontier.trailing_bytes == 27u);
        assert(host->fifo_capture.read_bytes == 64u && host->cp_fifo_distance == 0u);
        assert(host->cp_fifo_read_pointer == 0x1040u && host->cp_fifo_write_pointer == 0x1040u);
    } else if (strcmp(scenario, "wrap") == 0 || strcmp(scenario, "pause") == 0) {
        const bool wrap = strcmp(scenario, "wrap") == 0;
        const u32 required = command_bytes(bytes, "xf-split");
        if (wrap)
            host->pi_fifo_write_pointer = host->cp_fifo_write_pointer = host->cp_fifo_read_pointer = 0x1060u;
        enable(host);
        assert(host->error == NULL);
        ww_probe_gather_bytes(host, bytes, 0u, 32u, 1u);
        assert(host->error == NULL && host->frontier.size == 32u && !host->frontier.complete);
        const u32 prior = wrap ? 0x1000u : 0x1020u;
        assert(host->cp_fifo_read_pointer == prior && host->cp_fifo_distance == 0u);
        if (!wrap)
            mem_write16(&host->cpu, 0xCC000002u, 0x14u);
        ww_probe_gather_bytes(host, bytes, 32u, 64u, 1u);
        if (!wrap) {
            assert(!host->frontier.complete && host->cp_fifo_read_pointer == prior && host->cp_fifo_distance == 32u);
            enable(host);
        }
        assert(host->error == NULL && host->frontier.complete && host->frontier.size == required);
        assert(memcmp(host->frontier.bytes, bytes, required) == 0);
        assert(host->frontier.completion_pc == 0x80001000u + (required - 1u) * 4u);
        assert(host->cp_fifo_read_pointer == (wrap ? 0x1020u : 0x1040u) && host->cp_fifo_distance == 0u);
    } else {
        command_bytes(bytes, "bp");
        const bool two = strcmp(scenario, "queued-two") == 0;
        if (two)
            memcpy(bytes + 32u, bytes, 32u);
        if (strcmp(scenario, "unknown") == 0)
            bytes[0] = 0xFFu;
        if (strcmp(scenario, "bp-rejected") == 0)
            bytes[1] = 0x52u;
        if (strcmp(scenario, "xf-too-large") == 0) {
            bytes[0] = 0x10u;
            write_be32(bytes + 1u, 0x00FF0000u);
        }
        ww_probe_gather_bytes(host, bytes, 0u, two ? 64u : 32u, 1u);
        assert(host->error == NULL && !host->frontier.complete && host->cp_fifo_distance == (two ? 64u : 32u));
        assert(host->fifo_capture.count == (two ? 64u : 32u));
        if (strcmp(scenario, "memory-changed") == 0)
            host->cpu.ram[0x1000u] = 0xFFu;
        else if (strcmp(scenario, "alias") == 0 || strcmp(scenario, "partial-alias") == 0)
            assert(ppc_guest_alias_add(0x80001000u + (strcmp(scenario, "alias") == 0 ? 0u : 12u), 4u, NULL));
        else if (strcmp(scenario, "read-pointer") == 0)
            host->cp_fifo_read_pointer = 0x1020u;
        else if (strcmp(scenario, "distance") == 0)
            host->cp_fifo_distance = 31u;
        else if (strcmp(scenario, "missing-origin") == 0)
            host->fifo_capture.count = 0u;
        else if (strcmp(scenario, "trace-failure") == 0)
            fail_consume_trace = true;
        else if (strcmp(scenario, "counter-exhausted") == 0)
            host->fifo_capture.transfers = UINT64_MAX;
        else if (strcmp(scenario, "overflow-irq") == 0)
            host->cp_fifo_high_watermark = 16u;
        else if (strcmp(scenario, "cp-irq") == 0)
            dol_interrupts_set_source(&host->interrupts, DOL_PI_CAUSE_CP, true);
        else
            assert(strcmp(scenario, "queued") == 0 || two || strcmp(scenario, "unknown") == 0 ||
                   strcmp(scenario, "xf-too-large") == 0 || strcmp(scenario, "bp-rejected") == 0);
        const u32 rp = host->cp_fifo_read_pointer;
        const u32 wp = host->cp_fifo_write_pointer;
        const u32 distance = host->cp_fifo_distance;
        enable(host);
        if (strcmp(scenario, "queued") == 0 || two) {
            assert(host->error == NULL && host->frontier.complete && host->frontier.size == 5u);
            assert(memcmp(host->frontier.bytes, bytes, 5u) == 0);
            assert(host->frontier.pc == 0x80001000u && host->frontier.completion_pc == 0x80001010u);
            assert(host->cp_fifo_read_pointer == 0x1020u && host->cp_fifo_distance == distance - 32u);
            assert(host->fifo_capture.read_bytes == 32u && host->gx.fifo.size == 5u);
        } else {
            assert(host->error != NULL && host->error_address == 0xCC000002u && host->error_pc == 0x80321270u);
            assert(!host->frontier.complete);
            const bool parser_failed = strcmp(scenario, "unknown") == 0 || strcmp(scenario, "xf-too-large") == 0 ||
                                       strcmp(scenario, "bp-rejected") == 0;
            if (parser_failed) {
                assert(host->cp_fifo_read_pointer == 0x1020u && host->cp_fifo_distance == 0u);
                assert(host->fifo_capture.read_bytes == 32u && host->frontier.size != 0u && host->frontier.error != NULL);
            } else {
                assert(host->cp_fifo_read_pointer == rp && host->cp_fifo_distance == distance);
                assert(host->fifo_capture.read_bytes == 0u && host->frontier.size == 0u && host->gx.fifo.size == 0u);
            }
        }
        assert(host->cp_fifo_write_pointer == wp && host->pi_fifo_write_pointer == wp);
    }
    if (strcmp(scenario, "cp-irq") != 0)
        assert(memcmp(&interrupts, &host->interrupts, sizeof(interrupts)) == 0);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
