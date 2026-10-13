#include "probe_support.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

static bool fail_rom_trace;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (fail_rom_trace && stream == stderr && strncmp(format, "[dsp-rom-", 9u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static u64 read_register(WwHost* host, u32 address) {
    return host->cpu.external_read(&host->cpu, address, 2u);
}

static void control_write(WwHost* host, u16 value) {
    host->cpu.external_write(&host->cpu, WW_DSP_CONTROL, value, 2u);
}

static void rom_send(WwHost* host, u32 mail) {
    host->cpu.external_write(&host->cpu, 0xCC005000u, mail >> 16u, 2u);
    host->cpu.external_write(&host->cpu, 0xCC005002u, mail & 0xFFFFu, 2u);
}

static void rom_probe(WwHost* host, const char* scenario) {
    control_write(host, 0x0800u);
    assert(host->error == NULL && read_register(host, 0xCC005004u) == 0x8071u);
    if (strcmp(scenario, "rom-unconsumed-ready") != 0)
        assert(read_register(host, 0xCC005006u) == 0xFEEDu);
    if (strcmp(scenario, "rom-unselected") == 0)
        host->dsp.rom_selected = false;
    u8* memory = malloc(host->cpu.ram_size);
    assert(memory != NULL);
    memcpy(memory, host->cpu.ram, host->cpu.ram_size);
    const u64 cycles = host->cycles.absolute_cycles;
    const DolAudioDma audio = host->audio;
    const WwDspInterface dsp_before = host->dsp;
    WwDspInterface expected_dsp;
    const u32 dsp_mail = host->dsp.dsp_mailbox;
    const bool refusal = strcmp(scenario, "rom-unconsumed-ready") == 0 ||
                         strcmp(scenario, "rom-unselected") == 0 || strcmp(scenario, "rom-unknown-command") == 0 ||
                         strcmp(scenario, "rom-trace-failure") == 0;
    fail_rom_trace = strcmp(scenario, "rom-trace-failure") == 0;
    rom_send(host, strcmp(scenario, "rom-unknown-command") == 0 ? 0x80F31234u : 0x80F3A001u);
    if (refusal) {
        assert(host->error != NULL && host->error_address == 0xCC005002u);
        assert(host->dsp.rom_boot.parameter == 0u && host->dsp.rom_boot.received == 0u);
        assert(host->dsp.cpu_mailbox == (strcmp(scenario, "rom-unselected") == 0 ? 0x00F30000u : 0x80F30000u));
    } else {
        assert(host->error == NULL && read_register(host, 0xCC005000u) == 0x00F3u);
        fail_rom_trace = strcmp(scenario, "rom-parameter-trace-failure") == 0;
        rom_send(host, 0x00399420u);
        if (fail_rom_trace) {
            assert(host->error != NULL && host->dsp.rom_boot.parameter == 0x80F3A001u &&
                   host->dsp.rom_boot.received == 0u && host->dsp.rom_boot.source == 0u &&
                   host->dsp.cpu_mailbox == 0x0039A001u);
            goto verify_rom;
        }
        assert(host->error == NULL && read_register(host, 0xCC005000u) == 0x0039u);
        rom_send(host, 0x80F3C002u);
        rom_send(host, 0xCAFE0100u);
        rom_send(host, 0x80F3A002u);
        rom_send(host, 0xCAFE1D20u);
        rom_send(host, 0x80F3B002u);
        rom_send(host, 0u);
        assert(host->error == NULL);
        assert(host->dsp.rom_boot.source == 0x00399420u && host->dsp.rom_boot.length == 0x1D20u &&
               host->dsp.rom_boot.dmem_length == 0u && host->dsp.rom_boot.imem_address == 0x0100u &&
               host->dsp.rom_boot.received == 15u && host->dsp.rom_boot.parameter == 0u);
        if (strcmp(scenario, "rom-launch") == 0) {
            rom_send(host, 0x80F3D001u);
            assert(host->error == NULL);
            rom_send(host, 0xCAFE0010u);
            assert(host->error != NULL && host->error_address == 0xCC005002u &&
                   strstr(host->error, "DSP task backend") != NULL);
            assert(host->dsp.rom_boot.parameter == 0x80F3D001u && host->dsp.rom_boot.received == 15u);
        } else if (strcmp(scenario, "rom-reset") == 0) {
            control_write(host, 0x0805u);
            assert(host->error == NULL);
            assert(host->dsp.rom_boot.parameter == 0u && host->dsp.rom_boot.received == 0u &&
                   host->dsp.rom_boot.source == 0u && host->dsp.rom_boot.length == 0u);
        } else if (strcmp(scenario, "rom-invalid-init") == 0) {
            const WwDspRomBoot before = host->dsp.rom_boot;
            host->cpu.ram[WW_DSP_INIT_LOAD_ADDRESS] ^= 1u;
            control_write(host, 4u);
            host->cpu.ram[WW_DSP_INIT_LOAD_ADDRESS] ^= 1u;
            assert(host->error != NULL && memcmp(&host->dsp.rom_boot, &before, sizeof(before)) == 0);
        } else {
            assert(strcmp(scenario, "rom-config") == 0);
        }
    }
verify_rom:
    expected_dsp = dsp_before;
    expected_dsp.cpu_mailbox = host->dsp.cpu_mailbox;
    expected_dsp.rom_boot = host->dsp.rom_boot;
    expected_dsp.error = host->dsp.error;
    if (strcmp(scenario, "rom-reset") == 0) {
        expected_dsp.control = 0x0804u;
        expected_dsp.rom_mail_pending = true;
    }
    assert(memcmp(&host->dsp, &expected_dsp, sizeof(expected_dsp)) == 0);
    assert(host->cycles.absolute_cycles == cycles && host->cpu.timebase == 0u);
    assert(memcmp(memory, host->cpu.ram, host->cpu.ram_size) == 0);
    assert(memcmp(&audio, &host->audio, sizeof(audio)) == 0);
    assert(!dol_interrupts_external_pending(&host->interrupts));
    assert(host->dsp.dsp_mailbox == dsp_mail);
    assert(host->stream.decoded == 0u && host->frontier.size == 0u);
    free(memory);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    host->cpu.pc = 0x80302F34u;
    host->cpu.msr = 0u;
    u8* scratch = host->cpu.ram + WW_DSP_INIT_LOAD_ADDRESS;
    for (u32 index = 0u; index < 4096u; ++index)
        scratch[index] = index < 128u ? (u8)index : (u8)(index * 7u + 3u);
    control_write(host, 0x0805u);
    assert(host->error == NULL);
    if (strncmp(argv[1], "rom-", 4u) == 0) {
        rom_probe(host, argv[1]);
    } else if (strcmp(argv[1], "no-reference") == 0) {
        host->dsp.init_reference = NULL;
        control_write(host, 4u);
        assert(host->error != NULL && !host->dsp.init_loaded);
    } else if (strcmp(argv[1], "active-dma") == 0) {
        host->cpu.external_write(&host->cpu, 0xCC005020u, 0x1000u, 4u);
        host->cpu.external_write(&host->cpu, 0xCC005024u, 0u, 4u);
        host->cpu.external_write(&host->cpu, 0xCC005028u, 32u, 4u);
        assert(host->error == NULL);
        control_write(host, 4u);
        assert(host->error != NULL && !host->dsp.init_loaded);
        assert(read_register(host, WW_DSP_CONTROL) == 0x0A04u);
    } else if (strncmp(argv[1], "altered-", 8u) == 0) {
        const u32 index = (u32)strtoul(argv[1] + 8u, NULL, 10);
        assert(index < 128u);
        scratch[index] ^= 1u;
        control_write(host, 4u);
        assert(host->error != NULL && host->error_width == 2u && host->error_value == 4u);
        assert(host->error_address == WW_DSP_CONTROL && host->error_pc == 0x80302F34u);
        assert(read_register(host, WW_DSP_CONTROL) == 0x0804u);
    } else if (strcmp(argv[1], "short-memory") == 0) {
        const u32 original_size = host->cpu.ram_size;
        host->cpu.ram_size = WW_DSP_INIT_LOAD_ADDRESS + 128u;
        control_write(host, 4u);
        assert(host->error != NULL && read_register(host, WW_DSP_CONTROL) == 0x0804u);
        host->cpu.ram_size = original_size;
    } else if (strcmp(argv[1], "busy-mail") == 0) {
        control_write(host, 0x0800u);
        assert(read_register(host, 0xCC005004u) == 0x8071u);
        control_write(host, 4u);
        assert(host->error != NULL && read_register(host, 0xCC005004u) == 0x8071u);
    } else {
        if (strcmp(argv[1], "fractional-flag") == 0)
            charge_cycles(host, 7u);
        control_write(host, 4u);
        assert(host->error == NULL);
        assert(read_register(host, WW_DSP_CONTROL) == 0x0404u);
        if (strcmp(argv[1], "snapshot") == 0) {
            assert(host->dsp.init_loaded);
            assert(memcmp(host->dsp.init_image, scratch, 4096u) == 0);
            memset(scratch, 0xA5, 4096u);
            for (u32 index = 0u; index < 4096u; ++index)
                assert(host->dsp.init_image[index] == (index < 128u ? (u8)index : (u8)(index * 7u + 3u)));
            assert(read_register(host, 0xCC005004u) == 0u);
        } else if (strncmp(argv[1], "loaded-altered-", 15u) == 0) {
            if (strcmp(argv[1], "loaded-altered-running") == 0)
                control_write(host, 0u);
            host->dsp.init_image[127] ^= 1u;
            if (strcmp(argv[1], "loaded-altered-reset") == 0)
                control_write(host, 0x0805u);
            else if (strcmp(argv[1], "loaded-altered-release") == 0)
                control_write(host, 0u);
            else {
                assert(strcmp(argv[1], "loaded-altered-running") == 0);
                charge_cycles(host, 100000u);
            }
            assert(host->error != NULL && read_register(host, 0xCC005004u) == 0u);
        } else if (strcmp(argv[1], "flag") == 0 || strcmp(argv[1], "fractional-flag") == 0) {
            const u64 cycles = host->cycles.absolute_cycles;
            for (u32 index = 0u; index < 100u; ++index) {
                assert(read_register(host, WW_DSP_CONTROL) == 0x0404u);
                charge_cycles(host, 0u);
            }
            assert(host->cycles.absolute_cycles == cycles && host->cpu.timebase == 0u);
            charge_cycles(host, 1559u - cycles);
            assert(read_register(host, WW_DSP_CONTROL) == 0x0404u);
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            assert(host->cpu.cycle_budget == 1);
            charge_cycles(host, 1u);
            assert(read_register(host, WW_DSP_CONTROL) == 4u && host->cpu.timebase == 130u);
        } else if (strcmp(argv[1], "halted") == 0) {
            charge_cycles(host, 200000u);
            assert(read_register(host, WW_DSP_CONTROL) == 4u);
            assert(read_register(host, 0xCC005004u) == 0u);
        } else {
            memset(scratch, 0xA5, 128u);
            charge_cycles(host, 1560u);
            control_write(host, 0u);
            assert(host->error == NULL && read_register(host, WW_DSP_CONTROL) == 0u);
            memset(host->dsp.dram, 0xA5, sizeof(host->dsp.dram));
            if (strcmp(argv[1], "bulk-halt-mail") == 0) {
                charge_cycles(host, 98424u);
                assert(read_register(host, WW_DSP_CONTROL) == 4u);
                assert(read_register(host, 0xCC005004u) == 0x8054u);
                assert(read_register(host, 0xCC005006u) == 0x4348u);
                assert(read_register(host, 0xCC005004u) == 0x0054u);
                goto done;
            }
            if (strcmp(argv[1], "pause") == 0) {
                charge_cycles(host, 17u);
                control_write(host, 4u);
                charge_cycles(host, 100000u);
                assert(read_register(host, 0xCC005004u) == 0u);
                control_write(host, 0u);
                charge_cycles(host, 98411u - 17u);
            } else {
                charge_cycles(host, 98411u);
            }
            assert(read_register(host, 0xCC005004u) == 0u);
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            assert(host->cpu.cycle_budget == 1);
            charge_cycles(host, 1u);
            assert(read_register(host, 0xCC005004u) == 0x8054u);
            assert(host->dsp.dsp_mailbox == 0x80544348u);
            const bool consume_early = strcmp(argv[1], "mail-consume-early") == 0;
            if (consume_early) {
                assert(read_register(host, 0xCC005006u) == 0x4348u);
                assert(read_register(host, 0xCC005004u) == 0x0054u);
            }
            charge_cycles(host, 5u);
            assert(read_register(host, 0xCC005004u) == (consume_early ? 0x0054u : 0x8054u));
            charge_cycles(host, 1u);
            assert(read_register(host, 0xCC005004u) == (consume_early ? 0x0054u : 0x8054u));
            if (!consume_early)
                assert(read_register(host, 0xCC005006u) == 0x4348u);
            assert(read_register(host, 0xCC005004u) == 0x0054u);
            charge_cycles(host, 6u);
            assert(read_register(host, WW_DSP_CONTROL) == 4u);
            assert(read_register(host, 0xCC005004u) == 0x0054u);
            for (u32 index = 0u; index < sizeof(host->dsp.dram); ++index)
                assert(host->dsp.dram[index] == 0u);
            assert(!dol_interrupts_external_pending(&host->interrupts));
            assert(read_be32(host->audio.ai_regs) == DOL_AUDIO_DMA_AI_CONTROL_INIT);
            if (strcmp(argv[1], "reset-restored-scratch") == 0) {
                control_write(host, 0x0805u);
                assert(host->error == NULL && read_register(host, WW_DSP_CONTROL) == 0x0804u);
                control_write(host, 0x0800u);
                assert(host->error == NULL && read_register(host, 0xCC005004u) == 0x8071u);
                assert(read_register(host, 0xCC005006u) == 0xFEEDu);
            } else {
                assert(strcmp(argv[1], "mail") == 0 || strcmp(argv[1], "pause") == 0 || consume_early);
            }
        }
    }
done:
    ww_host_shutdown(host);
    assert(host->backing_bytes == 0u);
    free(host);
    return 0;
}
