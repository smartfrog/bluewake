#include "check/map_j2d_stubs.h"

#include "d/d_com_inf_game.h"
#include "d/d_bg_s.h"
#include "d/d_drawlist.h"
#include "d/d_map.h"
#include "dolphin/gx/GX.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_camera.h"
#include "f_op/f_op_overlap_mng.h"
#include "JAZelAudio/JAIZelBasic.h"
#include "JSystem/J2DGraph/J2DGrafContext.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "m_Do/m_Do_machine.h"
#include "m_Do/m_Do_mtx.h"
#include "SSystem/SComponent/c_bg_s.h"

#include <cstdio>
#include <new>

static stage_map_info_class g_mapInfo = {};
static stage_stag_info_class g_stagInfo = {};
static GXRenderModeObj g_mode = {};
GXRenderModeObj* mDoMch_render_c::mRenderModeObj = nullptr;
extern "C" bool cos_r3b_camera_draw(camera_process_class*);
void dMap_c::draw(f32 x, f32 z, int roomNo, f32 y) { dMap_c::mapDrawAll(x, z, roomNo, y); }

JAIZelBasic* JAIZelBasic::zel_basic = reinterpret_cast<JAIZelBasic*>(1);

void JAIZelBasic::getCameraInfo(Vec*, MtxP, u32) {}
void JAIZelBasic::getCameraMapInfo(u32) {}
void JAIZelBasic::setCameraPolygonPos(Vec*) {}
void JAIZelBasic::setCameraGroupInfo(u8) {}
void cM_rnd_c::init(int, int, int) {}
u32 fopCamM_GetParam(camera_class*) { return 0; }
s16 fpcLf_GetPriority(leafdraw_class*) { return 0; }
void dDlst_window_c::setScissor(f32, f32, f32, f32) {}
u32 dBgS::GetMtrlSndId(cBgS_PolyInfo&) { return 0; }
s32 dBgS::GetGrpSoundId(cBgS_PolyInfo&) { return 0; }
s8 dStage_roomControl_c::mStayNo = 0;
void dStage_roomControl_c::setStayNo(int n) { mStayNo = n; }
void dDlst_window_c::setViewPort(f32, f32, f32, f32, f32, f32) {}
const cXyz cXyz::Zero = cXyz(0.0f, 0.0f, 0.0f);
void dDlst_effectLine_c::update(cXyz&, GXColor&, u16, u16, u16, u16, f32, f32, f32, f32) {}
void dDlst_effectLine_c::draw() {}

f32 cBgS::GroundCross(cBgS_GndChk*) {
    return -G_CM3D_F_INF;
}

void C_MTXPerspective(Mtx44, f32, f32, f32, f32) {}
void C_MTXCopy(const Mtx, Mtx) {}
u32 C_MTXInverse(const Mtx, Mtx) { return 0; }
void C_MTXConcat(const Mtx, const Mtx, Mtx) {}
void mDoMtx_lookAt(Mtx, const Vec*, const Vec*, const Vec*, s16) {}
void mDoMtx_concatProjView(f32 const (*)[4], f32 const (*)[4], f32 (*)[4]) {}
extern "C" int pc_camera_override(float*, float*) { return 0; }
void pc_fps60_camera_drawn(view_class*) {}
BOOL fopOvlpM_IsDoingReq() { return FALSE; }

int main() {
    reset_gx_counters();

    auto* info = &g_dComIfG_gameInfo;
    dDlst_list_c& list = info->drawlist;
    list.mp2DOpa = list.mp2DOpaArr;
    list.mp2DOpaEnd = list.mp2DOpaArr + 64;

    J2DGrafContext graf(0.0f, 0.0f, 640.0f, 480.0f);
    dComIfGp_setCurrentGrafPort(reinterpret_cast<J2DOrthoGraph*>(&graf));
    info->play.mStageData.mpStagInfo = &g_stagInfo;
    g_mapInfo.field_0x34 = 255;
    g_mode.fbWidth = 640;
    g_mode.efbHeight = 480;
    mDoMch_render_c::setRenderModeObj(&g_mode);

    dMap_RoomInfo_c room = {};
    room.field_0x1 = 3;
    room.m_no = 0;
    room.mStageMapInfoP = &g_mapInfo;
    room.m_exist = 0;
    dMap_c::mNowRoomInfoP = &room;
    dMap_c::mRoomInfoCtrl.m_info = nullptr;
    dMap_c::setMapDispMode(0);
    new (&dMap_c::m2DSQdraw) dMap_2DSQ_c();
    new (&dMap_c::m2DSQdraw2) dMap_2DSQ_c();
    dMap_c::mDispPosLeftUpX = 25;
    dMap_c::mDispPosLeftUpY = 338;
    dMap_c::mDispSizeX = 120;
    dMap_c::mDispSizeY = 120;
    dMap_c::mAlpha = 255;
    dMap_c::mCompAlpha = 255;

    for (int i = 0; i < 8; i++) {
        new (&dMap_c::mFrameTex[i]) dMap_2DMtMapSpcl_c();
        new (&dMap_c::mFrameTexture[i]) dMap_2DMtMapSpcl_tex_c();
        dMap_c::mFrameTex[i].field_0x4 = 1;
        dMap_c::mFrameTex[i].field_0x5 = 0;
        dMap_c::mFrameTex[i].field_0x8 = &dMap_c::mFrameTexture[i];
        dMap_c::mFrameTexture[i].field_0x0 = 1;
        dMap_c::mFrameTexture[i].field_0x1 = 0;
        dMap_c::mFrameTexture[i].mColor = {255, 255, 255, 255};
        dMap_c::mFrameTexture[i].field_0x34 = 0.0f;
        dMap_c::mFrameTexture[i].field_0x38 = 0.0f;
        dMap_c::mFrameTexture[i].field_0x3c = 1.0f;
        dMap_c::mFrameTexture[i].field_0x40 = 1.0f;
    }

    alignas(camera_process_class) unsigned char cameraStorage[sizeof(camera_process_class)] = {};
    auto* camera = reinterpret_cast<camera_process_class*>(cameraStorage);
    camera->mPrm1 = 0;
    camera->mCamera.mForcusLine.m49 = 0;

    alignas(fopAc_ac_c) unsigned char playerStorage[sizeof(fopAc_ac_c)] = {};
    auto* player = reinterpret_cast<fopAc_ac_c*>(playerStorage);
    player->current.pos.x = 0.0f;
    player->current.pos.y = 0.0f;
    player->current.pos.z = 0.0f;

    dComIfGp_setWindowNum(1);
    info->play.setWindow(0, 0.0f, 0.0f, 640.0f, 480.0f, 1.0f, 100000.0f, 0, 0);
    dComIfGp_setCameraInfo(0, reinterpret_cast<camera_class*>(camera), 0, 0, 0);
    dComIfGp_setPlayer(0, player);
    dComIfGp_roomControl_setStayNo(0);

    const bool camera_ok = cos_r3b_camera_draw(camera);
    const int enqueued = static_cast<int>(list.mp2DOpa - list.mp2DOpaArr);
    const bool map_no_draw = g_gx.begin == 0;

    list.draw2DOpa();
    const int map_draw_begin = g_gx.begin;

    alignas(dMap_2DT2_c) unsigned char storage[sizeof(dMap_2DT2_c)] = {};
    dMap_2DT2_c* map = reinterpret_cast<dMap_2DT2_c*>(storage);
    map->setPos(0.0f, 0.0f);
    map->setWide(128.0f);
    map->setHeight(128.0f);
    map->setScaleX(1.0f);
    map->setScaleY(1.0f);
    map->setScissor(0, 0, 640, 480);
    map->setColorW(GXColor{255, 255, 255, 255});
    map->setColorB(GXColor{0, 0, 0, 0});
    map->dMap_2DT2_c::draw();

    std::printf("camera-draw: camera_ok=%d enqueued=%d map_pre_draw=%d map_begin=%d "
                "total_begin=%d ind=%d tevdirect=%d pos2f=%d\n",
                camera_ok, enqueued, map_no_draw, map_draw_begin, g_gx.begin,
                g_gx.set_num_ind_stages, g_gx.set_tev_direct, g_gx.pos2f);

    const bool ok = camera_ok && enqueued == 10 && map_no_draw && map_draw_begin >= 10 &&
                    g_gx.begin > map_draw_begin && g_gx.set_num_ind_stages >= 1 &&
                    g_gx.set_tev_direct >= 8 && g_gx.pos2f >= 4;
    std::printf("camera-draw: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
