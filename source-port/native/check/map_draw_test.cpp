#include "check/map_j2d_stubs.h"

#include "d/d_com_inf_game.h"
#include "d/d_drawlist.h"
#include "d/d_map.h"
#include "JSystem/J2DGraph/J2DGrafContext.h"

#include <cstdio>
#include <new>

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


    dMap_c::mapDrawAll(0.0f, 0.0f, 0, 0.0f);
    list.draw2DOpa();


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

    std::printf("map-draw: begin=%d pos3f=%d pos3s16=%d tex=%d loadtex=%d end=%d vtxfmtv=%d "
                "getfmt=%d ind=%d tevdirect=%d pos2f=%d\n",
                g_gx.begin, g_gx.pos3f, g_gx.pos3s16, g_gx.tex2f, g_gx.load_tex_obj, g_gx.end,
                g_gx.set_vtx_fmtv, g_gx.get_vtx_fmt, g_gx.set_num_ind_stages, g_gx.set_tev_direct,
                g_gx.pos2f);

    const bool ok = g_gx.begin >= 11 && g_gx.pos3f >= 8 && g_gx.pos3s16 >= 32 &&
                    g_gx.tex2f >= 36 && g_gx.load_tex_obj >= 9 && g_gx.end >= 10 &&
                    g_gx.set_vtx_fmtv >= 10 && g_gx.get_vtx_fmt >= 10 &&
                    g_gx.set_num_ind_stages >= 1 && g_gx.set_tev_direct >= 8 &&
                    g_gx.pos2f >= 4;
    std::printf("map-draw: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
