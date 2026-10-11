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

const StaticRecompModuleDesc* staticrecomp_get_module(void);
const BlueWakeRelData* staticrecomp_get_rel_data(u32* count);
const BlueWakeRelLifecycle* staticrecomp_get_rel_lifecycle(u32* count);
void bluewake_set_edge_service(BluewakeEdgeServiceFn fn, void* user);

#endif
