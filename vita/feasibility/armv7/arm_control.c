#include "arm_control.h"
#include <assert.h>

uint32_t arm_fp_mode(uint32_t initial, unsigned rn, unsigned fz) {
    assert(rn < 4 && fz < 2);
    static const uint32_t rounding[] = {0u, 3u << 22, 1u << 22, 2u << 22};
    return (initial & ~ARM_FP_MODE_MASK) | rounding[rn] | (fz << 24);
}

ArmFpEnvironment arm_fp_environment(void) {
    ArmFpEnvironment environment = {.available = arm_vfp_available()};
    if (environment.available)
        environment.initial = environment.restored = arm_vfp_read();
    return environment;
}

bool arm_fp_restore(ArmFpEnvironment* environment) {
    if (!environment->available)
        return true;
    arm_vfp_write(environment->initial);
    environment->restored = arm_vfp_read();
    return environment->restored == environment->initial;
}

bool arm_fp_observe_runtime(void* user, unsigned phase) {
    ArmFpRuntime* runtime = user;
    if (phase < 2) {
        bool restored = arm_fp_restore(runtime->environment);
        runtime->restore_ok = runtime->restore_ok && restored;
        runtime->before = runtime->environment->restored;
        return restored;
    }
    uint32_t value = runtime->environment->available ? arm_vfp_read() : 0;
    if (phase == 2)
        runtime->updated = value;
    else
        runtime->operated = value;
    return true;
}

bool arm_fp_restore_runtime(ArmFpRuntime* runtime) {
    bool restored = arm_fp_restore(runtime->environment);
    runtime->restored = runtime->environment->restored;
    runtime->restore_ok = runtime->restore_ok && restored;
    return runtime->restore_ok;
}
