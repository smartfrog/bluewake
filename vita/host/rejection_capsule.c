#include "rejection_capsule.h"

#include <stddef.h>

_Static_assert(sizeof(WwRejectionCapsule) <= WW_REJECTION_MAX_BYTES, "rejection capsule record bound");

static WwRejectionWriteAt writer;
static void* writer_user;

void ww_rejection_capsule_init(WwRejectionWriteAt write_at, void* user) {
    writer = write_at;
    writer_user = user;
}

static bool real_mem1(CPUState* cpu, u32 address, u32 size) {
    if ((address & 3u) != 0u || cpu->ram == NULL)
        return false;
    const u32 offset = address - GC_RAM_BASE;
    return offset <= cpu->ram_size && size <= cpu->ram_size - offset &&
           get_ram_ptr(cpu, address, size, NULL) == cpu->ram + offset;
}

static const char* persist(WwRejectionCapsule* record) {
    if (writer(writer_user, 0u, record, sizeof *record) != (int)sizeof *record) {
        record->io_error = 1u;
        return "admission rejection capsule write failed";
    }
    const u32 complete = WW_REJECTION_COMPLETE;
    if (writer(writer_user, (u32)offsetof(WwRejectionCapsule, complete), &complete, sizeof complete) != (int)sizeof complete) {
        record->io_error = 1u;
        return "admission rejection capsule write failed";
    }
    record->complete = WW_REJECTION_COMPLETE;
    return NULL;
}

const char* ww_host_rejection_capsule(WwHost* host, u32 address, const WwAdmissionRejection* rejection) {
    if (writer == NULL)
        return NULL;
    CPUState* cpu = &host->cpu;
    WwRejectionCapsule record = {0};
    record.magic = WW_REJECTION_MAGIC;
    record.version = WW_REJECTION_VERSION;
    record.bytes = sizeof record;
    record.address = address;
    record.pc = cpu->pc;
    record.lr = cpu->lr;
    record.sp = cpu->gpr[1];
    record.r3 = cpu->gpr[3];
    record.r13 = cpu->gpr[13];
    record.r31 = cpu->gpr[31];
    record.msr = cpu->msr;
    record.srr0 = cpu->srr0;
    record.srr1 = cpu->srr1;
    record.module_owned = rejection->module_owned;
    record.predicate_rel_resume = rejection->rel_resume;
    record.predicate_restore = rejection->restore;
    record.predicate_context = rejection->context;
    record.predicate_retrace_sleep = rejection->retrace_sleep;
    record.predicate_draw_done_sleep = rejection->draw_done_sleep;
    record.predicate_async_draw_done = rejection->async_draw_done;
    record.predicate_dsp_task = rejection->dsp_task;
    record.predicate_card_probe = rejection->card_probe;
    const u32 queue = cpu->gpr[3];
    record.queue_real_mem1 = real_mem1(cpu, queue, 8u);
    record.queue_alias_overlap = ww_host_linked_alias_overlaps(queue, 8u);
    if (record.queue_real_mem1 && !record.queue_alias_overlap) {
        record.queue_head = mem_read32(cpu, queue);
        record.queue_tail = mem_read32(cpu, queue + 4u);
    }
    // Same bounded active-thread layout as ww_host_observe_scheduler_idle.
    const u32 thread_global = 0x800000DCu;
    if (real_mem1(cpu, thread_global, 4u) && !ww_host_linked_alias_overlaps(thread_global, 4u)) {
        const u32 thread = mem_read32(cpu, thread_global);
        if (thread != 0u && real_mem1(cpu, thread, 0x300u) && !ww_host_linked_alias_overlaps(thread, 0x300u)) {
            record.thread_valid = 1u;
            record.thread_state = mem_read16(cpu, thread + 0x2C8u);
            record.thread_suspend = mem_read32(cpu, thread + 0x2CCu);
            record.thread_queue = mem_read32(cpu, thread + 0x2DCu);
            record.thread_saved_pc = mem_read32(cpu, thread + 0x198u);
            record.thread_saved_lr = mem_read32(cpu, thread + 0x84u);
            record.thread_sp = mem_read32(cpu, thread + 4u);
        }
    }
    return persist(&record);
}
