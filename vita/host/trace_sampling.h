#ifndef WW05_TRACE_SAMPLING_H
#define WW05_TRACE_SAMPLING_H

#include <stdbool.h>
#include <stdint.h>

#ifndef WW05_QUIET_LOGGING
#define WW05_QUIET_LOGGING 0
#endif

/* The boot trace window is armed once, for the probe dispatch at 0x803077D4,
 * and disarmed when that dispatch returns. Sixteen lines is the whole run's
 * allowance, shared by [edge] and [spr-fallback]; the count lives outside
 * WwHost so it does not move with the object the edge service names. It bounds
 * a flood, it does not repair host corruption. */
enum { WW_EDGE_TRACE_BUDGET = 16u };

static inline bool ww_host_trace_transfer(uint64_t transfer) {
    return transfer <= 8u || transfer % 1000u == 0u;
}

#endif
