#ifndef BLUEWAKE_DVD_FILE_INFO_H
#define BLUEWAKE_DVD_FILE_INFO_H

#include "core/cpu.h"
#include "gxruntime/dvd.h"

#define DVD_CB_STATE 0x0Cu
#define DVD_CB_CURRXFER 0x1Cu
#define DVD_CB_XFERRED 0x20u
#define DVD_FI_STARTADDR 0x30u
#define DVD_FI_LENGTH 0x34u
#define DVD_FI_CALLBACK 0x38u
#define DVD_MIN_TRANSFER_SIZE 0x20u

static inline bool host_dvd_fill_file_info(CPUState* cpu, s32 entry, u32 file_info) {
    u32 start = 0;
    u32 length = 0;
    if (file_info == 0u || !dvd_entry_info(entry, &start, &length))
        return false;
    mem_write32(cpu, file_info + DVD_FI_STARTADDR, start);
    mem_write32(cpu, file_info + DVD_FI_LENGTH, length);
    mem_write32(cpu, file_info + DVD_FI_CALLBACK, 0u);
    mem_write32(cpu, file_info + DVD_CB_STATE, 0u);
    return true;
}

#endif
