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

/* A chunk's return address into the host call is module code in the linked
 * guest text window, with the Thumb state bit set. Host .text is above this
 * window; 0xCCCCCCCC and small values are also rejected. */
static inline bool ww_host_module_return(const void* ret) {
    const uintptr_t value = (uintptr_t)ret;
    return value >= 0x80000000u && value < 0x82000000u && (value & 1u) != 0u;
}

#endif
