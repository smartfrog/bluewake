#include "fixture.h"
#include "direct_calls.h"
#include "gxruntime/guest_memory.h"
#include <stdio.h>
#include <string.h>

int fixture_dispatch(CPUState* cpu);

typedef struct BoundaryProbe {
    uintptr_t low;
    uintptr_t high;
    unsigned max_depth;
    u32 reject;
    u32 observations;
} BoundaryProbe;

static __attribute__((noinline)) bool can_skip(void* user, const CPUState* cpu, u32 address) {
    volatile u8 marker = 0;
    BoundaryProbe* probe = user;
    uintptr_t stack = (uintptr_t)&marker;
    if (stack < probe->low)
        probe->low = stack;
    if (stack > probe->high)
        probe->high = stack;
    if (bw_direct_depth > probe->max_depth)
        probe->max_depth = bw_direct_depth;
    probe->observations++;
    (void)cpu;
    return address != probe->reject;
}

static int calls_init(CPUState* cpu, BoundaryProbe* probe) {
    static const bool clear = false;
    static const u32 zero = 0;
    if (!cpu_init(cpu))
        return 1;
    cpu->cycle_budget = 1000000000;
    cpu->cycle_deadline_budget = 1000000000;
    probe->low = UINTPTR_MAX;
    if (!bluewake_composite_direct_calls_v2(true, &clear, &clear, &zero, &zero, can_skip, probe) ||
        !bluewake_composite_edge_filter(true)) {
        cpu_free(cpu);
        return 1;
    }
    return 0;
}

static void calls_free(CPUState* cpu) {
    bluewake_composite_direct_calls_v2(false, NULL, NULL, NULL, NULL, NULL, NULL);
    cpu_free(cpu);
}

static int shared_alias(void) {
    CPUState cpu;
    if (!cpu_init(&cpu))
        return 1;
    const u8 initial[64] = {0x01, 0x23, 0x45, 0x67};
    u8* backing = NULL;
    u8* resolved = NULL;
    int result = 1;
    if (!ppc_guest_alias_add(0xC1550000u, sizeof initial, initial) ||
        !ppc_guest_alias_get_storage(0xC1550000u, sizeof initial, &backing) ||
        !ppc_guest_alias_add_shared(0x81550000u, sizeof initial, backing))
        goto done;
    mem_write32(&cpu, 0x81550000u, 0x89ABCDEFu);
    if (get_ram_ptr(&cpu, 0xC1550000u, 4, NULL) != backing ||
        mem_read32(&cpu, 0xC1550000u) != 0x89ABCDEFu ||
        backing[0] != 0x89 || backing[3] != 0xEF ||
        !ppc_guest_alias_resolve(0xC155003Cu, 4, &resolved, NULL) ||
        resolved != backing + 60 ||
        ppc_guest_alias_resolve(0xC155003Du, 4, &resolved, NULL))
        goto done;
    printf("ALIAS unique_backing=64 views=2 storage=%p uintptr_bytes=%zu\n",
           (void*)backing, sizeof(uintptr_t));
    result = 0;
done:
    ppc_guest_alias_clear();
    cpu_free(&cpu);
    return result;
}

static int endian_bulk(void) {
    CPUState cpu;
    DolGuestMemory memory;
    if (!cpu_init(&cpu))
        return 1;
    if (!dol_guest_memory_init(&memory, NULL)) {
        cpu_free(&cpu);
        return 1;
    }
    int result = 1;
    const u32 bytes = 4096;
    for (u32 i = 0; i < bytes; i += 4) {
        if (!dol_guest_memory_write(&memory, memory.config.vm_base + i, 4, 0x01020304u ^ i))
            goto done;
    }
    dol_guest_memory_copy(&memory, &cpu, 0xC0001000u, memory.config.vm_base, bytes);
    for (u32 i = 0; i < bytes; i += 4) {
        if (mem_read32(&cpu, 0x80001000u + i) != (0x01020304u ^ i))
            goto done;
    }
    DolGuestAddressResolver resolver;
    DolGuestResolvedRange range;
    dol_guest_address_resolver_init(&resolver, &memory, &cpu);
    if (!dol_guest_address_resolver_resolve(&resolver, 0xC0001000u, bytes,
            DOL_GUEST_ADDRESS_VIRTUAL, DOL_GUEST_RESOURCE_GENERIC, &range) ||
        range.data != cpu.ram + 0x1000 ||
        dol_guest_address_resolver_resolve(&resolver, GC_RAM_BASE + cpu.ram_size - 1, 2,
            DOL_GUEST_ADDRESS_VIRTUAL, DOL_GUEST_RESOURCE_GENERIC, &range) ||
        cpu.ram[0x1000] != 0x01 || cpu.ram[0x1003] != 0x04)
        goto done;
    printf("BULK bytes=%u mem1=%u aux_vm=%u locked_cache=%u simultaneous_backing=%u\n",
           bytes, cpu.ram_size, memory.config.vm_size, memory.config.locked_cache_size,
           cpu.ram_size + memory.config.vm_size + memory.config.locked_cache_size);
    result = 0;
done:
    dol_guest_memory_shutdown(&memory);
    cpu_free(&cpu);
    return result;
}

static int direct_call(void) {
    CPUState cpu;
    BoundaryProbe probe = {.reject = 0x80004104u};
    if (calls_init(&cpu, &probe))
        return 1;
    cpu.pc = 0x80004100u;
    cpu.gpr[3] = 1;
    cpu.gpr[4] = 3;
    cpu.gpr[5] = 7;
    int result = !fixture_dispatch(&cpu) || cpu.pc != 0x80004104u ||
                 cpu.gpr[3] != 1 || cpu.gpr[4] != 21 || probe.observations != 2 ||
                 bw_direct_depth != 0;
    printf("DIRECT pc=%08X r3=%u r4=%u boundary_queries=%u\n",
           cpu.pc, cpu.gpr[3], cpu.gpr[4], probe.observations);
    calls_free(&cpu);
    return result;
}

static int indirect_call(void) {
    CPUState cpu;
    BoundaryProbe probe = {0};
    if (calls_init(&cpu, &probe))
        return 1;
    cpu.pc = 0x80004120u;
    cpu.lr = cpu.gpr[11] = 0x81234564u;
    cpu.gpr[3] = 1;
    cpu.gpr[5] = 7;
    cpu.gpr[12] = 0x80005120u;
    int result = !fixture_dispatch(&cpu) || cpu.pc != 0x81234564u ||
                 cpu.gpr[3] != 0 || cpu.gpr[4] != 7 || probe.observations < 2 ||
                 bw_direct_depth != 0;
    printf("INDIRECT pc=%08X r3=%u r4=%u boundary_queries=%u\n",
           cpu.pc, cpu.gpr[3], cpu.gpr[4], probe.observations);
    calls_free(&cpu);
    return result;
}

static __attribute__((noinline)) int stack_case(u32 depth, uintptr_t* span) {
    CPUState cpu;
    BoundaryProbe probe = {0};
    if (calls_init(&cpu, &probe))
        return 1;
    cpu.pc = cpu.gpr[12] = 0x80006000u;
    cpu.lr = 0x81234564u;
    cpu.gpr[1] = 0x80200000u;
    cpu.gpr[3] = depth;
    u32 dispatches = 0;
    while (cpu.pc != 0x81234564u && dispatches < depth * 2u + 16u) {
        if (!fixture_dispatch(&cpu))
            break;
        dispatches++;
    }
    *span = probe.high >= probe.low ? probe.high - probe.low : UINTPTR_MAX;
    int result = cpu.pc != 0x81234564u || cpu.gpr[3] != 0 || cpu.gpr[1] != 0x80200000u ||
                 bw_direct_depth != 0 || probe.max_depth != BW_DIRECT_DEPTH_MAX - 1u ||
                 *span > 64u * 1024u;
    printf("STACK guest_depth=%u host_depth_seen=%u span=%llu dispatches=%u pc=%08X r1=%08X\n",
           depth, probe.max_depth, (unsigned long long)*span, dispatches, cpu.pc, cpu.gpr[1]);
    calls_free(&cpu);
    return result;
}

static int sustained_stack(void) {
    const u32 depths[] = {64, 4096, 65536};
    uintptr_t baseline = 0;
    for (unsigned i = 0; i < sizeof depths / sizeof depths[0]; ++i) {
        uintptr_t span;
        if (stack_case(depths[i], &span))
            return 1;
        if (i == 0)
            baseline = span;
        else if (span != baseline) {
            fprintf(stderr, "stack span grew with guest depth\n");
            return 1;
        }
    }
    return 0;
}

int fixture_run(int argc, char** argv) {
    if (fixture_log_open("contract"))
        return 1;
    fixture_print_abi();
    const struct { const char* name; int (*run)(void); } tests[] = {
        {"shared-alias", shared_alias}, {"endian-bulk", endian_bulk},
        {"direct-call", direct_call},
        {"indirect-call", indirect_call},
        {"sustained-stack", sustained_stack},
    };
    int result = 0;
    bool matched = false;
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)
            continue;
        matched = true;
        int failure = tests[i].run();
        printf("%s=%s\n", tests[i].name, failure ? "FAIL" : "PASS");
        if (failure) {
            result = 1;
            break;
        }
    }
    if (!matched || argc > 2) {
        fprintf(stderr, "unknown scenario or excess arguments\n");
        result = 1;
    }
    return fixture_log_close() || result;
}
