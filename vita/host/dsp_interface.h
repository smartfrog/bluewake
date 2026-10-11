#ifndef WW05_DSP_INTERFACE_H
#define WW05_DSP_INTERFACE_H

#include "core/cpu.h"
#include "aram_dma.h"

#define WW_DSP_CONTROL 0xCC00500Au
#define WW_DSP_INIT_LOAD_ADDRESS 0x01000000u
#define WW_DSP_INIT_UCODE_BYTES 128u

#define WW_DSP_AR_INFO 0xCC005012u
#define WW_DSP_AR_MODE 0xCC005016u
#define WW_DSP_AR_REFRESH 0xCC00501Au

typedef struct WwDspInterface {
    u16 ar_info;
    u16 ar_refresh;
    u16 control;
    const u8* init_reference;
    const char* error;
    u32 cpu_mailbox;
    u32 dsp_mailbox;
    BluewakeAramDma aram;
    u64 aram_cycles_remaining;
    bool rom_selected;
    bool rom_mail_pending;
} WwDspInterface;

void ww_dsp_interface_init(WwDspInterface* dsp, const u8* init_reference);
bool ww_dsp_interface_read(WwDspInterface* dsp, u32 address, u8 size, u64* value);
bool ww_dsp_interface_write(WwDspInterface* dsp, CPUState* cpu, u32 address, u8 size, u64 value);
void ww_dsp_interface_advance(WwDspInterface* dsp, u64 cycles);
u64 ww_dsp_interface_deadline(const WwDspInterface* dsp);
bool ww_dsp_interface_interrupt_pending(const WwDspInterface* dsp);

#endif
