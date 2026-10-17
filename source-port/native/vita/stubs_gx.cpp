// E3 POC (build-f5/source-port-next-pa/REPORT.md, blocker 1): the GX and GD entry points the
// game's units reference, for the Vita source link, which is built without a renderer
// (AURORA_ENABLE_GX=OFF, so Aurora's aurora_gx and aurora_gd are not built).
//
// What it is: every GX entry point Aurora declares resolves here (minus the seventeen cos_sdk
// defines itself), so a game object links without a renderer. Draws and commands are counted
// (stubs_gx.h), the state a later unit reads back is recorded (viewport, scissor, projection,
// cull mode, line and point size, a texture object's geometry, the draw callback), and the FIFO
// objects exist. The GD display-list writer is real (stubs_gd.cpp): JSystem writes vertex
// descriptors and matrices into it.
//
// What it is not: a renderer. Nothing reaches a GPU, no pixels are produced, and the GX state
// Aurora would translate to WebGPU is dropped. A function with no state to record is a no-op and
// is listed in the table at the end of this file; the ones with a body are the ones whose result
// a caller reads. Getters that were never set return 0/NULL, and GXGetTexObj* answer from the
// object the last GXInitTexObj/GXInitTexObjCI registered.
//
// The signatures are Aurora's <dolphin/gx.h> at the pinned commit, included below: a changed
// header breaks this file's compile instead of silently mismatching a caller.
#include "stubs_gx.h"

#include <dolphin/gx.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr unsigned int kTextureObjects = 16;

struct TextureObject {
    const GXTexObj* obj;
    const void* data;
    u16 width;
    u16 height;
    u32 format;
    GXTexWrapMode wrapS;
    GXTexWrapMode wrapT;
    GXBool mipmap;
    u32 tlut;
};

struct State {
    f32 viewport[6]{0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f};
    u32 scissor[4]{0, 0, 640, 480};
    f32 projection[7]{};
    GXCullMode cull{GX_CULL_NONE};
    u8 lineWidth{1};
    GXTexOffset lineOffsets{GX_TO_ZERO};
    u8 pointSize{1};
    GXTexOffset pointOffsets{GX_TO_ZERO};
    GXDrawDoneCallback drawDoneCallback{nullptr};
    TextureObject textureObjects[kTextureObjects]{};
    unsigned int textureObjectNext{};
    bool inDraw{};
};

State sState;
CosGXStubCounters sCounters;
GXFifoObj sFifo;

void ReportOnce() {
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr,
                     "[cos] GX stub: this build has no renderer (AURORA_ENABLE_GX=OFF); draws and "
                     "commands are counted and nothing is rendered\n");
    }
}

TextureObject* FindTextureObject(const GXTexObj* obj, bool create) {
    for (unsigned int i = 0; i < kTextureObjects; i++) {
        if (sState.textureObjects[i].obj == obj) {
            return &sState.textureObjects[i];
        }
    }
    if (!create) {
        return nullptr;
    }
    TextureObject* slot = &sState.textureObjects[sState.textureObjectNext];
    sState.textureObjectNext = (sState.textureObjectNext + 1) % kTextureObjects;
    std::memset(slot, 0, sizeof(*slot));
    slot->obj = obj;
    return slot;
}

} // namespace

extern "C" {

void CosGXStubReset(void) {
    sState = State{};
    sCounters = CosGXStubCounters{};
}

void CosGXStubCountersGet(CosGXStubCounters* counters) {
    *counters = sCounters;
}

void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    (void)type;
    (void)vtxfmt;
    ReportOnce();
    sState.inDraw = true;
    sCounters.beginCalls++;
    sCounters.vertices += nverts;
}

void GXEnd(void) {
    sCounters.endCalls++;
    if (sState.inDraw) {
        sState.inDraw = false;
        sCounters.draws++;
    }
}

void GXCmd1u8(const u8 x) {
    (void)x;
    ReportOnce();
    sCounters.commandBytes += 1;
}

void GXCmd1u16(const u16 x) {
    (void)x;
    sCounters.commandBytes += 2;
}

void GXCmd1u32(const u32 x) {
    (void)x;
    sCounters.commandBytes += 4;
}

void GXCmd1u64(const u64 x) {
    (void)x;
    sCounters.commandBytes += 8;
}

void GXFlush(void) {
    ReportOnce();
    sCounters.flushes++;
}

void GXDrawDone(void) {
    sCounters.flushes++;
}

void GXCallDisplayList(const void* list, u32 nbytes) {
    (void)list;
    (void)nbytes;
    sCounters.copies++;
}

void GXCopyDisp(void* dest, GXBool clear) {
    (void)dest;
    (void)clear;
    sCounters.copies++;
}

void GXCopyTex(void* dest, GXBool clear) {
    (void)dest;
    (void)clear;
    sCounters.copies++;
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    sState.viewport[0] = left;
    sState.viewport[1] = top;
    sState.viewport[2] = wd;
    sState.viewport[3] = ht;
    sState.viewport[4] = nearz;
    sState.viewport[5] = farz;
}

void GXGetViewportv(f32* vp) {
    std::memcpy(vp, sState.viewport, sizeof(sState.viewport));
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht) {
    sState.scissor[0] = left;
    sState.scissor[1] = top;
    sState.scissor[2] = wd;
    sState.scissor[3] = ht;
}

void GXGetScissor(u32* left, u32* top, u32* wd, u32* ht) {
    *left = sState.scissor[0];
    *top = sState.scissor[1];
    *wd = sState.scissor[2];
    *ht = sState.scissor[3];
}

void GXSetProjectionv(const f32* ptr) {
    std::memcpy(sState.projection, ptr, sizeof(sState.projection));
}

void GXGetProjectionv(f32* p) {
    std::memcpy(p, sState.projection, sizeof(sState.projection));
}

void GXSetProjection(const void* mtx, GXProjectionType type) {
    (void)mtx;
    // The six derived values GXGetProjectionv reports (the hardware encoding) are not computed:
    // only the projection type survives a matrix-form GXSetProjection through this stub.
    sState.projection[0] = static_cast<f32>(type);
    for (unsigned int i = 1; i < 7; i++) {
        sState.projection[i] = 0.0f;
    }
}

void GXSetCullMode(GXCullMode mode) {
    sState.cull = mode;
}

void GXGetCullMode(GXCullMode* mode) {
    *mode = sState.cull;
}

void GXSetLineWidth(u8 width, GXTexOffset texOffsets) {
    sState.lineWidth = width;
    sState.lineOffsets = texOffsets;
}

void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets) {
    sState.pointSize = pointSize;
    sState.pointOffsets = texOffsets;
}

void GXGetLineWidth(u8* width, GXTexOffset* texOffsets) {
    *width = sState.lineWidth;
    *texOffsets = sState.lineOffsets;
}

void GXGetPointSize(u8* pointSize, GXTexOffset* texOffsets) {
    *pointSize = sState.pointSize;
    *texOffsets = sState.pointOffsets;
}

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb) {
    GXDrawDoneCallback previous = sState.drawDoneCallback;
    sState.drawDoneCallback = cb;
    return previous;
}

GXFifoObj* GXInit(void* base, u32 size) {
    (void)base;
    (void)size;
    return &sFifo;
}

GXFifoObj* GXGetCPUFifo(void) {
    return &sFifo;
}

GXFifoObj* GXGetGPFifo(void) {
    return &sFifo;
}

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
    (void)fifo;
    (void)base;
    (void)size;
}

void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {
    (void)fifo;
    (void)readPtr;
    (void)writePtr;
}

void GXGetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr) {
    (void)fifo;
    *readPtr = nullptr;
    *writePtr = nullptr;
}

void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underlow, u32* fifoCount,
                     GXBool* cpu_write, GXBool* gp_read, GXBool* fifowrap) {
    (void)fifo;
    *overhi = GX_FALSE;
    *underlow = GX_FALSE;
    *fifoCount = 0;
    *cpu_write = GX_FALSE;
    *gp_read = GX_FALSE;
    *fifowrap = GX_FALSE;
}

void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle,
                   GXBool* brkpt) {
    *overhi = GX_FALSE;
    *underlow = GX_FALSE;
    *readIdle = GX_TRUE;
    *cmdIdle = GX_TRUE;
    *brkpt = GX_FALSE;
}

void GXInitTexObj(GXTexObj* obj, const void* data, u16 width, u16 height, GXTexFmt format,
                  GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap) {
    TextureObject* tex = FindTextureObject(obj, true);
    tex->data = data;
    tex->width = width;
    tex->height = height;
    tex->format = static_cast<u32>(format);
    tex->wrapS = wrapS;
    tex->wrapT = wrapT;
    tex->mipmap = mipmap;
    tex->tlut = 0;
}

void GXInitTexObjCI(GXTexObj* obj, const void* data, u16 width, u16 height, GXCITexFmt format,
                    GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlut) {
    TextureObject* tex = FindTextureObject(obj, true);
    tex->data = data;
    tex->width = width;
    tex->height = height;
    tex->format = static_cast<u32>(format);
    tex->wrapS = wrapS;
    tex->wrapT = wrapT;
    tex->mipmap = mipmap;
    tex->tlut = tlut;
}

void GXInitTlutObj(GXTlutObj* obj, const void* data, GXTlutFmt format, u16 entries) {
    (void)obj;
    (void)data;
    (void)format;
    (void)entries;
}

void GXLoadTexObj(GXTexObj* obj, GXTexMapID id) {
    (void)obj;
    (void)id;
}

void GXLoadTlut(const GXTlutObj* obj, u32 idx) {
    (void)obj;
    (void)idx;
}

u16 GXGetTexObjWidth(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? tex->width : 0;
}

u16 GXGetTexObjHeight(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? tex->height : 0;
}

void* GXGetTexObjData(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? const_cast<void*>(tex->data) : nullptr;
}

u32 GXGetTexObjTlut(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? tex->tlut : 0;
}

GXTexWrapMode GXGetTexObjWrapS(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? tex->wrapS : GX_CLAMP;
}

GXTexWrapMode GXGetTexObjWrapT(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? tex->wrapT : GX_CLAMP;
}

GXTexFmt GXGetTexObjFmt(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? static_cast<GXTexFmt>(tex->format) : GX_TF_I4;
}

GXBool GXGetTexObjMipMap(const GXTexObj* tex_obj) {
    const TextureObject* tex = FindTextureObject(tex_obj, false);
    return tex != nullptr ? tex->mipmap : GX_FALSE;
}

void* GXGetTexObjUserData(const GXTexObj* obj) {
    (void)obj;
    return nullptr;
}

void GXDestroyTexObj(GXTexObj* obj) {
    TextureObject* tex = FindTextureObject(obj, false);
    if (tex != nullptr) {
        std::memset(tex, 0, sizeof(*tex));
    }
}

void GXDestroyTlutObj(GXTlutObj* obj) {
    (void)obj;
}

void GXGetVtxDesc(GXAttr attr, GXAttrType* type) {
    (void)attr;
    *type = GX_NONE;
}

void GXGetVtxDescv(GXVtxDescList* vcd) {
    vcd[0].attr = GX_VA_NULL;
    vcd[0].type = GX_NONE;
}

void GXGetVtxAttrFmt(GXVtxFmt idx, GXAttr attr, GXCompCnt* compCnt, GXCompType* compType, u8* shift) {
    (void)idx;
    (void)attr;
    *compCnt = GX_POS_XYZ;
    *compType = GX_F32;
    *shift = 0;
}

void GXGetLightColor(const GXLightObj* lt_obj, GXColor* color) {
    (void)lt_obj;
    std::memset(color, 0, sizeof(*color));
}

void GXGetLightPos(const GXLightObj* lt_obj, f32* x, f32* y, f32* z) {
    (void)lt_obj;
    *x = 0.0f;
    *y = 0.0f;
    *z = 0.0f;
}

u32 GXCompressZ16(u32 z24, GXZFmt16 zfmt) {
    (void)z24;
    (void)zfmt;
    return 0;
}

u32 GXDecompressZ16(u32 z16, GXZFmt16 zfmt) {
    (void)z16;
    (void)zfmt;
    return 0;
}

u32 GXEndDisplayList(void) {
    return 0;
}

u32 GXGenNormalTable(u8 depth, f32* table) {
    (void)depth;
    (void)table;
    return 0;
}

void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWaterMark, u32 loWaterMark) {
    (void)fifo;
    (void)hiWaterMark;
    (void)loWaterMark;
}

GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback callback) {
    (void)callback;
    return nullptr;
}

void GXGetVtxAttrFmtv(GXVtxFmt fmt, GXVtxAttrFmtList* vat) {
    (void)fmt;
    vat[0].attr = GX_VA_NULL;
    vat[0].cnt = GX_POS_XYZ;
    vat[0].type = GX_F32;
    vat[0].frac = 0;
}
void GXPeekZ(u16 x, u16 y, u32* z) {
    (void)x;
    (void)y;
    *z = 0;
}

void GXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx,
               f32* sy, f32* sz) {
    (void)x;
    (void)y;
    (void)z;
    (void)mtx;
    (void)pm;
    (void)vp;
    *sx = 0.0f;
    *sy = 0.0f;
    *sz = 0.0f;
}
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap, u8 max_lod) {
    (void)width;
    (void)height;
    (void)format;
    (void)mipmap;
    (void)max_lod;
    return 0;
}

u32 GXSetDispCopyYScale(f32 vscale) {
    (void)vscale;
    return 1;
}

} // extern "C"

// The GX entry points above and cos_sdk do not define: no-ops (or a zero answer), so a game
// unit links whether or not the source inventory saw the name. An inline helper in a game
// header hides its call from a source grep (GXColor4u8 was found by the link check).
void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp) {}
void GXAdjustForOverscan(GXRenderModeObj* rmin, GXRenderModeObj* rmout, u16 hor, u16 ver) {}
void GXBeginDisplayList(void* list, u32 size) {}
void GXBeginIndexed(GXVtxFmt vtxfmt, u16 nverts, const u16* indices, u32 nindices) {}
void GXClearVtxDesc(void) {}
void GXColor1u16(u16 clr) {}
void GXColor1u32(u32 clr) {}
void GXColor1x16(u16 index) {}
void GXColor1x8(u8 index) {}
void GXColor3u8(u8 r, u8 g, u8 b) {}
void GXColor4f32(float r, float g, float b, float a) {}
void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {}
void GXCreateFrameBuffer(u32 width, u32 height) {}
void GXDestroyCopyTex(void* dest) {}
void GXDrawCube(void) {}
void GXDrawCylinder(u8 numEdges) {}
void GXDrawDodeca(void) {}
void GXDrawIcosahedron(void) {}
void GXDrawOctahedron(void) {}
void GXDrawSphere(u8 numMajor, u8 numMinor) {}
void GXDrawSphere1(u8 depth) {}
void GXDrawTorus(f32 rc, u8 numc, u8 numt) {}
void GXEnableTexOffsets(GXTexCoordID coord, GXBool line_enable, GXBool point_enable) {}
void GXInitLightAttn(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {}
void GXInitLightAttnA(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2) {}
void GXInitLightAttnK(GXLightObj* lt_obj, f32 k0, f32 k1, f32 k2) {}
void GXInitLightColor(GXLightObj* lt_obj, GXColor color) {}
void GXInitLightDir(GXLightObj* lt_obj, f32 nx, f32 ny, f32 nz) {}
void GXInitLightDistAttn(GXLightObj* lt_obj, f32 ref_distance, f32 ref_brightness, GXDistAttnFn dist_func) {}
void GXInitLightPos(GXLightObj* lt_obj, f32 x, f32 y, f32 z) {}
void GXInitLightSpot(GXLightObj* lt_obj, f32 cutoff, GXSpotFn spot_func) {}
void GXInitSpecularDir(GXLightObj* lt_obj, f32 nx, f32 ny, f32 nz) {}
void GXInitTexObjBiasClamp(GXTexObj* obj, GXBool bias_clamp) {}
void GXInitTexObjData(GXTexObj* obj, const void* data) {}
void GXInitTexObjEdgeLOD(GXTexObj* obj, GXBool do_edge_lod) {}
void GXInitTexObjFilter(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt) {}
void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt, f32 min_lod, f32 max_lod, f32 lod_bias, GXBool bias_clamp, GXBool do_edge_lod, GXAnisotropy max_aniso) {}
void GXInitTexObjLODBias(GXTexObj* obj, f32 lod_bias) {}
void GXInitTexObjMaxAniso(GXTexObj* obj, GXAnisotropy max_aniso) {}
void GXInitTexObjMaxLOD(GXTexObj* obj, f32 max_lod) {}
void GXInitTexObjMinLOD(GXTexObj* obj, f32 min_lod) {}
void GXInitTexObjTlut(GXTexObj* obj_, u32 tlut) {}
void GXInitTexObjUserData(GXTexObj* obj, void* user_data) {}
void GXInitTexObjWrapMode(GXTexObj* obj, GXTexWrapMode s, GXTexWrapMode t) {}
void GXInitTlutObjData(GXTlutObj* obj, const void* data) {}
void GXInsertDebugMarker(const char* label) {}
void GXInvalidateTexAll() {}
void GXInvalidateTexRegion(const GXTexRegion* region) {}
void GXInvalidateVtxCache(void) {}
void GXLoadLightObjImm(GXLightObj* lt_obj, GXLightID light) {}
void GXLoadNrmMtxImm(const void* mtx, u32 id) {}
void GXLoadPosMtxImm(const void* mtx, u32 id) {}
void GXLoadPosMtxIndx(u16 mtxIndx, u32 id) {}
void GXLoadTexMtxImm(const void* mtx, u32 id, GXTexMtxType type) {}
void GXNormal1x16(u16 index) {}
void GXNormal1x8(u8 index) {}
void GXNormal3f32(f32 x, f32 y, f32 z) {}
void GXNormal3s16(s16 x, s16 y, s16 z) {}
void GXNormal3s8(s8 x, s8 y, s8 z) {}
void GXParam1f32(const f32 x) {}
void GXParam1s16(const s16 x) {}
void GXParam1s32(const s32 x) {}
void GXParam1s8(const s8 x) {}
void GXParam1u16(const u16 x) {}
void GXParam1u32(const u32 x) {}
void GXParam1u8(const u8 x) {}
void GXParam3f32(const f32 x, const f32 y, const f32 z) {}
void GXParam4f32(const f32 x, const f32 y, const f32 z, const f32 w) {}
void GXPixModeSync(void) {}
void GXPokeARGB(u16 x, u16 y, u32 color) {}
void GXPokeAlphaMode(GXCompare func, u8 threshold) {}
void GXPokeAlphaUpdate(GXBool update_enable) {}
void GXPokeBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op) {}
void GXPokeColorUpdate(GXBool update_enable) {}
void GXPokeDither(GXBool dither) {}
void GXPokeDstAlpha(GXBool enable, u8 alpha) {}
void GXPokeZ(u16 x, u16 y, u32 z) {}
void GXPokeZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) {}
void GXPopDebugGroup() {}
void GXPosition1x16(u16 index) {}
void GXPosition1x8(u8 index) {}
void GXPosition2f32(f32 x, f32 y) {}
void GXPosition2s16(s16 x, s16 y) {}
void GXPosition2s8(s8 x, s8 y) {}
void GXPosition2u16(u16 x, u16 y) {}
void GXPosition2u8(u8 x, u8 y) {}
void GXPosition3f32(f32 x, f32 y, f32 z) {}
void GXPosition3s16(s16 x, s16 y, s16 z) {}
void GXPosition3s8(s8 x, s8 y, s8 z) {}
void GXPosition3u16(u16 x, u16 y, u16 z) {}
void GXPosition3u8(u8 x, u8 y, u8 z) {}
void GXPushDebugGroup(const char* label) {}
void GXRestoreFrameBuffer(void) {}
void GXSaveCPUFifo(GXFifoObj* fifo) {}
void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {}
void GXSetAlphaUpdate(GXBool update_enable) {}
void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le) {}
void GXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op) {}
void GXSetCPUFifo(GXFifoObj* fifo) {}
void GXSetChanAmbColor(GXChannelID chan, GXColor amb_color) {}
void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn, GXAttnFn attn_fn) {}
void GXSetChanMatColor(GXChannelID chan, GXColor mat_color) {}
void GXSetClipMode(GXClipMode mode) {}
void GXSetCoPlanar(GXBool enable) {}
void GXSetColorUpdate(GXBool update_enable) {}
void GXSetCopyClear(GXColor clear_clr, u32 clear_z) {}
void GXSetCopyFilter(GXBool aa, u8 sample_pattern[12][2], GXBool vf, u8 vfilter[7]) {}
void GXSetCurrentMtx(u32 id) {}
void GXSetDispCopyDst(u16 wd, u16 ht) {}
void GXSetDispCopyGamma(GXGamma gamma) {}
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {}
void GXSetDither(GXBool dither) {}
void GXSetDrawDone(void) {}
void GXSetDstAlpha(GXBool enable, u8 alpha) {}
void GXSetFieldMask(GXBool odd_mask, GXBool even_mask) {}
void GXSetFieldMode(GXBool field_mode, GXBool half_aspect_ratio) {}
void GXSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {}
void GXSetFogColor(GXColor color) {}
void GXSetFogRangeAdj(GXBool enable, u16 center, GXFogAdjTable *table) {}
void GXSetGPFifo(GXFifoObj* fifo) {}
void GXSetIndTexCoordScale(GXIndTexStageID indStage, GXIndTexScale scaleS, GXIndTexScale scaleT) {}
void GXSetIndTexMtx(GXIndTexMtxID mtx_sel, const void* offset, s8 scale_exp) {}
void GXSetIndTexOrder(GXIndTexStageID ind_stage, GXTexCoordID tex_coord, GXTexMapID tex_map) {}
void GXSetNumChans(u8 nChans) {}
void GXSetNumIndStages(u8 nIndStages) {}
void GXSetNumTevStages(u8 nStages) {}
void GXSetNumTexGens(u8 nTexGens) {}
void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) {}
void GXSetProjectionFull(const void* mtx) {}
void GXSetScissorBoxOffset(s32 x_off, s32 y_off) {}
void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht) {}
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {}
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {}
void GXSetTevColor(GXTevRegID id, GXColor color) {}
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {}
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {}
void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) {}
void GXSetTevDirect(GXTevStageID tev_stage) {}
void GXSetTevIndTile(GXTevStageID tevStage, GXIndTexStageID indStage, u16 tileSizeS, u16 tileSizeT, u16 tileSpacingS, u16 tileSpacingT, GXIndTexFormat fmt, GXIndTexMtxID matrixSel, GXIndTexBiasSel biasSel, GXIndTexAlphaSel alphaSel) {}
void GXSetTevIndWarp(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXBool signed_offsets, GXBool replace_mode, GXIndTexMtxID matrix_sel) {}
void GXSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexFormat format, GXIndTexBiasSel bias_sel, GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s, GXIndTexWrap wrap_t, GXBool add_prev, GXBool ind_lod, GXIndTexAlphaSel alpha_sel) {}
void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {}
void GXSetTevKColor(GXTevKColorID id, GXColor color) {}
void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {}
void GXSetTevOp(GXTevStageID id, GXTevMode mode) {}
void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {}
void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel, GXTevSwapSel tex_sel) {}
void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {}
void GXSetTexCoordBias(GXTexCoordID coord, GXBool s_enable, GXBool t_enable) {}
void GXSetTexCoordCylWrap(GXTexCoordID coord, GXBool s_enable, GXBool t_enable) {}
void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx, GXBool normalize, u32 postmtx) {}
void GXSetTexCoordScaleManually(GXTexCoordID coord, GXBool enable, u16 ss, u16 ts) {}
void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) {}
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) {}
void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field) {}
void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {}
void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {}
void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list) {}
void GXSetVtxDesc(GXAttr attr, GXAttrType type) {}
void GXSetVtxDescv(GXVtxDescList* list) {}
void GXSetZCompLoc(GXBool before_tex) {}
void GXSetZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) {}
void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) {}
void GXTexCoord1f32(f32 s) {}
void GXTexCoord1s16(s16 s) {}
void GXTexCoord1s8(s8 s) {}
void GXTexCoord1u16(u16 s) {}
void GXTexCoord1u8(u8 s) {}
void GXTexCoord1x16(u16 index) {}
void GXTexCoord1x8(u8 index) {}
void GXTexCoord2f32(f32 s, f32 t) {}
void GXTexCoord2s16(s16 s, s16 t) {}
void GXTexCoord2s8(s8 s, s8 t) {}
void GXTexCoord2u16(u16 s, u16 t) {}
void GXTexCoord2u8(u8 s, u8 t) {}
void GXTexModeSync(void) {}
