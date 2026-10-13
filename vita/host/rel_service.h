#ifndef WW05_REL_SERVICE_H
#define WW05_REL_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#define WW_REL_MAX_IMPORTS 32u

typedef struct WwRelImage {
    uint32_t owner;
    uint32_t guest;
    uint32_t bytes;
    uint32_t capacity;
    uint32_t module_id;
    bool linked;
    bool unlinked;
    uint32_t dependencies[WW_REL_MAX_IMPORTS];
    uint32_t dependency_count;
    char name[96];
} WwRelImage;

#define WW_REL_MAX_IMAGES 512u
#define WW_REL_SCRATCH_BASE 0x81820000u
#define WW_REL_SCRATCH_LIMIT 0x81F80000u

#endif
