#ifndef WW_REL_LIFECYCLE_H
#define WW_REL_LIFECYCLE_H

#include "rel_contract.h"

typedef enum WwRelInitPhase {
    WW_REL_INIT_NONE,
    WW_REL_INIT_ACTIVE,
    WW_REL_INIT_RETURN_READY,
    WW_REL_INIT_RETURN_SUSPENDED
} WwRelInitPhase;

typedef enum WwRelRfiStage {
    WW_REL_RFI_VECTOR,
    WW_REL_RFI_HANDLER
} WwRelRfiStage;

typedef struct WwRelPendingInit {
    WwRelInitPhase phase;
    WwRelImage* image;
    const BlueWakeRelInitContractV1* certificate;
    const u8* identity;
    WwRelSection* sections;
    u32 module_id;
    u32 owner;
    u32 guest;
    u32 bytes;
    u32 capacity;
    u32 entry_pc;
    u32 successor;
    u32 r1;
    u32 r2;
    u32 r13;
    u32 r31;
    bool constructor_completed;
    bool entry_observation_open;
    u32 exception;
    u32 vector;
    u32 caller_msr;
    u32 context;
    u32 handler;
    u32 handoff_msr;
    WwRelRfiStage rfi_stage;
} WwRelPendingInit;

typedef enum WwRelUnlinkPhase {
    WW_REL_UNLINK_NONE,
    WW_REL_UNLINK_ENTRY_READY,
    WW_REL_UNLINK_EPILOG_ACTIVE,
    WW_REL_UNLINK_EPILOG_RETURNED,
    WW_REL_UNLINK_SDK_ACTIVE,
    WW_REL_UNLINK_SDK_RETURNED
} WwRelUnlinkPhase;

typedef struct WwRelPendingUnlink {
    WwRelUnlinkPhase phase;
    WwRelImage* image;
    const BlueWakeRelInitContractV1* certificate;
    const u8* identity;
    WwRelSection* sections;
    u32 module_id;
    u32 owner;
    u32 guest;
    u32 bytes;
    u32 capacity;
    u32 r1;
    u32 r2;
    u32 r13;
    u32 r31;
    u32 lr;
    u32 callback;
    bool destructor_open;
    bool destructor_observation_open;
    bool destructor_completed;
    u32 destructor_return;
    u32 destructor_table;
    u32 destructor_size;
    const u8* destructor_bytes;
} WwRelPendingUnlink;

/* The unlink caller's guest registers must survive the epilog/dtor walk. The
 * load path already snapshots r1/r2/r13/r31; the SDK-unlink entry at
 * 0x803056BC uses r1 as its stack. */
static inline bool ww_rel_unlink_caller_matches(const WwRelPendingUnlink* pending,
                                                const CPUState* cpu) {
    return pending != NULL && cpu != NULL &&
        cpu->gpr[1] == pending->r1 && cpu->gpr[2] == pending->r2 &&
        cpu->gpr[13] == pending->r13 && cpu->gpr[31] == pending->r31;
}

/* owner+0x3C is the callback the guest calls with mtlr/blrl at 0x80305B40.
 * It must still hold the relocated module code pointer captured before the
 * dtor; the raw unlinked value is a data word (0x65E20 in the fault). */
static inline bool ww_rel_unlink_callback_matches(const WwRelPendingUnlink* pending,
                                                  u32 live) {
    return pending != NULL && live == pending->callback;
}

struct WwHost;
void ww_host_rel_lifecycle_revoke(struct WwHost* host);
const char* ww_host_rel_lifecycle_boundary(struct WwHost* host, u32 address);
bool ww_host_rel_return_qualified(struct WwHost* host, u32 address);
const char* ww_host_rel_unlink_entry(struct WwHost* host, u32 address);
bool ww_host_rel_unlink_identity(struct WwHost* host, const WwRelPendingUnlink* pending);
const char* ww_host_rel_constructor_qualify(struct WwHost* host, const WwRelImage* image,
                                          u32 section, u32 offset, u32 caller);
const char* ww_host_rel_constructor_complete(struct WwHost* host, const WwRelImage* image, u32 address);
const char* ww_host_rel_destructor_qualify(struct WwHost* host, const WwRelImage* image,
                                         u32 section, u32 offset, u32 caller);
const char* ww_host_rel_destructor_complete(struct WwHost* host, const WwRelImage* image, u32 address);
const char* ww_host_rel_suspend_return(struct WwHost* host, u32 address, u32 exception, u32 vector);
const char* ww_host_rel_rfi_prepare(struct WwHost* host, u32 cia, u32 verified_vector);
const char* ww_host_rel_rfi_finish(struct WwHost* host, u32 cia, bool vector_handoff);

#endif
