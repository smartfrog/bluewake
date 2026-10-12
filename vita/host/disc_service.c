#include "boot_host.h"
#include "dvd_io.h"
#include "gxruntime/dvd.h"
#include "gxruntime/hle.h"

static const char* disc_ready(WwHost* host) {
    if (!host->disc_owned) {
        if (dvd_image_ready())
            return "DVD access cannot borrow another owner's disc image";
        if (!dvd_open_image(BW_DVD_INSTALL_PATH))
            return "DVD access requires the verified private image at ux0:data/bluewake/GZLE01.iso";
        host->disc_owned = true;
    }
    return dvd_image_ready() ? NULL : "DVD access lost its owned disc image";
}

static const char* dvd_path(CPUState* cpu, char path[256]) {
    const u32 address = cpu->gpr[3];
    for (u32 index = 0u; index < 256u; ++index) {
        if (address == 0u || address > UINT32_MAX - index)
            return "DVD path requires a bounded mapped guest string";
        const u8* byte = get_ram_ptr(cpu, address + index, 1u, NULL);
        if (byte == NULL)
            return "DVD path requires a bounded mapped guest string";
        path[index] = (char)*byte;
        if (*byte == 0u)
            return NULL;
    }
    return "DVD path is not terminated within 256 bytes";
}

bool ww_host_dvd_edge(WwHost* host, u32 address) {
    const bool entry_only = address == 0x8030F2B0u;
    const bool fast = address == 0x8030F5A4u;
    if (!entry_only && !fast && address != 0x8030F618u)
        return false;
    CPUState* cpu = &host->cpu;
    const u32 file_info = cpu->gpr[4];
    const u32 physical = file_info - GC_RAM_BASE;
    const char* error = NULL;
    char path[256] = {0};
    if ((cpu->lr & 3u) != 0u)
        error = "DVD return requires an aligned guest instruction";
    else if (!entry_only && (cpu->ram == NULL || (file_info & 3u) != 0u || physical > cpu->ram_size ||
             0x3Cu > cpu->ram_size - physical || ww_host_linked_alias_overlaps(file_info, 0x3Cu) ||
             get_ram_ptr(cpu, file_info, 0x3Cu, NULL) != cpu->ram + physical))
        error = "DVD file info requires an aligned real MEM1 range without aliases";
    if (error == NULL && !fast)
        error = dvd_path(cpu, path);
    if (error == NULL)
        error = disc_ready(host);
    if (error == NULL) {
        const s32 entry = fast ? (s32)cpu->gpr[3] : dvd_path_to_entrynum(path);
        if (fprintf(stderr, "[dvd-open] kind=%s path=%s entry=%d file=%08X backend=%s pc=%08X\n",
                    entry_only ? "entry" : fast ? "fast" : "open", path, entry, file_info,
                    bw_dvd_io_backend_name(), address) < 0) {
            error = "DVD open trace write failed";
        } else {
            cpu->gpr[3] = (u32)entry;
            if (!entry_only)
                dol_hle_DVDFastOpen(cpu);
            cpu->pc = cpu->lr;
        }
    }
    if (error != NULL)
        ww_host_fail(host, error, address, address);
    return true;
}

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
        error = disc_ready(host);
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
