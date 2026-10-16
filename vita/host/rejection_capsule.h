#ifndef WW05_REJECTION_CAPSULE_H
#define WW05_REJECTION_CAPSULE_H

#include "boot_host.h"

#include <stdbool.h>

#define WW_REJECTION_MAGIC 0x434A5257u
#define WW_REJECTION_VERSION 1u
#define WW_REJECTION_COMPLETE 0x524A4354u
#define WW_REJECTION_MAX_BYTES 256u

/* Allowlist results ww_host_edge already computed at the refused edge. */
typedef struct {
    bool rel_resume, module_owned;
    bool restore, context;
    bool retrace_sleep, draw_done_sleep, async_draw_done;
    bool dsp_task, card_probe;
} WwAdmissionRejection;

typedef int (*WwRejectionWriteAt)(void* user, uint32_t offset, const void* bytes, uint32_t size);

/* One fixed record per refusal; fields mirror the terminal tuple the designed-stop report could not retain. */
typedef struct {
    u32 magic, version, bytes, complete;
    u32 address, pc, lr, sp, r3, r13, r31;
    u32 msr, srr0, srr1;
    u32 module_owned;
    u32 queue_real_mem1, queue_alias_overlap, queue_head, queue_tail;
    u32 thread_valid, thread_state, thread_suspend, thread_queue;
    u32 thread_saved_pc, thread_saved_lr, thread_sp;
    u32 predicate_rel_resume, predicate_restore, predicate_context;
    u32 predicate_retrace_sleep, predicate_draw_done_sleep, predicate_async_draw_done;
    u32 predicate_dsp_task, predicate_card_probe;
    u32 io_error;
} WwRejectionCapsule;

/* A NULL writer disables the diagnostic; installed writers must stay inside WW_REJECTION_MAX_BYTES. */
void ww_rejection_capsule_init(WwRejectionWriteAt write_at, void* user);
const char* ww_host_rejection_capsule(WwHost* host, u32 address, const WwAdmissionRejection* rejection);
#ifdef __vita__
void ww_rejection_capsule_install_file(void);
#endif

#endif
