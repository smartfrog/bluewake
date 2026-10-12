#include "boot_host.h"
#include "dvd_io.h"
#include "gxruntime/dvd.h"
#include "dvd_file_info.h"
#include "callback_delivery.h"
#include "scheduler_contract.h"

const char* ww_host_disc_ready(WwHost* host) {
    if (!host->disc_owned) {
        if (dvd_image_ready())
            return "DVD access cannot borrow another owner's disc image";
        if (!dvd_open_image(BW_DVD_INSTALL_PATH))
            return "DVD access requires the verified private image at ux0:data/bluewake/GZLE01.iso";
        host->disc_owned = true;
    }
    return dvd_image_ready() ? NULL : "DVD access lost its owned disc image";
}

static bool mem1_range(WwHost* host, u32 guest, u32 length) {
    CPUState* cpu = &host->cpu;
    const u32 physical = guest - GC_RAM_BASE;
    return cpu->ram != NULL && physical <= cpu->ram_size && length <= cpu->ram_size - physical &&
           !ww_host_linked_alias_overlaps(guest, length) &&
           get_ram_ptr(cpu, guest, length, NULL) == cpu->ram + physical;
}

static bool file_info_valid(WwHost* host, u32 file_info) {
    return (file_info & 3u) == 0u && mem1_range(host, file_info, 0x3Cu);
}

static const char* read_disc(WwHost* host, u32 guest, u64 offset, u32 length) {
    if (length == 0u || offset + length > 0x80000000ull)
        return "DVD read requires a nonempty signed 32-bit disc range";
    const char* error = ww_host_disc_ready(host);
    if (error != NULL)
        return error;
    BwDvdIoStats before, after;
    bw_dvd_io_stats(&before);
    if (before.total_read_bytes > UINT64_MAX - length)
        return "DVD read exhausted the seam byte counter";
    dvd_read_to_guest(&host->cpu, guest, (u32)offset, length);
    bw_dvd_io_stats(&after);
    if (after.total_read_bytes < before.total_read_bytes || after.total_read_bytes - before.total_read_bytes != length)
        return "DVD read failed or was short; zero fill is not successful transfer";
    return NULL;
}

static void dvd_read_edge(WwHost* host, u32 address) {
    CPUState* cpu = &host->cpu;
    const bool asynchronous = address == 0x8030FADCu;
    const u32 callback = asynchronous ? cpu->gpr[7] : 0u;
    const bool deferred = callback != 0u;
    const u32 file_info = cpu->gpr[3];
    const u32 guest = cpu->gpr[4];
    const u32 length = cpu->gpr[5];
    const u32 offset = cpu->gpr[6];
    const char* error = NULL;
    u32 error_address = address;
    u64 disc = 0u;
    if (deferred && (host->module == NULL || host->module->dispatch == NULL ||
                    !ww_host_module_contains_address(host->module, callback))) {
        error = "DVD async callback requires a module-owned compiled instruction";
        error_address = callback;
    } else if (deferred && (host->dvd_completions.count >= BLUEWAKE_DVD_COMPLETION_CAPACITY ||
               host->cycles.absolute_cycles > UINT64_MAX - WW_DVD_CALLBACK_DELAY_CYCLES)) {
        error = "DVD async completion queue is full or its deadline would overflow";
    } else if ((cpu->lr & 3u) != 0u || !file_info_valid(host, file_info)) {
        error = "DVD read requires valid MEM1 file info and an aligned return";
    } else if (!mem1_range(host, guest, length)) {
        error = "DVD read requires a real MEM1 destination without aliases";
    } else {
        const u32 file_length = mem_read32(cpu, file_info + DVD_FI_LENGTH);
        disc = (u64)mem_read32(cpu, file_info + DVD_FI_STARTADDR) + offset;
        if (offset > file_length || (u64)length > (u64)file_length - offset + DVD_MIN_TRANSFER_SIZE)
            error = "DVD read exceeds the sourced file range and SDK padding";
    }
    if (error == NULL && fprintf(stderr, "[dvd-read] kind=%s guest=%08X disc=%08llX bytes=%u file=%08X backend=%s pc=%08X\n",
                                deferred ? "async-callback" : asynchronous ? "async-null" : "sync",
                                guest, (unsigned long long)disc, length, file_info,
                                bw_dvd_io_backend_name(), address) < 0)
        error = "DVD read trace write failed";
    if (error == NULL)
        error = read_disc(host, guest, disc, length);
    if (error != NULL) {
        ww_host_fail(host, error, error_address, address);
        return;
    }
    if (deferred) {
        const BluewakeDvdCompletion completion = {
            .ready_cycle = host->cycles.absolute_cycles + WW_DVD_CALLBACK_DELAY_CYCLES,
            .callback = callback, .file_info = file_info, .length = length,
        };
        if (!bluewake_dvd_enqueue(&host->dvd_completions, completion)) {
            ww_host_fail(host, "DVD async completion queue changed after validation", address, address);
            return;
        }
    }
    host_dvd_set_transfer_state(cpu, file_info, deferred ? 1u : 0u, deferred ? 0u : length);
    cpu->gpr[3] = asynchronous ? 1u : length;
    cpu->pc = cpu->lr;
}

static void prepare_callback(CPUState* cpu, void* user) {
    WwHost* host = user;
    bluewake_cycle_domain_begin_turn(&host->cycles, cpu);
    bluewake_cycle_domain_prepare_dispatch(&host->cycles, cpu);
}

static int dispatch_callback(CPUState* cpu, u32 address) {
    WwHost* host = cpu->external_user_data;
    const int result = ww_host_dispatch(host, address);
    ww_host_end_dispatch(host);
    return result;
}

void ww_host_dvd_complete(WwHost* host) {
    if (host == NULL || host->error != NULL || host->dvd_callback_active || host->dvd_completions.count == 0u)
        return;
    CPUState* cpu = &host->cpu;
    const bool safe = cpu->exception == 0u && (cpu->msr & PPC_MSR_EE) != 0u &&
                      bluewake_scheduler_interrupt_safe(cpu->pc);
    const BluewakeDvdCompletion completion = host->dvd_completions.entries[0];
    if (!safe || host->cycles.absolute_cycles < completion.ready_cycle)
        return;
    if (!file_info_valid(host, completion.file_info) || cpu->gpr[1] < GC_RAM_BASE + 8u ||
        !mem1_range(host, cpu->gpr[1] - 8u, 8u) || host->module == NULL || host->module->dispatch == NULL ||
        !ww_host_module_contains_address(host->module, completion.callback)) {
        ww_host_fail(host, "DVD completion requires valid file info, callback stack and module ownership",
                     completion.callback, cpu->pc);
        return;
    }
    if (fprintf(stderr, "[dvd-completion] stage=deliver callback=%08X file=%08X bytes=%u ready=%llu cycles=%llu\n",
                completion.callback, completion.file_info, completion.length,
                (unsigned long long)completion.ready_cycle, (unsigned long long)host->cycles.absolute_cycles) < 0) {
        ww_host_fail(host, "DVD completion trace write failed", completion.callback, cpu->pc);
        return;
    }
    BluewakeDvdCompletion ready;
    if (!bluewake_dvd_take_ready(&host->dvd_completions, host->cycles.absolute_cycles, safe, &ready)) {
        ww_host_fail(host, "DVD completion queue changed after validation", completion.callback, cpu->pc);
        return;
    }
    host_dvd_set_transfer_state(cpu, ready.file_info, 0u, ready.length);
    StaticRecompModuleDesc module = *host->module;
    module.dispatch = dispatch_callback;
    host->dvd_callback_active = true;
    const BluewakeCallbackDeliveryResult delivery = bluewake_deliver_guest_callback(
        cpu, &module, ready.callback, ready.length, ready.file_info, 4096u, prepare_callback, host);
    host->dvd_callback_active = false;
    if (fprintf(stderr, "[dvd-completion] stage=return callback=%08X completed=%u dispatches=%u pc=%08X exception=%08X\n",
                ready.callback, delivery.completed, delivery.dispatches, delivery.terminal_pc, delivery.exception) < 0)
        ww_host_fail(host, "DVD callback return trace write failed", ready.callback, cpu->pc);
    if (!delivery.completed)
        ww_host_fail(host, "DVD guest callback did not complete", ready.callback, delivery.terminal_pc);
}

static const char* guest_text(CPUState* cpu, u32 address, char path[256]) {
    for (u32 index = 0u; index < 256u; ++index) {
        if (address == 0u || address > UINT32_MAX - index)
            return "path requires a bounded mapped guest string";
        const u8* byte = get_ram_ptr(cpu, address + index, 1u, NULL);
        if (byte == NULL)
            return "path requires a bounded mapped guest string";
        path[index] = (char)*byte;
        if (*byte == 0u)
            return NULL;
    }
    return "path is not terminated within 256 bytes";
}

bool ww_host_dvd_edge(WwHost* host, u32 address) {
    if (address == 0x80240744u) {
        CPUState* cpu = &host->cpu;
        const u32 object = cpu->gpr[3];
        char name[256] = {0};
        const char* error = NULL;
        if ((object & 3u) != 0u || !mem1_range(host, object, 0x20u))
            error = "REL frontier requires a mapped real MEM1 module object";
        else
            error = guest_text(cpu, mem_read32(cpu, object + 0x1Cu), name);
        if (error == NULL && fprintf(stderr, "[rel-frontier] name=%s object=%08X module=%08X pc=%08X\n",
                                    name, object, mem_read32(cpu, object + 0x10u), address) < 0)
            error = "REL frontier trace write failed";
        if (error == NULL && host->rel_image.guest == 0u)
            error = ww_host_materialize_rel(host, object, name);
        if (error == NULL)
            error = ww_host_link_profile_list(host, object);
        if (error != NULL)
            ww_host_fail(host, error, address, address);
        return true;
    }
    if (address == 0x8030FBCCu || address == 0x8030FADCu) {
        dvd_read_edge(host, address);
        return true;
    }
    const bool entry_only = address == 0x8030F2B0u;
    const bool fast = address == 0x8030F5A4u;
    if (!entry_only && !fast && address != 0x8030F618u)
        return false;
    CPUState* cpu = &host->cpu;
    const u32 file_info = cpu->gpr[4];
    const char* error = NULL;
    char path[256] = {0};
    if ((cpu->lr & 3u) != 0u)
        error = "DVD return requires an aligned guest instruction";
    else if (!entry_only && !file_info_valid(host, file_info))
        error = "DVD file info requires an aligned real MEM1 range without aliases";
    if (error == NULL && !fast)
        error = guest_text(cpu, cpu->gpr[3], path);
    if (error == NULL)
        error = ww_host_disc_ready(host);
    if (error == NULL) {
        const s32 entry = fast ? (s32)cpu->gpr[3] : dvd_path_to_entrynum(path);
        if (fprintf(stderr, "[dvd-open] kind=%s path=%s entry=%d file=%08X backend=%s pc=%08X\n",
                    entry_only ? "entry" : fast ? "fast" : "open", path, entry, file_info,
                    bw_dvd_io_backend_name(), address) < 0) {
            error = "DVD open trace write failed";
        } else {
            if (entry_only)
                cpu->gpr[3] = (u32)entry;
            else
                cpu->gpr[3] = host_dvd_fill_file_info(cpu, entry, file_info) ? 1u : 0u;
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
        physical > host->cpu.ram_size || length > host->cpu.ram_size - physical ||
        offset + length > 0x80000000ull || !mem1_range(host, guest, length)) {
        error = "DI read requires a supported read DMA into real MEM1 and a signed 32-bit disc range";
    } else if (host->disc_reads == UINT64_MAX) {
        error = "DI read counter exhausted";
    } else if (ww_host_trace_transfer(host->disc_reads + 1u) &&
               fprintf(stderr, "[di-read] transfer=%llu guest=%08X disc=%08X bytes=%u backend=%s pc=%08X\n",
                       (unsigned long long)(host->disc_reads + 1u), guest, (u32)offset, length,
                       bw_dvd_io_backend_name(), host->cpu.pc) < 0) {
        error = "DI read trace write failed";
    }
    if (error == NULL) {
        error = read_disc(host, guest, offset, length);
        if (error == NULL)
            host->disc_reads++;
    }
    if (error != NULL) {
        ww_host_fail(host, error, command->command[0], host->cpu.pc);
        return DOL_DI_COMMAND_ERROR;
    }
    return DOL_DI_COMMAND_COMPLETE;
}
