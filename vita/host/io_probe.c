#include "module_exports.h"
#include "dispatch_loop.h"
#include "gxruntime/interrupts.h"
#include "gxruntime/si.h"
#include "gxruntime/exi.h"
#include "probe_support.h"
#include "external_vector.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BluewakeEdgeServiceFn edge;
static void* edge_user;

static int mmio_poll(CPUState* cpu, u32 address) {
    u32 reads = 0u;
    while (cpu->downcount > -cpu->cycle_budget) {
        cpu->pc = address;
        cpu->cycle_observation_suffix = 0u;
        cpu->downcount -= 1;
        assert(cpu->external_read(cpu, 0xCC00500Au, 2u) == 0x0804u);
        assert(++reads <= 256u);
    }
    assert(reads == 256u && cpu->cycle_budget == 0 && cpu->downcount == 0);
    return 1;
}

static int dispatch(CPUState* cpu, u32 address) {
    const char* game_stop = getenv("WW05_IO_GAME_STOP");
    if (game_stop != NULL) {
        cpu->msr |= PPC_MSR_DR;
        if (address == 0x80001000u) {
            cpu->gpr[1] = 0x80007000u;
            cpu->gpr[3] = 0x80008000u;
            cpu->gpr[4] = 321u;
            cpu->gpr[5] = 0x80008100u;
            memcpy(cpu->ram + 0x8000u, "synthetic_assert.cpp", 21u);
            memcpy(cpu->ram + 0x8100u, "ship assertion %s:%u", 20u);
            mem_write32(cpu, 0x80007004u, 0x80001004u);
            cpu->pc = strcmp(game_stop, "panic") == 0 ? 0x80006C4Cu : 0x8030150Cu;
            cpu->downcount -= 1;
            return 1;
        }
        assert(address == 0x80006C4Cu);
        cpu->gpr[3] = cpu->gpr[4] = cpu->gpr[5] = 0u;
        cpu->pc = 0x80006D64u;
        mem_write32(cpu, 0x01234567u, 0x01234567u);
        cpu->downcount -= 1;
        return 1;
    }
    static bool polled;
    static u32 repeated_handlers;
    static bool init_started;
    if (getenv("WW05_IO_CONSUMER") != NULL && address == 0x80001008u) {
        cpu->pc = address;
        cpu->external_read(cpu, 0xCC000008u, 2u);
        return 1;
    }
    const char* wpar = getenv("WW05_IO_WPAR");
    if (wpar != NULL) {
        const u32 value = strtoul(wpar, NULL, 0);
        ppc_mtspr(cpu, 921u, value, address);
        if (cpu->exception != 0u)
            return 1;
        assert(ppc_mfspr(cpu, 921u, address) == value && cpu->exception == 0u);
    }
    const char* fifo = getenv("WW05_IO_FIFO");
    if (fifo != NULL) {
        const char* scenario;
        const WwFifoProbeRegister* reg = ww_probe_fifo_register(fifo, &scenario);
        assert(reg != NULL && *scenario == '\0');
        cpu->external_write(cpu, reg->address, reg->value, reg->width);
        if (cpu->exception != 0u)
            return 1;
        assert(cpu->external_read(cpu, reg->address, reg->width) == (reg->value & reg->mask));
    }
    const char* cp_control = getenv("WW05_IO_CP_CONTROL");
    if (cp_control != NULL) {
        const u64 value = strtoul(cp_control, NULL, 0);
        cpu->external_write(cpu, 0xCC000002u, value, 2u);
        if (cpu->exception != 0u)
            return 1;
        assert(cpu->external_read(cpu, 0xCC000002u, 2u) == value && cpu->exception == 0u);
    }
    const char* cp_clear = getenv("WW05_IO_CP_CLEAR");
    if (cp_clear != NULL) {
        cpu->external_write(cpu, 0xCC000004u, strtoul(cp_clear, NULL, 0), 2u);
        if (cpu->exception != 0u)
            return 1;
        assert(cpu->external_read(cpu, 0xCC000004u, 2u) == 0u && cpu->exception == 0u);
    }
    static const struct {
        const char* environment;
        u32 address;
    } distances[] = {{"WW05_IO_CP_DISTANCE", 0xCC000030u}, {"WW05_IO_CP_DISTANCE_HIGH", 0xCC000032u}};
    for (size_t index = 0u; index < sizeof(distances) / sizeof(distances[0]); ++index) {
        const char* value = getenv(distances[index].environment);
        if (value != NULL) {
            cpu->external_write(cpu, distances[index].address, strtoul(value, NULL, 0), 2u);
            if (cpu->exception != 0u)
                return 1;
        }
    }
    if (getenv("WW05_IO_EXI") != NULL) {
        cpu->external_write(cpu, DOL_EXI_BASE + DOL_EXI_STATUS_OFF, 0x130u, 4u);
        cpu->external_write(cpu, DOL_EXI_BASE + DOL_EXI_IMMEDIATE_DATA_OFF, 0x20000100u, 4u);
        cpu->external_write(cpu, DOL_EXI_BASE + DOL_EXI_CONTROL_OFF, 0x35u, 4u);
        if (cpu->exception != 0u)
            return 1;
    }
    const char* cpu_boundary = getenv("WW05_IO_CPU_BOUNDARY");
    if (cpu_boundary != NULL && strcmp(cpu_boundary, "context") == 0) {
        if (address == 0x80001000u) {
            cpu->gpr[3] = 0x80002200u;
            mem_write32(cpu, cpu->gpr[3] + 12u, 0x11223344u);
            mem_write32(cpu, cpu->gpr[3] + 16u, 0x55667788u);
            mem_write32(cpu, cpu->gpr[3] + 408u, address + 4u);
            mem_write32(cpu, cpu->gpr[3] + 412u, PPC_MSR_FP | PPC_MSR_IR | PPC_MSR_DR);
            cpu->srr0 = 0xDEADBEEFu;
            cpu->srr1 = 0xA5A5A5A5u;
            cpu->pc = 0x80303A50u;
            cpu->downcount -= 2;
            return 1;
        }
        if (address == 0x80303A50u) {
            assert(cpu->gpr[3] == 0x80002200u && cpu->srr0 == 0xDEADBEEFu && cpu->srr1 == 0xA5A5A5A5u);
            const u32 context = cpu->gpr[3];
            ppc_mtspr(cpu, 26u, mem_read32(cpu, context + 408u), address);
            ppc_mtspr(cpu, 27u, mem_read32(cpu, context + 412u), address);
            cpu->gpr[4] = mem_read32(cpu, context + 16u);
            cpu->gpr[3] = mem_read32(cpu, context + 12u);
            ppc_rfi(cpu, address);
            cpu->downcount -= 5;
            return 1;
        }
        assert(address == 0x80001004u && cpu->gpr[3] == 0x11223344u && cpu->gpr[4] == 0x55667788u);
        assert(cpu->msr == (PPC_MSR_FP | PPC_MSR_IR | PPC_MSR_DR) && cpu->exception == 0u);
    } else if (cpu_boundary != NULL) {
        assert(strcmp(cpu_boundary, "restore") == 0);
        if (address == 0x80001000u) {
            cpu->gpr[11] = 0x80002080u;
            cpu->lr = address + 4u;
            for (u32 reg = 27u; reg <= 31u; ++reg) {
                cpu->gpr[reg] = 0xA5000000u | reg;
                mem_write32(cpu, cpu->gpr[11] - (32u - reg) * 4u, 0xB6000000u | reg);
            }
            cpu->pc = 0x80328F84u;
            cpu->downcount -= 2;
            return 1;
        }
        if (address == 0x80328F84u) {
            for (u32 reg = 27u; reg <= 31u; ++reg) {
                assert(cpu->gpr[reg] == (0xA5000000u | reg));
                cpu->gpr[reg] = mem_read32(cpu, cpu->gpr[11] - (32u - reg) * 4u);
            }
            cpu->pc = cpu->lr & ~3u;
            cpu->downcount -= 6;
            return 1;
        }
        assert(address == 0x80001004u);
        for (u32 reg = 27u; reg <= 31u; ++reg)
            assert(cpu->gpr[reg] == (0xB6000000u | reg));
    }
    if (getenv("WW05_IO_DSP_INIT") != NULL && !polled) {
        if (!init_started) {
            init_started = true;
            for (u32 index = 0u; index < 128u; ++index)
                cpu->ram[0x01000000u + index] = (u8)index;
            cpu->external_write(cpu, 0xCC00500Au, 0x0805u, 2u);
            cpu->external_write(cpu, 0xCC00500Au, 4u, 2u);
            cpu->external_write(cpu, 0xCC00500Au, 0u, 2u);
        }
        while (cpu->downcount > -cpu->cycle_budget) {
            cpu->pc = address;
            cpu->cycle_observation_suffix = 0u;
            cpu->downcount -= 1;
            const u64 high = cpu->external_read(cpu, 0xCC005004u, 2u);
            if (high & 0x8000u) {
                assert(high == 0x8054u && cpu->external_read(cpu, 0xCC005006u, 2u) == 0x4348u);
                polled = true;
                cpu->pc = address + 4u;
                break;
            }
        }
        return 1;
    }
    if (getenv("WW05_IO_ARAM_DMA") != NULL && !polled) {
        polled = true;
        const char* mode = getenv("WW05_IO_ARAM_DMA");
        const bool absent_write = strcmp(mode, "absent-hsp-write") == 0;
        const bool absent_hsp = absent_write || strcmp(mode, "absent-hsp") == 0;
        const char* prior_transfers = getenv("WW05_IO_PRIOR_ARAM_TRANSFERS");
        if (prior_transfers != NULL) {
            WwHost* host = cpu->external_user_data;
            host->dsp.aram.transfer_count = strtoull(prior_transfers, NULL, 10);
        }
        cpu->external_write(cpu, 0xCC005012u, absent_hsp ? 0x63u : 0x43u, 2u);
        cpu->external_write(cpu, 0xCC005020u, 0x1000u, 4u);
        cpu->external_write(cpu, 0xCC005024u,
                            absent_hsp || strcmp(mode, "hsp") == 0 ? 0x01000000u : 0u, 4u);
        cpu->external_write(cpu, 0xCC005028u, absent_hsp && !absent_write ? 0x80000020u : 32u, 4u);
        if (cpu->exception != 0u)
            return 1;
        u32 reads = 0u;
        for (;;) {
            cpu->pc = address;
            cpu->cycle_observation_suffix = 0u;
            cpu->downcount -= 1;
            const u64 control = cpu->external_read(cpu, 0xCC00500Au, 2u);
            assert(++reads <= 246u);
            if (control & 0x20u)
                break;
        }
        assert(reads == 246u);
        cpu->pc = address + 4u;
        return 1;
    }
    if (getenv("WW05_IO_MMIO_POLL") != NULL && !polled) {
        polled = true;
        return bluewake_chassis_dispatch_loop(cpu, address, mmio_poll, edge, edge_user);
    }
    const char* syscall = getenv("WW05_IO_SYSCALL");
    if (syscall != NULL && address == 0x80001000u) {
        u8* source = cpu->ram + (0x803077D0u - GC_RAM_BASE);
        memcpy(cpu->ram + 0xC00u, source, 28u);
        if (strcmp(syscall, "vector-changed") == 0)
            cpu->ram[0xC03u] ^= 1u;
        if (strcmp(syscall, "source-changed") == 0)
            source[3] ^= 1u;
        cpu->msr = PPC_MSR_RI;
        ppc_system_call_exception(cpu, address);
        cpu->downcount -= 12;
        if (strcmp(syscall, "trace-window") == 0)
            cpu->downcount -= 131072;
        return 1;
    }
    if (syscall != NULL && address == 0x803077D0u) {
        assert(cpu->exception == 0u);
        assert(cpu->srr0 == 0x80001004u);
        assert(cpu->srr1 == PPC_MSR_RI);
        assert(cpu->msr == 0u);
        if (strcmp(syscall, "yielding-handler") == 0) {
            ppc_fallback_instruction(cpu, 0x7D30FAA6u, address);
            assert(cpu->pc == address + 4u);
            return 1;
        }
        ppc_rfi(cpu, address);
        if (strcmp(syscall, "repeated-handlers") == 0 && ++repeated_handlers < 1001u)
            cpu->pc = 0x80001000u;
        cpu->downcount -= 12;
        return 1;
    }
    if (syscall != NULL && strcmp(syscall, "yielding-handler") == 0 && address == 0x803077D4u) {
        assert(cpu->exception == 0u && cpu->msr == 0u);
        assert(cpu->srr0 == 0x80001004u && cpu->srr1 == PPC_MSR_RI);
        cpu->gpr[10] = cpu->gpr[9] | 8u;
        ppc_fallback_instruction(cpu, 0x7D50FBA6u, 0x803077D8u);
        assert(cpu->pc == 0x803077DCu);
        assert(!edge(edge_user, cpu, cpu->pc));
        ppc_rfi(cpu, 0x803077E8u);
        cpu->downcount -= 12;
        return 1;
    }
    if (syscall != NULL)
        assert(address == 0x80001004u && cpu->msr == PPC_MSR_RI);
    if (getenv("WW05_IO_EXCEPTION") != NULL) {
        ppc_system_call_exception(cpu, address);
        return 1;
    }
    const char* interrupt = getenv("WW05_IO_IRQ");
    if (interrupt != NULL && address == 0x80001000u) {
        if (strcmp(interrupt, "fp-vector") == 0) {
            memcpy(cpu->ram + WW05_FP_VECTOR, ww05_fp_vector_reference, WW05_EXTERNAL_VECTOR_BYTES);
            mem_write32(cpu, 0x800000C0u, 0x80002000u);
            mem_write32(cpu, 0x80003010u, address + 4u);
            cpu->fpr[0] = 123.25;
            cpu->ps1[0] = -321.5;
            cpu->fpscr = 4u;
            cpu->msr &= ~PPC_MSR_FP;
            cpu->downcount -= 28;
            cpu->cycle_observation_suffix = 16u;
            assert(!ppc_fp_available(cpu, address + 4u));
            return 1;
        }
        if (strcmp(interrupt, "unsupported") == 0) {
            for (u32 index = 0u; index < 256u; ++index)
                cpu->ram[0x500u + index] = (u8)index;
        }
        if (strcmp(interrupt, "vector") == 0) {
            memcpy(cpu->ram + WW05_EXTERNAL_VECTOR, ww05_external_vector_reference, WW05_EXTERNAL_VECTOR_BYTES);
            mem_write32(cpu, 0x800000C0u, 0x80002000u);
            mem_write32(cpu, 0x80003010u, address + 4u);
        }
        if (strcmp(interrupt, "decrementer") == 0)
            ppc_mtspr(cpu, 22u, 0u, address);
        else {
            cpu->external_write(cpu, DOL_PI_INTERRUPT_MASK, DOL_PI_CAUSE_SI, 4u);
            cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010301u, 4u);
        }
        cpu->msr |= PPC_MSR_EE;
        cpu->pc = address + 4u;
        cpu->downcount -= 12;
        if (strcmp(interrupt, "inside") == 0)
            assert(edge(edge_user, cpu, cpu->pc));
        return 1;
    }
    if (interrupt != NULL && (address == 0x500u || address == 0x900u)) {
        assert(cpu->srr0 == 0x80001004u);
        assert(!(cpu->msr & PPC_MSR_EE));
        if (strcmp(interrupt, "unsupported") == 0 || strcmp(interrupt, "vector") == 0) {
            assert(cpu->host_call(cpu, address));
            cpu->downcount -= 12;
            return 1;
        }
        if (address == 0x500u)
            cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
        ppc_rfi(cpu, address);
        cpu->downcount -= 12;
        return 1;
    }
    if (interrupt != NULL) {
        assert(address == 0x80001004u);
        if (strcmp(interrupt, "vector") == 0 || strcmp(interrupt, "fp-vector") == 0) {
            const bool fp = strcmp(interrupt, "fp-vector") == 0;
            assert(cpu->gpr[3] == (fp ? 7u : 4u) && cpu->gpr[4] == 0x80002000u);
            assert(cpu->msr == (PPC_MSR_IR | PPC_MSR_DR));
            assert(mem_read32(cpu, 0x80002198u) == address);
            if (fp)
                assert(cpu->fpr[0] == 123.25 && cpu->ps1[0] == -321.5 && cpu->fpscr == 4u);
        } else {
            assert(cpu->msr & PPC_MSR_EE);
        }
    }
    cpu->pc = address;
    WwHost* host = cpu->external_user_data;
    if (getenv("WW05_IO_CONSUMER") != NULL) {
        ww_probe_fifo_setup(host, true);
        host->cp_fifo_high_watermark = 96u;
        mem_write16(cpu, 0xCC000004u, 3u);
        mem_write16(cpu, 0xCC000002u, 0x15u);
        assert(host->error == NULL);
        cpu->gpr[1] = 0x817FFF00u;
        mem_write8(cpu, 0xCC008000u, 0x61u);
        cpu->pc = address + 4u;
        mem_write32(cpu, 0xCC008000u, 0x41123456u);
        cpu->pc = address + 8u;
        for (u8 index = 5u; index < 32u; ++index)
            mem_write8(cpu, 0xCC008000u, 0u);
    } else {
        assert(ww_gx_write(&host->frontier, &host->gx, 0x61u, 1u, cpu->pc, cpu->gpr[1]));
        assert(ww_gx_write(&host->frontier, &host->gx, 0xFEFFFFFFu, 4u, cpu->pc, cpu->gpr[1]));
    }
    cpu->downcount -= 12;
    if (edge != NULL)
        edge(edge_user, cpu, address + 4u);
    return 1;
}

#if WW05_ENABLE_IKADA_CONTRACT
#include "ship_fixture.inc"
#include "ship_contract.inc"
#include "rel_contract_fixture.inc"
#include "ikada_fixture.inc"
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
#include "actor_84_fixture.inc"
#include "actor_85_fixture.inc"
#endif

static bool ikada_fixture_enabled(void) {
    return getenv("WW05_IO_IKADA") != NULL;
}

static u8 startup_dol[260];
static BlueWakeShipContractV1 startup_ship;

static const StaticRecompModuleDesc* startup_module(StaticRecompModuleDesc* module) {
    static StaticRecompRange ranges[64];
    u32 count = 0u;
    for (u32 at = 0u; at < module->num_code_ranges; ++at) ranges[count++] = module->code_ranges[at];
    for (u32 at = 0u; at < sizeof(fixture_ranges) / sizeof(fixture_ranges[0]); ++at) ranges[count++] = fixture_ranges[at];
    ranges[count++] = (StaticRecompRange){0xC1E000DCu,0xC1E000FCu};
    ranges[count++] = (StaticRecompRange){0xC15500F4u,0xC1550174u};
    static StaticRecompRelModule descriptors[5];
    descriptors[0] = fixture_rel;
    descriptors[1] = (StaticRecompRelModule){.module_id=400u,.section_count=8u,.section_info_offset=0x4Cu,
        .file_size=sizeof(title_raw),.sections=title_sections,.num_sections=8u};
    descriptors[2] = (StaticRecompRelModule){.module_id=60u,.section_count=21u,.section_info_offset=0x4Cu,
        .file_size=sizeof(ikada_raw),.sections=ikada_sections,.num_sections=6u};
    static StaticRecompRange chunks[10];
    static u64 hashes[10];
    chunks[0] = (StaticRecompRange){0xC15500F4u,0xC1550154u};
    chunks[1] = (StaticRecompRange){0xC1550154u,0xC1550174u};
    hashes[0] = ikada_hashes[0]; hashes[1] = ikada_hashes[1];
    for (u32 at = 0u; at < 3u; ++at) { chunks[at + 2u] = fixture_chunks[at]; hashes[at + 2u] = fixture_hashes[at]; }
    chunks[5] = (StaticRecompRange){0xC1E000DCu,0xC1E000FCu}; hashes[5] = title_hash;
    module->code_ranges = ranges; module->num_code_ranges = count;
    module->chunk_ranges = chunks; module->chunk_hashes = hashes; module->num_chunk_ranges = 6u;
    module->rel_modules = descriptors; module->num_rel_modules = 3u;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    descriptors[3] = (StaticRecompRelModule){.module_id=84u,.section_count=17u,.section_info_offset=0x4Cu,
        .file_size=sizeof(actor_84_raw),.sections=actor_84_sections,.num_sections=4u};
    descriptors[4] = (StaticRecompRelModule){.module_id=85u,.section_count=17u,.section_info_offset=0x4Cu,
        .file_size=sizeof(actor_85_raw),.sections=actor_85_sections,.num_sections=4u};
    for (u32 at = 0u; at < 2u; ++at) {
        const u32 base = 0xC19A00D4u + at * 0x10000u;
        ranges[count++] = (StaticRecompRange){base,base + 128u};
        chunks[6u + at * 2u] = (StaticRecompRange){base,base + 96u};
        chunks[7u + at * 2u] = (StaticRecompRange){base + 96u,base + 128u};
        hashes[6u + at * 2u] = at == 0u ? actor_84_hashes[0] : actor_85_hashes[0];
        hashes[7u + at * 2u] = at == 0u ? actor_84_hashes[1] : actor_85_hashes[1];
    }
    for (u32 at = 1u; at < 10u; ++at)
        for (u32 previous = at; previous != 0u && chunks[previous].start < chunks[previous - 1u].start; --previous) {
            const StaticRecompRange range = chunks[previous];
            const u64 hash = hashes[previous];
            chunks[previous] = chunks[previous - 1u]; hashes[previous] = hashes[previous - 1u];
            chunks[previous - 1u] = range; hashes[previous - 1u] = hash;
        }
    module->num_code_ranges = count; module->num_chunk_ranges = 10u; module->num_rel_modules = 5u;
#endif
    return module;
}
#endif

const StaticRecompModuleDesc* staticrecomp_get_module(void) {
    static const StaticRecompRange ranges[] = {{0x80001000u, 0x80001008u}, {0x803077D0u, 0x803077ECu},
        {0x500u, 0x904u}, {0x80328F84u, 0x80328F9Cu}, {0x80303A50u, 0x80303B28u}};
    static StaticRecompModuleDesc module = {
        .abi_version = STATICRECOMP_ABI_VERSION, .cpu_abi_version = GXRUNTIME_CPU_ABI_VERSION,
        .cpu_state_size = sizeof(CPUState), .game_id = "GZLE01", .entry_point = 0x80001000u,
        .dispatch = dispatch, .code_ranges = ranges, .num_code_ranges = 3u};
    const char* interrupt = getenv("WW05_IO_IRQ");
    module.num_code_ranges = interrupt != NULL &&
        (strcmp(interrupt, "vector") == 0 || strcmp(interrupt, "fp-vector") == 0) ? 2u : 3u;
    if (getenv("WW05_IO_CPU_BOUNDARY") != NULL)
        module.num_code_ranges = 5u;
#if WW05_ENABLE_IKADA_CONTRACT
    if (ikada_fixture_enabled()) return startup_module(&module);
#endif
    return &module;
}

const BlueWakeRelData* staticrecomp_get_rel_data(u32* count) {
#if WW05_ENABLE_IKADA_CONTRACT
    if (ikada_fixture_enabled()) {
        static BlueWakeRelData selected[14];
        selected[0] = fixture_data[0]; selected[1] = fixture_data[1]; selected[2] = title_canonical[1];
        for (u32 at = 1u; at < 5u; ++at) selected[at + 2u] = ikada_canonical[at];
        selected[7] = (BlueWakeRelData){60u,6u,0xC15581A0u,532u,NULL};
        *count = 8u;
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        for (u32 at = 1u; at < 4u; ++at) {
            selected[7u + at] = actor_84_canonical[at]; selected[10u + at] = actor_85_canonical[at];
        }
        *count = 14u;
#endif
        return selected;
    }
#endif
    static const BlueWakeRelData data = {0};
    *count = 0u;
    return &data;
}

const BlueWakeShipContractV1* staticrecomp_get_ship_contract_v1(void) {
#if WW05_ENABLE_IKADA_CONTRACT
    if (ikada_fixture_enabled()) {
        write_be32(startup_dol, 256u); write_be32(startup_dol + 0x48u, 0x80001000u);
        write_be32(startup_dol + 0x90u, 4u); write_be32(startup_dol + 0xD8u, 0x80002000u);
        write_be32(startup_dol + 0xDCu, 256u); write_be32(startup_dol + 0xE0u, 0x80001000u);
        startup_ship = s_ship_contract;
        startup_ship.dol_bytes = startup_dol; startup_ship.dol_size = sizeof(startup_dol);
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        startup_ship.build_identity = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
#endif
        return &startup_ship;
    }
#endif
    return NULL;
}

const BlueWakeRelInitContractSetV1* staticrecomp_get_rel_init_contracts_v1(void) {
#if WW05_ENABLE_IKADA_CONTRACT
    if (ikada_fixture_enabled()) {
        const char* mode = getenv("WW05_IO_IKADA");
        if (strcmp(mode, "missing-set") == 0) return NULL;
        static BlueWakeRelLifecycleV2 lifecycle[] = {{60u,0xC15500F4u,0xC1550120u,0xC155014Cu},
            {168u,0xC1B100F4u,0xC1B10120u,0xC1B1014Cu},{400u,0xC1E000DCu,0xC1E000E0u,0xC1E000E4u}};
        static BlueWakeRelInitContractV1 contracts[5];
        contracts[0] = (BlueWakeRelInitContractV1){60u,sizeof(ikada_raw),ikada_raw,ikada_canonical,5u,2u,0u,
            0xC155010Cu,&lifecycle[0],s_ship_contract.build_identity};
        contracts[1] = (BlueWakeRelInitContractV1){168u,s_ship_contract.rel_size,s_ship_contract.rel_bytes,
            s_ship_contract.canonical_sections,s_ship_contract.section_count,4u,0u,0xC1B1010Cu,&lifecycle[1],s_ship_contract.build_identity};
        contracts[2] = (BlueWakeRelInitContractV1){400u,sizeof(title_raw),title_raw,title_canonical,2u,2u,0u,
            0xC1E000F4u,&lifecycle[2],s_ship_contract.build_identity};
        static BlueWakeRelInitContractSetV1 set;
        set = (BlueWakeRelInitContractSetV1){1u,sizeof(startup_dol),startup_dol,contracts,3u,s_ship_contract.build_identity};
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
        static const BlueWakeRelLifecycleV2 actor_lifecycle[] = {
            {84u,0xC19A00D4u,0xC19A0100u,0xC19A012Cu}, {85u,0xC19B00D4u,0xC19B0100u,0xC19B012Cu}};
        contracts[0].build_identity = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        contracts[1].build_identity = startup_ship.build_identity;
        contracts[2].build_identity = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
        contracts[3] = (BlueWakeRelInitContractV1){84u,sizeof(actor_84_raw),actor_84_raw,actor_84_canonical,4u,2u,0u,
            0xC19A00ECu,&actor_lifecycle[0],"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
        contracts[4] = (BlueWakeRelInitContractV1){85u,sizeof(actor_85_raw),actor_85_raw,actor_85_canonical,4u,2u,0u,
            0xC19B00ECu,&actor_lifecycle[1],"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
        set.contract_count = 5u;
        set.build_identity = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
        if (strcmp(mode, "batch-missing-member") == 0) set.contract_count = 4u;
        if (strcmp(mode, "batch-bad-pin") == 0) contracts[3].build_identity = contracts[0].build_identity;
        if (strcmp(mode, "batch-bad-set") == 0) set.build_identity = contracts[0].build_identity;
#endif
        if (strcmp(mode, "missing-60") == 0) { set.contracts++; set.contract_count--; }
        if (strcmp(mode, "duplicate") == 0) contracts[2] = contracts[0];
        if (strcmp(mode, "invalid-certificate") == 0) contracts[0].ctor_return_pc = 0u;
        if (strcmp(mode, "bad-dol") == 0) {
            static u8 wrong_dol[sizeof(startup_dol)];
            memcpy(wrong_dol, startup_dol, sizeof(wrong_dol)); wrong_dol[259] ^= 1u;
            set.dol_bytes = wrong_dol;
        }
        return &set;
    }
#endif
    static const BlueWakeRelInitContractSetV1 invalid = {.version = 2u};
    return getenv("WW05_IO_TITLE_INVALID") != NULL ? &invalid : NULL;
}

void bluewake_set_edge_service(BluewakeEdgeServiceFn callback, void* user) {
    edge = callback;
    edge_user = user;
}

static FILE* report;
static FILE* dispatch_trace;
static bool dispatch_marked;
static bool failure(const char* name) {
    const char* selected = getenv("WW05_IO_FAILURE");
    return selected != NULL && strcmp(selected, name) == 0;
}

int __real_setvbuf(FILE* stream, char* buffer, int mode, size_t size);
int __wrap_setvbuf(FILE* stream, char* buffer, int mode, size_t size) {
    return stream == stderr && failure("stderr-buffer") ? -1 : __real_setvbuf(stream, buffer, mode, size);
}

FILE* __real_fopen(const char* path, const char* mode);
FILE* __wrap_fopen(const char* path, const char* mode) {
    if (strcmp(path, "ww05.dispatch") == 0 && failure("dispatch-open")) {
        errno = EIO;
        return NULL;
    }
    FILE* stream = __real_fopen(path, mode);
    if (strcmp(path, "ww05.partial") == 0)
        report = stream;
    if (strcmp(path, "ww05.dispatch") == 0)
        dispatch_trace = stream;
    if (strcmp(path, "ww05.dispatch") == 0 && stream != NULL)
        dispatch_marked = true;
    return stream;
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr &&
        ((strncmp(format, "[rel-admission]", 15u) == 0 && failure("ikada-admission-log")) ||
         (strncmp(format, "[rel-batch-member]", 18u) == 0 && failure("batch-member-log")) ||
         (strncmp(format, "[rel-batch-admission]", 21u) == 0 && failure("batch-admission-log")) ||
         (strncmp(format, "[exception]", 11u) == 0 && failure("external-state-log")) ||
         (strncmp(format, "[exception-vector]", 18u) == 0 && failure("external-vector-log")) ||
         (strncmp(format, "[exception-vector-dispatch]", 27u) == 0 && failure("external-dispatch-log")) ||
         (strncmp(format, "[exception-vector-return]", 25u) == 0 && failure("external-return-log")) ||
         (strncmp(format, "[fp-exception-resume]", 21u) == 0 && failure("fp-resume-log")) ||
         (strncmp(format, "[aram-expansion-request]", 24u) == 0 && failure("aram-expansion-log")) ||
         (strncmp(format, "[aram-hsp-absent]", 17u) == 0 && failure("aram-absent-log")) ||
         (strncmp(format, "[cp-consume]", 12u) == 0 && failure("cp-consume-log")) ||
         (strncmp(format, "[wpar]", 6u) == 0 && failure("wpar-log")) ||
         (strncmp(format, "[exi]", 5u) == 0 && failure("exi-log")))) {
        errno = EIO;
        return -1;
    }
    if (stream == stderr && dispatch_marked && strncmp(format, "[dispatch] stage=enter", 22u) == 0 &&
        failure("dispatch-entry-log")) {
        errno = EIO;
        return -1;
    }
    if ((stream == report && failure("write")) ||
        (stream == dispatch_trace && failure("dispatch-write"))) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int __real_fflush(FILE* stream);
int __wrap_fflush(FILE* stream) {
    int result = __real_fflush(stream);
    return ((stream == report && failure("flush")) ||
            (stream == dispatch_trace && failure("dispatch-flush"))) ? EOF : result;
}

int __real_fclose(FILE* stream);
int __wrap_fclose(FILE* stream) {
    bool fail = (stream == report && failure("close")) ||
                (stream == dispatch_trace && failure("dispatch-close"));
    if (stream == dispatch_trace)
        dispatch_trace = NULL;
    int result = __real_fclose(stream);
    return fail ? EOF : result;
}

int __real_rename(const char* from, const char* to);
int __wrap_rename(const char* from, const char* to) {
    if (failure("rename")) {
        errno = EIO;
        return -1;
    }
    return __real_rename(from, to);
}

int __real_fixture_log_close(void);
int __wrap_fixture_log_close(void) {
    return failure("log-close") ? 1 : __real_fixture_log_close();
}
