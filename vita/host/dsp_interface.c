#include "dsp_interface.h"
#include "gxruntime/aram.h"

#include <string.h>

#define DSP_HALT 0x0004u
#define DSP_INIT 0x0800u
#define DSP_INTERRUPT_MASKS 0x0150u
#define DSP_INTERRUPT_ACKS 0x00A8u
#define DSP_MAIL_BUSY 0x80000000u
#define DSP_ROM_MAIL 0x8071FEEDu
#define DSP_ARAM_INTERRUPT 0x0020u
#define DSP_DMA_BUSY 0x0200u
#define ARAM_CYCLES_PER_BLOCK 246u

void ww_dsp_interface_init(WwDspInterface* dsp, const u8* init_reference) {
    *dsp = (WwDspInterface){.ar_refresh = 156u, .control = DSP_INIT | DSP_HALT,
                           .init_reference = init_reference};
}

static bool reject(WwDspInterface* dsp, const char* error) {
    if (dsp->error == NULL)
        dsp->error = error;
    return false;
}

static bool init_matches(const WwDspInterface* dsp, const CPUState* cpu) {
    return dsp->init_reference != NULL && cpu != NULL && cpu->ram != NULL &&
           cpu->ram_size >= WW_DSP_INIT_LOAD_ADDRESS + WW_DSP_INIT_UCODE_BYTES &&
           memcmp(cpu->ram + WW_DSP_INIT_LOAD_ADDRESS, dsp->init_reference, WW_DSP_INIT_UCODE_BYTES) == 0;
}

static bool control_write(WwDspInterface* dsp, const CPUState* cpu, u16 value) {
    if (value & ~(DSP_INIT | DSP_HALT | 1u | DSP_INTERRUPT_MASKS | DSP_INTERRUPT_ACKS | DSP_DMA_BUSY))
        return reject(dsp, "unsupported DSP control operation");
    if ((dsp->control & DSP_INIT) != 0u && (value & DSP_INIT) == 0u)
        return reject(dsp, "DSP initialization transfer not implemented");
    const bool reset = (value & 1u) != 0u;
    const bool running = (value & DSP_HALT) == 0u;
    if ((reset || running) && !init_matches(dsp, cpu))
        return reject(dsp, "DSP reset/release requires byte-identical documented init ucode");
    if (running && !reset && !dsp->rom_selected)
        return reject(dsp, "DSP release requires verified reset-selected ROM");
    if (running && (dsp->cpu_mailbox & DSP_MAIL_BUSY))
        return reject(dsp, "queued ROM mail protocol not implemented");
    if (reset) {
        dsp->rom_selected = true;
        dsp->rom_mail_pending = true;
    }
    if ((value & DSP_ARAM_INTERRUPT) && dsp->aram_cycles_remaining == 0u)
        bluewake_aram_dma_acknowledge(&dsp->aram);
    dsp->control = value & (DSP_INIT | DSP_HALT | DSP_INTERRUPT_MASKS);
    return true;
}

static bool aram_access(u32 address, u8 size) {
    return bluewake_aram_dma_contains(address) &&
           (size == 2u || size == 4u) && (address & (size - 1u)) == 0u &&
           address + size <= BLUEWAKE_ARAM_DMA_BASE + BLUEWAKE_ARAM_DMA_BYTES;
}

static bool aram_dma_write(WwDspInterface* dsp, CPUState* cpu, u32 address, u8 size, u64 value) {
    if (!aram_access(address, size))
        return false;
    if (dsp->aram_cycles_remaining != 0u)
        return reject(dsp, "ARAM DMA register write while busy not implemented");
    const bool trigger = address + size == BLUEWAKE_ARAM_DMA_BASE + BLUEWAKE_ARAM_DMA_BYTES;
    if (trigger) {
        const u32 aram_address = ((u32)dsp->aram.regs[2] << 16u) | dsp->aram.regs[3];
        if (aram_buffer() == NULL)
            return reject(dsp, "ARAM DMA requires owned backing memory");
        if (cpu == NULL || cpu->ram == NULL)
            return reject(dsp, "ARAM DMA requires owned MEM1 backing memory");
        if (aram_address >= ARAM_SIZE)
            return reject(dsp, "ARAM DMA expansion/HSP address not implemented");
        if ((dsp->ar_info & 0xFu) == 4u)
            return reject(dsp, "ARAM DMA mirrored layout not implemented");
    }
    const u32 index = (address - BLUEWAKE_ARAM_DMA_BASE) / 2u;
    const u32 high_mask = index == 4u ? 0x83FFu : 0x03FFu;
    const u64 mask = size == 4u ? ((u64)high_mask << 16u) | 0xFFE0u :
                     (index & 1u) != 0u ? 0xFFE0u : high_mask;
    if (!bluewake_aram_dma_write(&dsp->aram, cpu, address, size, value & mask))
        return reject(dsp, "ARAM DMA requires a nonzero mapped MEM1 range and acknowledged prior transfer");
    if (trigger) {
        const u32 main_end = dsp->aram.last_main_address - GC_RAM_BASE + dsp->aram.last_length;
        const u32 aram_end = dsp->aram.last_aram_address + dsp->aram.last_length;
        dsp->aram.regs[0] = (u16)(main_end >> 16u);
        dsp->aram.regs[1] = (u16)main_end;
        dsp->aram.regs[2] = (u16)(aram_end >> 16u);
        dsp->aram.regs[3] = (u16)aram_end;
        dsp->aram.regs[4] = dsp->aram.last_direction != 0u ? 0x8000u : 0u;
        dsp->aram.regs[5] = 0u;
        dsp->aram_cycles_remaining = (u64)(dsp->aram.last_length / 32u) * ARAM_CYCLES_PER_BLOCK;
    }
    return true;
}

void ww_dsp_interface_advance(WwDspInterface* dsp, u64 cycles) {
    dsp->aram_cycles_remaining = cycles < dsp->aram_cycles_remaining ? dsp->aram_cycles_remaining - cycles : 0u;
}

u64 ww_dsp_interface_deadline(const WwDspInterface* dsp) {
    return dsp->aram_cycles_remaining != 0u ? dsp->aram_cycles_remaining : UINT64_MAX;
}

bool ww_dsp_interface_interrupt_pending(const WwDspInterface* dsp) {
    return dsp->aram_cycles_remaining == 0u && (dsp->control & (DSP_ARAM_INTERRUPT << 1u)) &&
           bluewake_aram_dma_interrupt_pending(&dsp->aram);
}

bool ww_dsp_interface_read(WwDspInterface* dsp, u32 address, u8 size, u64* value) {
    if (aram_access(address, size)) {
        *value = bluewake_aram_dma_read(&dsp->aram, address, size);
        return true;
    }
    if (size != 2u)
        return false;
    switch (address) {
    case 0xCC005000u: *value = dsp->cpu_mailbox >> 16u; return true;
    case 0xCC005002u:
        *value = dsp->cpu_mailbox & 0xFFFFu;
        dsp->cpu_mailbox &= ~DSP_MAIL_BUSY;
        return true;
    case 0xCC005004u:
        if (!(dsp->control & DSP_HALT) && dsp->rom_mail_pending)
            dsp->dsp_mailbox = DSP_ROM_MAIL;
        *value = dsp->dsp_mailbox >> 16u;
        return true;
    case 0xCC005006u:
        if (!(dsp->control & DSP_HALT) && dsp->rom_mail_pending) {
            dsp->dsp_mailbox = DSP_ROM_MAIL;
            dsp->rom_mail_pending = false;
        }
        *value = dsp->dsp_mailbox & 0xFFFFu;
        dsp->dsp_mailbox &= ~DSP_MAIL_BUSY;
        return true;
    case WW_DSP_CONTROL:
        *value = dsp->control | (dsp->aram_cycles_remaining != 0u ? DSP_DMA_BUSY :
                 bluewake_aram_dma_interrupt_pending(&dsp->aram) ? DSP_ARAM_INTERRUPT : 0u);
        return true;
    case WW_DSP_AR_INFO: *value = dsp->ar_info; return true;
    case WW_DSP_AR_MODE: *value = 1u; return true;
    case WW_DSP_AR_REFRESH: *value = dsp->ar_refresh; return true;
    default: return false;
    }
}

bool ww_dsp_interface_write(WwDspInterface* dsp, CPUState* cpu, u32 address, u8 size, u64 value) {
    if (bluewake_aram_dma_contains(address))
        return aram_dma_write(dsp, cpu, address, size, value);
    if (size != 2u)
        return false;
    switch (address) {
    case 0xCC005000u:
        dsp->cpu_mailbox = ((dsp->cpu_mailbox & 0xFFFFu) | ((u32)(u16)value << 16u)) & ~DSP_MAIL_BUSY;
        return true;
    case 0xCC005002u:
        if (!(dsp->control & DSP_HALT))
            return reject(dsp, "running ROM mail protocol not implemented");
        dsp->cpu_mailbox = (dsp->cpu_mailbox & 0xFFFF0000u) | (u16)value | DSP_MAIL_BUSY;
        return true;
    case WW_DSP_CONTROL: return control_write(dsp, cpu, (u16)value);
    case WW_DSP_AR_INFO: dsp->ar_info = (u16)value & 0x007Fu; return true;
    case WW_DSP_AR_REFRESH: dsp->ar_refresh = (u16)value & 0x07FFu; return true;
    default: return false;
    }
}
