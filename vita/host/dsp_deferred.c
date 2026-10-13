#include "dsp_interface.h"

#include <stdio.h>

static bool reply_pending(const WwDspInterface* dsp) {
    return (dsp->dsp_mailbox & 0x80000000u) != 0u || dsp->deferred_reply_mail != 0u ||
           dsp->deferred_task_interrupt;
}

static void publish_reply(WwDspInterface* dsp, u32 first, u32 second) {
    dsp->dsp_mailbox = first;
    dsp->deferred_reply_mail = second;
    dsp->deferred_task_interrupt = true;
}

const char* ww_dsp_deferred_launch(WwDspInterface* dsp, u16 start, u32 pc) {
    if (dsp == NULL || !dsp->audio_deferred)
        return "deferred DSP launch requires explicit audio deferral";
    const WwDspRomBoot* boot = &dsp->rom_boot;
    if (dsp->deferred_task_started || boot->received != 15u || boot->dmem_length != 0u ||
        boot->length == 0u || (boot->length & 1u) != 0u || boot->imem_address >= 0x1000u ||
        boot->length / 2u > 0x1000u - boot->imem_address || start < boot->imem_address ||
        (u32)start - boot->imem_address >= boot->length / 2u)
        return "deferred DSP supports one bounded bootstrap task, not a real task engine";
    if (reply_pending(dsp))
        return "deferred DSP reply mailbox must be consumed and acknowledged before bootstrap";
    if (fprintf(stderr, "[audio-deferred] event=task-bootstrap source=%08X bytes=%u start=%04X pc=%08X synthetic_mail=DCD10000,F3551111 synthetic_irq=0080 code_transfer=none task_body=no-op pcm_output=none guest_callbacks=not-skipped\n",
                boot->source, boot->length, start, pc) < 0)
        return "deferred DSP bootstrap trace write failed";
    dsp->rom_boot.parameter = 0u;
    dsp->rom_selected = false;
    dsp->deferred_task_started = true;
    publish_reply(dsp, 0xDCD10000u, 0xF3551111u);
    return NULL;
}

const char* ww_dsp_deferred_mail(WwDspInterface* dsp, u32 mail, u32 pc) {
    if (dsp == NULL || !dsp->audio_deferred || !dsp->deferred_task_started)
        return "deferred DSP input requires the declared no-op task";
    const bool control = dsp->deferred_setup_discarded;
    const char* kind = control ? "control" : "setup";
    const u8 count = control ? 2u : 5u;
    const u32 command = control ? 0x8D000000u : 0x81000000u;
    const u8 left = dsp->deferred_words_left;
    if (left == 0u) {
        if (dsp->deferred_control_discarded || mail != count)
            return "deferred DSP post-bootstrap task command not modeled; audio=deferred";
        if (reply_pending(dsp))
            return "deferred DSP input requires consumed and acknowledged prior replies";
        if (fprintf(stderr, "[audio-deferred] event=%s-count mail=%08X payload_words=%u pc=%08X action=accepted-for-no-op reply=none pcm_output=none\n",
                    kind, mail, count, pc) < 0)
            return "deferred DSP command count trace write failed";
        dsp->deferred_words_left = count;
        dsp->deferred_word_count = count;
        return NULL;
    }
    if (dsp->deferred_word_count != count || left > count ||
        (left == count && (mail & 0xFF000000u) != command))
        return "deferred DSP requires command 01 setup or post-setup command 0D; audio processing not modeled";
    if (left == 1u && reply_pending(dsp))
        return "deferred DSP command acceptance ACK would overwrite an unconsumed reply";
    if (fprintf(stderr, "[audio-deferred] event=%s-word mail=%08X words_left_after=%u pc=%08X action=discarded configuration=not-executed reply=none pcm_output=none\n",
                kind, mail, left - 1u, pc) < 0)
        return "deferred DSP command word trace write failed";
    if (left == 1u &&
        fprintf(stderr, "[audio-deferred] event=%s-acceptance-ack command=%02X sync=%04X pc=%08X synthetic_mail=DCD10004,%08X synthetic_irq=0080 origin=no-op-intake configuration=not-executed pcm_output=none guest_callbacks=not-skipped\n",
                kind, (command >> 24u) & 0x7Fu, dsp->deferred_command_sync, pc, 0xF3550000u | dsp->deferred_command_sync) < 0)
        return "deferred DSP command acceptance ACK trace write failed";
    if (left == count)
        dsp->deferred_command_sync = (u16)(mail >> 16u);
    dsp->deferred_words_left = left - 1u;
    if (left == 1u) {
        if (control)
            dsp->deferred_control_discarded = true;
        else
            dsp->deferred_setup_discarded = true;
        publish_reply(dsp, 0xDCD10004u, 0xF3550000u | dsp->deferred_command_sync);
    }
    return NULL;
}
