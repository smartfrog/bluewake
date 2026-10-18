#ifndef WW05_RETURN_ADDRESS_H
#define WW05_RETURN_ADDRESS_H

#include <stdbool.h>
#include <stdint.h>

/* ww_host_dispatch is entered from the chassis glue with a linked Thumb
 * return. A poisoned LR (0xCCCCCCCC in the title-boundary fault) is host
 * frame corruption, not a guest bx; fail before it reaches the module. */
static inline bool ww_host_linked_thumb_return(const void* ret) {
    const uintptr_t value = (uintptr_t)ret;
    return (value & 1u) != 0u && value >= 0x81000000u && value < 0x98f315d8u;
}

#endif
