#include "dsp_interface.h"

#include <stdio.h>

const char* ww_dsp_rom_receive(WwDspInterface* dsp, u32 mail, u32 pc) {
    if (dsp == NULL)
        return "DSP ROM receiver requires owned state";
    if (!dsp->rom_selected || (dsp->control & WW_DSP_HALT_BIT) != 0u)
        return "running ROM mail protocol not implemented for this DSP execution mode";
    if (dsp->rom_mail_pending || (dsp->dsp_mailbox & 0x80000000u) != 0u)
        return "DSP ROM receiver requires the prior outgoing mail to be consumed";
    WwDspRomBoot next = dsp->rom_boot;
    const u32 parameter = dsp->rom_boot.parameter;
    if (parameter == 0u) {
        switch (mail) {
        case 0x80F3A001u:
        case 0x80F3A002u:
        case 0x80F3B002u:
        case 0x80F3C002u:
        case 0x80F3D001u:
            next.parameter = mail;
            break;
        default:
            return "unsupported DSP ROM command; no error mail injected";
        }
    } else {
        switch (parameter) {
        case 0x80F3A001u: next.source = mail; next.received |= 1u; break;
        case 0x80F3A002u: next.length = (u16)mail; next.received |= 2u; break;
        case 0x80F3B002u: next.dmem_length = (u16)mail; next.received |= 4u; break;
        case 0x80F3C002u: next.imem_address = (u16)mail; next.received |= 8u; break;
        case 0x80F3D001u:
            if (fprintf(stderr, "[dsp-rom-task-request] source=%08X bytes=%u dmem_bytes=%u imem=%04X start=%04X fields=%02X pc=%08X transfer=not-performed task_execution=not-started reply=none\n",
                        dsp->rom_boot.source, dsp->rom_boot.length, dsp->rom_boot.dmem_length,
                        dsp->rom_boot.imem_address, (u16)mail, dsp->rom_boot.received, pc) < 0)
                return "DSP ROM task request trace write failed";
            return "DSP ROM task launch requires a DSP task backend; transfer and execution not implemented";
        default:
            return "unsupported DSP ROM parameter state";
        }
        next.parameter = 0u;
    }
    if (fprintf(stderr, "[dsp-rom-mail] mail=%08X parameter=%08X source=%08X bytes=%u dmem_bytes=%u imem=%04X pc=%08X mode=source-backed-ROM-receiver reply=none\n",
                mail, parameter, next.source, next.length,
                next.dmem_length, next.imem_address, pc) < 0)
        return "DSP ROM receiver trace write failed";
    dsp->rom_boot = next;
    return NULL;
}
