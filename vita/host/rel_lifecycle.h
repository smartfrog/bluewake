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

struct WwHost;
void ww_host_rel_lifecycle_revoke(struct WwHost* host);
const char* ww_host_rel_lifecycle_boundary(struct WwHost* host, u32 address);
bool ww_host_rel_return_qualified(struct WwHost* host, u32 address);
const char* ww_host_rel_constructor_qualify(struct WwHost* host, const WwRelImage* image,
                                          u32 section, u32 offset, u32 caller);
const char* ww_host_rel_constructor_complete(struct WwHost* host, const WwRelImage* image, u32 address);
const char* ww_host_rel_suspend_return(struct WwHost* host, u32 address, u32 exception, u32 vector);
const char* ww_host_rel_rfi_prepare(struct WwHost* host, u32 cia, u32 verified_vector);
const char* ww_host_rel_rfi_finish(struct WwHost* host, u32 cia, bool vector_handoff);

#endif
