#ifndef WW05_REL_SERVICE_H
#define WW05_REL_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#define WW_REL_MAX_IMPORTS 32u

typedef struct WwRelSection {
    uint32_t flags;
    uint32_t size;
    uint32_t linked_start;
} WwRelSection;

typedef struct WwRelImage {
    uint32_t owner;
    uint32_t guest;
    uint32_t bytes;
    uint32_t capacity;
    uint32_t module_id;
    uint64_t raw_hash;
    WwRelSection* sections;
    uint32_t section_count;
    uint32_t section_table;
    bool linked;
    bool unlinked;
    uint32_t dependencies[WW_REL_MAX_IMPORTS];
    uint32_t dependency_count;
    char name[96];
} WwRelImage;

#define WW_REL_MAX_IMAGES 512u
#define WW_REL_SCRATCH_BASE 0x81820000u
#define WW_REL_SCRATCH_LIMIT 0x81F80000u
#define WW_REL_MODULE_REGION_LIMIT 0x82000000u
#define WW_REL_SHIP_SCRATCH_BASE 0x81F90000u
#define WW_REL_SHIP_SCRATCH_LIMIT WW_REL_MODULE_REGION_LIMIT

struct WwHost;
bool ww_host_rel_raw_unchanged(const WwRelImage* image);
uint64_t ww_host_rel_raw_hash(const uint8_t* bytes, uint32_t size);
const char* ww_host_rel_constructor_boundary(struct WwHost* host, uint32_t address);

#endif
