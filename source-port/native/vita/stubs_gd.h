// E3 POC: the display-list writer behind the game's GD references (native/vita/stubs_gd.cpp).
#ifndef COS_VITA_GD_STUB_H
#define COS_VITA_GD_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

// Drops the sink and makes it the current display list again (tests).
void CosGDStubReset(void);
// How many times a display list ran past its buffer since CosGDStubReset.
unsigned int CosGDStubOverflows(void);

#ifdef __cplusplus
}
#endif

#endif
