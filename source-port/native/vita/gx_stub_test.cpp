// E3 POC: focused test of the Vita GX/GD stub (native/vita/stubs_gx.cpp, stubs_gd.cpp).
//
// It runs on the host against Aurora's pinned <dolphin/gx.h> and <dolphin/gd.h> headers, with no
// Aurora library linked: the stub is the only definition of the GX and GD symbols here. A build
// without a renderer must still count the game's draws, answer the state getters the game reads
// back, and keep the GD display-list writer inside its buffer.
//
//   ninja cos_vita_gx_stub_test && build/cos_vita_gx_stub_test
//
// Exit code 0 when every check holds, 1 otherwise (each failure is printed).
#include "stubs_gd.h"
#include "stubs_gx.h"

#include <dolphin/gd.h>
#include <dolphin/gx.h>

#include <cstdio>
#include <cstring>

namespace {

int sFailures;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "gx-stub-test: FAIL %s\n", what);
        sFailures++;
    }
}

bool SameBytes(const void* a, const void* b, unsigned int size) {
    return std::memcmp(a, b, size) == 0;
}

void DrawDoneCallback() {}

void TestDrawCounters() {
    CosGXStubReset();
    CosGXStubCounters counters{};
    CosGXStubCountersGet(&counters);
    Check(counters.beginCalls == 0 && counters.draws == 0 && counters.vertices == 0,
          "reset clears the counters");

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXEnd();
    GXBegin(GX_TRIANGLES, GX_VTXFMT1, 3);
    GXEnd();
    CosGXStubCountersGet(&counters);
    Check(counters.beginCalls == 2 && counters.endCalls == 2, "GXBegin and GXEnd are counted");
    Check(counters.draws == 2, "each completed GXBegin/GXEnd pair is a draw");
    Check(counters.vertices == 7, "GXBegin's vertex count is accumulated");

    GXEnd();
    CosGXStubCountersGet(&counters);
    Check(counters.draws == 2 && counters.endCalls == 3,
          "a GXEnd without an open GXBegin is not a draw");
}

void TestCommandCounters() {
    CosGXStubReset();
    GXCmd1u8(0x61);
    GXCmd1u16(0x1234);
    GXCmd1u32(0x11223344);
    GXCmd1u64(0x1122334455667788);
    GXFlush();
    GXDrawDone();
    GXCopyDisp(nullptr, GX_TRUE);
    GXCopyTex(nullptr, GX_FALSE);
    CosGXStubCounters counters{};
    CosGXStubCountersGet(&counters);
    Check(counters.commandBytes == 15, "GXCmd1u* bytes are counted (1+2+4+8)");
    Check(counters.flushes == 2, "GXFlush and GXDrawDone are counted");
    Check(counters.copies == 2, "GXCopyDisp and GXCopyTex are counted");
}

void TestViewportScissorProjection() {
    CosGXStubReset();
    GXSetViewport(10.0f, 20.0f, 320.0f, 240.0f, 0.5f, 1000.0f);
    const f32 viewport[6] = {10.0f, 20.0f, 320.0f, 240.0f, 0.5f, 1000.0f};
    f32 readViewport[6]{};
    GXGetViewportv(readViewport);
    Check(SameBytes(viewport, readViewport, sizeof(viewport)), "GXGetViewportv returns GXSetViewport");

    GXSetScissor(5, 6, 7, 8);
    u32 left = 0, top = 0, wd = 0, ht = 0;
    GXGetScissor(&left, &top, &wd, &ht);
    Check(left == 5 && top == 6 && wd == 7 && ht == 8, "GXGetScissor returns GXSetScissor");

    const f32 projection[7] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    GXSetProjectionv(projection);
    f32 readProjection[7]{};
    GXGetProjectionv(readProjection);
    Check(SameBytes(projection, readProjection, sizeof(projection)),
          "GXGetProjectionv returns GXSetProjectionv");

    f32 matrix[4][4]{};
    GXSetProjection(matrix, GX_ORTHOGRAPHIC);
    GXGetProjectionv(readProjection);
    Check(readProjection[0] == 1.0f, "GXSetProjection keeps the projection type");
}

void TestCullLineAndPoint() {
    CosGXStubReset();
    GXSetCullMode(GX_CULL_BACK);
    GXCullMode cull = GX_CULL_NONE;
    GXGetCullMode(&cull);
    Check(cull == GX_CULL_BACK, "GXGetCullMode returns GXSetCullMode");

    GXSetLineWidth(3, GX_TO_ONE);
    u8 lineWidth = 0;
    GXTexOffset lineOffsets = GX_TO_ZERO;
    GXGetLineWidth(&lineWidth, &lineOffsets);
    Check(lineWidth == 3 && lineOffsets == GX_TO_ONE, "GXGetLineWidth returns GXSetLineWidth");

    GXSetPointSize(5, GX_TO_ZERO);
    u8 pointSize = 0;
    GXTexOffset pointOffsets = GX_TO_ONE;
    GXGetPointSize(&pointSize, &pointOffsets);
    Check(pointSize == 5 && pointOffsets == GX_TO_ZERO, "GXGetPointSize returns GXSetPointSize");
}

void TestVertexFormatTerminator() {
    CosGXStubReset();
    GXVtxAttrFmtList list[2]{};
    list[1].attr = GX_VA_POS;
    GXGetVtxAttrFmtv(GX_VTXFMT0, list);
    Check(list[0].attr == GX_VA_NULL, "GXGetVtxAttrFmtv returns an empty list (GX_VA_NULL first)");
    Check(list[1].attr == GX_VA_POS, "GXGetVtxAttrFmtv writes only the terminator");
}

void TestTextureObject() {
    CosGXStubReset();
    GXTexObj plain{};
    const void* data = &plain;
    GXInitTexObj(&plain, data, 32, 64, GX_TF_RGBA8, GX_CLAMP, GX_REPEAT, GX_FALSE);
    Check(GXGetTexObjWidth(&plain) == 32 && GXGetTexObjHeight(&plain) == 64,
          "GXInitTexObj's size is read back");
    Check(GXGetTexObjData(&plain) == data, "GXInitTexObj's data is read back");
    Check(GXGetTexObjWrapS(&plain) == GX_CLAMP && GXGetTexObjWrapT(&plain) == GX_REPEAT,
          "GXInitTexObj's wrap modes are read back");

    GXTexObj indexed{};
    GXInitTexObjCI(&indexed, data, 16, 16, GX_TF_C4, GX_REPEAT, GX_CLAMP, GX_FALSE, 3);
    Check(GXGetTexObjTlut(&indexed) == 3, "GXInitTexObjCI's TLUT is read back");
    Check(GXGetTexObjFmt(&indexed) == static_cast<GXTexFmt>(GX_TF_C4),
          "GXInitTexObjCI's format is read back");
    Check(GXGetTexObjMipMap(&indexed) == GX_FALSE, "GXInitTexObjCI's mipmap flag is read back");

    GXTexObj unknown{};
    Check(GXGetTexObjWidth(&unknown) == 0 && GXGetTexObjData(&unknown) == nullptr,
          "an unregistered texture object answers zero and NULL");
}

void TestCallbacks() {
    CosGXStubReset();
    Check(GXSetDrawDoneCallback(&DrawDoneCallback) == nullptr, "the first done callback has none before it");
    Check(GXSetDrawDoneCallback(nullptr) == &DrawDoneCallback,
          "GXSetDrawDoneCallback returns the previous callback");
}

void TestGdWriter() {
    CosGDStubReset();
    u8 buffer[64]{};
    GDLObj list{};
    GDInitGDLObj(&list, buffer, sizeof(buffer));
    Check(__GDCurrentDL == &list, "GDInitGDLObj makes the list current");
    GDWrite_u8(0x61);
    GDWrite_u16(0x1234);
    GDWrite_u32(0x11223344);
    GDFlushCurrToMem();
    const u8 expected[7] = {0x61, 0x12, 0x34, 0x11, 0x22, 0x33, 0x44};
    Check(SameBytes(buffer, expected, sizeof(expected)), "GDWrite_* writes big-endian bytes");
    Check(GDGetCurrOffset() == 32, "GDFlushCurrToMem pads the list to 32 bytes");
    Check(buffer[7] == 0, "the padding is zero");
}

void TestGdOverflowKeepsWritesInside() {
    CosGDStubReset();
    u8 small[8]{};
    GDLObj list{};
    GDInitGDLObj(&list, small, sizeof(small));
    for (int i = 0; i < 32; i++) {
        GDWrite_u8(0xA5);
    }
    Check(list.ptr >= list.start && list.ptr <= list.top, "an overflow leaves the writer in bounds");
    Check(CosGDStubOverflows() > 0, "an overflow is counted");
}

void TestGdSinkWithoutInit() {
    CosGDStubReset();
    GDWrite_u8(0x01);
    Check(__GDCurrentDL->ptr == __GDCurrentDL->start + 1,
          "the default sink accepts a write before any GDInitGDLObj");
}

} // namespace

int main() {
    TestDrawCounters();
    TestCommandCounters();
    TestViewportScissorProjection();
    TestCullLineAndPoint();
    TestVertexFormatTerminator();
    TestTextureObject();
    TestCallbacks();
    TestGdWriter();
    TestGdOverflowKeepsWritesInside();
    TestGdSinkWithoutInit();
    if (sFailures != 0) {
        std::fprintf(stderr, "gx-stub-test: %d check(s) failed\n", sFailures);
        return 1;
    }
    std::printf("gx-stub-test: ok\n");
    return 0;
}
