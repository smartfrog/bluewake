#ifndef WW05_EDGE_ADMISSION_H
#define WW05_EDGE_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>

// Observation-only edges the host may let the guest execute itself. These
// checks are deliberately strict: the dispatch address must be the program
// counter, the block must be module-owned and no linked alias may overlap it.
// None of these edges runs a host service; the guest's own compiled block runs
// unchanged. 0x8022F9FC is d_s_name's scene-create block (the NAME_SCENE
// milestone); 0x80120188 is checkLavaFace's GroundCross return, which also
// requires the caller-return LR. The draw family - 0x8015EA5C daVrbox2_Draw,
// 0x8015E3F0 daVrbox_Draw, 0x8015D80C daSea_Draw and 0x800D8DB8 daBg_Draw -
// is entered as a block, so the LR is whatever the scene graph caller left and
// is deliberately not part of the admission. 0x800A0B60 is daBg's
// GetAttributeCode block entry, observed the same way. Their refusal capsules
// showed the compiled block entered normally (pc == address, module-owned, no
// alias).
static inline bool ww_edge_admit_observation(uint32_t address, uint32_t pc,
                                             uint32_t lr, bool module_owned,
                                             bool alias_overlap) {
    if (!module_owned || pc != address || alias_overlap)
        return false;
    if (address == 0x80120188u)
        return lr == address;
    return address == 0x8022F9FCu || address == 0x8015EA5Cu ||
           address == 0x8015E3F0u || address == 0x8015D80Cu ||
           address == 0x800D8DB8u || address == 0x800A0B60u;
}

#endif
