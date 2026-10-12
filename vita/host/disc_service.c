#include "boot_host.h"
#include "dvd_io.h"
#include "gxruntime/dvd.h"

DolDiCommandResult ww_host_di_read(WwHost* host, DolDiCommand* command) {
    if (host == NULL)
        return DOL_DI_COMMAND_ERROR;
    if (host->error != NULL)
        return DOL_DI_COMMAND_ERROR;
    if (command == NULL) {
        ww_host_fail(host, "DI read requires a command", 0u, host->cpu.pc);
        return DOL_DI_COMMAND_ERROR;
    }
    const u32 physical = command->dma_address;
    const u32 guest = GC_RAM_BASE | physical;
    const u32 length = command->dma_length;
    const u64 offset = (u64)command->command[1] << 2u;
    const char* error = NULL;
    if (command->command[0] != 0xA8000000u || command->command[2] != length ||
        command->cpu != &host->cpu || !command->dma || command->write || length == 0u ||
        host->cpu.ram == NULL || physical > host->cpu.ram_size || length > host->cpu.ram_size - physical ||
        offset + length > 0x80000000ull ||
        get_ram_ptr(&host->cpu, guest, length, NULL) != host->cpu.ram + physical ||
        ww_host_linked_alias_overlaps(guest, length)) {
        error = "DI read requires a supported read DMA into real MEM1 and a signed 32-bit disc range";
    } else if (host->disc_reads == UINT64_MAX) {
        error = "DI read counter exhausted";
    } else if (ww_host_trace_transfer(host->disc_reads + 1u) &&
               fprintf(stderr, "[di-read] transfer=%llu guest=%08X disc=%08X bytes=%u backend=%s pc=%08X\n",
                       (unsigned long long)(host->disc_reads + 1u), guest, (u32)offset, length,
                       bw_dvd_io_backend_name(), host->cpu.pc) < 0) {
        error = "DI read trace write failed";
    } else if (!host->disc_owned) {
        if (dvd_image_ready())
            error = "DI read cannot borrow another owner's disc image";
        else if (!dvd_open_image(BW_DVD_INSTALL_PATH))
            error = "DI read requires the verified private image at ux0:data/bluewake/GZLE01.iso";
        else
            host->disc_owned = true;
    }
    if (error == NULL) {
        BwDvdIoStats before, after;
        bw_dvd_io_stats(&before);
        if (!dvd_image_ready() || before.total_read_bytes > UINT64_MAX - length) {
            error = "DI read lost its disc image or exhausted the seam byte counter";
        } else {
            dvd_read_to_guest(&host->cpu, guest, (u32)offset, length);
            bw_dvd_io_stats(&after);
            if (after.total_read_bytes < before.total_read_bytes ||
                after.total_read_bytes - before.total_read_bytes != length)
                error = "DI disc read failed or was short; zero fill is not successful DMA";
            else
                host->disc_reads++;
        }
    }
    if (error != NULL) {
        ww_host_fail(host, error, command->command[0], host->cpu.pc);
        return DOL_DI_COMMAND_ERROR;
    }
    return DOL_DI_COMMAND_COMPLETE;
}
