#include "probe_support.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static u32 fail_command;
static u32 trace_calls;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && strncmp(format, "[gx-command]", 12u) == 0 && ++trace_calls == fail_command) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    ww_probe_fifo_setup(host, true);
    host->cp_fifo_high_watermark = 96u;
    host->stream.enabled = true;
    host->stream.log_limit = strcmp(argv[1], "log-limit") == 0 ? 2u : 64u;
    mem_write16(&host->cpu, 0xCC000002u, 0x15u);
    assert(host->error == NULL);
    u8 bytes[2048] = {0x61u, 0x41u, 0x12u, 0x34u, 0x56u,
                      0x08u, 0x30u, 0x12u, 0x34u, 0x56u, 0x78u,
                      0x10u, 0x00u, 0x01u, 0x10u, 0x00u,
                      0x3Fu, 0x80u, 0x00u, 0x00u, 0x40u, 0x00u, 0x00u, 0x00u};
    const char* scenario = argv[1];
    const bool failing = strcmp(scenario, "unknown") == 0 || strcmp(scenario, "trace-first") == 0 ||
                         strcmp(scenario, "trace-second") == 0 || strcmp(scenario, "counter") == 0;
    if (strcmp(scenario, "unknown") == 0)
        bytes[5] = 0xFFu;
    else if (strcmp(scenario, "trace-first") == 0)
        fail_command = 1u;
    else if (strcmp(scenario, "trace-second") == 0)
        fail_command = 2u;
    else if (strcmp(scenario, "counter") == 0)
        host->stream.decoded = UINT64_MAX;
    else if (strcmp(scenario, "split-wrap") == 0) {
        memset(bytes + 5u, 0, sizeof(bytes) - 5u);
        bytes[5] = 0x10u;
        write_be32(bytes + 6u, 0x00080000u);
        host->pi_fifo_write_pointer = host->cp_fifo_write_pointer = host->cp_fifo_read_pointer = 0x1060u;
    }
    ww_probe_gather_bytes(host, bytes, 0u, 32u, 1u);
    if (failing) {
        assert(host->error != NULL);
        assert(host->cp_fifo_read_pointer == 0x1020u && host->cp_fifo_distance == 0u);
        if (strcmp(scenario, "unknown") == 0 || strcmp(scenario, "trace-second") == 0)
            assert(host->frontier.complete && host->stream.decoded == 1u);
        else
            assert(!host->frontier.complete);
    } else {
        assert(host->error == NULL && host->frontier.complete && host->frontier.size == 5u);
        assert(memcmp(host->frontier.bytes, bytes, 5u) == 0);
        assert(host->frontier.pc == 0x80001000u && host->frontier.completion_pc == 0x80001010u);
        assert(host->frontier.trailing_bytes == 27u);
        assert(!ww_host_edge(host, &host->cpu, host->cpu.pc) && !host->edge_stopped);
        if (strcmp(scenario, "split-wrap") == 0) {
            assert(host->stream.command.size == 27u && host->stream.decoded == 1u);
            ww_probe_gather_bytes(host, bytes, 32u, 64u, 1u);
            assert(host->stream.decoded == 20u && host->gx.last_xf_count == 9u);
            assert(host->cp_fifo_read_pointer == 0x1020u && host->cp_fifo_distance == 0u);
        } else if (strcmp(scenario, "many-blocks") == 0) {
            ww_probe_gather_bytes(host, bytes, 32u, sizeof(bytes), 1u);
            assert(host->stream.decoded == 2027u && host->stream.logged == 64u);
            assert(host->gx.fifo.size == sizeof(bytes) && host->fifo_capture.transfers == 64u);
            assert(host->fifo_capture.count == 0u && host->fifo_capture.read_bytes == 0u);
        } else {
            assert(host->stream.decoded == 11u && host->gx.fifo.size == 32u);
            assert(host->gx.bp_regs[0x41u] == 0x123456u && host->gx.matrix_index_a == 0x12345678u);
            assert(host->gx.last_xf_count == 2u);
            assert(host->stream.logged == (strcmp(scenario, "log-limit") == 0 ? 2u : 11u));
            if (strcmp(scenario, "second-block") == 0) {
                memcpy(bytes + 32u, bytes, 32u);
                bytes[33] = 0x42u;
                ww_probe_gather_bytes(host, bytes, 32u, 64u, 1u);
                assert(host->stream.decoded == 22u && host->gx.bp_regs[0x42u] == 0x123456u);
                assert(host->gx.fifo.size == 64u);
                assert(memcmp(host->frontier.bytes, bytes, 5u) == 0);
            }
        }
        assert(host->error == NULL && !host->edge_stopped);
        assert(host->frontier.pc == 0x80001000u && host->frontier.completion_pc == 0x80001010u);
    }
    ww_host_shutdown(host);
    free(host);
    return 0;
}
