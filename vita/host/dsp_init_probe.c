#include "probe_support.h"
#include <stdlib.h>
#include <string.h>

static u64 read_register(WwHost* host, u32 address) {
    return host->cpu.external_read(&host->cpu, address, 2u);
}

static void control_write(WwHost* host, u16 value) {
    host->cpu.external_write(&host->cpu, WW_DSP_CONTROL, value, 2u);
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
    if (strcmp(argv[1], "no-reference") == 0) {
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
            assert(read_register(host, 0xCC005004u) == 0x0054u);
            charge_cycles(host, 5u);
            assert(read_register(host, 0xCC005004u) == 0x0054u);
            charge_cycles(host, 1u);
            assert(read_register(host, 0xCC005004u) == 0x8054u);
            assert(read_register(host, 0xCC005006u) == 0x4348u);
            assert(read_register(host, 0xCC005004u) == 0x0054u);
            charge_cycles(host, 6u);
            assert(read_register(host, WW_DSP_CONTROL) == 4u);
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
                assert(strcmp(argv[1], "mail") == 0 || strcmp(argv[1], "pause") == 0);
            }
        }
    }
done:
    ww_host_shutdown(host);
    assert(host->backing_bytes == 0u);
    free(host);
    return 0;
}
