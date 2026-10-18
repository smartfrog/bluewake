// Host test of the callable g_fpcLf_Method chain (cmake/vita_profile_slice.cmake): the table's
// five function pointers must be real, and the fpcLf_* -> fpcMtd_* path must run on a zeroed leaf
// whose profile points at the reduced g_fopAc_Method. fpcMtd_Method treats a NULL method as
// success, so the reduced actor table makes the path finite. No pc_harness, no disc.
//
//   ninja cos_vita_fpc_chain_test && build/native-mac/cos_vita_fpc_chain_test
#include "f_pc/f_pc_leaf.h"
#include "f_op/f_op_actor.h"

#include <cstdio>

int main() {
    int bad = 0;
    if (g_fpcLf_Method.base.mpCreateFunc == nullptr) {
        bad++;
    }
    if (g_fpcLf_Method.base.mpDeleteFunc == nullptr) {
        bad++;
    }
    if (g_fpcLf_Method.base.mpExecuteFunc == nullptr) {
        bad++;
    }
    if (g_fpcLf_Method.base.mpIsDeleteFunc == nullptr) {
        bad++;
    }
    if (g_fpcLf_Method.mpDrawFunc == nullptr) {
        bad++;
    }

    // Raw storage: the leaf is never constructed or destroyed, only the fields fpcLf_Create
    // reads are set.
    alignas(leafdraw_class) unsigned char storage[sizeof(leafdraw_class)] = {};
    auto& leaf = *reinterpret_cast<leafdraw_class*>(storage);
    leaf_process_profile_definition profile = {};
    profile.sub_method = &g_fopAc_Method.base;
    profile.mPriority = 7;
    leaf.mpProf = &profile.base;

    auto create = reinterpret_cast<cPhs_State (*)(leafdraw_class*)>(g_fpcLf_Method.base.mpCreateFunc);
    auto draw = reinterpret_cast<BOOL (*)(leafdraw_class*)>(g_fpcLf_Method.mpDrawFunc);
    auto execute = reinterpret_cast<BOOL (*)(leafdraw_class*)>(g_fpcLf_Method.base.mpExecuteFunc);
    auto isDelete = reinterpret_cast<BOOL (*)(leafdraw_class*)>(g_fpcLf_Method.base.mpIsDeleteFunc);
    auto remove = reinterpret_cast<BOOL (*)(leafdraw_class*)>(g_fpcLf_Method.base.mpDeleteFunc);

    const cPhs_State state = create(&leaf);
    const BOOL drew = draw(&leaf);
    const BOOL executed = execute(&leaf);
    const BOOL isDel = isDelete(&leaf);
    const BOOL removed = remove(&leaf);

    std::printf("fpc-chain: create=%d draw=%d execute=%d isDelete=%d delete=%d drawMtd=%p "
                "priority=%d subtype=%d bad=%d\n",
                (int)state, drew, executed, isDel, removed, (void*)leaf.mpDrawMtd,
                (int)leaf.mDwPi.mPriority, (int)leaf.mSubType, bad);
    if (leaf.mpDrawMtd != &g_fopAc_Method.base) {
        bad++;
    }
    if (leaf.mDwPi.mPriority != 7) {
        bad++;
    }
    if (leaf.mSubType != 0) {
        bad++;
    }
    if (state != 1 || drew != 1 || executed != 1 || isDel != 1 || removed != 1) {
        bad++;
    }
    return bad != 0;
}
