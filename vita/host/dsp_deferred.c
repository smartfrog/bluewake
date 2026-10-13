#include "dsp_interface.h"

#include <stdio.h>

const char* ww_dsp_deferred_launch(WwDspInterface* dsp, u16 start, u32 pc) {
    if (dsp == NULL || !dsp->audio_deferred)
        return "deferred DSP launch requires explicit audio deferral";
    const WwDspRomBoot* boot = &dsp->rom_boot;
    if (dsp->deferred_task_started || boot->received != 15u || boot->dmem_length != 0u ||
        boot->length == 0u || (boot->length & 1u) != 0u || boot->imem_address >= 0x1000u ||
        boot->length / 2u > 0x1000u - boot->imem_address || start < boot->imem_address ||
        (u32)start - boot->imem_address >= boot->length / 2u)
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

const char* ww_dsp_deferred_mail(WwDspInterface* dsp, u32 mail, u32 pc) {
    if (dsp == NULL || !dsp->audio_deferred || !dsp->deferred_task_started)
        return "deferred DSP input requires the declared no-op task";
    const u8 left = dsp->deferred_setup_words_left;
    if (left == 0u) {
        if (dsp->deferred_setup_discarded || mail != 5u)
            return "deferred DSP post-bootstrap task command not modeled; audio=deferred";
        if (fprintf(stderr, "[audio-deferred] event=setup-count mail=%08X payload_words=5 pc=%08X action=accepted-for-no-op reply=none pcm_output=none\n",
                    mail, pc) < 0)
            return "deferred DSP setup count trace write failed";
        dsp->deferred_setup_words_left = 5u;
        return NULL;
    }
    if (left > 5u || (left == 5u && (mail & 0xFF000000u) != 0x81000000u))
        return "deferred DSP expects bounded command 01 setup; audio processing not modeled";
    if (fprintf(stderr, "[audio-deferred] event=setup-word mail=%08X words_left_after=%u pc=%08X action=discarded configuration=not-executed reply=none pcm_output=none\n",
                mail, left - 1u, pc) < 0)
        return "deferred DSP setup word trace write failed";
    dsp->deferred_setup_words_left = left - 1u;
    if (left == 1u)
        dsp->deferred_setup_discarded = true;
    return NULL;
}
