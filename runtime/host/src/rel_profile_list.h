#ifndef BLUEWAKE_REL_PROFILE_LIST_H
#define BLUEWAKE_REL_PROFILE_LIST_H

#include "../../../cmake/composite/module_exports.h"

#define BLUEWAKE_PROFILE_LIST_GLOBAL 0x803F6A68u

static inline const BlueWakeRelData* bluewake_rel_profile_list(const BlueWakeRelData* data,
                                                              u32 count, u32 module_id) {
    if (module_id != 1u || data == NULL)
        return NULL;
    for (u32 index = 0u; index < count; ++index) {
        const BlueWakeRelData* image = &data[index];
        if (image->module_id == module_id && image->section_index == 4u &&
            image->linked_start != 0u && image->size != 0u)
            return image;
    }
    return NULL;
}

#endif
