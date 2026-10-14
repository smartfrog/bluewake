#ifndef BLUEWAKE_MODULE_EXPORTS_H
#define BLUEWAKE_MODULE_EXPORTS_H

#include "StaticRecompABI.h"
#include "edge_intercept_abi.h"

typedef struct BlueWakeRelData {
    u32 module_id;
    u32 section_index;
    u32 linked_start;
    u32 size;
    const u8* bytes;
} BlueWakeRelData;

typedef struct BlueWakeRelLifecycle {
    u32 module_id;
    u32 prolog_address;
} BlueWakeRelLifecycle;

/* Zero means absent. These linked PCs describe header entries, not certified code.
 * A separate export preserves the legacy lifecycle array's 8-byte stride. */
typedef struct BlueWakeRelLifecycleV2 {
    u32 module_id;
    u32 prolog_address;
    u32 epilog_address;
    u32 unresolved_address;
} BlueWakeRelLifecycleV2;

typedef struct BlueWakeShipContractV1 {
    u32 version;
    u32 dol_size;
    const u8* dol_bytes;
    u32 rel_size;
    const u8* rel_bytes;
    const BlueWakeRelData* canonical_sections;
    u32 section_count;
    const char* build_identity;
} BlueWakeShipContractV1;

const StaticRecompModuleDesc* staticrecomp_get_module(void);
const BlueWakeRelData* staticrecomp_get_rel_data(u32* count);
const BlueWakeRelLifecycle* staticrecomp_get_rel_lifecycle(u32* count);
const BlueWakeRelLifecycleV2* staticrecomp_get_rel_lifecycle_v2(u32* count);
const BlueWakeShipContractV1* staticrecomp_get_ship_contract_v1(void);
void bluewake_set_edge_service(BluewakeEdgeServiceFn fn, void* user);

#endif
