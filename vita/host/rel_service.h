#ifndef WW05_REL_SERVICE_H
#define WW05_REL_SERVICE_H

#include <stdint.h>

typedef struct WwRelImage {
    uint32_t owner;
    uint32_t guest;
    uint32_t bytes;
    uint32_t capacity;
    uint32_t module_id;
} WwRelImage;

#define WW_REL_SCRATCH_BASE 0x81820000u
#define WW_REL_SCRATCH_LIMIT 0x81F80000u

#endif
