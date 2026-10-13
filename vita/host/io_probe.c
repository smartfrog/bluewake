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
    return &module;
}

const BlueWakeRelData* staticrecomp_get_rel_data(u32* count) {
    static const BlueWakeRelData data = {0};
    *count = 0u;
    return &data;
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
        ((strncmp(format, "[exception]", 11u) == 0 && failure("external-state-log")) ||
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

int __real_link(const char* from, const char* to);
int __wrap_link(const char* from, const char* to) {
    if (failure("rename")) {
        errno = EIO;
        return -1;
    }
    return __real_link(from, to);
}

int __real_fixture_log_close(void);
int __wrap_fixture_log_close(void) {
    return failure("log-close") ? 1 : __real_fixture_log_close();
}
