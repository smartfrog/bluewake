#ifndef WW_FP_RUNTIME_H
#define WW_FP_RUNTIME_H
#include "fixture.h"

typedef struct FpCase {
    const char* id;
    u32 pc;
    u64 a;
    u64 b;
    u64 expected;
    u32 mode;
    u32 gqr;
    u32 width;
    u32 boundary;
} FpCase;

typedef bool (*FpObservation)(void* user, unsigned phase);
int fixture_fp_case(const FpCase* test, u64* actual, int* path, FpObservation observe, void* user);
#endif
