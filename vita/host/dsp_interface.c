#include "dsp_interface.h"

void ww_dsp_interface_init(WwDspInterface* dsp) {
    *dsp = (WwDspInterface){.ar_refresh = 156u};
}

bool ww_dsp_interface_read(const WwDspInterface* dsp, u32 address, u8 size, u64* value) {
    if (size != 2u)
        return false;
    switch (address) {
    case WW_DSP_AR_INFO: *value = dsp->ar_info; return true;
    case WW_DSP_AR_MODE: *value = 1u; return true;
    case WW_DSP_AR_REFRESH: *value = dsp->ar_refresh; return true;
    default: return false;
    }
}

bool ww_dsp_interface_write(WwDspInterface* dsp, u32 address, u8 size, u64 value) {
    if (size != 2u)
        return false;
    switch (address) {
    case WW_DSP_AR_INFO: dsp->ar_info = (u16)value & 0x007Fu; return true;
    case WW_DSP_AR_REFRESH: dsp->ar_refresh = (u16)value & 0x07FFu; return true;
    default: return false;
    }
}
