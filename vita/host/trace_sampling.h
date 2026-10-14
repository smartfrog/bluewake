#ifndef WW05_TRACE_SAMPLING_H
#define WW05_TRACE_SAMPLING_H

#include <stdbool.h>
#include <stdint.h>

#ifndef WW05_QUIET_LOGGING
#define WW05_QUIET_LOGGING 0
#endif

static inline bool ww_host_trace_transfer(uint64_t transfer) {
    return transfer <= 8u || transfer % 1000u == 0u;
}

#endif
