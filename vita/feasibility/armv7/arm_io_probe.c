#include "arm_control.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t fake_mode = 0xa6000090u;

bool __real_arm_vfp_available(void);
uint32_t __real_arm_vfp_read(void);
void __real_arm_vfp_write(uint32_t value);
uint64_t __real_arm_vfp_operation(unsigned operation, uint64_t a, uint64_t b);

bool __wrap_arm_vfp_available(void) {
    return getenv("ARM_FAKE_GUARD") ? true : __real_arm_vfp_available();
}

uint32_t __wrap_arm_vfp_read(void) {
    return getenv("ARM_FAKE_GUARD") ? fake_mode : __real_arm_vfp_read();
}

void __wrap_arm_vfp_write(uint32_t value) {
    if (getenv("ARM_FAKE_GUARD"))
        fake_mode = value;
    else
        __real_arm_vfp_write(value);
}

uint64_t __wrap_arm_vfp_operation(unsigned operation, uint64_t a, uint64_t b) {
    if (!getenv("ARM_FAKE_GUARD"))
        return __real_arm_vfp_operation(operation, a, b);
    fake_mode |= 0x80u;
    return a;
}

__attribute__((destructor)) static void audit_fake_guard(void) {
    if (!getenv("ARM_FAKE_GUARD"))
        return;
    FILE* stream = fopen("fake-guard.audit", "wx");
    if (!stream)
        abort();
    char line[80];
    int length = snprintf(line, sizeof line, "FAKE initial=a6000090 final=%08x\n", fake_mode);
    int error = length < 0 || length >= (int)sizeof line || fwrite(line, 1, (size_t)length, stream) != (size_t)length;
    int closed = fclose(stream);
    if (error || closed != 0)
        abort();
}
