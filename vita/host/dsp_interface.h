#ifndef WW05_DSP_INTERFACE_H
#define WW05_DSP_INTERFACE_H

#include "core/cpu.h"
#include "aram_dma.h"

#define WW_DSP_CONTROL 0xCC00500Au
#define WW_DSP_HALT_BIT 0x0004u
#define WW_DSP_INIT_LOAD_ADDRESS 0x01000000u
#define WW_DSP_INIT_UCODE_BYTES 128u
#define WW_DSP_INIT_TRANSFER_BYTES 4096u
#define WW_DSP_DRAM_BYTES 8192u

#define WW_DSP_AR_INFO 0xCC005012u
#define WW_DSP_AR_MODE 0xCC005016u
#define WW_DSP_AR_REFRESH 0xCC00501Au

typedef struct {
    u32 parameter;
    u32 source;
    u16 length;
    u16 dmem_length;
    u16 imem_address;
    u8 received;
} WwDspRomBoot;

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
    u32 init_cycles_remaining;
    u16 init_ticks_remaining;
    u8 init_image[WW_DSP_INIT_TRANSFER_BYTES];
    u8 dram[WW_DSP_DRAM_BYTES];
    bool init_loaded;
    bool rom_selected;
    bool rom_mail_pending;
    WwDspRomBoot rom_boot;
} WwDspInterface;

void ww_dsp_interface_init(WwDspInterface* dsp, const u8* init_reference);
bool ww_dsp_interface_read(WwDspInterface* dsp, u32 address, u8 size, u64* value);
const char* ww_dsp_rom_receive(WwDspInterface* dsp, u32 mail, u32 pc);
bool ww_dsp_interface_write(WwDspInterface* dsp, CPUState* cpu, u32 address, u8 size, u64 value);
bool ww_dsp_interface_advance(WwDspInterface* dsp, u64 cycles, u64 ticks);
u64 ww_dsp_interface_deadline(const WwDspInterface* dsp);
bool ww_dsp_interface_interrupt_pending(const WwDspInterface* dsp);

#endif
