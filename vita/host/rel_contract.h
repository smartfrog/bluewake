#ifndef WW_REL_CONTRACT_H
#define WW_REL_CONTRACT_H

#include "module_exports.h"
#include "rel_service.h"

struct WwHost;
const StaticRecompRelSection* ww_rel_section(const StaticRecompRelModule* rel, u32 index);
const BlueWakeRelData* ww_rel_data(struct WwHost* host, u32 module, u32 section);
bool ww_rel_translated_section(struct WwHost* host, u32 start, u32 size);
bool ww_rel_code_matches(struct WwHost* host, const StaticRecompRelSection* section, const u8* raw, u32 offset);
const char* ww_rel_replay(struct WwHost* host, const StaticRecompRelModule* rel, const u8* raw,
                         u32 bytes, u8* relocated, const WwRelImage* sdk, bool certified);
const char* ww_rel_validate_certificate(struct WwHost* host, const BlueWakeRelInitContractV1* certificate);
const char* ww_rel_check_dol(const u8* bytes, u32 size, const char* path);
/* Borrowed immutable producer metadata; registration grants no lifecycle admission. */
const char* ww_host_register_rel_contracts(struct WwHost* host, const BlueWakeRelInitContractSetV1* set,
                                         const char* dol);
const BlueWakeRelInitContractV1* ww_host_rel_certificate(struct WwHost* host, u32 module_id);
#if WW05_TESTS_ONLY
const char* ww_host_rel_test_admission(struct WwHost* host);
#endif
const char* ww_host_rel_sdk_prepare(struct WwHost* host, WwRelImage* image, bool fixed);
const char* ww_host_rel_sdk_finish(struct WwHost* host, WwRelImage* image);
const char* ww_host_rel_sdk_observe(struct WwHost* host, u32 address);
bool ww_host_rel_sdk_integrity(struct WwHost* host);

#endif
