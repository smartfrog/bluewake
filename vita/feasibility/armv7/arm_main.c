#define _POSIX_C_SOURCE 200809L
#include "fp_runtime.h"
#include "arm_control.h"
#include "arm_identity.h"
#include "diagnostic_io.h"

typedef struct ArmCase {
    const char* id;
    const char* operation_name;
    unsigned operation;
    unsigned rn;
    unsigned fz;
    uint64_t a;
    uint64_t b;
    uint64_t expected;
} ArmCase;

#include "arm_vectors.h"
#include "fp_vectors.h"

#ifdef __vita__
#define ARM_DIRECTORY "ux0:data/bluewake-ww-fixture/"
#define ARM_PLATFORM "VITA"
#else
#define ARM_DIRECTORY ""
#define ARM_PLATFORM "HOST"
#endif

int fixture_run(int argc, char** argv) {
    (void)argv;
    ArmFpEnvironment environment = arm_fp_environment();
    FILE* report = NULL;
    int error = 1;
    bool restore_ok = true;
    unsigned control_failures = 0, runtime_failures = 0;
    const unsigned control_count = sizeof arm_cases / sizeof arm_cases[0];
    const unsigned runtime_count = sizeof fp_cases / sizeof fp_cases[0];
    char token[33], source[41], package[65];
    const char* partial = ARM_DIRECTORY "arm.partial";
    const char* complete = ARM_DIRECTORY "arm.report";
    if (argc > 1 || diagnostic_request(ARM_DIRECTORY "arm.request", WW_ARM_SOURCE, token, source, package) ||
        !diagnostic_absent(partial) || !diagnostic_absent(complete))
        goto cleanup;
    if (fixture_log_open("arm", token))
        goto cleanup;
    report = diagnostic_open_candidate(partial);
    if (!report)
        goto cleanup;
    error = fprintf(report, "ARM08 %s %s %s method=scalar-vfp-v1 phase=guest platform=%s\n",
                    token, source, package, ARM_PLATFORM) < 0;
    error |= fprintf(report, "ENV available=%d initial=%08x\n", environment.available, environment.initial) < 0;
    for (unsigned i = 0; i < control_count && !error; ++i) {
        const ArmCase* test = &arm_cases[i];
        uint32_t requested = 0, readback = 0, after = 0;
        uint64_t actual = 0;
        bool failure = false;
        if (environment.available) {
            requested = arm_fp_mode(environment.initial, test->rn, test->fz);
            arm_vfp_write(requested);
            readback = arm_vfp_read();
            actual = arm_vfp_operation(test->operation, test->a, test->b);
            after = arm_vfp_read();
            bool restored = arm_fp_restore(&environment);
            restore_ok = restore_ok && restored;
            failure = readback != requested || (after & ARM_FP_MODE_MASK) != (requested & ARM_FP_MODE_MASK) ||
                      actual != test->expected || !restored;
        }
        control_failures += failure;
        error = fprintf(report, "CONTROL %s op=%s rn=%u fz=%u a=%016llx b=%016llx expected=%016llx "
                        "requested=%08x readback=%08x after=%08x actual=%016llx restored=%08x %s\n",
                        test->id, test->operation_name, test->rn, test->fz,
                        (unsigned long long)test->a, (unsigned long long)test->b,
                        (unsigned long long)test->expected, requested, readback, after,
                        (unsigned long long)actual, environment.restored,
                        !environment.available ? "SKIP" : failure ? "FAIL" : "PASS") < 0;
    }
    for (unsigned i = 0; i < runtime_count && !error; ++i) {
        ArmFpRuntime observation = {.environment = &environment, .restore_ok = true};
        u64 actual;
        int path;
        int failure = fixture_fp_case(&fp_cases[i], &actual, &path, arm_fp_observe_runtime, &observation);
        bool restored = arm_fp_restore_runtime(&observation);
        restore_ok = restore_ok && restored;
        failure = failure || !restored;
        runtime_failures += failure;
        error = fprintf(report, "RUNTIME %s mode=%u expected=%016llx actual=%016llx path=%d "
                        "before=%08x updated=%08x operated=%08x restored=%08x restore=%d %s\n",
                        fp_cases[i].id, fp_cases[i].mode, (unsigned long long)fp_cases[i].expected,
                        (unsigned long long)actual, path, observation.before, observation.updated,
                        observation.operated, observation.restored, restored, failure ? "FAIL" : "PASS") < 0;
    }
    error |= fixture_log_close();
    bool final_restored = arm_fp_restore(&environment);
    restore_ok = restore_ok && final_restored;
    if (!error)
        error = fprintf(report, "RESTORE final=%08x %s\n", environment.restored, final_restored ? "PASS" : "FAIL") < 0;
    if (!error)
        error = fprintf(report, "END controls=%s runtime=%s restore=%s control_cases=%u runtime_cases=%u "
                        "control_failures=%u runtime_failures=%u\n",
                        !environment.available ? "SKIP" : control_failures ? "FAIL" : "PASS",
                        runtime_failures ? "FAIL" : "PASS", restore_ok ? "PASS" : "FAIL",
                        control_count, runtime_count, control_failures, runtime_failures) < 0;
    if (!error) {
        error = fixture_publish_report(report, partial, complete, token);
        report = NULL;
    }
cleanup:
    if (report && fclose(report) != 0)
        error = 1;
    bool restored = arm_fp_restore(&environment);
    return error || control_failures || runtime_failures || !restore_ok || !restored;
}
