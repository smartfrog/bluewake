#include "arm_control.h"

#if defined(__vita__) && defined(__arm__) && !defined(__SOFTFP__)
bool arm_vfp_available(void) { return true; }

uint32_t arm_vfp_read(void) {
    uint32_t value;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(value) : : "memory");
    return value;
}

void arm_vfp_write(uint32_t value) {
    __asm__ __volatile__("vmsr fpscr, %0" : : "r"(value) : "memory");
}

__attribute__((noinline)) uint64_t arm_vfp_add(uint64_t a, uint64_t b) {
    uint32_t low, high;
    __asm__ __volatile__("vmov d0, %2, %3\n\tvmov d1, %4, %5\n\t"
                         "vadd.f64 d2, d0, d1\n\tvmov %0, %1, d2"
                         : "=r"(low), "=r"(high)
                         : "r"((uint32_t)a), "r"((uint32_t)(a >> 32)),
                           "r"((uint32_t)b), "r"((uint32_t)(b >> 32))
                         : "d0", "d1", "d2", "memory");
    return ((uint64_t)high << 32) | low;
}

__attribute__((noinline)) uint64_t arm_vfp_mul(uint64_t a, uint64_t b) {
    uint32_t low, high;
    __asm__ __volatile__("vmov d0, %2, %3\n\tvmov d1, %4, %5\n\t"
                         "vmul.f64 d2, d0, d1\n\tvmov %0, %1, d2"
                         : "=r"(low), "=r"(high)
                         : "r"((uint32_t)a), "r"((uint32_t)(a >> 32)),
                           "r"((uint32_t)b), "r"((uint32_t)(b >> 32))
                         : "d0", "d1", "d2", "memory");
    return ((uint64_t)high << 32) | low;
}

__attribute__((noinline)) uint64_t arm_vfp_convert(uint64_t a) {
    uint32_t low, high;
    __asm__ __volatile__("vmov d0, %2, %3\n\tvcvt.f32.f64 s4, d0\n\t"
                         "vcvt.f64.f32 d3, s4\n\tvmov %0, %1, d3"
                         : "=r"(low), "=r"(high)
                         : "r"((uint32_t)a), "r"((uint32_t)(a >> 32))
                         : "d0", "d2", "d3", "memory");
    return ((uint64_t)high << 32) | low;
}

uint64_t arm_vfp_operation(unsigned operation, uint64_t a, uint64_t b) {
    if (operation == 0)
        return arm_vfp_add(a, b);
    if (operation == 1)
        return arm_vfp_mul(a, b);
    return arm_vfp_convert(a);
}
#else
bool arm_vfp_available(void) { return false; }
uint32_t arm_vfp_read(void) { return 0; }
void arm_vfp_write(uint32_t value) { (void)value; }
uint64_t arm_vfp_operation(unsigned operation, uint64_t a, uint64_t b) {
    (void)operation;
    (void)a;
    (void)b;
    return 0;
}
#endif
