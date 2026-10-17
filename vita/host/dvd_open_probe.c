#include "probe_support.h"
#include "gxruntime/dvd.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

static bool fail_trace;
static unsigned admission_lines;

int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (stream == stderr && strncmp(format, "[edge-admit]", 12u) == 0)
        admission_lines++;
    if (fail_trace && stream == stderr && (strncmp(format, "[dvd-open]", 10u) == 0 ||
                                          strncmp(format, "[rel-frontier]", 14u) == 0)) {
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
    const StaticRecompRange ranges[] = {{0x8030F2B0u, 0x8030FC00u}, {0x80240744u, 0x80240748u},
                                        {0x80017FD8u, 0x80017FDCu}, {0x80120188u, 0x8012018Cu},
                                        {0x800A0B60u, 0x800A0B64u}, {0x80322B20u, 0x80322B24u}};
    const StaticRecompModuleDesc module = {.code_ranges = ranges, .num_code_ranges = 6u};
    host->module = &module;
    CPUState* cpu = &host->cpu;
    const char* scenario = argv[1];
#if WW_EDGE_OBSERVATION_ADMIT
    const bool expect_admission = true;
#else
    const bool expect_admission = false;
#endif
    const bool rel = strncmp(scenario, "rel-", 4u) == 0;
    const bool archive = strncmp(scenario, "archive", 7u) == 0;
    cpu->pc = strcmp(scenario, "fast") == 0 ? 0x8030F5A4u :
              strcmp(scenario, "entry") == 0 ? 0x8030F2B0u :
              archive ? 0x80017FD8u : 0x8030F618u;
    cpu->lr = 0x80001004u;
    cpu->gpr[3] = strcmp(scenario, "fast") == 0 ? 1u : 0x80001000u;
    cpu->gpr[4] = 0x80002000u;
    const char* path = strcmp(scenario, "not-found") == 0 || strcmp(scenario, "archive-absent") == 0 ?
        "missing.bin" : "boot.bin";
    memcpy(cpu->ram + 0x1000u, path, strlen(path) + 1u);
    if (rel) {
        cpu->pc = 0x80240744u;
        mem_write32(cpu, 0x8000101Cu, 0x80003000u);
        memcpy(cpu->ram + 0x3000u, "synthetic_module", sizeof("synthetic_module"));
        if (strcmp(scenario, "rel-bad-object") == 0)
            cpu->gpr[3] = 0x817FFFF0u;
        else if (strcmp(scenario, "rel-bad-name") == 0)
            mem_write32(cpu, 0x8000101Cu, 0x90000000u);
        else if (strcmp(scenario, "rel-unterminated") == 0)
            memset(cpu->ram + 0x3000u, 'a', 256u);
        else if (strcmp(scenario, "rel-trace") == 0)
            fail_trace = true;
    }
    u8 object_before[0x20u];
    memcpy(object_before, cpu->ram + 0x1000u, sizeof(object_before));
    memset(cpu->ram + 0x1FF0u, 0xA5u, 0x50u);
    if (strcmp(scenario, "bad-path") == 0 || strcmp(scenario, "archive-bad-path") == 0)
        cpu->gpr[3] = 0x90000000u;
    else if (strcmp(scenario, "unterminated") == 0 || strcmp(scenario, "archive-unterminated") == 0)
        memset(cpu->ram + 0x1000u, 'a', 256u);
    else if (strcmp(scenario, "bad-file-info") == 0)
        cpu->gpr[4] = 0x817FFFF0u;
    else if (strcmp(scenario, "alias") == 0)
        assert(ppc_guest_alias_add(0x80002008u, 4u, NULL));
    else if (strcmp(scenario, "unowned") == 0 || strcmp(scenario, "archive-unowned") == 0 ||
             strcmp(scenario, "archive-unrelated-unowned") == 0)
        host->module = NULL;
    else if (strcmp(scenario, "bad-return") == 0 || strcmp(scenario, "archive-bad-return") == 0)
        cpu->lr |= 2u;
    else if (strcmp(scenario, "trace") == 0 || strcmp(scenario, "archive-trace") == 0)
        fail_trace = true;
    else if (strcmp(scenario, "archive-null") == 0)
        cpu->gpr[3] = 0u;
    else if (strcmp(scenario, "archive-r4") == 0)
        cpu->gpr[4] = 0x817FFFF0u;
    else if (strcmp(scenario, "archive-unrelated") == 0 ||
             strcmp(scenario, "archive-unrelated-unowned") == 0 ||
             strcmp(scenario, "archive-unrelated-mismatch") == 0)
        cpu->pc = 0x80120188u;
    else if (strcmp(scenario, "archive-unrelated-observation") == 0)
        cpu->pc = 0x800A0B60u;
    else if (strcmp(scenario, "archive-unrelated-service") == 0)
        cpu->pc = 0x80322B20u;
    else if (strcmp(scenario, "archive-unrelated-alias") == 0) {
        assert(ppc_guest_alias_add(0x80120188u, 4u, NULL));
        cpu->pc = 0x80120188u;
    }
    const bool caller_return = strcmp(scenario, "archive-unrelated") == 0 ||
                               strcmp(scenario, "archive-unrelated-unowned") == 0 ||
                               strcmp(scenario, "archive-unrelated-observation") == 0 ||
                               strcmp(scenario, "archive-unrelated-service") == 0 ||
                               strcmp(scenario, "archive-unrelated-alias") == 0;
    const bool observation_edge = caller_return || strcmp(scenario, "archive-unrelated-mismatch") == 0;
    if (caller_return)
        cpu->lr = cpu->pc;
    const bool borrowed = strcmp(scenario, "borrowed") == 0 || strcmp(scenario, "archive-borrowed") == 0;
    if (borrowed)
        assert(dvd_open_image("ux0:data/bluewake/GZLE01.iso"));
    const u32 address = cpu->pc;
    const CPUState cpu_before = host->cpu;
    const bool fails = strcmp(scenario, "missing-disc") == 0 || strcmp(scenario, "archive-missing-disc") == 0 ||
                       strcmp(scenario, "bad-path") == 0 || strcmp(scenario, "archive-bad-path") == 0 ||
                       strcmp(scenario, "archive-null") == 0 ||
                       strcmp(scenario, "unterminated") == 0 || strcmp(scenario, "archive-unterminated") == 0 ||
                       strcmp(scenario, "bad-file-info") == 0 ||
                       strcmp(scenario, "alias") == 0 || strcmp(scenario, "unowned") == 0 ||
                       strcmp(scenario, "archive-unowned") == 0 ||
                       (strcmp(scenario, "archive-unrelated") == 0 && !expect_admission) ||
                       strcmp(scenario, "archive-unrelated-unowned") == 0 ||
                       strcmp(scenario, "archive-unrelated-mismatch") == 0 ||
                       strcmp(scenario, "archive-unrelated-observation") == 0 ||
                       strcmp(scenario, "archive-unrelated-service") == 0 ||
                       strcmp(scenario, "archive-unrelated-alias") == 0 ||
                       strcmp(scenario, "trace") == 0 || strcmp(scenario, "archive-trace") == 0 ||
                       strcmp(scenario, "bad-return") == 0 || strcmp(scenario, "archive-bad-return") == 0 ||
                       borrowed || rel;
    const bool admitted = strcmp(scenario, "archive-unrelated") == 0 && expect_admission;
    assert(ww_host_edge(host, cpu, cpu->pc) == !admitted);
    if (fails) {
        assert(host->error != NULL && host->error_pc == address && host->edge_stopped);
        assert(cpu->pc == address);
        if (observation_edge)
            assert(admission_lines == 0u);
        if (archive) {
            const char* expected = NULL;
            if (strcmp(scenario, "archive-unowned") == 0 ||
                strncmp(scenario, "archive-unrelated", 17u) == 0)
                expected = "desktop interception unsupported; guest not skipped";
            else if (strcmp(scenario, "archive-bad-path") == 0 || strcmp(scenario, "archive-null") == 0)
                expected = "path requires a bounded mapped guest string";
            else if (strcmp(scenario, "archive-unterminated") == 0)
                expected = "path is not terminated within 256 bytes";
            else if (strcmp(scenario, "archive-bad-return") == 0)
                expected = "DVD return requires an aligned guest instruction";
            else if (strcmp(scenario, "archive-missing-disc") == 0)
                expected = "DVD access requires the verified private image at ux0:data/bluewake/GZLE01.iso";
            else if (strcmp(scenario, "archive-borrowed") == 0)
                expected = "DVD access cannot borrow another owner's disc image";
            else if (strcmp(scenario, "archive-trace") == 0)
                expected = "DVD open trace write failed";
            if (expected != NULL)
                assert(strcmp(host->error, expected) == 0);
        }
        if (archive && !borrowed && strcmp(scenario, "archive-trace") != 0)
            assert(!host->disc_owned && !dvd_image_ready());
        if (rel) {
            assert(memcmp(object_before, cpu->ram + 0x1000u, sizeof(object_before)) == 0);
            if (strcmp(scenario, "rel-frontier") != 0)
                assert(!host->disc_owned && !dvd_image_ready());
            if (strcmp(scenario, "rel-trace") == 0)
                assert(strcmp(host->error, "REL frontier trace write failed") == 0);
        }
        for (u32 index = 0u; index < 0x50u; ++index)
            assert(cpu->ram[0x1FF0u + index] == 0xA5u);
    } else if (observation_edge) {
        assert(host->error == NULL && !host->edge_stopped && cpu->pc == address && cpu->lr == address);
        assert(admission_lines == 1u);
        assert(!ww_host_edge(host, cpu, cpu->pc));
        assert(admission_lines == 1u);
        assert(cpu->pc == cpu_before.pc && cpu->lr == cpu_before.lr && cpu->msr == cpu_before.msr);
        assert(cpu->srr0 == cpu_before.srr0 && cpu->srr1 == cpu_before.srr1);
        assert(memcmp(cpu->gpr, cpu_before.gpr, sizeof(cpu_before.gpr)) == 0);
        assert(memcmp(cpu->gqr, cpu_before.gqr, sizeof(cpu_before.gqr)) == 0);
        assert(memcmp(cpu->fpr, cpu_before.fpr, sizeof(cpu_before.fpr)) == 0);
        assert(cpu->fpscr == cpu_before.fpscr);
        for (u32 index = 0u; index < 0x50u; ++index)
            assert(cpu->ram[0x1FF0u + index] == 0xA5u);
    } else {
        assert(host->error == NULL && !host->edge_stopped && cpu->pc == cpu->lr);
        if (archive) {
            const s32 entry = strcmp(scenario, "archive-absent") == 0 ? -1 : 1;
            assert((s32)cpu->gpr[3] == entry);
            assert(cpu->gpr[4] == (strcmp(scenario, "archive-r4") == 0 ? 0x817FFFF0u : 0x80002000u));
            assert(host->disc_owned);
            for (u32 index = 0u; index < 0x50u; ++index)
                assert(cpu->ram[0x1FF0u + index] == 0xA5u);
        } else {
            assert(cpu->gpr[3] == (strcmp(scenario, "not-found") == 0 ? 0u : 1u));
            if (strcmp(scenario, "not-found") != 0 && strcmp(scenario, "entry") != 0) {
                assert(mem_read32(cpu, 0x8000200Cu) == 0u);
                assert(mem_read32(cpu, 0x80002030u) == 0x500u);
                assert(mem_read32(cpu, 0x80002034u) == 0x30u);
                assert(mem_read32(cpu, 0x80002038u) == 0u);
                assert(mem_read32(cpu, 0x80002000u) == 0xA5A5A5A5u);
            }
        }
    }
    assert(host->cycles.absolute_cycles == 0u && host->gx.fifo.size == 0u);
    ww_host_shutdown(host);
    if (borrowed) {
        assert(dvd_image_ready());
        dvd_close_image();
    }
    assert(!dvd_image_ready());
    free(host);
    return 0;
}
