#ifndef WW_ARM_CONTROL_H
#define WW_ARM_CONTROL_H
#include <stdbool.h>
#include <stdint.h>

#define ARM_FP_MODE_MASK 0x01c00000u

typedef struct ArmFpEnvironment {
    bool available;
    uint32_t initial;
    uint32_t restored;
} ArmFpEnvironment;

typedef struct ArmFpRuntime {
    ArmFpEnvironment* environment;
    uint32_t before;
    uint32_t updated;
    uint32_t operated;
    uint32_t restored;
    bool restore_ok;
} ArmFpRuntime;

bool arm_vfp_available(void);
uint32_t arm_vfp_read(void);
void arm_vfp_write(uint32_t value);
uint64_t arm_vfp_operation(unsigned operation, uint64_t a, uint64_t b);
uint32_t arm_fp_mode(uint32_t initial, unsigned rn, unsigned fz);
ArmFpEnvironment arm_fp_environment(void);
bool arm_fp_restore(ArmFpEnvironment* environment);
bool arm_fp_observe_runtime(void* user, unsigned phase);
bool arm_fp_restore_runtime(ArmFpRuntime* runtime);
#endif
