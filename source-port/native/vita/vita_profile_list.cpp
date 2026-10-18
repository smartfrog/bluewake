// P-A follow-up: the bounded actor slice of the Vita boot path's profile list.
//
// The real list (game/src/f_pc/f_pc_profile_lst.cpp) names all 502 g_profile_* definitions.
// This one names the 72-unit slice from native/cmake/vita_profile_slice.cmake: those units
// compile for ARMv7/ILP32 against the generated stub assets, and the slice reduction keeps
// their real g_profile_* identities (mProcName, list IDs, size, flags). The other 430
// definitions stay for the compile sweep, so the check reports 72 of 502.
//
// The slots and the base method-table pointers are set by a static initialiser, before main:
// the real list and its pointers are static data too. pc::staticInitCheck then verifies every
// mProcName against its slot (and the list pointer and terminator). Keep this list in step with
// vita_profile_slice.cmake; a missing profile fails the link, a wrong slot fails the check.
//
// The base tables (g_fpcLf_Method, g_fopAc_Method, ...) are the reduced data of the real
// f_pc/f_op units: their method functions are still the actor/API code the slice does not link,
// so only their addresses are real. The actor method table at offset 0x24 stays NULL.
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_profile_lst.h"
#include "f_pc/f_pc_leaf.h"
#include "f_op/f_op_actor.h"

process_profile_definition* g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e + 1] = {};

namespace {

void wireActor(actor_process_profile_definition& profile) {
    profile.base.base.sub_method = &g_fpcLf_Method.base;
    profile.base.sub_method = &g_fopAc_Method.base;
}

struct SliceProfileList {
    SliceProfileList() {
        g_fpcPfLst_ProfileList[fpcNm_ALLDIE_e] = &g_profile_ALLDIE.base.base;
        wireActor(g_profile_ALLDIE);
        g_fpcPfLst_ProfileList[fpcNm_Canon_e] = &g_profile_Canon.base.base;
        wireActor(g_profile_Canon);
        g_fpcPfLst_ProfileList[fpcNm_BRIDGE_e] = &g_profile_BRIDGE.base.base;
        wireActor(g_profile_BRIDGE);
        g_fpcPfLst_ProfileList[fpcNm_AMI_PROP_e] = &g_profile_AMI_PROP.base.base;
        wireActor(g_profile_AMI_PROP);
        g_fpcPfLst_ProfileList[fpcNm_BITA_e] = &g_profile_BITA.base.base;
        wireActor(g_profile_BITA);
        g_fpcPfLst_ProfileList[fpcNm_BB_e] = &g_profile_BB.base.base;
        wireActor(g_profile_BB);
        g_fpcPfLst_ProfileList[fpcNm_EP_e] = &g_profile_EP.base.base;
        wireActor(g_profile_EP);
        g_fpcPfLst_ProfileList[fpcNm_FF_e] = &g_profile_FF.base.base;
        wireActor(g_profile_FF);
        g_fpcPfLst_ProfileList[fpcNm_BK_e] = &g_profile_BK.base.base;
        wireActor(g_profile_BK);
        g_fpcPfLst_ProfileList[fpcNm_DEMO_KMM_e] = &g_profile_DEMO_KMM.base.base;
        wireActor(g_profile_DEMO_KMM);
        g_fpcPfLst_ProfileList[fpcNm_DEMO_DK_e] = &g_profile_DEMO_DK.base.base;
        wireActor(g_profile_DEMO_DK);
        g_fpcPfLst_ProfileList[fpcNm_AM_e] = &g_profile_AM.base.base;
        wireActor(g_profile_AM);
        g_fpcPfLst_ProfileList[fpcNm_AM2_e] = &g_profile_AM2.base.base;
        wireActor(g_profile_AM2);
        g_fpcPfLst_ProfileList[fpcNm_CC_e] = &g_profile_CC.base.base;
        wireActor(g_profile_CC);
        g_fpcPfLst_ProfileList[fpcNm_BL_e] = &g_profile_BL.base.base;
        wireActor(g_profile_BL);
        g_fpcPfLst_ProfileList[fpcNm_BPW_e] = &g_profile_BPW.base.base;
        wireActor(g_profile_BPW);
        g_fpcPfLst_ProfileList[fpcNm_BO_e] = &g_profile_BO.base.base;
        wireActor(g_profile_BO);
        g_fpcPfLst_ProfileList[fpcNm_BWD_e] = &g_profile_BWD.base.base;
        wireActor(g_profile_BWD);
        g_fpcPfLst_ProfileList[fpcNm_BWDS_e] = &g_profile_BWDS.base.base;
        wireActor(g_profile_BWDS);
        g_fpcPfLst_ProfileList[fpcNm_BWDG_e] = &g_profile_BWDG.base.base;
        wireActor(g_profile_BWDG);
        g_fpcPfLst_ProfileList[fpcNm_ESA_e] = &g_profile_ESA.base.base;
        wireActor(g_profile_ESA);
        g_fpcPfLst_ProfileList[fpcNm_DR_e] = &g_profile_DR.base.base;
        wireActor(g_profile_DR);
        g_fpcPfLst_ProfileList[fpcNm_DR2_e] = &g_profile_DR2.base.base;
        wireActor(g_profile_DR2);
        g_fpcPfLst_ProfileList[fpcNm_DAIOCTA_e] = &g_profile_DAIOCTA.base.base;
        wireActor(g_profile_DAIOCTA);
        g_fpcPfLst_ProfileList[fpcNm_DAIOCTA_EYE_e] = &g_profile_DAIOCTA_EYE.base.base;
        wireActor(g_profile_DAIOCTA_EYE);
        g_fpcPfLst_ProfileList[fpcNm_Beam_e] = &g_profile_Beam.base.base;
        wireActor(g_profile_Beam);
        g_fpcPfLst_ProfileList[fpcNm_BTD_e] = &g_profile_BTD.base.base;
        wireActor(g_profile_BTD);
        g_fpcPfLst_ProfileList[fpcNm_BMD_e] = &g_profile_BMD.base.base;
        wireActor(g_profile_BMD);
        g_fpcPfLst_ProfileList[fpcNm_BMDHAND_e] = &g_profile_BMDHAND.base.base;
        wireActor(g_profile_BMDHAND);
        g_fpcPfLst_ProfileList[fpcNm_BMDFOOT_e] = &g_profile_BMDFOOT.base.base;
        wireActor(g_profile_BMDFOOT);
        g_fpcPfLst_ProfileList[fpcNm_DK_e] = &g_profile_DK.base.base;
        wireActor(g_profile_DK);
        g_fpcPfLst_ProfileList[fpcNm_BDK_e] = &g_profile_BDK.base.base;
        wireActor(g_profile_BDK);
        g_fpcPfLst_ProfileList[fpcNm_BDKOBJ_e] = &g_profile_BDKOBJ.base.base;
        wireActor(g_profile_BDKOBJ);
        g_fpcPfLst_ProfileList[fpcNm_BST_e] = &g_profile_BST.base.base;
        wireActor(g_profile_BST);
        g_fpcPfLst_ProfileList[fpcNm_FGANON_e] = &g_profile_FGANON.base.base;
        wireActor(g_profile_FGANON);
        g_fpcPfLst_ProfileList[fpcNm_BGN_e] = &g_profile_BGN.base.base;
        wireActor(g_profile_BGN);
        g_fpcPfLst_ProfileList[fpcNm_BGN2_e] = &g_profile_BGN2.base.base;
        wireActor(g_profile_BGN2);
        g_fpcPfLst_ProfileList[fpcNm_BGN3_e] = &g_profile_BGN3.base.base;
        wireActor(g_profile_BGN3);
        g_fpcPfLst_ProfileList[fpcNm_BOSSITEM_e] = &g_profile_BOSSITEM.base.base;
        wireActor(g_profile_BOSSITEM);
        g_fpcPfLst_ProfileList[fpcNm_Demo_Item_e] = &g_profile_Demo_Item.base.base;
        wireActor(g_profile_Demo_Item);
        g_fpcPfLst_ProfileList[fpcNm_DEKU_ITEM_e] = &g_profile_DEKU_ITEM.base.base;
        wireActor(g_profile_DEKU_ITEM);
        g_fpcPfLst_ProfileList[fpcNm_Coming2_e] = &g_profile_Coming2.base.base;
        wireActor(g_profile_Coming2);
        g_fpcPfLst_ProfileList[fpcNm_Coming3_e] = &g_profile_Coming3.base.base;
        wireActor(g_profile_Coming3);
        g_fpcPfLst_ProfileList[fpcNm_BOMB_e] = &g_profile_BOMB.base.base;
        wireActor(g_profile_BOMB);
        g_fpcPfLst_ProfileList[fpcNm_Bomb2_e] = &g_profile_Bomb2.base.base;
        wireActor(g_profile_Bomb2);
        g_fpcPfLst_ProfileList[fpcNm_BOMB_FLOWER_e] = &g_profile_BOMB_FLOWER.base.base;
        wireActor(g_profile_BOMB_FLOWER);
        g_fpcPfLst_ProfileList[fpcNm_ACORN_LEAF_e] = &g_profile_ACORN_LEAF.base.base;
        wireActor(g_profile_ACORN_LEAF);
        g_fpcPfLst_ProfileList[fpcNm_FAN_e] = &g_profile_FAN.base.base;
        wireActor(g_profile_FAN);
        g_fpcPfLst_ProfileList[fpcNm_DOOR10_e] = &g_profile_DOOR10.base.base;
        wireActor(g_profile_DOOR10);
        g_fpcPfLst_ProfileList[fpcNm_DOOR12_e] = &g_profile_DOOR12.base.base;
        wireActor(g_profile_DOOR12);
        g_fpcPfLst_ProfileList[fpcNm_ATDOOR_e] = &g_profile_ATDOOR.base.base;
        wireActor(g_profile_ATDOOR);
        g_fpcPfLst_ProfileList[fpcNm_ANDSW0_e] = &g_profile_ANDSW0.base.base;
        wireActor(g_profile_ANDSW0);
        g_fpcPfLst_ProfileList[fpcNm_ANDSW2_e] = &g_profile_ANDSW2.base.base;
        wireActor(g_profile_ANDSW2);
        g_fpcPfLst_ProfileList[fpcNm_DAI_e] = &g_profile_DAI.base.base;
        wireActor(g_profile_DAI);
        g_fpcPfLst_ProfileList[fpcNm_BIGELF_e] = &g_profile_BIGELF.base.base;
        wireActor(g_profile_BIGELF);
        g_fpcPfLst_ProfileList[fpcNm_AUCTION_e] = &g_profile_AUCTION.base.base;
        wireActor(g_profile_AUCTION);
        g_fpcPfLst_ProfileList[fpcNm_DISAPPEAR_e] = &g_profile_DISAPPEAR.base.base;
        wireActor(g_profile_DISAPPEAR);
        g_fpcPfLst_ProfileList[fpcNm_DEMO00_e] = &g_profile_DEMO00.base.base;
        wireActor(g_profile_DEMO00);
        g_fpcPfLst_ProfileList[fpcNm_Dummy_e] = &g_profile_Dummy.base.base;
        wireActor(g_profile_Dummy);
        g_fpcPfLst_ProfileList[fpcNm_TagRock_e] = &g_profile_TagRock.base.base;
        wireActor(g_profile_TagRock);
        g_fpcPfLst_ProfileList[fpcNm_FallRock_e] = &g_profile_FallRock.base.base;
        wireActor(g_profile_FallRock);
        g_fpcPfLst_ProfileList[fpcNm_BRANCH_e] = &g_profile_BRANCH.base.base;
        wireActor(g_profile_BRANCH);
        g_fpcPfLst_ProfileList[fpcNm_BOOMERANG_e] = &g_profile_BOOMERANG.base.base;
        wireActor(g_profile_BOOMERANG);
        g_fpcPfLst_ProfileList[fpcNm_AGB_e] = &g_profile_AGB.base.base;
        wireActor(g_profile_AGB);
        g_fpcPfLst_ProfileList[fpcNm_AGBSW0_e] = &g_profile_AGBSW0.base.base;
        wireActor(g_profile_AGBSW0);
        g_fpcPfLst_ProfileList[fpcNm_BG_e] = &g_profile_BG.def.base.base;
        wireActor(g_profile_BG.def);
        g_fpcPfLst_ProfileList[fpcNm_ATT_e] = &g_profile_ATT.base.base;
        wireActor(g_profile_ATT);
        g_fpcPfLst_ProfileList[fpcNm_STANDITEM_e] = &g_profile_STANDITEM.base.base;
        wireActor(g_profile_STANDITEM);
        g_fpcPfLst_ProfileList[fpcNm_BOKO_e] = &g_profile_BOKO.base.base;
        wireActor(g_profile_BOKO);
        g_fpcPfLst_ProfileList[fpcNm_ARROW_e] = &g_profile_ARROW.base.base;
        wireActor(g_profile_ARROW);
        g_fpcPfLst_ProfileList[fpcNm_ARROW_ICEEFF_e] = &g_profile_ARROW_ICEEFF.base.base;
        wireActor(g_profile_ARROW_ICEEFF);
        g_fpcPfLst_ProfileList[fpcNm_ARROW_LIGHTEFF_e] = &g_profile_ARROW_LIGHTEFF.base.base;
        wireActor(g_profile_ARROW_LIGHTEFF);
    }
};

const SliceProfileList sSliceProfileList;

} // namespace
