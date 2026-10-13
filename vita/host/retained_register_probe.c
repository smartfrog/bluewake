#include "probe_support.h"
#include <stdlib.h>

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    ww_probe_fifo_setup(host, true);
    const u32 address = argc == 3 ? (u32)strtoul(argv[2], NULL, 0) : 0xCC000006u;
    const bool volume = address == 0xCC006C04u;
    const bool timing = address == 0xCC006C0Cu;
    const bool audio_dma = address == 0xCC005030u || address == 0xCC005032u || address == 0xCC005036u;
    const bool word = volume || timing;
    host->cpu.pc = audio_dma ? 0x80316950u : timing ? 0x80316E24u : volume ? 0x80316E00u : 0x803202C4u;
    assert(word || audio_dma || address == 0xCC000006u || (address >= 0xCC001000u && address <= 0xCC001008u && !(address & 1u)));
    const u8 register_width = word ? 4u : 2u;
    const u32 mask = address == 0xCC005030u ? 0x03FFu : address == 0xCC005032u ? 0xFFE0u :
                     address == 0xCC005036u ? 0x7FFFu : word ? UINT32_MAX : address == 0xCC000006u ? 7u : UINT16_MAX;
    const char* scenario = argv[1];
    const bool bad_read = strncmp(scenario, "read-width-", 11u) == 0;
    const bool bad_write = strncmp(scenario, "write-width-", 12u) == 0;
    const bool credit = strcmp(scenario, "credit") == 0;
    const bool active = strcmp(scenario, "active") == 0;
    const u8 width = bad_read ? (u8)strtoul(scenario + 11u, NULL, 0) :
                     bad_write ? (u8)strtoul(scenario + 12u, NULL, 0) : register_width;
    const u32 value = strncmp(scenario, "value-", 6u) == 0 ? (u32)strtoul(scenario + 6u, NULL, 0) : 0u;
    const bool refused_timing = timing && value != 0u;
    const bool refused_enable = address == 0xCC005036u && (value & DOL_AUDIO_DMA_ENABLE) != 0u;
    const bool running = strcmp(scenario, "audio-running") == 0;
    const bool dma_running = strcmp(scenario, "audio-dma-running") == 0;
    if (running)
        write_be32(host->audio.ai_regs, DOL_AUDIO_DMA_AI_CONTROL_INIT | DOL_AUDIO_DMA_AI_PSTAT_BIT);
    if (dma_running)
        host->audio.control = DOL_AUDIO_DMA_ENABLE;
    if (active) {
        host->cp_control = 0x15u;
        host->gather.size = 2u;
        host->gather.bytes[0] = 0x08u;
        host->gather.bytes[1] = 0x87u;
        host->cp_fifo_distance = 32u;
        host->frontier.size = 2u;
        dol_interrupts_set_source(&host->interrupts, DOL_PI_CAUSE_CP, true);
    }
    const WwHost before = *host;
    assert((timing ? read_be32(host->audio.ai_regs + 12u) : volume ? mem_read32(&host->cpu, address) : mem_read16(&host->cpu, address)) == 0u);
    if (credit) {
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        host->cpu.downcount -= 256;
    }
    if (bad_read)
        host->cpu.external_read(&host->cpu, address, width);
    else
        host->cpu.external_write(&host->cpu, address, value, width);
    if (bad_read || bad_write || running || dma_running || refused_timing || refused_enable) {
        assert(host->error != NULL && host->error_address == address && host->error_pc == before.cpu.pc);
        if (!bad_read)
            assert(host->error_width == width && host->error_value == value);
    } else {
        assert(host->error == NULL && host->cpu.exception == 0u);
        assert((word ? mem_read32(&host->cpu, address) : mem_read16(&host->cpu, address)) == (value & mask));
        if (strcmp(scenario, "independent") == 0) {
            if (audio_dma) {
                mem_write16(&host->cpu, 0xCC005030u, 0x1234u);
                mem_write16(&host->cpu, 0xCC005032u, 0x5678u);
                assert(mem_read16(&host->cpu, 0xCC005030u) == 0x0234u);
                assert(mem_read16(&host->cpu, 0xCC005032u) == 0x5660u);
                assert(host->audio.source_address == 0x02345660u);
            } else if (volume) {
                mem_write32(&host->cpu, address, 0xA55AFF34u);
                assert(mem_read32(&host->cpu, address) == 0xA55AFF34u);
            } else {
                mem_write16(&host->cpu, address, 0xA55Au);
                for (u32 reg = 0xCC001000u; reg <= 0xCC001008u; reg += 2u)
                    assert(mem_read16(&host->cpu, reg) == (reg == address ? 0xA55Au : 0u));
            }
        }
    }
    assert(memcmp(&before.gather, &host->gather, sizeof(host->gather)) == 0);
    assert(memcmp(&before.frontier, &host->frontier, sizeof(host->frontier)) == 0);
    assert(memcmp(&before.gx, &host->gx, sizeof(host->gx)) == 0);
    assert(memcmp(&before.interrupts, &host->interrupts, sizeof(host->interrupts)) == 0);
    assert(host->cp_control == before.cp_control && host->cp_fifo_distance == before.cp_fifo_distance);
    assert(host->cp_fifo_read_pointer == before.cp_fifo_read_pointer);
    assert(host->cp_fifo_write_pointer == before.cp_fifo_write_pointer);
    DolAudioDma expected_audio = before.audio;
    if (volume && !bad_read && !bad_write && !running && !dma_running)
        write_be32(expected_audio.ai_regs + 4u, strcmp(scenario, "independent") == 0 ? 0xA55AFF34u : value);
    if (audio_dma && !bad_read && !bad_write && !running && !dma_running && !refused_enable) {
        dol_audio_dma_dsp_mmio_write(&expected_audio, address - 0xCC005000u, 2u, value & mask);
        if (strcmp(scenario, "independent") == 0) {
            dol_audio_dma_dsp_mmio_write(&expected_audio, DOL_AUDIO_DMA_DSP_DMA_START_HI_OFF, 2u, 0x0234u);
            dol_audio_dma_dsp_mmio_write(&expected_audio, DOL_AUDIO_DMA_DSP_DMA_START_LO_OFF, 2u, 0x5660u);
        }
    }
    assert(memcmp(&host->audio, &expected_audio, sizeof(expected_audio)) == 0);
    assert(host->cycles.absolute_cycles == (credit ? 256u : 0u));
    if (credit) {
        assert(host->cpu.cycle_budget == 0 && host->cpu.timebase == 21u);
        assert(bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu) == 0u);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
