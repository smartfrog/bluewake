#include "dsp_interface.h"

#include <string.h>

#define DSP_HALT 0x0004u
#define DSP_INIT 0x0800u
#define DSP_INTERRUPT_MASKS 0x0150u
#define DSP_INTERRUPT_ACKS 0x00A8u
#define DSP_MAIL_BUSY 0x80000000u
#define DSP_ROM_MAIL 0x8071FEEDu

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
    if (value & ~(DSP_INIT | DSP_HALT | 1u | DSP_INTERRUPT_MASKS | DSP_INTERRUPT_ACKS))
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
    dsp->control = value & (DSP_INIT | DSP_HALT | DSP_INTERRUPT_MASKS);
    return true;
}

bool ww_dsp_interface_read(WwDspInterface* dsp, u32 address, u8 size, u64* value) {
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
    case WW_DSP_CONTROL: *value = dsp->control; return true;
    case WW_DSP_AR_INFO: *value = dsp->ar_info; return true;
    case WW_DSP_AR_MODE: *value = 1u; return true;
    case WW_DSP_AR_REFRESH: *value = dsp->ar_refresh; return true;
    default: return false;
    }
}

bool ww_dsp_interface_write(WwDspInterface* dsp, const CPUState* cpu, u32 address, u8 size, u64 value) {
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
