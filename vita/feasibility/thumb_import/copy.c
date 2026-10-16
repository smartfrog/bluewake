#include "probe.h"
#include <string.h>

/* Explicit libc call preserves the struct-assignment ABI without compiler copy lowering. */
__attribute__((noinline)) void probe_copy(void *destination, const void *source) {
    memcpy(destination, source, PROBE_BYTES);
}
