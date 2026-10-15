#ifndef WW_REL_PROFILE_BLOCK_H
#define WW_REL_PROFILE_BLOCK_H

#include "rel_service.h"
#include "module_exports.h"

typedef enum WwRelProfilePhase {
    WW_REL_PROFILE_NONE, WW_REL_PROFILE_EXPOSED, WW_REL_PROFILE_SIZE,
    WW_REL_PROFILE_SDK, WW_REL_PROFILE_RETURNED, WW_REL_PROFILE_SHRUNK,
    WW_REL_PROFILE_ACTIVE, WW_REL_PROFILE_CONSTRUCTED, WW_REL_PROFILE_COMPLETE,
    WW_REL_PROFILE_FAILED
} WwRelProfilePhase;

typedef struct WwRelBlock {
    WwRelProfilePhase phase;
    WwRelImage* image;
    const WwRelSection* sections;
    const struct BlueWakeRelInitContractV1* certificate;
    const u8* identity;
    u8* storage;
    u32 owner, guest, bytes, capacity, raw_fix, extent, logical_size, window;
    bool accounting_observed;
    bool input_refusal_reported;
} WwRelBlock;

typedef struct WwRelProfileBlock {
    WwRelBlock allocation;
    u8* raw;
    u32 publication, name_base, r1, r2, r13, r31;
} WwRelProfileBlock;

typedef enum WwRelProfileService {
    WW_REL_PROFILE_MISSED, WW_REL_PROFILE_HANDLED, WW_REL_PROFILE_ERROR
} WwRelProfileService;

struct WwHost;
bool ww_host_rel_profile_enabled(const struct WwHost* host);
const char* ww_host_rel_profile_expose(struct WwHost* host, WwRelImage* image);
const char* ww_host_rel_profile_observe(struct WwHost* host, u32 address);
WwRelProfileService ww_host_rel_profile_service(struct WwHost* host, u32 address);
const char* ww_host_rel_profile_lifecycle(struct WwHost* host, u32 address);
const char* ww_host_rel_profile_constructor(struct WwHost* host, const WwRelImage* image,
                                          u32 section, u32 offset, u32 caller, bool complete);
bool ww_host_rel_profile_completed(struct WwHost* host, const WwRelImage* image);
bool ww_host_rel_profile_return_qualified(struct WwHost* host, u32 address);
void ww_host_rel_profile_revoke(struct WwHost* host);
const char* ww_host_rel_title_expose(struct WwHost* host, WwRelImage* image);
bool ww_host_rel_title_inputs(struct WwHost* host, const WwRelImage* image);
const char* ww_host_rel_title_entry(struct WwHost* host, WwRelImage* image, bool fixed);
const char* ww_host_rel_title_return(struct WwHost* host, const WwRelImage* image);
void ww_host_rel_title_entered(struct WwHost* host);
void ww_host_rel_title_returned(struct WwHost* host);
bool ww_host_rel_title_accounted(struct WwHost* host, const WwRelImage* image);
bool ww_host_rel_title_published(struct WwHost* host, const WwRelImage* image);
void ww_host_rel_title_failed(struct WwHost* host);
const char* ww_rel_replay_resident(struct WwHost* host, const StaticRecompRelModule* rel,
    const u8* raw, u32 bytes, u8* relocated, const WwRelImage* sdk);
const char* ww_rel_sdk_structure(const WwRelImage* image, const u8* raw, u8* expected, u32 next);

#endif
