#ifdef NDEBUG
#undef NDEBUG
#endif
#include "boot_host.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The [edge] trace budget belongs to the run, not to whichever WwHost the edge
 * service is handed. A stale or wrong user pointer must not be able to open a
 * second allowance: the 2026-10-17 watch run printed 5,844,424 of these lines
 * against a 16-print arm, because the budget lived in the object `user` named.
 *
 * The hosts below are static and zeroed; a non-null error makes ww_host_edge
 * return right after the trace, so nothing past the print is ever walked. */
int __real_fprintf(FILE* stream, const char* format, ...);
static unsigned edge_lines;
int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && strncmp(format, "[edge] ", 7u) == 0)
        ++edge_lines;
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static WwHost hosts[3];

static void arm(WwHost* host) {
    host->error = "edge trace budget probe stop";
    host->dispatch_trace_remaining = WW_EDGE_TRACE_BUDGET;
}

static void poke(WwHost* host, unsigned count) {
    for (unsigned i = 0u; i < count; ++i)
        (void)ww_host_edge(host, &host->cpu, 0x80100000u);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "cap") == 0) {
        arm(&hosts[0]);
        poke(&hosts[0], WW_EDGE_TRACE_BUDGET + 4u);
        assert(edge_lines == WW_EDGE_TRACE_BUDGET);
        assert(hosts[0].dispatch_trace_remaining == 0u);
    } else if (strcmp(argv[1], "crosstalk") == 0) {
        arm(&hosts[0]);
        arm(&hosts[1]);
        poke(&hosts[0], WW_EDGE_TRACE_BUDGET / 2u + 2u);
        poke(&hosts[1], WW_EDGE_TRACE_BUDGET / 2u + 2u);
        assert(edge_lines == WW_EDGE_TRACE_BUDGET);
    } else if (strcmp(argv[1], "disarmed") == 0) {
        hosts[0].error = "edge trace budget probe stop";
        poke(&hosts[0], WW_EDGE_TRACE_BUDGET + 4u);
        assert(edge_lines == 0u);
    } else {
        return 2;
    }
    return 0;
}
