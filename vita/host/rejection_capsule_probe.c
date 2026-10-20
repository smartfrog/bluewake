#include "boot_host.h"
#include "rejection_capsule.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define REFUSAL "desktop interception unsupported; guest not skipped"
#define CAPSULE_FAILURE "admission rejection capsule write failed"

static unsigned char output[WW_REJECTION_MAX_BYTES + 16u];
static unsigned writes;
static unsigned write_offsets[4];
static unsigned write_sizes[4];
static unsigned fail_write;

static int sink(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    assert(writes < 4u);
    write_offsets[writes] = offset;
    write_sizes[writes] = size;
    ++writes;
    assert(offset <= WW_REJECTION_MAX_BYTES && size <= WW_REJECTION_MAX_BYTES - offset);
    if (fail_write == 1u && writes == 1u)
        return -1;
    if (fail_write == 2u && writes == 1u) {
        memcpy(output + offset, bytes, size - 1u);
        return (int)size - 1;
    }
    if (fail_write == 3u && writes == 2u)
        return -1;
    memcpy(output + offset, bytes, size);
    return (int)size;
}

static int dispatch(CPUState* cpu, u32 address) {
    (void)cpu;
    (void)address;
    return 0;
}

static StaticRecompRange module_range;
static StaticRecompModuleDesc module_for(u32 start, u32 end) {
    module_range = (StaticRecompRange){start, end};
    StaticRecompModuleDesc module = {.dispatch = dispatch, .code_ranges = &module_range, .num_code_ranges = 1u};
    return module;
}

static WwHost* host_new(void) {
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL);
    assert(ww_host_init(host));
    return host;
}

static void set_sleep_shape(WwHost* host, u32 address, u32 lr, u32 r3, u32 r13) {
    host->cpu.pc = address;
    host->cpu.lr = lr;
    host->cpu.gpr[1] = 0x817FFF00u;
    host->cpu.gpr[3] = r3;
    host->cpu.gpr[13] = r13;
    host->cpu.gpr[31] = 0u;
    host->cpu.msr = PPC_MSR_FP | PPC_MSR_ME;
    host->cpu.srr0 = 0x802AE070u;
    host->cpu.srr1 = 0x0000A032u;
}

static void expect_refusal(WwHost* host, u32 address) {
    assert(ww_host_edge(host, &host->cpu, address));
    assert(host->edge_stopped && host->error != NULL);
    assert(host->error_address == address && host->error_pc == address);
}

static WwRejectionCapsule record(void) {
    assert(writes >= 1u);
    WwRejectionCapsule capsule;
    memcpy(&capsule, output, sizeof capsule);
    return capsule;
}

static u32 persisted_complete(void) {
    u32 complete;
    memcpy(&complete, output + offsetof(WwRejectionCapsule, complete), sizeof complete);
    return complete;
}

static u64 ram_checksum(const CPUState* cpu) {
    u64 hash = 1469598103934665603ull;
    for (u32 at = 0u; at < cpu->ram_size; ++at) {
        hash ^= cpu->ram[at];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void queue_values(WwHost* host) {
    mem_write32(&host->cpu, 0x803F7B44u, 0x11223344u);
    mem_write32(&host->cpu, 0x803F7B48u, 0x55667788u);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = host_new();
    const char* scenario = argv[1];
    if (strcmp(scenario, "retrace-unowned") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        queue_values(host);
        expect_refusal(host, 0x80308A9Cu);
        assert(strcmp(host->error, REFUSAL) == 0);
        assert(writes == 2u);
        const WwRejectionCapsule capsule = record();
        assert(capsule.magic == WW_REJECTION_MAGIC && capsule.version == WW_REJECTION_VERSION);
        assert(capsule.bytes == sizeof capsule && capsule.complete == WW_REJECTION_COMPLETE && capsule.io_error == 0u);
        assert(capsule.address == 0x80308A9Cu && capsule.pc == 0x80308A9Cu);
        assert(capsule.lr == 0x80313A2Cu && capsule.sp == 0x817FFF00u);
        assert(capsule.r3 == 0x803F7B44u && capsule.r13 == 0x803FE0E0u && capsule.r31 == 0u);
        assert(capsule.msr == (PPC_MSR_FP | PPC_MSR_ME));
        assert(capsule.srr0 == 0x802AE070u && capsule.srr1 == 0x0000A032u);
        assert(capsule.module_owned == 0u);
        assert(capsule.queue_real_mem1 == 1u && capsule.queue_alias_overlap == 0u);
        assert(capsule.queue_head == 0x11223344u && capsule.queue_tail == 0x55667788u);
        assert(capsule.thread_valid == 0u);
        assert(capsule.predicate_retrace_sleep == 1u);
        assert(capsule.predicate_draw_done_sleep == 0u && capsule.predicate_async_draw_done == 0u);
        assert(capsule.predicate_rel_resume == 0u && capsule.predicate_restore == 0u && capsule.predicate_context == 0u);
        assert(capsule.predicate_dsp_task == 0u && capsule.predicate_card_probe == 0u);
    } else if (strcmp(scenario, "draw-done-mismatch") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        StaticRecompModuleDesc module = module_for(0x80308A9Cu, 0x80308AA0u);
        host->module = &module;
        set_sleep_shape(host, 0x80308A9Cu, 0x80322C38u, 0x803F7C98u, 0x803FE0DCu);
        mem_write32(&host->cpu, 0x803F7C98u, 0xAABBCCDDu);
        mem_write32(&host->cpu, 0x803F7C9Cu, 0xEEFF0011u);
        expect_refusal(host, 0x80308A9Cu);
        assert(strcmp(host->error, REFUSAL) == 0);
        const WwRejectionCapsule capsule = record();
        assert(capsule.module_owned == 1u);
        assert(capsule.predicate_draw_done_sleep == 0u && capsule.predicate_retrace_sleep == 0u);
        assert(capsule.r3 == 0x803F7C98u && capsule.lr == 0x80322C38u);
        assert(capsule.queue_real_mem1 == 1u && capsule.queue_alias_overlap == 0u);
        assert(capsule.queue_head == 0xAABBCCDDu && capsule.queue_tail == 0xEEFF0011u);
        assert(writes == 2u);
    } else if (strcmp(scenario, "restore-unowned") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80328F84u, 0x8030141Cu, 0x803A2960u, 0x803FE0E0u);
        expect_refusal(host, 0x80328F84u);
        assert(strcmp(host->error, REFUSAL) == 0);
        const WwRejectionCapsule capsule = record();
        assert(capsule.predicate_restore == 1u && capsule.module_owned == 0u);
        assert(capsule.predicate_context == 0u && capsule.predicate_retrace_sleep == 0u);
        assert(writes == 2u);
    } else if (strcmp(scenario, "no-writer") == 0) {
        ww_rejection_capsule_init(NULL, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        queue_values(host);
        expect_refusal(host, 0x80308A9Cu);
        assert(strcmp(host->error, REFUSAL) == 0);
        assert(writes == 0u);
    } else if (strcmp(scenario, "delta") == 0) {
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        queue_values(host);
        ww_rejection_capsule_init(NULL, NULL);
        const WwHost before = *host;
        const u64 ram_before = ram_checksum(&host->cpu);
        expect_refusal(host, 0x80308A9Cu);
        const WwHost no_capsule = *host;
        assert(writes == 0u);
        *host = before;
        writes = 0u;
        ww_rejection_capsule_init(sink, NULL);
        expect_refusal(host, 0x80308A9Cu);
        const WwHost capsule_state = *host;
        assert(writes == 2u);
        assert(memcmp(&no_capsule, &capsule_state, sizeof capsule_state) == 0);
        assert(ram_checksum(&host->cpu) == ram_before);
    } else if (strcmp(scenario, "unmapped-queue") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x90000000u, 0x9000659Cu);
        expect_refusal(host, 0x80308A9Cu);
        const WwRejectionCapsule capsule = record();
        assert(capsule.queue_real_mem1 == 0u && capsule.queue_alias_overlap == 0u);
        assert(capsule.queue_head == 0u && capsule.queue_tail == 0u);
        assert(capsule.predicate_retrace_sleep == 0u);
        assert(writes == 2u);
    } else if (strcmp(scenario, "alias-queue") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        static const u8 alias_bytes[8] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu, 0x00u, 0x11u};
        assert(ppc_guest_alias_add(0x803F7B44u, 8u, alias_bytes));
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        expect_refusal(host, 0x80308A9Cu);
        const WwRejectionCapsule capsule = record();
        assert(capsule.queue_alias_overlap == 1u);
        assert(capsule.queue_head == 0u && capsule.queue_tail == 0u);
        assert(capsule.predicate_retrace_sleep == 0u);
        assert(writes == 2u);
    } else if (strcmp(scenario, "thread-valid") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        mem_write32(&host->cpu, 0x800000DCu, 0x803F1000u);
        mem_write16(&host->cpu, 0x803F1000u + 0x2C8u, 0x0002u);
        mem_write32(&host->cpu, 0x803F1000u + 0x2CCu, 0xFFFFFFFFu);
        mem_write32(&host->cpu, 0x803F1000u + 0x2DCu, 0x803F7B44u);
        mem_write32(&host->cpu, 0x803F1000u + 0x198u, 0x80308A9Cu);
        mem_write32(&host->cpu, 0x803F1000u + 0x84u, 0x80313A2Cu);
        mem_write32(&host->cpu, 0x803F1000u + 4u, 0x817FFF00u);
        expect_refusal(host, 0x80308A9Cu);
        const WwRejectionCapsule capsule = record();
        assert(capsule.thread_valid == 1u);
        assert(capsule.thread_state == 0x0002u && capsule.thread_suspend == 0xFFFFFFFFu);
        assert(capsule.thread_queue == 0x803F7B44u);
        assert(capsule.thread_saved_pc == 0x80308A9Cu && capsule.thread_saved_lr == 0x80313A2Cu);
        assert(capsule.thread_sp == 0x817FFF00u);
    } else if (strcmp(scenario, "thread-unmapped") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        mem_write32(&host->cpu, 0x800000DCu, 0x90000000u);
        expect_refusal(host, 0x80308A9Cu);
        const WwRejectionCapsule capsule = record();
        assert(capsule.thread_valid == 0u);
        assert(capsule.thread_state == 0u && capsule.thread_suspend == 0u && capsule.thread_queue == 0u);
        assert(capsule.thread_saved_pc == 0u && capsule.thread_saved_lr == 0u && capsule.thread_sp == 0u);
    } else if (strcmp(scenario, "failed-write") == 0 || strcmp(scenario, "short-write") == 0 ||
               strcmp(scenario, "short-complete") == 0) {
        fail_write = strcmp(scenario, "failed-write") == 0 ? 1u : strcmp(scenario, "short-write") == 0 ? 2u : 3u;
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        queue_values(host);
        ww_rejection_capsule_init(sink, NULL);
        const WwHost before = *host;
        expect_refusal(host, 0x80308A9Cu);
        assert(strcmp(host->error, CAPSULE_FAILURE) == 0);
        assert(host->error_address == 0x80308A9Cu && host->error_pc == 0x80308A9Cu);
        assert(persisted_complete() != WW_REJECTION_COMPLETE);
        const WwHost failed = *host;
        *host = before;
        writes = 0u;
        fail_write = 0u;
        expect_refusal(host, 0x80308A9Cu);
        assert(strcmp(host->error, REFUSAL) == 0);
        assert(writes == 2u);
        const char* success_error = host->error;
        host->error = failed.error;
        assert(memcmp(&failed, host, sizeof failed) == 0);
        host->error = success_error;
    } else if (strcmp(scenario, "second-call") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        expect_refusal(host, 0x80308A9Cu);
        assert(writes == 2u);
        assert(ww_host_edge(host, &host->cpu, 0x80308A9Cu));
        assert(host->edge_stopped && strcmp(host->error, REFUSAL) == 0 && writes == 2u);
    } else if (strcmp(scenario, "vrbox2-no-record") == 0 || strcmp(scenario, "vrbox-no-record") == 0) {
        const bool vrbox2 = strcmp(scenario, "vrbox2-no-record") == 0;
        const u32 address = vrbox2 ? 0x8015EA5Cu : 0x8015E3F0u;
        ww_rejection_capsule_init(sink, NULL);
        StaticRecompModuleDesc module = module_for(address, vrbox2 ? 0x8015EC30u : 0x8015E530u);
        host->module = &module;
        set_sleep_shape(host, address, 0x800190A0u, 0x80410000u, 0x803FE0E0u);
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        const CPUState before = host->cpu;
        assert(!ww_host_edge(host, &host->cpu, address));
        assert(host->error == NULL && !host->edge_stopped && writes == 0u);
        assert(memcmp(&host->cpu, &before, sizeof before) == 0);
    } else if (strcmp(scenario, "compiled-no-record") == 0) {
        ww_rejection_capsule_init(sink, NULL);
        StaticRecompModuleDesc module = module_for(0x80328F84u, 0x80328F9Cu);
        host->module = &module;
        set_sleep_shape(host, 0x80328F84u, 0x8030141Cu, 0x803A2960u, 0x803FE0E0u);
        assert(!ww_host_edge(host, &host->cpu, 0x80328F84u));
        assert(host->error == NULL && !host->edge_stopped && writes == 0u);
    } else if (strcmp(scenario, "bounded") == 0) {
        assert(sizeof(WwRejectionCapsule) <= WW_REJECTION_MAX_BYTES);
        ww_rejection_capsule_init(sink, NULL);
        set_sleep_shape(host, 0x80308A9Cu, 0x80313A2Cu, 0x803F7B44u, 0x803FE0E0u);
        expect_refusal(host, 0x80308A9Cu);
        assert(writes == 2u);
        assert(write_offsets[0] == 0u && write_sizes[0] == sizeof(WwRejectionCapsule));
        assert(write_offsets[1] == offsetof(WwRejectionCapsule, complete) && write_sizes[1] == sizeof(u32));
        assert(write_offsets[0] + write_sizes[0] <= WW_REJECTION_MAX_BYTES);
        assert(write_offsets[1] + write_sizes[1] <= WW_REJECTION_MAX_BYTES);
        const WwRejectionCapsule capsule = record();
        assert(capsule.bytes == sizeof(WwRejectionCapsule));
        assert(capsule.io_error == 0u && capsule.complete == WW_REJECTION_COMPLETE);
    } else {
        assert(!"unknown rejection capsule scenario");
    }
    return 0;
}
