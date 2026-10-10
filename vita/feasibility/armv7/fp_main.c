#include "fixture.h"
#include "direct_calls.h"
#include "fp_identity.h"
#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <string.h>
#include <sys/stat.h>

typedef struct FpCase {
    const char* id;
    u32 pc;
    u64 a;
    u64 b;
    u64 expected;
    u32 mode;
    u32 gqr;
    u32 width;
    u32 boundary;
} FpCase;

#include "fp_vectors.h"

#ifdef __vita__
#define FP_DIRECTORY "ux0:data/bluewake-ww-fixture/"
#else
#define FP_DIRECTORY ""
#endif

int fixture_dispatch(CPUState* cpu);

typedef struct FpBoundary {
    u32 reject;
    u32 observations;
    u32 mode;
    bool mode_ok;
} FpBoundary;

static bool can_skip(void* user, const CPUState* cpu, u32 address) {
    FpBoundary* probe = user;
    probe->observations++;
    probe->mode_ok = probe->mode_ok && (cpu->fpscr & 7u) == probe->mode;
    return address != probe->reject;
}

static int run_case(const FpCase* test, u64* actual, int* path) {
    fenv_t saved;
    CPUState cpu;
    *actual = 0;
    *path = 0;
    if (fegetenv(&saved) != 0 || !cpu_init(&cpu))
        return 1;
    static const bool clear = false;
    static const u32 zero = 0;
    FpBoundary probe = {.reject = test->boundary ? 0x80007108u : 0,
                        .mode = test->mode, .mode_ok = true};
    bool ready = bluewake_composite_direct_calls_v2(true, &clear, &clear, &zero, &zero,
                                                   can_skip, &probe) &&
                 bluewake_composite_edge_filter(true);
    cpu.pc = test->pc;
    cpu.lr = 0x81234564u;
    cpu.gpr[4] = 0x80001000u;
    cpu.gpr[12] = 0x80007000u;
    cpu.cycle_budget = cpu.cycle_deadline_budget = 1000000;
    cpu.msr = PPC_MSR_FP;
    cpu.hid2 = PPC_HID2_PSE | PPC_HID2_LSQE;
    cpu.gqr[0] = test->gqr;
    memcpy(&cpu.fpr[2], &test->a, sizeof test->a);
    memcpy(&cpu.fpr[3], &test->b, sizeof test->b);
    cpu.fpscr = test->mode;
    ppc_fpscr_control_updated(&cpu);
    if (ready && fixture_dispatch(&cpu)) {
        *path = cpu.pc == (test->boundary ? probe.reject : 0x81234564u) &&
                bw_direct_depth == 0 && probe.mode_ok &&
                (test->pc < 0x80007100u || probe.observations == 2);
        if (test->width == 1)
            *actual = mem_read8(&cpu, cpu.gpr[4]);
        else if (test->width == 2)
            *actual = mem_read16(&cpu, cpu.gpr[4]);
        else
            memcpy(actual, &cpu.fpr[1], sizeof *actual);
    }
    bluewake_composite_direct_calls_v2(false, NULL, NULL, NULL, NULL, NULL, NULL);
    cpu.fpscr = 0;
    ppc_fpscr_control_updated(&cpu);
    cpu_free(&cpu);
    if (fesetenv(&saved) != 0)
        *path = 0;
    return !*path || *actual != test->expected;
}

static bool hex_string(const char* text, size_t length) {
    if (strlen(text) != length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!isxdigit((unsigned char)text[i]))
            return false;
    return true;
}

static int read_request(char* token, char* source, char* package) {
    FILE* request = fopen(FP_DIRECTORY "fp.request", "r");
    if (!request)
        return 1;
    int fields = fscanf(request, "%32s %40s %64s", token, source, package);
    char extra;
    int trailing = fscanf(request, " %c", &extra);
    int error = ferror(request);
    int closed = fclose(request);
    return fields != 3 || trailing != EOF || error || closed != 0 ||
           !hex_string(token, 32) || !hex_string(source, 40) || !hex_string(package, 64) ||
           strcmp(source, WW_FP_SOURCE) != 0;
}

static int absent(const char* path) {
    struct stat info;
    return stat(path, &info) < 0 && errno == ENOENT;
}

int fixture_run(int argc, char** argv) {
    (void)argv;
    if (argc > 1)
        return 1;
    char token[33], source[41], package[65];
    const char* partial = FP_DIRECTORY "fp.partial";
    const char* complete = FP_DIRECTORY "fp.report";
    if (read_request(token, source, package) || !absent(partial) || !absent(complete))
        return 1;
    if (fixture_log_open("fp"))
        return 1;
    FILE* report = fopen(partial, "w");
    if (!report)
        return 1;
    int error = fprintf(report, "RUN %s %s %s\n", token, source, package) < 0;
    unsigned failures = 0;
    unsigned count = sizeof fp_cases / sizeof fp_cases[0];
    for (unsigned i = 0; i < count; ++i) {
        u64 actual;
        int path;
        int failure = run_case(&fp_cases[i], &actual, &path);
        failures += failure;
        if (fprintf(report, "CASE %s mode=%u expected=%016llx actual=%016llx path=%d %s\n",
                    fp_cases[i].id, fp_cases[i].mode, (unsigned long long)fp_cases[i].expected,
                    (unsigned long long)actual, path, failure ? "FAIL" : "PASS") < 0)
            error = 1;
    }
    int log_error = fixture_log_close();
    if (error || log_error) {
        int closed = fclose(report);
        if (closed != 0)
            return 1;
        return 1;
    }
    if (fprintf(report, "END %s cases=%u failures=%u\n", failures ? "FAIL" : "PASS",
                count, failures) < 0) {
        int closed = fclose(report);
        if (closed != 0)
            return 1;
        return 1;
    }
    int publication_error = fixture_publish_report(report, partial, complete);
    return publication_error || failures != 0;
}
