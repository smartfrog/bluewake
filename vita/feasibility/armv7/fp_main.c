#include "fp_runtime.h"
#include "fp_identity.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "fp_vectors.h"

#ifdef __vita__
#define FP_DIRECTORY "ux0:data/bluewake-ww-fixture/"
#else
#define FP_DIRECTORY ""
#endif

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
        int failure = fixture_fp_case(&fp_cases[i], &actual, &path, NULL, NULL);
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
