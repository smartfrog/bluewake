#include "dsp_interface.h"

#include <stdio.h>

const char* ww_dsp_deferred_launch(WwDspInterface* dsp, u16 start, u32 pc) {
    if (dsp == NULL || !dsp->audio_deferred)
        return "deferred DSP launch requires explicit audio deferral";
    const WwDspRomBoot* boot = &dsp->rom_boot;
    if (dsp->deferred_task_started || boot->received != 15u || boot->dmem_length != 0u ||
        boot->length == 0u || (boot->length & 1u) != 0u || boot->imem_address >= 0x1000u ||
        boot->length / 2u > 0x1000u - boot->imem_address || start < boot->imem_address ||
        start - boot->imem_address >= boot->length / 2u)
        return "deferred DSP supports one bounded bootstrap task, not a real task engine";
    if (fprintf(stderr, "[audio-deferred] event=task-bootstrap source=%08X bytes=%u start=%04X pc=%08X synthetic_mail=DCD10000,F3551111 synthetic_irq=0080 code_transfer=none task_body=no-op pcm_output=none guest_callbacks=not-skipped\n",
                boot->source, boot->length, start, pc) < 0)
        return "deferred DSP bootstrap trace write failed";
    dsp->rom_boot.parameter = 0u;
    dsp->rom_selected = false;
    dsp->deferred_task_started = true;
    dsp->deferred_task_interrupt = true;
    dsp->deferred_handshake_pending = true;
    dsp->dsp_mailbox = 0xDCD10000u;
    return NULL;
}
