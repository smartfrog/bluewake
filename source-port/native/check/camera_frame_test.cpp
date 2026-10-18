#include "check/map_j2d_stubs.h"

#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "d/d_map.h"
#include "f_op/f_op_camera.h"
#include "JSystem/J2DGraph/J2DGrafContext.h"

#include <cstdio>
#include <new>

void cM_rnd_c::init(int, int, int) {}
void dDlst_effectLine_c::update(cXyz&, GXColor&, u16, u16, u16, u16, f32, f32, f32, f32) {}
void dDlst_effectLine_c::draw() {}

static stage_map_info_class g_mapInfo = {};
static stage_stag_info_class g_stagInfo = {};

int main() {
    reset_gx_counters();

    auto* info = &g_dComIfG_gameInfo;
    dDlst_list_c& list = info->drawlist;
    list.mp2DOpa = list.mp2DOpaArr;
    list.mp2DOpaEnd = list.mp2DOpaArr + 64;

    J2DGrafContext graf(0.0f, 0.0f, 640.0f, 480.0f);
    dComIfGp_setCurrentGrafPort(reinterpret_cast<J2DOrthoGraph*>(&graf));

    info->play.mStageData.mpStagInfo = &g_stagInfo;

    dMap_RoomInfo_c room = {};
    room.field_0x1 = 3;
    room.m_no = 0;
    room.mStageMapInfoP = &g_mapInfo;
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
    dCamForcusLine& focus = camera->mCamera.mForcusLine;
    focus.m49 = 0;
    focus.m48 = 0;
    focus.m4C = 10;
    focus.m50 = 10;
    focus.m54 = 10;
    focus.m38 = cXyz(0.0f, 0.0f, 0.0f);
    focus.m44 = GXColor{255, 255, 255, 255};
    focus.m58 = 10;
    focus.m5A = 10;
    focus.m5C = 10;
    focus.m5E = 10;
    focus.m60 = 1.0f;
    focus.m64 = 1.0f;
    focus.m68 = 1.0f;
    focus.m6C = 1.0f;

    const bool camera_ok = camera->mCamera.Draw();
    const int afterCamera = static_cast<int>(list.mp2DOpa - list.mp2DOpaArr);
    const bool camera_no_draw = g_gx.begin == 0;

    dMap_c::mapDrawAll(0.0f, 0.0f, 0, 0.0f);
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

    std::printf("camera-frame: camera_ok=%d camera_items=%d enqueued=%d map_pre_draw=%d "
                "map_begin=%d total_begin=%d ind=%d tevdirect=%d pos2f=%d\n",
                camera_ok, afterCamera, enqueued, map_no_draw, map_draw_begin, g_gx.begin,
                g_gx.set_num_ind_stages, g_gx.set_tev_direct, g_gx.pos2f);

    const bool ok = camera_ok && afterCamera == 0 && enqueued == 10 && map_no_draw &&
                    map_draw_begin >= 10 && g_gx.begin > map_draw_begin &&
                    g_gx.set_num_ind_stages >= 1 && g_gx.set_tev_direct >= 8 &&
                    g_gx.pos2f >= 4;
    std::printf("camera-frame: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
