// P-A follow-up: the bounded actor slice of the Vita boot path's profile list.
//
// The real list (game/src/f_pc/f_pc_profile_lst.cpp) names all 502 g_profile_* definitions.
// This one names the first slice from native/cmake/vita_profile_slice.cmake: those 24
// actor units compile for ARMv7/ILP32 against the generated stub assets, and the slice
// reduction keeps their real g_profile_* identities (mProcName, list IDs, size, flags)
// while dropping the actor code and method tables it cannot link yet. The rest of the
// 502 definitions stay for the compile sweep, so the check reports 24 of 502.
//
// The slots are filled by a static initialiser, before main: the real list is static data
// too. pc::staticInitCheck then verifies every mProcName against its slot (and the list
// pointer and terminator). Keep this list in step with vita_profile_slice.cmake; a missing
// profile fails the link, a wrong slot fails the check.
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_profile_lst.h"

process_profile_definition* g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e + 1] = {};

namespace {

struct SliceProfileList {
    SliceProfileList() {
        g_fpcPfLst_ProfileList[fpcNm_ALLDIE_e] = &g_profile_ALLDIE.base.base;
        g_fpcPfLst_ProfileList[fpcNm_AMI_PROP_e] = &g_profile_AMI_PROP.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BB_e] = &g_profile_BB.base.base;
        g_fpcPfLst_ProfileList[fpcNm_AM_e] = &g_profile_AM.base.base;
        g_fpcPfLst_ProfileList[fpcNm_AM2_e] = &g_profile_AM2.base.base;
        g_fpcPfLst_ProfileList[fpcNm_Beam_e] = &g_profile_Beam.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BDK_e] = &g_profile_BDK.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BDKOBJ_e] = &g_profile_BDKOBJ.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BGN_e] = &g_profile_BGN.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BGN2_e] = &g_profile_BGN2.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BGN3_e] = &g_profile_BGN3.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BOMB_FLOWER_e] = &g_profile_BOMB_FLOWER.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ACORN_LEAF_e] = &g_profile_ACORN_LEAF.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ATDOOR_e] = &g_profile_ATDOOR.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ANDSW0_e] = &g_profile_ANDSW0.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ANDSW2_e] = &g_profile_ANDSW2.base.base;
        g_fpcPfLst_ProfileList[fpcNm_AUCTION_e] = &g_profile_AUCTION.base.base;
        g_fpcPfLst_ProfileList[fpcNm_AGB_e] = &g_profile_AGB.base.base;
        g_fpcPfLst_ProfileList[fpcNm_AGBSW0_e] = &g_profile_AGBSW0.base.base;
        g_fpcPfLst_ProfileList[fpcNm_BG_e] = &g_profile_BG.def.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ATT_e] = &g_profile_ATT.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ARROW_e] = &g_profile_ARROW.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ARROW_ICEEFF_e] = &g_profile_ARROW_ICEEFF.base.base;
        g_fpcPfLst_ProfileList[fpcNm_ARROW_LIGHTEFF_e] = &g_profile_ARROW_LIGHTEFF.base.base;
    }
};

const SliceProfileList sSliceProfileList;

} // namespace
