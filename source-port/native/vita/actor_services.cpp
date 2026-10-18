// The bounded service edge of the real actor base table.
//
// f_op_actor.cpp's fopAc_* wrappers are the real actor manager code; these are the game services
// they call that would otherwise pull d_map, d_demo, d_event, the audio engine or JSystem with
// them. Each is a stub, not the game's: they are enough for the table to be real and for the
// static-init path (which creates no actor) never to reach them.
//
// Not stubbed here: g_dComIfG_gameInfo (d_com_inf_game.cpp), dKy_tevstr_init (d_kankyo.cpp),
// dMenu_flag/dMenu_pause (d_meter.cpp) and dStage_roomControl_c::mStayNo (d_stage.cpp) all come
// from their real units in the callable group.
#include "d/d_demo.h"
#include "d/d_event.h"
#include "d/d_map.h"
#include "f_op/f_op_actor_mng.h"
#include "JAZelAudio/JAIZelBasic.h"
#include "JSystem/JUtility/JUTAssert.h"

#include <cstdio>
#include <cstdlib>

void fopAcM_DeleteHeap(fopAc_ac_c*) {}

BOOL fopAcM_cullingCheck(fopAc_ac_c*) {
    return FALSE;
}

void dDemo_actor_c::setActor(fopAc_ac_c*) {}

dDemo_actor_c* dDemo_object_c::getActor(u8) {
    return nullptr;
}

s32 dEvt_control_c::moveApproval(void*) {
    return dEvtMove_NOMOVE_e;
}

void dMap_c::drawActorPointMiniMap(fopAc_ac_c*) {}

JAIZelBasic* JAIZelBasic::zel_basic = nullptr;

void JAIZelBasic::seDeleteObject(Vec*) {}

u32 JUTAssertion::getSDevice() {
    return 0;
}

void JUTAssertion::showAssert(u32, const char* file, int line, const char* assertion) {
    std::fprintf(stderr, "[cos] actor service stub: JUT_ASSERT %s:%d %s\n", file, line, assertion);
    std::abort();
}
