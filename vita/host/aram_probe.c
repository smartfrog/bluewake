#include "probe_support.h"
#include "gxruntime/aram.h"
#include <stdlib.h>
#include <string.h>

static u64 read_register(WwHost* host, u32 address, u8 size) {
    return host->cpu.external_read(&host->cpu, address, size);
}

static void write_register(WwHost* host, u32 address, u64 value, u8 size) {
    host->cpu.external_write(&host->cpu, address, value, size);
}

static void start_dma(WwHost* host, u32 main, u32 aram, u32 length, bool from_aram) {
    write_register(host, 0xCC005020u, main, 4u);
    write_register(host, 0xCC005024u, aram, 4u);
    write_register(host, 0xCC005028u, length | (from_aram ? 0x80000000u : 0u), 4u);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL);
    if (strcmp(argv[1], "existing-owner") == 0) {
        aram_init();
        u8* original = aram_buffer();
        assert(original != NULL);
        assert(!ww_host_init(host));
        ww_host_shutdown(host);
        assert(aram_buffer() == original);
        aram_free();
        free(host);
        return 0;
    }
    assert(ww_host_init(host));
    host->cpu.pc = 0x80302EC8u;
    host->cpu.msr = 0u;
    write_register(host, WW_DSP_AR_INFO, 0x43u, 2u);
    if (strcmp(argv[1], "registers") == 0) {
        write_register(host, 0xCC005020u, 0x8100123Fu, 4u);
        assert(read_register(host, 0xCC005020u, 4u) == 0x01001220u);
        write_register(host, 0xCC005024u, UINT16_MAX, 2u);
        write_register(host, 0xCC005026u, UINT16_MAX, 2u);
        assert(read_register(host, 0xCC005024u, 4u) == 0x03FFFFE0u);
        write_register(host, 0xCC005028u, UINT16_MAX, 2u);
        assert(read_register(host, 0xCC005028u, 2u) == 0x83FFu);
        assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0804u);
    } else if (strcmp(argv[1], "width") == 0) {
        write_register(host, 0xCC005020u, 5u, 1u);
        assert(host->error != NULL && host->error_width == 1u && host->error_value == 5u);
        assert(read_register(host, 0xCC005020u, 4u) == 0u);
    } else {
        u8* aram = aram_buffer();
        assert(aram != NULL);
        for (u32 index = 0u; index < 64u; ++index) {
            host->cpu.ram[0x1000u + index] = (u8)(index * 3u + 1u);
            aram[0x400u + index] = (u8)(255u - index);
        }
        if (strncmp(argv[1], "absent-hsp-", 11u) == 0) {
            const bool valid = strcmp(argv[1], "absent-hsp-read") == 0;
            const bool write = strcmp(argv[1], "absent-hsp-write") == 0;
            const bool long_read = strcmp(argv[1], "absent-hsp-long") == 0;
            const bool other_address = strcmp(argv[1], "absent-hsp-address") == 0;
            const bool main_outside = strcmp(argv[1], "absent-hsp-main") == 0;
            assert(valid || write || long_read || other_address || main_outside);
            host->cpu.pc = 0x803176ACu;
            memset(host->cpu.ram + 0x0040CD00u, 0xA5u, 128u);
            memset(aram, 0xB6u, 64u);
            write_register(host, WW_DSP_AR_INFO, 0x63u, 2u);
            write_register(host, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_DSP, 4u);
            write_register(host, WW_DSP_CONTROL, 0x0844u, 2u);
            write_register(host, 0xCC005020u, main_outside ? 0x01800000u : 0x0040CD20u, 4u);
            write_register(host, 0xCC005024u, other_address ? 0x01100000u : 0x01000000u, 4u);
            write_register(host, 0xCC005028u, write ? 0u : 0x8000u, 2u);
            write_register(host, 0xCC00502Au, long_read ? 64u : 32u, 2u);
            assert((host->error == NULL) == valid);
            for (u32 index = 0u; index < 128u; ++index)
                assert(host->cpu.ram[0x0040CD00u + index] == 0xA5u);
            for (u32 index = 0u; index < 64u; ++index)
                assert(aram[index] == 0xB6u);
            if (valid) {
                assert(host->dsp.aram.transfer_count == 1u && host->cycles.absolute_cycles == 0u);
                assert(read_register(host, 0xCC005020u, 4u) == 0x0040CD40u);
                assert(read_register(host, 0xCC005024u, 4u) == 0x01000020u);
                assert(read_register(host, 0xCC005028u, 4u) == 0x80000000u);
                assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0A44u);
                charge_cycles(host, 245u);
                assert(!dol_interrupts_external_pending(&host->interrupts));
                charge_cycles(host, 1u);
                assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0864u);
                assert(dol_interrupts_external_pending(&host->interrupts));
                write_register(host, WW_DSP_CONTROL, 0x0864u, 2u);
                assert(!dol_interrupts_external_pending(&host->interrupts));
                assert(host->dsp.dsp_mailbox == 0u && read_be32(host->audio.ai_regs) == DOL_AUDIO_DMA_AI_CONTROL_INIT);
            } else {
                assert(host->error_pc == 0x803176ACu && host->error_address == 0xCC00502Au);
                assert(host->dsp.aram.transfer_count == 0u && host->dsp.aram_cycles_remaining == 0u);
            }
        } else if (strcmp(argv[1], "to-aram") == 0) {
            start_dma(host, 0x1000u, 0x400u, 64u, false);
            assert(host->error == NULL);
            assert(memcmp(aram + 0x400u, host->cpu.ram + 0x1000u, 64u) == 0);
            assert(read_register(host, 0xCC005020u, 4u) == 0x1040u);
            assert(read_register(host, 0xCC005024u, 4u) == 0x440u);
            assert(read_register(host, 0xCC005028u, 4u) == 0u);
            assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0A04u);
            charge_cycles(host, 491u);
            assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0A04u);
            charge_cycles(host, 1u);
            assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0824u);
        } else if (strcmp(argv[1], "from-aram") == 0) {
            start_dma(host, 0x1000u, 0x400u, 64u, true);
            assert(host->error == NULL);
            assert(memcmp(aram + 0x400u, host->cpu.ram + 0x1000u, 64u) == 0);
            assert(host->cpu.ram[0x0FFFu] == 0u && host->cpu.ram[0x1040u] == 0u);
            assert(read_register(host, 0xCC005028u, 4u) == 0x80000000u);
        } else if (strcmp(argv[1], "timing") == 0 || strcmp(argv[1], "ack-busy") == 0) {
            write_register(host, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_DSP, 4u);
            write_register(host, WW_DSP_CONTROL, 0x0844u, 2u);
            start_dma(host, 0x1000u, 0x400u, 32u, false);
            assert(host->error == NULL && !dol_interrupts_external_pending(&host->interrupts));
            if (strcmp(argv[1], "ack-busy") == 0)
                write_register(host, WW_DSP_CONTROL, 0x0A64u, 2u);
            for (u32 index = 0u; index < 100u; ++index) {
                assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0A44u);
                charge_cycles(host, 0u);
            }
            assert(host->cycles.absolute_cycles == 0u && host->cpu.timebase == 0u);
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            assert(host->cpu.cycle_budget == 246);
            charge_cycles(host, 245u);
            assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0A44u);
            assert(!dol_interrupts_external_pending(&host->interrupts));
            bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
            bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
            assert(host->cpu.cycle_budget == 1);
            charge_cycles(host, 1u);
            assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0864u);
            assert(dol_interrupts_external_pending(&host->interrupts));
            assert(host->cycles.absolute_cycles == 246u && host->cpu.timebase == 20u);
            write_register(host, WW_DSP_CONTROL, 0x0864u, 2u);
            assert(read_register(host, WW_DSP_CONTROL, 2u) == 0x0844u);
            assert(!dol_interrupts_external_pending(&host->interrupts));
            assert(read_register(host, 0xCC005004u, 2u) == 0u);
            assert(read_be32(host->audio.ai_regs) == DOL_AUDIO_DMA_AI_CONTROL_INIT);
        } else if (strcmp(argv[1], "unmapped-main") == 0 || strcmp(argv[1], "hsp") == 0 ||
                   strcmp(argv[1], "hsp-halfword") == 0 ||
                   strcmp(argv[1], "zero") == 0 || strcmp(argv[1], "mirror-mode") == 0) {
            const u8 original = aram[0x400u];
            const bool halfword = strcmp(argv[1], "hsp-halfword") == 0;
            if (strcmp(argv[1], "mirror-mode") == 0)
                write_register(host, WW_DSP_AR_INFO, 0x44u, 2u);
            if (halfword) {
                write_register(host, 0xCC005020u, 0x1000u, 4u);
                write_register(host, 0xCC005024u, 0x01000000u, 4u);
                write_register(host, 0xCC005028u, 0x8000u, 2u);
                write_register(host, 0xCC00502Au, 32u, 2u);
            } else {
                start_dma(host, strcmp(argv[1], "unmapped-main") == 0 ? 0x01800000u : 0x1000u,
                          strcmp(argv[1], "hsp") == 0 ? 0x01000000u : 0x400u,
                          strcmp(argv[1], "zero") == 0 ? 0u : 32u, false);
            }
            assert(host->error != NULL && host->error_address == (halfword ? 0xCC00502Au : 0xCC005028u));
            assert(host->error_width == (halfword ? 2u : 4u) && host->error_pc == 0x80302EC8u);
            assert(aram[0x400u] == original && read_register(host, WW_DSP_CONTROL, 2u) == 0x0804u);
            for (u32 index = 0u; index < 64u; ++index)
                assert(host->cpu.ram[0x1000u + index] == (u8)(index * 3u + 1u));
        } else if (strcmp(argv[1], "busy-write") == 0) {
            start_dma(host, 0x1000u, 0x400u, 32u, false);
            assert(host->error == NULL);
            write_register(host, 0xCC005020u, 0x2000u, 4u);
            assert(host->error != NULL && host->error_address == 0xCC005020u);
            assert(read_register(host, 0xCC005020u, 4u) == 0x1020u);
        } else {
            assert(!"unknown ARAM scenario");
        }
    }
    ww_host_shutdown(host);
    assert(aram_buffer() == NULL && host->backing_bytes == 0u);
    free(host);
    return 0;
}
