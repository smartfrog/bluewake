// E3 POC: the counters of the GX/GD stub (native/vita/stubs_gx.cpp, stubs_gd.cpp).
//
// A build without a renderer (AURORA_ENABLE_GX=OFF, the Vita source link) still has to link the
// game's GX and GD references. The stub records what the game asked for so a test can prove the
// calls reach it; nothing renders.
#ifndef COS_VITA_GX_STUB_H
#define COS_VITA_GX_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CosGXStubCounters {
    unsigned int beginCalls;   // GXBegin
    unsigned int endCalls;     // GXEnd
    unsigned int draws;        // GXBegin/GXEnd pairs that completed
    unsigned int vertices;     // vertices announced by GXBegin
    unsigned int commandBytes; // bytes handed to GXCmd1u8/u16/u32/u64
    unsigned int flushes;      // GXFlush and GXDrawDone
    unsigned int copies;       // GXCopyDisp and GXCopyTex
} CosGXStubCounters;

void CosGXStubReset(void);
void CosGXStubCountersGet(CosGXStubCounters* counters);

#ifdef __cplusplus
}
#endif

#endif
