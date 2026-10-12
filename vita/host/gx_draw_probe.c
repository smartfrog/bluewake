#include "probe_support.h"
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

static bool fail_trace;
static bool fail_layout_journal;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr &&
        ((fail_trace && strncmp(format, "[gx-draw-layout]", 16u) == 0) ||
         (fail_layout_journal && strncmp(format, "[gx-command-layout]", 19u) == 0))) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static const char* feed(WwHost* host, const u8* bytes, u32 size, u32 pc) {
    for (u32 index = 0u; index < size; ++index) {
        const WwFifoOrigin origin = {.pc = pc + index * 4u, .sp = 0x817FFF00u};
        const char* error = ww_gx_stream_write(host, bytes[index], &origin, 0u);
        if (error != NULL)
            return error;
    }
    return NULL;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    host->stream.enabled = true;
    host->stream.log_limit = strcmp(scenario, "journal") == 0 ? 2048u : 64u;
    const u8 first[] = {0x61u, 0x41u, 0x12u, 0x34u, 0x56u};
    assert(feed(host, first, sizeof(first), 0x80001000u) == NULL);
    const WwGxCheckpoint frontier = host->frontier;
    fail_trace = strcmp(scenario, "trace") == 0;
    fail_layout_journal = strcmp(scenario, "layout-journal-failure") == 0;
    u8 cp[] = {0x08u, 0x50u, 0u, 0u, 2u, 0u,
               0x08u, 0x60u, 0u, 0u, 0u, 0u,
               0x08u, 0x70u, 0u, 0u, 0u, 9u};
    const u8 format = strcmp(scenario, "format") == 0 ? 3u : 0u;
    cp[13] += format;
    if (strcmp(scenario, "missing") != 0)
        assert(feed(host, cp, sizeof(cp), 0x80002000u) == NULL);
    if (strcmp(scenario, "layout-change") == 0) {
        const u8 change[] = {0x08u, 0x70u, 0u, 0u, 0u, 8u};
        assert(feed(host, change, sizeof(change), 0x80002100u) == NULL);
    }
    if (strcmp(scenario, "history-wrap") == 0)
        for (u32 index = 0u; index < 100u; ++index)
            assert(feed(host, cp + 12u, 6u, 0x80002200u + index * 24u) == NULL);
    if (strcmp(scenario, "journal") == 0) {
        const u8 nop = 0u;
        for (u32 index = 0u; index < 985u; ++index)
            assert(feed(host, &nop, 1u, 0x80002200u + index * 4u) == NULL);
    }
    const u64 decoded_before = host->stream.decoded;
    const u32 stride = strcmp(scenario, "layout-change") == 0 ? 8u : 12u;
    u8 draw[39] = {0x90u | format, 0u, 3u};
    if (strcmp(scenario, "reserved") == 0)
        draw[0] = 0x88u;
    if (strcmp(scenario, "oversize") == 0)
        draw[1] = draw[2] = 0xFFu;
    if (strcmp(scenario, "zero") == 0)
        draw[1] = draw[2] = 0u;
    const u32 command_bytes = 3u + 3u * stride;
    for (u32 index = 3u; index < command_bytes; ++index)
        draw[index] = (u8)(index * 13u);
    const bool failure = strcmp(scenario, "missing") == 0 || strcmp(scenario, "reserved") == 0 ||
                         strcmp(scenario, "oversize") == 0 || strcmp(scenario, "zero") == 0 || fail_trace;
    const char* error = feed(host, draw, command_bytes - (fail_layout_journal ? 0u : 1u), 0x80003000u);
    if (fail_layout_journal) {
        assert(error != NULL && host->stream.decoded == decoded_before);
        assert(host->stream.command.complete && host->stream.logged == 4u);
    } else if (failure) {
        assert(error != NULL && host->stream.decoded == decoded_before);
        assert(host->stream.command.size <= 3u);
    } else {
        assert(error == NULL && host->stream.decoded == decoded_before);
        assert(host->stream.command.required == command_bytes && !host->stream.command.complete);
        assert(feed(host, draw + command_bytes - 1u, 1u, 0x80004000u) == NULL);
        assert(host->stream.decoded == decoded_before + 1u && host->stream.command.size == 0u);
        assert(host->gx.vertex_layouts[format].valid && host->gx.vertex_layouts[format].vertex_size == stride);
        bool found = false;
        for (u32 index = 0u; index < host->gx.trace_count; ++index) {
            const DolGxRecompTraceEvent* event = &host->gx.trace[index];
            found |= event->kind == DOL_GX_RECOMP_EVENT_DRAW && event->a == 0x90u && event->b == format &&
                     event->c == 3u && event->d == stride;
        }
        assert(found);
        assert(feed(host, first, sizeof(first), 0x80005000u) == NULL);
        assert(host->stream.decoded == decoded_before + 2u);
    }
    assert(memcmp(&frontier, &host->frontier, sizeof(frontier)) == 0);
    assert(host->renderer == NULL && host->cycles.absolute_cycles == 0u);
    ww_host_shutdown(host);
    free(host);
    return 0;
}
