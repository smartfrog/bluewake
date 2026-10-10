#include "arm_control.h"
#include <stdio.h>
#include <string.h>

static uint32_t mode = 0xa6000090u;
static unsigned writes;
static bool broken;

bool arm_vfp_available(void) { return true; }
uint32_t arm_vfp_read(void) { return mode; }
void arm_vfp_write(uint32_t value) {
    writes++;
    mode = broken ? value ^ (1u << 24) : value;
}
uint64_t arm_vfp_operation(unsigned operation, uint64_t a, uint64_t b) {
    (void)operation;
    (void)b;
    mode |= 0x10u;
    return a;
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    if (strcmp(argv[1], "mapping") == 0) {
        const uint32_t expected[] = {0, 0x00c00000u, 0x00400000u, 0x00800000u};
        const uint32_t initial[] = {0xa6000090u, 0xa7c00090u};
        for (unsigned i = 0; i < 2; ++i)
            for (unsigned rn = 0; rn < 4; ++rn)
                for (unsigned fz = 0; fz < 2; ++fz)
                    if (arm_fp_mode(initial[i], rn, fz) !=
                        (0xa6000090u | expected[rn] | (fz << 24)))
                        return 2;
        return 0;
    }
    ArmFpEnvironment environment = arm_fp_environment();
    if (strcmp(argv[1], "restore") == 0) {
        for (unsigned i = 0; i < 71; ++i) {
            mode = 0x01c00000u;
            if (!arm_fp_restore(&environment) || mode != environment.initial)
                return 3;
        }
        return writes != 71;
    }
    if (strcmp(argv[1], "readback-error") == 0) {
        broken = true;
        return arm_fp_restore(&environment) ? 4 : 0;
    }
    if (strcmp(argv[1], "runtime-observer") == 0) {
        for (unsigned i = 0; i < 71; ++i) {
            ArmFpRuntime runtime = {.environment = &environment, .restore_ok = true};
            mode = 0x01c00000u;
            if (!arm_fp_observe_runtime(&runtime, 0) || mode != environment.initial)
                return 5;
            mode = 0x00400000u;
            if (!arm_fp_observe_runtime(&runtime, 1) || runtime.before != environment.initial)
                return 6;
            mode = 0x00800000u;
            if (!arm_fp_observe_runtime(&runtime, 2) || mode != 0x00800000u || runtime.updated != mode)
                return 7;
            mode |= 0x10u;
            if (!arm_fp_observe_runtime(&runtime, 3) || runtime.operated != mode)
                return 8;
            if (!arm_fp_restore_runtime(&runtime) || runtime.restored != environment.initial)
                return 9;
        }
        return 0;
    }
    return 1;
}
