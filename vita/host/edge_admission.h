#ifndef WW05_EDGE_ADMISSION_H
#define WW05_EDGE_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>
#include "edge_intercept_table.h"

#define WW_EDGE_ADMISSION_COUNT (BLUEWAKE_EDGE_DRAW_COUNT + 2u)

static inline uint32_t ww_edge_observation_index(uint32_t address) {
    if (address == 0x80120188u)
        return 0u;
    if (address == 0x8022F9FCu)
        return 1u;
    return 2u + bluewake_edge_draw_index(address);
}

static inline const char* ww_edge_observation_symbol(uint32_t index) {
    if (index == 0u)
        return "checkLavaFace.GroundCross-return";
    if (index == 1u)
        return "d_s_name.scene-create";
    return g_edge_draws[index - 2u].symbol;
}

// Observation-only edges the host may let the guest execute itself. These
// checks are deliberately strict: the dispatch address must be the program
// counter, the block must be module-owned and no linked alias may overlap it.
// None of these edges runs a host service; the guest's own compiled block runs
// unchanged. 0x8022F9FC is d_s_name's scene-create block (the NAME_SCENE
// milestone); 0x80120188 is checkLavaFace's GroundCross return, which also
// requires the caller-return LR. Generated actor draw entries have no LR shape
// requirement: their translated bodies and return values remain authoritative.
static inline bool ww_edge_admit_observation(uint32_t address, uint32_t pc,
                                             uint32_t lr, bool module_owned,
                                             bool alias_overlap) {
    if (!module_owned || pc != address || alias_overlap)
        return false;
    if (address == 0x80120188u)
        return lr == address;
    return address == 0x8022F9FCu || bluewake_edge_draw_index(address) != BLUEWAKE_EDGE_DRAW_COUNT;
}

#endif
