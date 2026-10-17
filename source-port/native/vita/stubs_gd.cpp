// E3 POC (build-f5/source-port-next-pa/REPORT.md, blocker 1): GD, the game's display-list writer,
// for the Vita source link (AURORA_ENABLE_GX=OFF, so aurora_gd is not built).
//
// GDWrite_u8/u16/u24/u32/u64, GDOverflowCheck, GDGetCurrOffset, GDSetCurrent and the command
// helpers are inline in Aurora's <dolphin/gd.h>: they write through __GDCurrentDL. That pointer
// and the out-of-line functions live here. The writer is real, because JSystem builds vertex
// descriptors and matrices into it; the sink keeps writes inside its buffer even when a game
// display list is never set, and an overflow resets the current list instead of writing past it.
//
// The GDSet* commands (vertex descriptors, arrays) are dropped: this build has no command
// processor to receive them. A caller that depends on the bytes reaching a GPU needs E3's
// option-C emitters, not this stub.
#include "stubs_gd.h"

#include <dolphin/gd.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr u32 kSinkSize = 4096;

u8 sSink[kSinkSize];
GDLObj sSinkObject = {sSink, kSinkSize, sSink, sSink + kSinkSize};
GDOverflowCb sOverflowCallback;
unsigned int sOverflows;

void ReportOnce() {
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr,
                     "[cos] GX stub: GD display lists are written to a buffer and dropped; nothing "
                     "is rendered (AURORA_ENABLE_GX=OFF)\n");
    }
}

} // namespace

GDLObj* __GDCurrentDL = &sSinkObject;

extern "C" {

void CosGDStubReset(void) {
    sSinkObject.start = sSink;
    sSinkObject.length = kSinkSize;
    sSinkObject.ptr = sSink;
    sSinkObject.top = sSink + kSinkSize;
    sOverflows = 0;
    sOverflowCallback = nullptr;
    __GDCurrentDL = &sSinkObject;
}

unsigned int CosGDStubOverflows(void) {
    return sOverflows;
}

void GDInitGDLObj(GDLObj* dl, void* start, u32 length) {
    dl->start = static_cast<u8*>(start);
    dl->length = length;
    dl->ptr = dl->start;
    dl->top = dl->start + length;
    __GDCurrentDL = dl;
}

void GDOverflowed(void) {
    ReportOnce();
    sOverflows++;
    if (sOverflowCallback != nullptr) {
        sOverflowCallback();
    }
    // The inline writers check before every write but then write unconditionally, so the only
    // safe landing after the check failed is the start of the list: the commands of this list are
    // dropped and the next write fits.
    __GDCurrentDL->ptr = __GDCurrentDL->start;
}

void GDSetOverflowCallback(GDOverflowCb callback) {
    sOverflowCallback = callback;
}

GDOverflowCb GDGetOverflowCallback(void) {
    return sOverflowCallback;
}

void GDPadCurr32(void) {
    while (((u32)(__GDCurrentDL->ptr - __GDCurrentDL->start) & 31) != 0) {
        GDWrite_u8(0);
    }
}

void GDFlushCurrToMem(void) {
    GDPadCurr32();
}

void GDSetVtxDescv(const GXVtxDescList* attrPtr) {
    (void)attrPtr;
    ReportOnce();
}

void GDSetArray(GXAttr attr, void* base_ptr, u8 stride) {
    (void)attr;
    (void)base_ptr;
    (void)stride;
    ReportOnce();
}

void GDSetArrayRaw(GXAttr attr, u32 base_ptr_raw, u8 stride) {
    (void)attr;
    (void)base_ptr_raw;
    (void)stride;
    ReportOnce();
}

void GDSetArraySized(GXAttr attr, void* base_ptr, u32 size, u8 stride, bool le) {
    (void)attr;
    (void)base_ptr;
    (void)size;
    (void)stride;
    (void)le;
    ReportOnce();
}

} // extern "C"

// Every remaining GD entry point Aurora declares (all 83 prototypes of <dolphin/gd.h>, minus
// the writer above): dropped, so a game unit links whether or not the source inventory saw
// the name.
void GDInsertDebugMarker(const char* label) {}
void GDLoadLightObjIndx(u32 lt_obj_indx, GXLightID light) {}
void GDLoadNrmMtxImm(const Mtx mtx, u32 id) {}
void GDLoadNrmMtxImm3x3(const f32 mtx[3][3], u32 id) {}
void GDLoadNrmMtxIndx3x3(u16 mtx_indx, u32 id) {}
void GDLoadPosMtxImm(const Mtx mtx, u32 id) {}
void GDLoadPosMtxIndx(u16 mtx_indx, u32 id) {}
void GDLoadTexMtxImm(const Mtx mtx, u32 id, GXTexMtxType type) {}
void GDLoadTexMtxIndx(u16 mtx_indx, u32 id, GXTexMtxType type) {}
void GDLoadTlut(void* tlut_ptr, u32 tmem_addr, GXTlutSize size) {}
void GDLoadTlutRaw(u32 tlut_ptr_raw, u32 tmem_addr, GXTlutSize size) {}
void GDPatchArrayPtr(void* base_ptr) {}
void GDPatchTexImgPtr(void* image_ptr) {}
void GDPopDebugGroup() {}
void GDPushDebugGroup(const char* label) {}
s32 GDReadDLFile(const char* fName, u32* numDLs, u32* numPLs, GDGList** DLDescArray, GDGList** PLDescArray) { return {}; }
void GDSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {}
void GDSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp logic_op) {}
void GDSetBlendModeEtc(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp logic_op, u8 color_update_enable, u8 alpha_update_enable, u8 dither_enable) {}
void GDSetChanAmbColor(GXChannelID chan, GXColor color) {}
void GDSetChanCtrl(GXChannelID chan, u8 enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn, GXAttnFn attn_fn) {}
void GDSetChanMatColor(GXChannelID chan, GXColor color) {}
void GDSetCoPlanar(u8 enable) {}
void GDSetCullMode(GXCullMode mode) {}
void GDSetCurrentMtx(u32 pn, u32 t0, u32 t1, u32 t2, u32 t3, u32 t4, u32 t5, u32 t6, u32 t7) {}
void GDSetDrawSync(u16 token) {}
void GDSetDstAlpha(u8 enable, u8 alpha) {}
void GDSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {}
void GDSetGenMode(u8 nTexGens, u8 nChans, u8 nTevs) {}
void GDSetGenMode2(u8 nTexGens, u8 nChans, u8 nTevs, u8 nInds, GXCullMode cm) {}
void GDSetIndTexCoordScale(GXIndTexStageID indStageEven, GXIndTexScale scaleS0, GXIndTexScale scaleT0, GXIndTexScale scaleS1, GXIndTexScale scaleT1) {}
void GDSetIndTexMtx(GXIndTexMtxID mtx_id, const f32 offset[2][3], s8 scale_exp) {}
void GDSetIndTexOrder(GXTexCoordID texCoord0, GXTexMapID texMap0, GXTexCoordID texCoord1, GXTexMapID texMap1, GXTexCoordID texCoord2, GXTexMapID texMap2, GXTexCoordID texCoord3, GXTexMapID texMap3) {}
void GDSetLPSize(u8 lineWidth, u8 pointSize, GXTexOffset lineOffset, GXTexOffset pointOffset, u8 lineHalfAspect) {}
void GDSetLightAttn(GXLightID light, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {}
void GDSetLightColor(GXLightID light, GXColor color) {}
void GDSetLightDir(GXLightID light, f32 nx, f32 ny, f32 nz) {}
void GDSetLightDistAttn(GXLightID light, f32 ref_dist, f32 ref_br, GXDistAttnFn dist_func) {}
void GDSetLightPos(GXLightID light, f32 x, f32 y, f32 z) {}
void GDSetLightSpot(GXLightID light, f32 cutoff, GXSpotFn spot_func) {}
void GDSetProjection(const Mtx44 mtx, GXProjectionType type) {}
void GDSetSpecularDir(GXLightID light, f32 nx, f32 ny, f32 nz) {}
void GDSetSpecularDirHA(GXLightID light, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {}
void GDSetTevAlphaCalcAndSwap(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d, GXTevOp op, GXTevBias bias, GXTevScale scale, u8 clamp, GXTevRegID out_reg, GXTevSwapSel ras_sel, GXTevSwapSel tex_sel) {}
void GDSetTevColor(GXTevRegID reg, GXColor color) {}
void GDSetTevColorCalc(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d, GXTevOp op, GXTevBias bias, GXTevScale scale, u8 clamp, GXTevRegID out_reg) {}
void GDSetTevColorS10(GXTevRegID reg, GXColorS10 color) {}
void GDSetTevDirect(GXTevStageID tev_stage) {}
void GDSetTevIndBumpST(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexMtxID matrix_sel) {}
void GDSetTevIndBumpXYZ(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexMtxID matrix_sel) {}
void GDSetTevIndRepeat(GXTevStageID tev_stage) {}
void GDSetTevIndTile(GXTevStageID tev_stage, GXIndTexStageID ind_stage, u16 tilesize_s, u16 tilesize_t, u16 tilespacing_s, u16 tilespacing_t, GXIndTexFormat format, GXIndTexMtxID matrix_sel, GXIndTexBiasSel bias_sel, GXIndTexAlphaSel alpha_sel) {}
void GDSetTevIndWarp(GXTevStageID tev_stage, GXIndTexStageID ind_stage, u8 signed_offset, u8 replace_mode, GXIndTexMtxID matrix_sel) {}
void GDSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexFormat format, GXIndTexBiasSel bias_sel, GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s, GXIndTexWrap wrap_t, u8 add_prev, u8 utc_lod, GXIndTexAlphaSel alpha_sel) {}
void GDSetTevKColor(GXTevKColorID reg, GXColor color) {}
void GDSetTevKonstantSel(GXTevStageID evenStage, GXTevKColorSel kcsel0, GXTevKAlphaSel kasel0, GXTevKColorSel kcsel1, GXTevKAlphaSel kasel1) {}
void GDSetTevOp(GXTevStageID stage, GXTevMode mode) {}
void GDSetTevOrder(GXTevStageID evenStage, GXTexCoordID coord0, GXTexMapID map0, GXChannelID color0, GXTexCoordID coord1, GXTexMapID map1, GXChannelID color1) {}
void GDSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {}
void GDSetTexCached(GXTexMapID id, u32 tmem_even, GXTexCacheSize size_even, u32 tmem_odd, GXTexCacheSize size_odd) {}
void GDSetTexCoordGen(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u8 normalize, u32 postmtx) {}
void GDSetTexCoordScale(GXTexCoordID coord, u16 s_scale, u16 t_scale) {}
void GDSetTexCoordScale2(GXTexCoordID coord, u16 s_scale, u8 s_bias, u8 s_wrap, u16 t_scale, u8 t_bias, u8 t_wrap) {}
void GDSetTexCoordScaleAndTOEs(GXTexCoordID coord, u16 s_scale, u8 s_bias, u8 s_wrap, u16 t_scale, u8 t_bias, u8 t_wrap, u8 line_offset, u8 point_offset) {}
void GDSetTexImgAttr(GXTexMapID id, u16 width, u16 height, GXTexFmt format) {}
void GDSetTexImgPtr(GXTexMapID id, void* image_ptr) {}
void GDSetTexImgPtrRaw(GXTexMapID id, u32 image_ptr_raw) {}
void GDSetTexLookupMode(GXTexMapID id, GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, GXTexFilter min_filt, GXTexFilter mag_filt, f32 min_lod, f32 max_lod, f32 lod_bias, u8 bias_clamp, u8 do_edge_lod, GXAnisotropy max_aniso) {}
void GDSetTexPreLoaded(GXTexMapID id, u32 tmem_even, u32 tmem_odd) {}
void GDSetTexTlut(GXTexMapID id, u32 tmem_addr, GXTlutFmt format) {}
void GDSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list) {}
void GDSetZMode(u8 compare_enable, GXCompare func, u8 update_enable) {}
void GDSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) {}
