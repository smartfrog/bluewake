#include "boot_host.h"
#include "cache_flush_fallback.h"
#include "external_memory.h"
#include "interrupt_sources.h"
#include "edge_intercept_table.h"
#include "gxruntime/boot.h"
#include "gxruntime/guest_memory_dirty.h"

#include <string.h>

void ww_host_fail(WwHost* host, const char* error, u32 address, u32 pc) {
    if (host->error == NULL) {
        host->error = error;
        host->error_address = address;
        host->error_pc = pc;
    }
    host->cpu.exception |= PPC_EXC_PROGRAM;
}

static WwHost* owner(CPUState* cpu) {
    return cpu->external_user_data;
}

static u32 canonical_dispatch_address(const CPUState* cpu, u32 address) {
    if (address < cpu->ram_size)
        return address | GC_RAM_BASE;
    if (address >= 0xC0000000u && address < 0xC0400000u)
        return address & ~0x40000000u;
    return address;
}

static void advance(CPUState* cpu, u64 cycles, void* user) {
    WwHost* host = user;
    const u64 total = cycles + host->clock_remainder;
    const u64 ticks = total / 12u;
    host->clock_remainder = (u32)(total % 12u);
    cpu->timebase += ticks;
    if (host->decrementer_valid) {
        if (ticks > host->decrementer)
            ww_host_fail(host, "decrementer interrupt delivery unsupported before GX", 22u, cpu->pc);
        host->decrementer -= (u32)ticks;
    }
}

static void observe(CPUState* cpu) {
    WwHost* host = owner(cpu);
    bluewake_cycle_domain_observe(&host->cycles, cpu, cpu->cycle_observation_suffix);
}

static u32 spr_read(CPUState* cpu, u16 spr, u32 cia) {
    WwHost* host = owner(cpu);
    if (spr == 287u)
        return 0x00083214u;
    if (spr == 1008u)
        return host->hid0;
    if (spr == 1017u)
        return host->l2cr;
    if (spr == 22u || spr == 268u || spr == 269u) {
        observe(cpu);
        return spr == 22u ? host->decrementer :
               spr == 268u ? (u32)cpu->timebase : (u32)(cpu->timebase >> 32);
    }
    ww_host_fail(host, "unsupported SPR read", spr, cia);
    return 0u;
}

static void spr_write(CPUState* cpu, u16 spr, u32 value, u32 cia) {
    WwHost* host = owner(cpu);
    if (spr == 284u || spr == 285u) {
        bluewake_cycle_domain_write_timebase(&host->cycles, cpu, spr, value);
    } else if (spr == 22u) {
        observe(cpu);
        host->decrementer = value;
        host->decrementer_valid = true;
    } else if (spr == 1008u) {
        host->hid0 = value;
    } else if (spr == 1017u) {
        if (value & ~0x80200000u) {
            ww_host_fail(host, "unsupported L2CR control flags", spr, cia);
            return;
        }
        if (value & 0x00200000u) {
            // There are no L2 lines in this coherent-memory host to invalidate.
            ppc_memory_fence();
        }
        host->l2cr = value;
    } else {
        ww_host_fail(host, "unsupported SPR write", spr, cia);
    }
}

static void cache_control(CPUState* cpu, u8 operation, u32 ea, u32 cia) {
    if (operation == PPC_CACHE_DCBST || operation == PPC_CACHE_DCBF) {
        dol_guest_memory_dirty_mark(ea & ~31u, 32u);
        return;
    }
    if (operation == PPC_CACHE_ICBI) {
        WwHost* host = owner(cpu);
        const u32 address = canonical_dispatch_address(cpu, ea);
        if (host->module == NULL || !ww_code_guard_matches(&host->code_guard, cpu, address)) {
            ww_host_fail(host, "instruction cache invalidation requires unmodified mapped DOL code", address, cia);
            return;
        }
        ppc_memory_fence();
        return;
    }
    ww_host_fail(owner(cpu), "unsupported cache operation", operation, cia);
}

static void fallback(CPUState* cpu, u32 raw, u32 cia) {
    if (bluewake_cache_flush_fallback(cpu, raw, cia))
        return;
    if ((raw >> 26) == 31u) {
        const u32 xo = (raw >> 1) & 0x3FFu;
        const u16 spr = (u16)(((raw >> 16) & 31u) | ((raw >> 6) & 0x3E0u));
        const u8 reg = (u8)((raw >> 21) & 31u);
        if (xo == 982u) {
            const u32 ra = (raw >> 16) & 31u;
            const u32 rb = (raw >> 11) & 31u;
            const u32 ea = (ra == 0u ? 0u : cpu->gpr[ra]) + cpu->gpr[rb];
            ppc_cache_control(cpu, PPC_CACHE_ICBI, ea, cia);
            if (!cpu->exception)
                cpu->pc = cia + 4u;
            return;
        }
        if (xo == 339u || xo == 467u) {
            if (xo == 339u)
                cpu->gpr[reg] = ppc_mfspr(cpu, spr, cia);
            else
                ppc_mtspr(cpu, spr, cpu->gpr[reg], cia);
            if (!cpu->exception)
                cpu->pc = cia + 4u;
            return;
        }
    }
    ww_host_fail(owner(cpu), "unsupported instruction fallback", raw, cia);
}

static bool host_call(CPUState* cpu, u32 service) {
    WwHost* host = owner(cpu);
    const u32 address = canonical_dispatch_address(cpu, service);
    if (host->module != NULL && (address & 3u) == 0u) {
        for (u32 index = 0u; index < host->module->num_code_ranges; ++index) {
            const StaticRecompRange* range = &host->module->code_ranges[index];
            if (address >= range->start && address < range->end)
                return false;
        }
    }
    ww_host_fail(host, "unsupported host call", service, cpu->pc);
    return true;
}

static DolDiCommandResult di_command(void* user, DolDiCommand* command) {
    WwHost* host = user;
    ww_host_fail(host, "DVD command requires private disc service (not packaged)", command->command[0], host->cpu.pc);
    return DOL_DI_COMMAND_ERROR;
}

static bool exi_transfer(void* user, DolExiTransfer* transfer) {
    WwHost* host = user;
    ww_host_fail(host, "EXI transfer unsupported", transfer->chip_select, host->cpu.pc);
    return false;
}

static bool in_range(u32 address, u8 size, u32 base, u32 length) {
    return address >= base && address - base < length && size <= length - (address - base);
}

static bool interrupt_register(u32 address, u8 size, bool write) {
    return (size == 4u && (address == DOL_PI_INTERRUPT_CAUSE || address == DOL_PI_INTERRUPT_MASK)) ||
           (size == 2u && (address == DOL_PE_INTERRUPT_STATUS || (!write && address == DOL_PE_TOKEN))) ||
           (size == 2u && (address & 1u) == 0u && in_range(address, size, DOL_VI_BASE, DOL_VI_REGISTER_BYTES));
}

static void refresh(WwHost* host) {
    BluewakeInterruptSources sources = {.di = dol_di_interrupt_pending(&host->di),
                                       .si = dol_si_interrupt_pending(&host->si)};
    bluewake_interrupt_sources_refresh(&host->interrupts, &host->cycles, &host->cpu, &sources);
    dol_interrupts_set_source(&host->interrupts, DOL_PI_CAUSE_EXI, dol_exi_interrupt_pending(&host->exi));
    if (dol_interrupts_external_pending(&host->interrupts))
        ww_host_fail(host, "external interrupt delivery unsupported before GX", host->interrupts.pi_cause, host->cpu.pc);
}

static u64 mmio_read(CPUState* cpu, u32 address, u8 size) {
    WwHost* host = owner(cpu);
    u64 value;
    if (bluewake_external_memory_read(cpu, address, size, &value) ||
        dol_guest_memory_read(&host->memory, address, size, &value))
        return value;
    observe(cpu);
    if (address == 0xCC00401Cu && size == 2u)
        value = host->mi_interrupt_mask;
    else if (address == 0xCC00500Au && size == 2u)
        value = 0x0804u | host->dsp_interrupt_mask;
    else if (address == 0xCC006C00u && size == 4u)
        dol_audio_dma_ai_mmio_read(&host->audio, DOL_AUDIO_DMA_AI_CONTROL_OFF, size, &value);
    else if (interrupt_register(address, size, false))
        value = dol_interrupts_mmio_read(&host->interrupts, address, size);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_DI_BASE, DOL_DI_REGISTER_BYTES))
        value = dol_di_mmio_read(&host->di, address, size);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_EXI_BASE, DOL_EXI_REGISTER_BYTES))
        value = dol_exi_mmio_read(&host->exi, address, size);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_SI_BASE, DOL_SI_REGISTER_BYTES))
        value = dol_si_mmio_read(&host->si, address, size);
    else {
        ww_host_fail(host, "unsupported MMIO read", address, cpu->pc);
        return 0u;
    }
    refresh(host);
    return value;
}

static void mmio_write(CPUState* cpu, u32 address, u64 value, u8 size) {
    WwHost* host = owner(cpu);
    if (in_range(address, size, 0xCC008000u, 0x20u)) {
        if (!ww_gx_write(&host->frontier, &host->gx, value, size, cpu->pc, cpu->gpr[1]))
            ww_host_fail(host, host->frontier.error, address, cpu->pc);
        return;
    }
    if (bluewake_external_memory_write(cpu, address, value, size) ||
        dol_guest_memory_write(&host->memory, address, size, value))
        return;
    observe(cpu);
    if (address == 0xCC00401Cu && size == 2u) {
        if (value & ~0x1Full) {
            ww_host_fail(host, "unsupported MI interrupt mask bits", address, cpu->pc);
            return;
        }
        host->mi_interrupt_mask = (u16)value;
    } else if (address == 0xCC00500Au && size == 2u) {
        if ((value & ~0x01F8ull) != 0x0804u) {
            ww_host_fail(host, "DSP execution/reset requires a real DSP backend", address, cpu->pc);
            return;
        }
        host->dsp_interrupt_mask = (u16)value & 0x0150u;
    } else if (address == 0xCC006C00u && size == 4u) {
        if ((value & ~0x3Cull) != DOL_AUDIO_DMA_AI_CONTROL_INIT) {
            ww_host_fail(host, "AI stream/rate change requires an audio backend", address, cpu->pc);
            return;
        }
        dol_audio_dma_ai_mmio_write(&host->audio, DOL_AUDIO_DMA_AI_CONTROL_OFF, size, value & ~8ull);
    } else if (interrupt_register(address, size, true))
        dol_interrupts_mmio_write(&host->interrupts, address, size, value);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_DI_BASE, DOL_DI_REGISTER_BYTES))
        dol_di_mmio_write(&host->di, cpu, address, size, value);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_EXI_BASE, DOL_EXI_REGISTER_BYTES))
        dol_exi_mmio_write(&host->exi, cpu, address, size, value);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_SI_BASE, DOL_SI_REGISTER_BYTES)) {
        if (address == DOL_SI_BASE + DOL_SI_COMCSR_OFF && ((u32)value & DOL_SI_TSTART)) {
            if (host->error == NULL) {
                host->error_width = size;
                host->error_value = value;
            }
            ww_host_fail(host, "SI transfer unsupported (no fabricated controller)", address, cpu->pc);
            return;
        }
        dol_si_mmio_write(&host->si, address, size, value);
    } else {
        ww_host_fail(host, "unsupported MMIO write", address, cpu->pc);
        return;
    }
    refresh(host);
}

bool ww_host_edge(void* user, CPUState* cpu, u32 address) {
    WwHost* host = user;
    host->edges++;
    if (host->frontier.complete || host->error != NULL) {
        host->edge_stopped = true;
        return true;
    }
    if (bluewake_edge_maybe_intercept(address)) {
        ww_host_fail(host, "desktop interception unsupported; guest not skipped", address, cpu->pc);
        host->edge_stopped = true;
        return true;
    }
    return false;
}

bool ww_host_init(WwHost* host) {
    if (host == NULL)
        return false;
    memset(host, 0, sizeof(*host));
    BlueWakeModuleStorage storage = {.cpu = &host->cpu};
    if (!bw_module_cpu_init(&storage))
        return false;
    if (!dol_guest_memory_init(&host->memory, NULL)) {
        bw_module_cpu_free(&storage);
        return false;
    }
    host->backing_bytes = host->cpu.ram_size + (u64)host->memory.config.vm_size + host->memory.config.locked_cache_size;
    host->cpu.external_user_data = host;
    host->cpu.external_read = mmio_read;
    host->cpu.external_write = mmio_write;
    host->cpu.spr_read = spr_read;
    host->cpu.spr_write = spr_write;
    host->cpu.cache_control = cache_control;
    host->cpu.instruction_fallback = fallback;
    host->cpu.host_call = host_call;
    bluewake_cycle_domain_init(&host->cycles, 256, advance, NULL, host);
    dol_interrupts_init(&host->interrupts);
    dol_di_init(&host->di);
    dol_di_set_command_callback(&host->di, di_command, host);
    dol_exi_init(&host->exi);
    dol_exi_set_transfer_callback(&host->exi, exi_transfer, host);
    dol_si_init(&host->si);
    dol_audio_dma_init(&host->audio);
    DolGuestAddressResolver resolver;
    dol_guest_address_resolver_init(&resolver, &host->memory, &host->cpu);
    dol_gx_recomp_init(&host->gx, &resolver);
    return true;
}

void ww_host_shutdown(WwHost* host) {
    host->module = NULL;
    ww_code_guard_free(&host->code_guard);
    ppc_guest_alias_clear();
    host->alias_bytes = 0u;
    host->alias_count = 0u;
    dol_guest_memory_shutdown(&host->memory);
    BlueWakeModuleStorage storage = {.cpu = &host->cpu};
    bw_module_cpu_free(&storage);
    host->backing_bytes = 0u;
}

static bool add_alias(WwHost* host, u32 address, u32 size, const u8* bytes) {
    if ((u64)address + size > UINT32_MAX + 1ull || !ppc_guest_alias_add(address, size, bytes)) {
        ww_host_fail(host, "REL alias allocation/range failure", address, host->cpu.pc);
        return false;
    }
    host->alias_bytes += size;
    host->alias_count++;
    return true;
}

bool ww_host_load(WwHost* host, const StaticRecompModuleDesc* module,
                   const BlueWakeRelData* data, u32 count, const char* dol) {
    BlueWakeModuleStorage storage;
    const char* error = bw_module_select_storage(module, NULL, NULL, &host->cpu, &storage);
    if (error != NULL || data == NULL || module->abi_version != STATICRECOMP_ABI_VERSION ||
        module->code_ranges == NULL || module->num_code_ranges == 0u) {
        ww_host_fail(host, error != NULL ? error : "static host requires ordinary CPU ABI, code ranges and REL export", 0u, 0u);
        return false;
    }
    DolLayout layout;
    if (!dol_load_into_ram(&host->cpu, dol, &layout) || layout.entry_point != module->entry_point) {
        ww_host_fail(host, "DOL load/descriptor entry mismatch", module->entry_point, 0u);
        return false;
    }
    if (!ww_code_guard_init(&host->code_guard, &host->cpu, &layout)) {
        ww_host_fail(host, "DOL code snapshot allocation/range failure", 0u, 0u);
        return false;
    }
    for (u32 i = 0u; i < count; ++i) {
        const BlueWakeRelData* image = &data[i];
        if (image->size == 0u || image->linked_start == 0u ||
            (image->bytes == NULL && image->module_id == 336u && image->section_index == 6u))
            continue;
        if (!add_alias(host, image->linked_start, image->size, image->bytes))
            return false;
    }
    for (u32 i = 0u; i < module->num_rel_modules; ++i) {
        const StaticRecompRelModule* rel = &module->rel_modules[i];
        for (u32 j = 0u; j < rel->num_sections; ++j) {
            const StaticRecompRelSection* section = &rel->sections[j];
            if (section->size == 0u || section->linked_start == 0u ||
                (section->module_id == 336u && section->section_index == 6u))
                continue;
            bool skip = false;
            for (u32 k = 0u; k < count; ++k)
                if (data[k].module_id == section->module_id && data[k].section_index == section->section_index)
                    skip = true;
            for (u32 k = 0u; k < module->num_code_ranges; ++k)
                if ((u64)module->code_ranges[k].start < (u64)section->linked_start + section->size &&
                    section->linked_start < module->code_ranges[k].end)
                    skip = true;
            if (!skip && !add_alias(host, section->linked_start, section->size, NULL))
                return false;
        }
    }
    boot_setup_os_globals(&host->cpu, &layout);
    host->module = module;
    host->cpu.pc = module->entry_point;
    ppc_lazy_fp_set_enabled(true);
    return true;
}
