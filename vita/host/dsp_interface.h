#ifndef WW05_DSP_INTERFACE_H
#define WW05_DSP_INTERFACE_H

#include "core/types.h"

#define WW_DSP_AR_INFO 0xCC005012u
#define WW_DSP_AR_MODE 0xCC005016u
#define WW_DSP_AR_REFRESH 0xCC00501Au

typedef struct WwDspInterface {
    u16 ar_info;
    u16 ar_refresh;
} WwDspInterface;

void ww_dsp_interface_init(WwDspInterface* dsp);
bool ww_dsp_interface_read(const WwDspInterface* dsp, u32 address, u8 size, u64* value);
bool ww_dsp_interface_write(WwDspInterface* dsp, u32 address, u8 size, u64 value);

#endif
