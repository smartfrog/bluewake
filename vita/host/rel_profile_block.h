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

typedef struct WwRelProfileBlock {
    WwRelProfilePhase phase;
    WwRelImage* image;
    const WwRelSection* sections;
    u8* raw;
    u32 owner, guest, bytes, capacity, extent, logical_size, publication, name_base;
    u32 window, r1, r2, r13, r31;
    bool accounting_observed;
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
const char* ww_rel_replay_resident(struct WwHost* host, const StaticRecompRelModule* rel,
    const u8* raw, u32 bytes, u8* relocated, const WwRelImage* sdk);
const char* ww_rel_sdk_structure(const WwRelImage* image, const u8* raw, u8* expected, u32 next);

#endif
