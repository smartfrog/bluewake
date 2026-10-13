#include "boot_host.h"
#include <psp2/ctrl.h>

_Static_assert(SCE_CTRL_START == 0x0008u, "Vita Start mapping");
_Static_assert(SCE_CTRL_CROSS == 0x4000u, "Vita Cross mapping");
_Static_assert(SCE_CTRL_CIRCLE == 0x2000u, "Vita Circle mapping");

static int vita_read(void* user, WwPadSample* sample) {
    (void)user;
    SceCtrlData data = {0};
    const int result = sceCtrlPeekBufferPositive(0, &data, 1);
    if (result == 1)
        *sample = (WwPadSample){data.buttons, data.lx, data.ly, data.rx, data.ry};
    return result;
}

int ww_pad_vita_enable(WwHost* host) {
    const int result = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    host->pad_source_result = result;
    if (result >= 0) {
        host->pad_read = vita_read;
        host->pad_user = NULL;
    }
    return result;
}
