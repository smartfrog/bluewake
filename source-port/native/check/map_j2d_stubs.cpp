#include "check/map_j2d_stubs.h"

#include "dolphin/gx/GX.h"
#include "dolphin/gx/GXGeometry.h"
#include "dolphin/gx/GXLighting.h"
#include "dolphin/gx/GXPixel.h"
#include "dolphin/gx/GXTev.h"
#include "dolphin/gx/GXTransform.h"
#include "dolphin/gx/GXCull.h"
#include "dolphin/gx/GXBump.h"
#include "dolphin/mtx/mtx.h"
#include "m_Do/m_Do_mtx.h"

GxCounters g_gx;
Mtx mDoMtx_stack_c::now;
extern "C" unsigned char g_dComIfG_gameInfo[0x1D1C8] = {};
extern "C" GXColor g_clearColor = {};

void reset_gx_counters() {
    g_gx = {};
}

void mDoMtx_ZrotM(Mtx m, s16) {
    C_MTXIdentity(m);
}

extern "C" void C_MTXIdentity(Mtx m) {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.0f : 0.0f;
}

extern "C" void C_MTXTrans(Mtx m, f32 x, f32 y, f32 z) {
    C_MTXIdentity(m);
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
}

extern "C" {
void GXSetVtxAttrFmt(GXVtxFmt, GXAttr, GXCompCnt, GXCompType, u8) { g_gx.set_vtx_fmt++; }
void GXClearVtxDesc() {}
void GXSetVtxDesc(GXAttr, GXAttrType) {}
void GXLoadTexObj(GXTexObj*, GXTexMapID) {}
void GXSetNumChans(u8) {}
void GXSetTevColor(GXTevRegID, GXColor) { g_gx.set_tev_color++; }
void GXSetNumTexGens(u8) {}
void GXSetTexCoordGen2(GXTexCoordID, GXTexGenType, GXTexGenSrc, u32, GXBool, u32) {}
void GXSetNumTevStages(u8) {}
void GXSetTevOrder(GXTevStageID, GXTexCoordID, GXTexMapID, GXChannelID) {}
void GXSetTevColorIn(GXTevStageID, GXTevColorArg, GXTevColorArg, GXTevColorArg, GXTevColorArg) {}
void GXSetTevColorOp(GXTevStageID, GXTevOp, GXTevBias, GXTevScale, GXBool, GXTevRegID) {}
void GXSetTevAlphaIn(GXTevStageID, GXTevAlphaArg, GXTevAlphaArg, GXTevAlphaArg, GXTevAlphaArg) {}
void GXSetTevAlphaOp(GXTevStageID, GXTevOp, GXTevBias, GXTevScale, GXBool, GXTevRegID) {}
void GXSetZCompLoc(GXBool) {}
void GXSetZMode(GXBool, GXCompare, GXBool) {}
void GXSetBlendMode(GXBlendMode, GXBlendFactor, GXBlendFactor, GXLogicOp) {}
void GXSetAlphaCompare(GXCompare, u8, GXAlphaOp, GXCompare, u8) { g_gx.set_alpha_compare++; }
void GXSetFog(GXFogType, f32, f32, f32, f32, GXColor) { g_gx.set_fog++; }
void GXSetCullMode(GXCullMode) {}
void GXSetDither(GXBool) {}
void GXSetClipMode(GXClipMode) {}
void GXLoadPosMtxImm(const void*, u32) { g_gx.load_pos_mtx++; }
void GXSetCurrentMtx(u32) {}
void GXSetScissor(u32, u32, u32, u32) { g_gx.set_scissor++; }
void GXBegin(GXPrimitive, GXVtxFmt, u16) { g_gx.begin++; }
void GXPosition2f32(f32, f32) { g_gx.pos2f++; }
void GXTexCoord2f32(f32, f32) { g_gx.tex2f++; }
void GXSetNumIndStages(u8) { g_gx.set_num_ind_stages++; }
void GXSetTevDirect(GXTevStageID) { g_gx.set_tev_direct++; }
void GXSetTevOp(GXTevStageID, GXTevMode) {}
void GXLoadTexMtxImm(const void*, u32, GXTexMtxType) { g_gx.load_tex_mtx++; }
void GXSetChanCtrl(GXChannelID, GXBool, GXColorSrc, GXColorSrc, u32, GXDiffuseFn, GXAttnFn) {
    g_gx.set_chan_ctrl++;
}
void GXSetLineWidth(u8, GXTexOffset) { g_gx.set_line_width++; }
void GXSetViewport(f32, f32, f32, f32, f32, f32) {}
}
