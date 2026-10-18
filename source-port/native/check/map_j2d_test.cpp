#include "check/map_j2d_stubs.h"

#include "d/d_com_inf_game.h"
#include "d/d_map.h"
#include "JSystem/J2DGraph/J2DGrafContext.h"

#include <cstdio>
#include <cstring>

int main() {
    reset_gx_counters();

    J2DGrafContext graf(0.0f, 0.0f, 640.0f, 480.0f);
    dComIfGp_setCurrentGrafPort(reinterpret_cast<J2DOrthoGraph*>(&graf));

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

    std::printf("map-j2d: ind=%d tevdirect=%d posmtx=%d texmtx=%d begin=%d pos=%d tex=%d "
                "vtxfmt=%d line=%d scissor=%d alpha=%d fog=%d chan=%d tevcolor=%d\n",
                g_gx.set_num_ind_stages, g_gx.set_tev_direct, g_gx.load_pos_mtx, g_gx.load_tex_mtx,
                g_gx.begin, g_gx.pos2f, g_gx.tex2f, g_gx.set_vtx_fmt, g_gx.set_line_width,
                g_gx.set_scissor, g_gx.set_alpha_compare, g_gx.set_fog, g_gx.set_chan_ctrl,
                g_gx.set_tev_color);

    const bool ok = g_gx.set_num_ind_stages >= 1 && g_gx.set_tev_direct >= 8 &&
                    g_gx.load_pos_mtx >= 2 && g_gx.load_tex_mtx >= 1 && g_gx.begin >= 1 &&
                    g_gx.pos2f >= 4 && g_gx.tex2f >= 4 && g_gx.set_vtx_fmt >= 6 &&
                    g_gx.set_line_width >= 2 && g_gx.set_scissor >= 1 &&
                    g_gx.set_alpha_compare >= 2 && g_gx.set_fog >= 1 &&
                    g_gx.set_chan_ctrl >= 2 && g_gx.set_tev_color >= 2;
    std::printf("map-j2d: %s\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
