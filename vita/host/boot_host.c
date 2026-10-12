#include "boot_host.h"
#include "cache_flush_fallback.h"
#include "external_memory.h"
#include "interrupt_sources.h"
#include "external_vector.h"
#include "di_inquiry.h"
#include "edge_intercept_table.h"
#include "callback_delivery.h"
#include "gxruntime/boot.h"
#include "gxruntime/guest_memory_dirty.h"
#include "gxruntime/aram.h"
#include "gxruntime/dvd.h"
#include "ww05_dsp_init.h"
#include "guest_timing.h"

#include <string.h>

#define PPC_EXC_EXTERNAL 0x00000040u
#define PPC_VECTOR_EXTERNAL 0x00500u
#define PPC_EXC_DECREMENTER 0x00000080u
#define PPC_VECTOR_DECREMENTER 0x00900u
#define WW_HOST_CYCLES_PER_TIMEBASE_TICK GUEST_CYCLES_PER_TIMEBASE_TICK

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

bool ww_host_linked_alias_overlaps(u32 address, u32 size) {
    for (u32 offset = 0u; offset < size; ++offset) {
        u8* alias;
        if (ppc_guest_alias_resolve(address + offset, 1u, &alias, NULL))
            return true;
    }
    return false;
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
    const u64 ticks = total / WW_HOST_CYCLES_PER_TIMEBASE_TICK;
    const bool aram_busy = host->dsp.aram_cycles_remaining != 0u;
    const u32 prior_dsp_mailbox = host->dsp.dsp_mailbox;
    const bool init_active = !host->dsp.rom_selected && host->dsp.init_cycles_remaining != 0u;
    if (!ww_dsp_interface_advance(&host->dsp, cycles, ticks))
        ww_host_fail(host, host->dsp.error, WW_DSP_CONTROL, cpu->pc);
    if (init_active && host->dsp.dsp_mailbox != prior_dsp_mailbox && (host->dsp.dsp_mailbox & 0x80000000u)) {
        if (fprintf(stderr, "[dsp-init-mail] value=%08X cycles=%llu\n", host->dsp.dsp_mailbox,
                    (unsigned long long)host->cycles.absolute_cycles) < 0)
            ww_host_fail(host, "DSP init mail trace write failed", 0xCC005004u, cpu->pc);
    }
    if (init_active && host->dsp.init_cycles_remaining == 0u) {
        if (fprintf(stderr, "[dsp-init-halt] cycles=%llu\n", (unsigned long long)host->cycles.absolute_cycles) < 0)
            ww_host_fail(host, "DSP init halt trace write failed", WW_DSP_CONTROL, cpu->pc);
    }
    const u64 transfer = host->dsp.aram.transfer_count;
    if (aram_busy && host->dsp.aram_cycles_remaining == 0u && ww_host_trace_transfer(transfer)) {
        if (fprintf(stderr, "[aram-complete] transfer=%llu cycles=%llu\n",
                    (unsigned long long)transfer, (unsigned long long)host->cycles.absolute_cycles) < 0)
            ww_host_fail(host, "ARAM completion trace write failed", WW_DSP_CONTROL, cpu->pc);
    }
    host->clock_remainder = (u32)(total % WW_HOST_CYCLES_PER_TIMEBASE_TICK);
    cpu->timebase += ticks;
    if (host->decrementer_valid) {
        if (ticks > host->decrementer)
            host->decrementer_pending = true;
        host->decrementer -= (u32)ticks;
    }
    ww_host_vi_advance(host, cycles);
}

static u64 deadline(const CPUState* cpu, void* user) {
    const WwHost* host = user;
    if (cpu->exception == 0u && (cpu->msr & PPC_MSR_EE) && host->decrementer_pending)
        return 1u;
    const u64 decrementer_expiry = host->decrementer_valid ?
        ((u64)host->decrementer + 1u) * WW_HOST_CYCLES_PER_TIMEBASE_TICK - host->clock_remainder : UINT64_MAX;
    const u64 dsp_expiry = ww_dsp_interface_deadline(&host->dsp);
    u64 expiry = decrementer_expiry < dsp_expiry ? decrementer_expiry : dsp_expiry;
    if (host->vi_clock_enabled) {
        u64 next;
        if (dol_event_clock_next_deadline(&host->vi_clock.events, &next)) {
            const u64 now = dol_vi_clock_now(&host->vi_clock);
            const u64 vi_expiry = next > now ? next - now : 1u;
            if (vi_expiry < expiry)
                expiry = vi_expiry;
        }
    }
    if (host->dsp.init_ticks_remaining != 0u) {
        const u64 init_expiry = (u64)host->dsp.init_ticks_remaining * WW_HOST_CYCLES_PER_TIMEBASE_TICK - host->clock_remainder;
        if (init_expiry < expiry)
            expiry = init_expiry;
    }
    if (host->cycles.dispatch_cycles == 0u)
        return expiry;
    const u64 remaining = host->cycles.dispatch_cycles < (u64)host->cycles.cap ?
        (u64)host->cycles.cap - host->cycles.dispatch_cycles : 1u;
    return expiry < remaining ? expiry : remaining;
}

static void close_spent_quantum(WwHost* host, CPUState* cpu) {
    // Zero remaining allowance makes emitted budget guards yield without manufacturing a cycle charge.
    if (host->cycles.dispatch_cycles >= (u64)host->cycles.cap)
        cpu->cycle_budget = 0;
}

static void observe(CPUState* cpu) {
    WwHost* host = owner(cpu);
    bluewake_cycle_domain_observe(&host->cycles, cpu, cpu->cycle_observation_suffix);
    close_spent_quantum(host, cpu);
}

static bool fifo_has_input(const WwHost* host);

static u32 spr_read(CPUState* cpu, u16 spr, u32 cia) {
    WwHost* host = owner(cpu);
    if (spr == 272u)
        return host->sprg0;
    u32 bat_value;
    if (ww_bat_read(&host->bat, spr, &bat_value))
        return bat_value;
    if (spr == 287u)
        return 0x00083214u;
    if (spr == 1008u)
        return host->hid0;
    if (spr == 1017u)
        return host->l2cr;
    if (spr == 921u) {
        observe(cpu);
        return host->wpar | (host->gather.size != 0u ? 1u : 0u);
    }
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
    if (spr == 272u) {
        host->sprg0 = value;
    } else if (spr == 921u) {
        observe(cpu);
        if (host->error != NULL)
            return;
        const char* error = NULL;
        if (value != 0x0C008000u || fifo_has_input(host) || host->cp_fifo_distance != 0u)
            error = "WPAR requires observed empty gather configuration; redirection or post-input reset not implemented";
        else if (fprintf(stderr, "[wpar] spr=921 address=%08X bne=0 gather_bytes=%u cp_distance=%u pc=%08X\n",
                         value, host->gather.size, host->cp_fifo_distance, cia) < 0)
            error = "WPAR trace write failed";
        if (error != NULL) {
            host->error_width = 4u;
            host->error_value = value;
            ww_host_fail(host, error, spr, cia);
            return;
        }
        host->wpar = value;
    } else if (spr == 284u || spr == 285u) {
        bluewake_cycle_domain_write_timebase(&host->cycles, cpu, spr, value);
    } else if (spr == 22u) {
        observe(cpu);
        if ((host->decrementer & 0x80000000u) == 0u && (value & 0x80000000u) != 0u)
            host->decrementer_pending = true;
        host->decrementer = value;
        host->decrementer_valid = true;
        bluewake_cycle_domain_rebudget(&host->cycles, cpu);
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
    } else if (ww_bat_contains(spr)) {
        const char* error = ww_bat_write(&host->bat, spr, value, cpu->msr);
        if (error == NULL && (spr & 1u) == 0u && (value & 3u) != 0u) {
            u32 lower;
            if (!ww_bat_read(&host->bat, spr + 1u, &lower))
                error = "BAT trace cannot read matching lower register";
            else if (fprintf(stderr, "[bat] spr=%u upper=%08X lower=%08X bytes=%u pc=%08X\n", spr, value,
                             lower, (((value >> 2u) & 0x7FFu) + 1u) << 17u, cia) < 0)
                error = "BAT trace write failed";
        }
        if (error != NULL) {
            if (host->error == NULL) {
                host->error_width = 4u;
                host->error_value = value;
            }
            ww_host_fail(host, error, spr, cia);
            return;
        }
    } else {
        ww_host_fail(host, "unsupported SPR write", spr, cia);
    }
    close_spent_quantum(host, cpu);
}

static void cache_control(CPUState* cpu, u8 operation, u32 ea, u32 cia) {
    if (operation == PPC_CACHE_DCBST || operation == PPC_CACHE_DCBF) {
        dol_guest_memory_dirty_mark(ea & ~31u, 32u);
        return;
    }
    if (operation == PPC_CACHE_DCBI) {
        // Guest memory is coherent; there are no stale data-cache lines to discard.
        ppc_memory_fence();
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
        if (xo == 470u || xo == 982u) {
            const u32 ra = (raw >> 16) & 31u;
            const u32 rb = (raw >> 11) & 31u;
            const u32 ea = (ra == 0u ? 0u : cpu->gpr[ra]) + cpu->gpr[rb];
            ppc_cache_control(cpu, xo == 470u ? PPC_CACHE_DCBI : PPC_CACHE_ICBI, ea, cia);
            if (!cpu->exception)
                cpu->pc = cia + 4u;
            return;
        }
        if (xo == 339u || xo == 467u) {
            WwHost* host = owner(cpu);
            const bool trace = host->dispatch_trace_remaining != 0u && spr == 1008u;
            if (trace && fprintf(stderr, "[spr-fallback] stage=enter pc=%08X spr=%u value=%08X\n",
                                 cia, spr, cpu->gpr[reg]) < 0) {
                ww_host_fail(host, "SPR fallback trace write failed", spr, cia);
                return;
            }
            if (xo == 339u)
                cpu->gpr[reg] = ppc_mfspr(cpu, spr, cia);
            else
                ppc_mtspr(cpu, spr, cpu->gpr[reg], cia);
            if (!cpu->exception)
                cpu->pc = cia + 4u;
            if (trace && fprintf(stderr, "[spr-fallback] stage=return pc=%08X spr=%u hid0=%08X exception=%08X\n",
                                 cpu->pc, spr, host->hid0, cpu->exception) < 0)
                ww_host_fail(host, "SPR fallback trace write failed", spr, cia);
            return;
        }
    }
    ww_host_fail(owner(cpu), "unsupported instruction fallback", raw, cia);
}

bool ww_host_module_contains_address(const StaticRecompModuleDesc* module, u32 address) {
    if (module != NULL && (address & 3u) == 0u) {
        for (u32 index = 0u; index < module->num_code_ranges; ++index) {
            const StaticRecompRange* range = &module->code_ranges[index];
            if (address >= range->start && address < range->end)
                return true;
        }
    }
    return false;
}

static bool host_call(CPUState* cpu, u32 service) {
    WwHost* host = owner(cpu);
    const u32 address = canonical_dispatch_address(cpu, service);
    if (ww_host_module_contains_address(host->module, address))
        return false;
    ww_host_fail(host, "unsupported host call", service, cpu->pc);
    return true;
}

static DolDiCommandResult di_command(void* user, DolDiCommand* command) {
    WwHost* host = user;
    if (command->command[0] == 0x12000000u) {
        const u32 physical = command->dma_address;
        const u32 guest = GC_RAM_BASE | physical;
        if (command->cpu != &host->cpu || host->cpu.ram == NULL ||
            !command->dma || command->write || command->dma_length != 32u ||
            physical > host->cpu.ram_size || 32u > host->cpu.ram_size - physical ||
            get_ram_ptr(&host->cpu, guest, 32u, NULL) != host->cpu.ram + physical) {
            ww_host_fail(host, "DI inquiry requires a 32-byte read DMA into real MEM1", command->command[0], host->cpu.pc);
            return DOL_DI_COMMAND_ERROR;
        }
        if (ww_host_linked_alias_overlaps(guest, 32u)) {
            ww_host_fail(host, "DI inquiry DMA overlaps a linked alias", command->command[0], host->cpu.pc);
            return DOL_DI_COMMAND_ERROR;
        }
        if (fprintf(stderr, "[di-inquiry] guest=%08X bytes=32 sourced=12 reserved=preserved pc=%08X\n", guest, host->cpu.pc) < 0) {
            ww_host_fail(host, "DI inquiry trace write failed", command->command[0], host->cpu.pc);
            return DOL_DI_COMMAND_ERROR;
        }
        bluewake_di_inquiry_write(command->cpu, guest);
        return DOL_DI_COMMAND_COMPLETE;
    }
    return ww_host_di_read(host, command);
}

static u8 sram_read8(void* user, u32 physical) {
    WwHost* host = user;
    return mem_read8(&host->cpu, physical | GC_RAM_BASE);
}

static void sram_write8(void* user, u32 physical, u8 value) {
    WwHost* host = user;
    mem_write8(&host->cpu, physical | GC_RAM_BASE, value);
}

static bool exi_transfer(void* user, DolExiTransfer* transfer) {
    WwHost* host = user;
    const u32 frequency = (host->exi.channels[transfer->channel].status & DOL_EXI_STATUS_CLK_MASK) >> 4u;
    const u32 control_address = DOL_EXI_BASE + transfer->channel * DOL_EXI_CHANNEL_STRIDE + DOL_EXI_CONTROL_OFF;
    if (host->exi_transfers == UINT64_MAX) {
        ww_host_fail(host, "EXI transfer counter exhausted", control_address, host->cpu.pc);
        return false;
    }
    const u64 count = ++host->exi_transfers;
    if (ww_host_trace_transfer(count) &&
        fprintf(stderr, "[exi] transfer=%llu channel=%u chip_select=%u frequency_code=%u direction=%u length=%u dma=%u dma_address=%08X immediate=%08X pc=%08X\n",
                (unsigned long long)count, transfer->channel, transfer->chip_select, frequency, transfer->direction,
                transfer->length, transfer->dma, transfer->dma_address, *transfer->immediate_data, host->cpu.pc) < 0) {
        ww_host_fail(host, "EXI transfer trace write failed", control_address, host->cpu.pc);
        return false;
    }
    if (transfer->channel == 0u && transfer->chip_select == 2u &&
        transfer->direction <= DOL_EXI_TRANSFER_WRITE) {
        BluewakeIplSram* sram = &host->sram;
        if (!sram->command_latched) {
            const u32 address = (*transfer->immediate_data & 0x7FFFFFFFu) >> 6u;
            if (transfer->dma || transfer->direction != DOL_EXI_TRANSFER_WRITE ||
                transfer->length != 4u || address < 0x800004u ||
                address - 0x800004u >= BLUEWAKE_IPL_SRAM_SIZE) {
                ww_host_fail(host, "IPL command requires an implemented SRAM region", *transfer->immediate_data, host->cpu.pc);
                return false;
            }
        } else if (transfer->direction != (sram->write ? DOL_EXI_TRANSFER_WRITE : DOL_EXI_TRANSFER_READ) ||
                   sram->cursor > BLUEWAKE_IPL_SRAM_SIZE ||
                   transfer->length > BLUEWAKE_IPL_SRAM_SIZE - sram->cursor) {
            ww_host_fail(host, "SRAM transfer direction/range unsupported", sram->cursor, host->cpu.pc);
            return false;
        }
        const u32 physical = transfer->dma_address & 0x03FFFFE0u;
        if (transfer->dma && (physical > host->cpu.ram_size || transfer->length > host->cpu.ram_size - physical)) {
            ww_host_fail(host, "SRAM DMA requires mapped guest RAM", physical, host->cpu.pc);
            return false;
        }
        bluewake_ipl_sram_write(sram, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF,
                               host->exi.channels[0].control, sram_read8, sram_write8, host);
        if (!transfer->dma && transfer->direction == DOL_EXI_TRANSFER_READ)
            *transfer->immediate_data = bluewake_ipl_sram_read(sram, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF);
        return true;
    }
    ww_host_fail(host, "EXI transfer unsupported", transfer->chip_select, host->cpu.pc);
    return false;
}

static u32* fifo_register(WwHost* host, u32 address, u8 size) {
    if ((address == 0xCC000020u || address == 0xCC000022u) && size == 2u)
        return &host->cp_fifo_base;
    if ((address == 0xCC000024u || address == 0xCC000026u) && size == 2u)
        return &host->cp_fifo_end;
    if ((address == 0xCC000028u || address == 0xCC00002Au) && size == 2u)
        return &host->cp_fifo_high_watermark;
    if ((address == 0xCC00002Cu || address == 0xCC00002Eu) && size == 2u)
        return &host->cp_fifo_low_watermark;
    if ((address == 0xCC000034u || address == 0xCC000036u) && size == 2u)
        return &host->cp_fifo_write_pointer;
    if ((address == 0xCC000038u || address == 0xCC00003Au) && size == 2u)
        return &host->cp_fifo_read_pointer;
    if (size != 4u)
        return NULL;
    if (address == 0xCC00300Cu)
        return &host->pi_fifo_base;
    if (address == 0xCC003010u)
        return &host->pi_fifo_end;
    if (address == 0xCC003014u)
        return &host->pi_fifo_write_pointer;
    return NULL;
}

static bool in_range(u32 address, u8 size, u32 base, u32 length) {
    return address >= base && address - base < length && size <= length - (address - base);
}

static bool external_memory_address(CPUState* cpu, u32 address, u8 size, u32* resolved) {
    *resolved = address;
    if (!(cpu->msr & PPC_MSR_DR) && in_range(address, size, 0u, cpu->ram_size)) {
        const u32 cached = address | GC_RAM_BASE;
        if (ww_host_linked_alias_overlaps(cached, size))
            return false;
        *resolved = cached;
    }
    return true;
}

static bool interrupt_register(u32 address, u8 size, bool write) {
    return (size == 4u && (address == DOL_PI_INTERRUPT_CAUSE || address == DOL_PI_INTERRUPT_MASK)) ||
           (size == 2u && (address == DOL_PE_INTERRUPT_STATUS || (!write && address == DOL_PE_TOKEN))) ||
           (size == 2u && (address & 1u) == 0u && in_range(address, size, DOL_VI_BASE, DOL_VI_REGISTER_BYTES));
}

static void refresh(WwHost* host) {
    BluewakeInterruptSources sources = {.di = dol_di_interrupt_pending(&host->di),
                                       .dsp = ww_dsp_interface_interrupt_pending(&host->dsp),
                                       .si = dol_si_interrupt_pending(&host->si)};
    bluewake_interrupt_sources_refresh(&host->interrupts, &host->cycles, &host->cpu, &sources);
    dol_interrupts_set_source(&host->interrupts, DOL_PI_CAUSE_EXI, dol_exi_interrupt_pending(&host->exi));
    close_spent_quantum(host, &host->cpu);
}

static bool fifo_has_input(const WwHost* host) {
    return host->frontier.size != 0u || host->gather.size != 0u || host->gather.transfers != 0u;
}

static u16* pe_config_register(WwHost* host, u32 address, u8 size) {
    if (size != 2u || address < 0xCC001000u || address > 0xCC001008u || (address & 1u) != 0u)
        return NULL;
    return &host->pe_config[(address - 0xCC001000u) / 2u];
}

static u64 mmio_read(CPUState* cpu, u32 address, u8 size) {
    WwHost* host = owner(cpu);
    u64 value;
    u32 memory_address;
    if (!external_memory_address(cpu, address, size, &memory_address)) {
        ww_host_fail(host, "physical MEM1 access overlaps a linked alias", address, cpu->pc);
        return 0u;
    }
    if (bluewake_external_memory_read(cpu, memory_address, size, &value) ||
        dol_guest_memory_read(&host->memory, address, size, &value))
        return value;
    observe(cpu);
    if (ww_dsp_interface_read(&host->dsp, address, size, &value)) {
        refresh(host);
        return value;
    }
    if (address == 0xCC00003Au && size == 2u && fifo_has_input(host) && (host->cp_control & 1u) == 0u) {
        ww_host_fail(host, "CP read pointer sync requires empty disabled init; FIFO processor backend not implemented", address, cpu->pc);
        return 0u;
    }
    u32* fifo = fifo_register(host, address, size);
    u16* pe = pe_config_register(host, address, size);
    if (pe != NULL)
        value = *pe;
    else if (fifo != NULL)
        value = size == 2u ? (u16)(*fifo >> ((address & 2u) != 0u ? 16u : 0u)) : *fifo;
    else if (address == 0xCC000002u && size == 2u)
        value = host->cp_control;
    else if (address == 0xCC000004u && size == 2u)
        value = 0u;
    else if (address == 0xCC000006u && size == 2u)
        value = host->cp_perf_select;
    else if ((address == 0xCC000030u || address == 0xCC000032u) && size == 2u && (host->cp_control & 1u) != 0u)
        value = (u16)(host->cp_fifo_distance >> (address == 0xCC000032u ? 16u : 0u));
    else if (address == 0xCC00401Cu && size == 2u)
        value = host->mi_interrupt_mask;
    else if (address == 0xCC004020u && size == 2u)
        value = host->mi_unknown1;
    else if (address == 0xCC004010u && size == 2u)
        value = host->mi_protection;
    else if (address == 0xCC003024u && size == 4u)
        value = 0u; // Cold boot only; reset writes still require a drive-reset backend.
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

static void fail_mmio_write(WwHost* host, const char* error, u32 address, u64 value, u8 size) {
    if (host->error == NULL) {
        host->error_width = size;
        host->error_value = value;
    }
    ww_host_fail(host, error, address, host->cpu.pc);
}

static void mmio_write(CPUState* cpu, u32 address, u64 value, u8 size) {
    WwHost* host = owner(cpu);
    if (in_range(address, size, 0xCC008000u, 0x20u)) {
        observe(cpu);
        if (host->error != NULL)
            return;
        const char* error = ww_fifo_gather_write(host, value, size);
        if (error == NULL)
            error = ww_fifo_consume_first(host);
        if (error != NULL)
            fail_mmio_write(host, error, address, value, size);
        close_spent_quantum(host, cpu);
        return;
    }
    u32 memory_address;
    if (!external_memory_address(cpu, address, size, &memory_address)) {
        fail_mmio_write(host, "physical MEM1 access overlaps a linked alias", address, value, size);
        return;
    }
    if (bluewake_external_memory_write(cpu, memory_address, value, size)) {
        if (memory_address != address)
            clear_matching_reservation(cpu, address);
        return;
    }
    if (dol_guest_memory_write(&host->memory, address, size, value))
        return;
    observe(cpu);
    const u64 prior_aram_transfer = host->dsp.aram.transfer_count;
    const u16 prior_dsp_control = host->dsp.control;
    const bool dsp_written = ww_dsp_interface_write(&host->dsp, cpu, address, size, value);
    if (host->dsp.error != NULL) {
        fail_mmio_write(host, host->dsp.error, address, value, size);
        return;
    }
    if (dsp_written) {
        if ((prior_dsp_control & 0x0800u) && !(host->dsp.control & 0x0800u)) {
            if (fprintf(stderr, "[dsp-init-transfer] main=81000000 bytes=%u verified=%u init_code_ticks=%u pc=%08X\n",
                        WW_DSP_INIT_TRANSFER_BYTES, WW_DSP_INIT_UCODE_BYTES, host->dsp.init_ticks_remaining, cpu->pc) < 0)
                fail_mmio_write(host, "DSP init transfer trace write failed", address, value, size);
        }
        const BluewakeAramDma* aram = &host->dsp.aram;
        if (aram->transfer_count != prior_aram_transfer && ww_host_trace_transfer(aram->transfer_count)) {
            if (fprintf(stderr, "[aram-dma] transfer=%llu main=%08X aram=%08X bytes=%u direction=%u completion_in=%llu pc=%08X\n",
                        (unsigned long long)aram->transfer_count, aram->last_main_address,
                        aram->last_aram_address, aram->last_length, aram->last_direction,
                        (unsigned long long)host->dsp.aram_cycles_remaining, cpu->pc) < 0)
                fail_mmio_write(host, "ARAM transfer trace write failed", address, value, size);
        }
        refresh(host);
        return;
    }
    u32* fifo = fifo_register(host, address, size);
    u16* pe = pe_config_register(host, address, size);
    if (pe != NULL) {
        *pe = (u16)value;
    } else if (address == 0xCC000002u && size == 2u) {
        if (value != 0u && value != 4u && value != 0x14u && value != 0x15u) {
            fail_mmio_write(host, "CP control requests an unimplemented FIFO mode or interrupt mask", address, value, size);
            return;
        }
        host->cp_control = (u16)value;
        const char* error = ww_fifo_consume_first(host);
        if (error != NULL) {
            fail_mmio_write(host, error, address, value, size);
            return;
        }
    } else if (address == 0xCC000004u && size == 2u) {
        // The pinned ClearMetrics path has no side effects and does not acknowledge FIFO flags.
        if (value != 4u && (value != 3u || (host->cp_control & 1u) != 0u || host->cp_fifo_distance != 0u ||
            fifo_has_input(host) || (dol_interrupts_pi_cause(&host->interrupts) & DOL_PI_CAUSE_CP) != 0u)) {
            fail_mmio_write(host, "CP clear requires observed idle overflow/underflow acknowledgement; active FIFO or CP IRQ handling not implemented", address, value, size);
            return;
        }
    } else if (address == 0xCC000006u && size == 2u) {
        host->cp_perf_select = (u16)value & 7u;
    } else if ((address == 0xCC000030u || address == 0xCC000032u) && size == 2u) {
        if (value != 0u || host->cp_fifo_distance != 0u || fifo_has_input(host)) {
            fail_mmio_write(host, "CP FIFO distance requires empty disabled init; FIFO processor backend not implemented", address, value, size);
            return;
        }
    } else if (address == 0xCC00003Au && size == 2u && fifo_has_input(host)) {
        fail_mmio_write(host, "CP read pointer sync requires empty disabled init; FIFO processor backend not implemented", address, value, size);
        return;
    } else if (fifo != NULL) {
        if (fifo_has_input(host)) {
            fail_mmio_write(host, "FIFO reconfiguration after input requires a processor synchronization backend", address, value, size);
            return;
        }
        if (size == 2u) {
            const u32 shift = (address & 2u) != 0u ? 16u : 0u;
            const u32 mask = shift != 0u ? 0x03FFu : 0xFFE0u;
            *fifo = (*fifo & ~(0xFFFFu << shift)) | (((u32)value & mask) << shift);
        } else {
            *fifo = (u32)value & 0xFFFFFFE0u;
        }
    } else if (address == 0xCC00401Cu && size == 2u) {
        if (value & ~0x1Full) {
            fail_mmio_write(host, "unsupported MI interrupt mask bits", address, value, size);
            return;
        }
        host->mi_interrupt_mask = (u16)value;
    } else if (address == 0xCC004020u && size == 2u) {
        if (value != 0u) {
            fail_mmio_write(host, "nonzero MI UNKNOWN1 operation not implemented", address, value, size);
            return;
        }
        host->mi_unknown1 = (u16)value;
    } else if (address == 0xCC004010u && size == 2u) {
        if (value != 0xFFu) {
            fail_mmio_write(host, "MI protection requires unrestricted init; protection backend not implemented", address, value, size);
            return;
        }
        host->mi_protection = (u16)value;
    } else if (address == 0xCC006C00u && size == 4u) {
        if ((value & ~0x3Cull) != DOL_AUDIO_DMA_AI_CONTROL_INIT) {
            fail_mmio_write(host, "AI stream/rate change requires an audio backend", address, value, size);
            return;
        }
        dol_audio_dma_ai_mmio_write(&host->audio, DOL_AUDIO_DMA_AI_CONTROL_OFF, size, value & ~8ull);
    } else if (interrupt_register(address, size, true))
        dol_interrupts_mmio_write(&host->interrupts, address, size, value);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_DI_BASE, DOL_DI_REGISTER_BYTES))
        dol_di_mmio_write(&host->di, cpu, address, size, value);
    else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_EXI_BASE, DOL_EXI_REGISTER_BYTES)) {
        if (address < DOL_EXI_BASE + DOL_EXI_CHANNEL_STRIDE && address != DOL_EXI_BASE + DOL_EXI_CONTROL_OFF)
            bluewake_ipl_sram_write(&host->sram, address, (u32)value, sram_read8, sram_write8, host);
        dol_exi_mmio_write(&host->exi, cpu, address, size, value);
    } else if (size == 4u && (address & 3u) == 0u && in_range(address, size, DOL_SI_BASE, DOL_SI_REGISTER_BYTES)) {
        dol_si_mmio_write(&host->si, address, size, value);
        if (address == DOL_SI_BASE + DOL_SI_COMCSR_OFF && ((u32)value & DOL_SI_TSTART)) {
            const u32 channel = ((u32)value >> 1u) & 3u;
            u8* status = host->si.regs + DOL_SI_STATUS_OFF;
            u8* control = host->si.regs + DOL_SI_COMCSR_OFF;
            // An empty port completes with COMERR/NOREP, leaving the receive buffer untouched.
            write_be32(status, read_be32(status) | (0x08000000u >> (channel * 8u)));
            write_be32(control, read_be32(control) | 0x20000000u);
        }
    } else {
        fail_mmio_write(host, "unsupported MMIO write", address, value, size);
        return;
    }
    refresh(host);
}

bool ww_host_edge(void* user, CPUState* cpu, u32 address) {
    WwHost* host = user;
    host->edges++;
    if (host->dispatch_trace_remaining != 0u) {
        --host->dispatch_trace_remaining;
        if (fprintf(stderr, "[edge] pc=%08X downcount=%lld budget=%lld exception=%08X\n",
                    address, (long long)cpu->downcount, (long long)cpu->cycle_budget, cpu->exception) < 0) {
            ww_host_fail(host, "dispatch edge trace write failed", address, cpu->pc);
            host->edge_stopped = true;
            return true;
        }
    }
    if ((host->frontier.complete && !host->stream.enabled) || host->error != NULL) {
        host->edge_stopped = true;
        return true;
    }
    if (host->dvd_callback_active && address == BLUEWAKE_CALLBACK_RETURN_SENTINEL)
        return true;
    // Composite edges follow completed blocks; only in-block observations exclude an instruction suffix.
    bluewake_cycle_domain_flush(&host->cycles, cpu);
    refresh(host);
    if (!ww_bat_memory_supported(&host->bat, cpu->msr)) {
        ww_host_fail(host, "BAT translation requires a complete supported fixed memory view", address, cpu->pc);
        host->edge_stopped = true;
        return true;
    }
    if (host->cycles.absolute_cycles >= host->next_edge_progress || (host->edges & 0xFFFFu) == 0u) {
        if (fprintf(stderr, "[edge-progress] edges=%llu cycles=%llu pc=%08X tb=%llu dec=%08X pending=%u ee=%u downcount=%lld budget=%lld\n",
                    (unsigned long long)host->edges, (unsigned long long)host->cycles.absolute_cycles,
                    address, (unsigned long long)cpu->timebase, host->decrementer,
                    host->decrementer_pending, (cpu->msr & PPC_MSR_EE) != 0u,
                    (long long)cpu->downcount, (long long)cpu->cycle_budget) < 0) {
            ww_host_fail(host, "edge progress log write failed", address, cpu->pc);
            host->edge_stopped = true;
            return true;
        }
        host->next_edge_progress = host->cycles.absolute_cycles + 65536u;
    }
    if (cpu->exception == 0u && (cpu->msr & PPC_MSR_EE)) {
        const bool external = dol_interrupts_external_pending(&host->interrupts);
        if (external || host->decrementer_pending) {
            const u32 exception = external ? PPC_EXC_EXTERNAL : PPC_EXC_DECREMENTER;
            const u32 vector = external ? PPC_VECTOR_EXTERNAL : PPC_VECTOR_DECREMENTER;
            ppc_take_exception(cpu, exception, vector, address, 0u);
            cpu->exception &= ~exception;
            if (!external) {
                host->decrementer_pending = false;
                if (fprintf(stderr, "[decrementer] stage=delivered vector=%08X srr0=%08X srr1=%08X dec=%08X tb=%llu\n",
                            cpu->pc, cpu->srr0, cpu->srr1, host->decrementer,
                            (unsigned long long)cpu->timebase) < 0)
                    ww_host_fail(host, "decrementer delivery log write failed", 22u, cpu->pc);
            }
            bluewake_cycle_domain_rebudget(&host->cycles, cpu);
            return true;
        }
    }
    const bool observed_retrace_sleep = address == 0x80308A9Cu && cpu->lr == 0x80313A2Cu &&
        cpu->gpr[3] == 0x803F7B44u && cpu->gpr[13] - 26012u == cpu->gpr[3] &&
        cpu->ram != NULL && cpu->ram_size >= 0x003F7B4Cu &&
        get_ram_ptr(cpu, cpu->gpr[3], 8u, NULL) == cpu->ram + 0x003F7B44u &&
        !ww_host_linked_alias_overlaps(cpu->gpr[3], 8u);
    const bool observed_draw_done_sleep = address == 0x80308A9Cu && cpu->lr == 0x80322C38u &&
        cpu->gpr[3] == 0x803F7C9Cu && cpu->gpr[13] - 25668u == cpu->gpr[3] &&
        cpu->ram != NULL && cpu->ram_size >= 0x003F7CA4u &&
        get_ram_ptr(cpu, cpu->gpr[3], 8u, NULL) == cpu->ram + 0x003F7C9Cu &&
        !ww_host_linked_alias_overlaps(cpu->gpr[3], 8u) &&
        ww_host_module_contains_address(host->module, address);
    const bool observed_async_draw_done = address == 0x80322BC8u && cpu->lr == 0x80322BC8u &&
        cpu->gpr[3] == 0u && cpu->gpr[13] == 0x803FE0E0u && cpu->gpr[31] == 0u &&
        cpu->ram != NULL && cpu->ram_size >= 0x003F7CA4u &&
        get_ram_ptr(cpu, 0x803F7C98u, 12u, NULL) == cpu->ram + 0x003F7C98u &&
        !ww_host_linked_alias_overlaps(0x803F7C98u, 12u) &&
        ww_host_module_contains_address(host->module, address);
    if (observed_draw_done_sleep || observed_async_draw_done) {
        const char* error = ww_host_finish_gpu(host);
        if (error != NULL) {
            ww_host_fail(host, error, address, address);
            host->edge_stopped = true;
            return true;
        }
    }
    const bool compiled_cpu_boundary = (address == 0x80328F84u || address == 0x80303A50u ||
                                       observed_retrace_sleep || observed_draw_done_sleep || observed_async_draw_done) &&
                                         ww_host_module_contains_address(host->module, address);
    const bool desktop_edge = bluewake_edge_maybe_intercept(address);
    if (address == 0x80307EF4u && host->rel_image.linked && !host->scheduler_idle_observed) {
        const char* error = ww_host_observe_scheduler_idle(host);
        if (error != NULL) {
            ww_host_fail(host, error, address, address);
            host->edge_stopped = true;
            return true;
        }
        host->scheduler_idle_observed = true;
    }
    if (desktop_edge && ww_host_module_contains_address(host->module, address) && ww_host_dvd_edge(host, address)) {
        host->edge_stopped = host->error != NULL;
        return true;
    }
    if (desktop_edge && !compiled_cpu_boundary) {
        ww_host_fail(host, "desktop interception unsupported; guest not skipped", address, cpu->pc);
        host->edge_stopped = true;
        return true;
    }
    return false;
}

typedef struct {
    u32 address;
    const u8* reference;
    void (*dispatch)(CPUState* cpu);
} WwObservedVector;

static const WwObservedVector* observed_vector(u32 address) {
    static const WwObservedVector vectors[] = {
        {WW05_EXTERNAL_VECTOR, ww05_external_vector_reference, func_00000500},
        {WW05_FP_VECTOR, ww05_fp_vector_reference, func_00000800},
        {WW05_DECREMENTER_VECTOR, ww05_decrementer_vector_reference, func_00000900},
    };
    for (u32 index = 0u; index < sizeof(vectors) / sizeof(vectors[0]); ++index) {
        if (address >= vectors[index].address && address < vectors[index].address + WW05_EXTERNAL_VECTOR_BYTES)
            return &vectors[index];
    }
    return NULL;
}

static bool observed_vector_matches(const CPUState* cpu, const WwObservedVector* vector) {
    return (cpu->pc & 3u) == 0u &&
           (cpu->msr & (PPC_MSR_EE | PPC_MSR_PR | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_LE | PPC_MSR_IP | PPC_MSR_FP)) == 0u &&
           cpu->ram != NULL && cpu->ram_size >= vector->address + WW05_EXTERNAL_VECTOR_BYTES &&
           memcmp(cpu->ram + vector->address, vector->reference, WW05_EXTERNAL_VECTOR_BYTES) == 0;
}

bool ww_host_resume_exception(WwHost* host) {
    CPUState* cpu = &host->cpu;
    if (cpu->exception == PPC_EXC_FP_UNAVAILABLE && cpu->pc == WW05_FP_VECTOR && host->module != NULL) {
        if (!observed_vector_matches(cpu, observed_vector(cpu->pc)))
            return false;
        if (fprintf(stderr, "[fp-exception-resume] vector=%08X matched=1 srr0=%08X srr1=%08X msr=%08X\n",
                    cpu->pc, cpu->srr0, cpu->srr1, cpu->msr) < 0) {
            ww_host_fail(host, "FP exception handoff log write failed", cpu->pc, cpu->pc);
            return false;
        }
        cpu->exception = 0u;
        return true;
    }
    const u32 source = 0x803077D0u;
    if (cpu->exception != PPC_EXC_SYSTEM_CALL || cpu->pc != PPC_VECTOR_SYSTEM_CALL ||
        host->module == NULL)
        return false;
    const bool trace = ww_host_trace_syscall(host->syscall_resumes + 1u);
    if (trace && fprintf(stderr, "[exception-handoff] stage=guard vector=%08X source=%08X srr0=%08X\n",
                cpu->pc, source, cpu->srr0) < 0) {
        ww_host_fail(host, "exception handoff log write failed", source, cpu->pc);
        return false;
    }
    const bool matched = ww_code_guard_copied_text_matches(&host->code_guard, cpu, source,
                                                         GC_RAM_BASE | PPC_VECTOR_SYSTEM_CALL, 28u);
    if (trace && fprintf(stderr, "[exception-handoff] stage=guard-result matched=%u\n", matched) < 0) {
        ww_host_fail(host, "exception handoff log write failed", source, cpu->pc);
        return false;
    }
    if (!matched)
        return false;
    for (u32 index = 0u; index < host->module->num_code_ranges; ++index) {
        const StaticRecompRange* range = &host->module->code_ranges[index];
        if (source < range->start || (u64)source + 28u > range->end)
            continue;
        ++host->syscall_resumes;
        if (trace && fprintf(stderr, "[exception-resume] bits=%08X vector=%08X compiled=%08X resume=%llu srr0=%08X srr1=%08X\n",
                    cpu->exception, cpu->pc, source, (unsigned long long)host->syscall_resumes,
                    cpu->srr0, cpu->srr1) < 0) {
            ww_host_fail(host, "exception handoff log write failed", source, cpu->pc);
            return false;
        }
        // This verified cache handler has no PC-relative instructions; its RFI uses the saved SRR0.
        cpu->exception = 0u;
        cpu->pc = source;
        return true;
    }
    return false;
}

u64 ww_host_end_dispatch(WwHost* host) {
    CPUState* cpu = &host->cpu;
    if (cpu->exception == PPC_EXC_FP_UNAVAILABLE && cpu->cycle_observation_suffix != 0u) {
        const s64 unexecuted = (s64)cpu->cycle_observation_suffix;
        if (cpu->downcount > -unexecuted)
            ww_host_fail(host, "FP exception suffix exceeds outstanding prepayment", cpu->srr0, cpu->pc);
        else
            cpu->downcount += unexecuted;
        cpu->cycle_observation_suffix = 0u;
    }
    return bluewake_cycle_domain_end_turn(&host->cycles, cpu);
}

int ww_host_dispatch(WwHost* host, u32 address) {
    if (host == NULL)
        return 0;
    CPUState* cpu = &host->cpu;
    if (host->module == NULL || host->module->dispatch == NULL) {
        ww_host_fail(host, "dispatch requires a loaded module", address, cpu->pc);
        return 0;
    }
    const WwObservedVector* vector = observed_vector(address);
    if (vector != NULL &&
        !ww_host_module_contains_address(host->module, address) &&
        !ww_host_module_contains_address(host->module, canonical_dispatch_address(cpu, address))) {
        if (cpu->pc != address || cpu->exception != 0u || !observed_vector_matches(cpu, vector)) {
            ww_host_fail(host, "exception vector requires the exact observed program and physical exception state", address, cpu->pc);
            return 0;
        }
        if (fprintf(stderr, "[exception-vector-dispatch] vector=%08X pc=%08X bytes=%u matched=1\n", vector->address, address, WW05_EXTERNAL_VECTOR_BYTES) < 0) {
            ww_host_fail(host, "exception vector dispatch log write failed", address, cpu->pc);
            return -1;
        }
        if (cpu->cycle_budget <= 0 || cpu->downcount <= -cpu->cycle_budget)
            return 1;
        vector->dispatch(cpu);
        if (fprintf(stderr, "[exception-vector-return] input=%08X pc=%08X srr0=%08X srr1=%08X msr=%08X downcount=%lld\n",
                    address, cpu->pc, cpu->srr0, cpu->srr1, cpu->msr, (long long)cpu->downcount) < 0) {
            ww_host_fail(host, "exception vector return log write failed", address, cpu->pc);
            return -1;
        }
        return 1;
    }
    return host->module->dispatch(&host->cpu, address);
}

bool ww_host_init(WwHost* host) {
    if (host == NULL)
        return false;
    memset(host, 0, sizeof(*host));
    if (aram_buffer() != NULL)
        return false;
    BlueWakeModuleStorage storage = {.cpu = &host->cpu};
    if (!bw_module_cpu_init(&storage))
        return false;
    if (!dol_guest_memory_init(&host->memory, NULL)) {
        bw_module_cpu_free(&storage);
        return false;
    }
    host->backing_bytes = host->cpu.ram_size + (u64)host->memory.config.vm_size + host->memory.config.locked_cache_size;
    aram_init();
    if (aram_buffer() == NULL) {
        ww_host_shutdown(host);
        return false;
    }
    host->aram_owned = true;
    host->backing_bytes += ARAM_SIZE;
    host->cpu.external_user_data = host;
    host->cpu.external_read = mmio_read;
    host->cpu.external_write = mmio_write;
    host->cpu.spr_read = spr_read;
    host->cpu.spr_write = spr_write;
    host->cpu.cache_control = cache_control;
    host->cpu.instruction_fallback = fallback;
    host->cpu.host_call = host_call;
    host->decrementer = UINT32_MAX;
    ww_bat_init(&host->bat);
    host->decrementer_valid = true;
    bluewake_cycle_domain_init(&host->cycles, 256, advance, deadline, host);
    dol_interrupts_init(&host->interrupts);
    dol_vi_clock_init(&host->vi_clock);
    dol_vi_clock_configure(&host->vi_clock, GUEST_CPU_CYCLES_PER_VI_RETRACE, GUEST_VI_REFRESH_HZ,
                           GUEST_CPU_CYCLES_PER_SECOND / GUEST_CYCLES_PER_TIMEBASE_TICK);
    host->vi_clock_enabled = WW05_ENABLE_VI_CLOCK;
    dol_di_init(&host->di);
    dol_di_set_command_callback(&host->di, di_command, host);
    dol_exi_init(&host->exi);
    bluewake_ipl_sram_init(&host->sram, "default");
    dol_exi_set_transfer_callback(&host->exi, exi_transfer, host);
    dol_si_init(&host->si);
    dol_audio_dma_init(&host->audio);
    ww_dsp_interface_init(&host->dsp, ww05_dsp_init_reference);
    DolGuestAddressResolver resolver;
    dol_guest_address_resolver_init(&resolver, &host->memory, &host->cpu);
    dol_gx_recomp_init(&host->gx, &resolver);
    return true;
}

void ww_host_shutdown(WwHost* host) {
    // VITA-WW-17: the renderer resolves against this host's memory and CPU,
    // so it goes before they are freed.
    ww_renderer_destroy(host->renderer);
    host->renderer = NULL;
    if (host->disc_owned) {
        dvd_close_image();
        host->disc_owned = false;
    }
    if (host->aram_owned) {
        aram_free();
        host->aram_owned = false;
    }
    host->module = NULL;
    host->rel_data = NULL;
    host->rel_data_count = 0u;
    ww_code_guard_free(&host->code_guard);
    ppc_guest_alias_clear();
    host->alias_bytes = 0u;
    host->alias_count = 0u;
    memset(&host->rel_image, 0, sizeof(host->rel_image));
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
    host->rel_data = data;
    host->rel_data_count = count;
    host->cpu.pc = module->entry_point;
    ppc_lazy_fp_set_enabled(true);
    return true;
}
