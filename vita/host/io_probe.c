#include "module_exports.h"
#include "dispatch_loop.h"
#include "gxruntime/interrupts.h"
#include "gxruntime/si.h"
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
        if (address == 0x500u)
            cpu->external_write(cpu, DOL_SI_BASE + DOL_SI_COMCSR_OFF, 0xC0010300u, 4u);
        ppc_rfi(cpu, address);
        cpu->downcount -= 12;
        return 1;
    }
    if (interrupt != NULL)
        assert(address == 0x80001004u && (cpu->msr & PPC_MSR_EE));
    cpu->pc = address;
    cpu->external_write(cpu, 0xCC008000u, 0x61u, 1u);
    cpu->external_write(cpu, 0xCC008000u, 0xFEFFFFFFu, 4u);
    cpu->downcount -= 12;
    if (edge != NULL)
        edge(edge_user, cpu, address + 4u);
    return 1;
}

const StaticRecompModuleDesc* staticrecomp_get_module(void) {
    static const StaticRecompRange ranges[] = {{0x80001000u, 0x80001008u}, {0x803077D0u, 0x803077ECu}};
    static const StaticRecompModuleDesc module = {
        .abi_version = STATICRECOMP_ABI_VERSION, .cpu_abi_version = GXRUNTIME_CPU_ABI_VERSION,
        .cpu_state_size = sizeof(CPUState), .game_id = "GZLE01", .entry_point = 0x80001000u,
        .dispatch = dispatch, .code_ranges = ranges, .num_code_ranges = 2u};
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
    return stream;
}

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && strncmp(format, "[dispatch] stage=enter", 22u) == 0 &&
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
