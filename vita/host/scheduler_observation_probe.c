#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_trace && stream == stderr && (strncmp(format, "[scheduler-idle", 15u) == 0 ||
                                         strncmp(format, "[dsp-task-boot]", 15u) == 0 ||
                                         strncmp(format, "[card-probe]", 12u) == 0)) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    const bool task_boot = strncmp(argv[1], "dsp-", 4u) == 0;
    const bool card_probe = strncmp(argv[1], "card-", 5u) == 0;
    cpu->pc = card_probe ? 0x8031D400u : task_boot ? 0x803193ACu : 0x80307EF4u;
    const u32 entry = cpu->pc;
    cpu->gpr[1] = 0x80007000u;
    cpu->gpr[3] = card_probe ? 0u : 0x80004000u;
    cpu->gpr[13] = 0x8040E0E0u;
    const StaticRecompRange range = {cpu->pc, cpu->pc + (card_probe ? 56u : 4u)};
    const StaticRecompModuleDesc module = {.code_ranges = &range, .num_code_ranges = 1u};
    const StaticRecompRange short_range = {entry, entry + 4u};
    const StaticRecompModuleDesc short_module = {.code_ranges = &short_range, .num_code_ranges = 1u};
    host->module = &module;
    if (task_boot || card_probe) {
        const u32 bytes = card_probe ? 56u : 32u;
        const DolLayout layout = {.text_address = {entry}, .text_size = {bytes}};
        for (u32 index = 0u; index < bytes; ++index)
            cpu->ram[entry - GC_RAM_BASE + index] = (u8)(index + 33u);
        for (u32 index = 0u; index < 64u; ++index)
            cpu->ram[0x4000u + index] = (u8)index;
        assert(ww_code_guard_init(&host->code_guard, cpu, &layout));
        if (strcmp(argv[1], "dsp-code") == 0 || strcmp(argv[1], "card-code") == 0)
            cpu->ram[entry - GC_RAM_BASE + 12u] ^= 1u;
        if (strcmp(argv[1], "card-code-tail") == 0)
            cpu->ram[entry - GC_RAM_BASE + 55u] ^= 1u;
        if (strcmp(argv[1], "card-short-range") == 0)
            host->module = &short_module;
        if (strcmp(argv[1], "dsp-unregistered") == 0 || strcmp(argv[1], "card-unregistered") == 0)
            host->module = NULL;
        if (strcmp(argv[1], "dsp-unmapped") == 0)
            cpu->gpr[3] = 0x81800000u;
        if (strcmp(argv[1], "dsp-stack") == 0)
            cpu->gpr[1] = 0x80000010u;
        if (strcmp(argv[1], "card-stack") == 0)
            cpu->gpr[1] = 0x80000004u;
        if (strcmp(argv[1], "card-stack-high") == 0)
            cpu->gpr[1] = 0x81800000u;
        if (strcmp(argv[1], "dsp-available") == 0)
            host->dsp.dsp_mailbox = 0x8ABC1234u;
        if (strcmp(argv[1], "dsp-empty") == 0)
            host->dsp.dsp_mailbox = 0x00ABCDEFu;
        if (strcmp(argv[1], "dsp-rom-pending") == 0 || strcmp(argv[1], "dsp-init-pending") == 0 ||
            strcmp(argv[1], "dsp-halted-pending") == 0) {
            memcpy(cpu->ram + WW_DSP_INIT_LOAD_ADDRESS, host->dsp.init_reference, WW_DSP_INIT_UCODE_BYTES);
            if (strcmp(argv[1], "dsp-init-pending") == 0) {
                cpu->external_write(cpu, WW_DSP_CONTROL, 0u, 2u);
            } else {
                cpu->external_write(cpu, WW_DSP_CONTROL, 0x0805u, 2u);
                if (strcmp(argv[1], "dsp-rom-pending") == 0)
                    cpu->external_write(cpu, WW_DSP_CONTROL, 0x0800u, 2u);
            }
            assert(host->error == NULL);
        }
    }
    host->rel_image.linked = strcmp(argv[1], "unlinked") != 0;
    host->rel_image.module_id = 1u;
    mem_write16(cpu, 0x803A2960u + 0x2C8u, 4u);
    mem_write32(cpu, 0x803A2960u + 0x2DCu, 0x80006008u);
    mem_write32(cpu, 0x803A2960u + 0x198u, 0x803059D0u);
    mem_write32(cpu, 0x803A2960u + 4u, 0x80007000u);
    mem_write32(cpu, 0x80007004u, 0x80001234u);
    mem_write32(cpu, 0x800000DCu, 0x803A2960u);
    const u32 size = cpu->ram_size;
    u8* before = malloc(size);
    assert(before != NULL);
    memcpy(before, cpu->ram, size);
    u32 registers[32];
    memcpy(registers, cpu->gpr, sizeof(registers));
    const WwDspInterface dsp_before = host->dsp;
    fail_trace = strcmp(argv[1], "trace") == 0 || strcmp(argv[1], "dsp-trace") == 0 || strcmp(argv[1], "card-trace") == 0;
    const bool intercepted = ww_host_edge(host, cpu, cpu->pc);
    const bool refused = fail_trace || (task_boot && (strcmp(argv[1], "dsp-code") == 0 ||
        strcmp(argv[1], "dsp-unregistered") == 0 || strcmp(argv[1], "dsp-unmapped") == 0 || strcmp(argv[1], "dsp-stack") == 0)) ||
        (card_probe && (strcmp(argv[1], "card-code") == 0 || strcmp(argv[1], "card-unregistered") == 0 ||
                        strcmp(argv[1], "card-code-tail") == 0 || strcmp(argv[1], "card-short-range") == 0 ||
                        strcmp(argv[1], "card-stack") == 0 || strcmp(argv[1], "card-stack-high") == 0));
    if (refused) {
        assert(intercepted && host->error != NULL && host->edge_stopped);
        assert(strstr(host->error, card_probe ? "CARDProbe" : task_boot ? "DSP" : "scheduler") != NULL);
        if (card_probe)
            assert(!host->card_probe_observed);
    } else {
        assert(!intercepted && host->error == NULL && cpu->exception == 0u);
        if (card_probe)
            assert(host->card_probe_observed);
        if (strcmp(argv[1], "repeat") == 0 || strcmp(argv[1], "dsp-repeat") == 0 || strcmp(argv[1], "card-repeat") == 0)
            assert(!ww_host_edge(host, cpu, cpu->pc));
        if (task_boot && (strcmp(argv[1], "dsp-empty") == 0 || strcmp(argv[1], "dsp-available") == 0 ||
                         strcmp(argv[1], "dsp-rom-pending") == 0 || strcmp(argv[1], "dsp-init-pending") == 0 ||
                         strcmp(argv[1], "dsp-halted-pending") == 0)) {
            cpu->pc = 0x80319080u;
            const u64 value = cpu->external_read(cpu, 0xCC005004u, 2u);
            if (strcmp(argv[1], "dsp-empty") == 0 || strcmp(argv[1], "dsp-halted-pending") == 0) {
                assert(value == 0u && host->error != NULL && host->error_address == 0xCC005004u &&
                       host->error_pc == cpu->pc && strstr(host->error, "DSP task") != NULL);
            } else if (strcmp(argv[1], "dsp-available") == 0) {
                assert(value == 0x8ABCu && host->error == NULL);
            } else {
                assert(value == (strcmp(argv[1], "dsp-rom-pending") == 0 ? 0x8071u : 0u) && host->error == NULL);
            }
        }
    }
    assert((cpu->pc == entry || (task_boot && cpu->pc == 0x80319080u)) && host->cycles.absolute_cycles == 0u && cpu->timebase == 0u);
    assert(memcmp(registers, cpu->gpr, sizeof(registers)) == 0);
    WwDspInterface expected_dsp = dsp_before;
    if (strcmp(argv[1], "dsp-rom-pending") == 0)
        expected_dsp.dsp_mailbox = 0x8071FEEDu;
    assert(memcmp(&host->dsp, &expected_dsp, sizeof(expected_dsp)) == 0);
    assert(memcmp(before, cpu->ram, size) == 0);
    free(before);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
