#define _POSIX_C_SOURCE 200809L
#include "fixture.h"
#include "diagnostic_io.h"
#include <stdio.h>
#ifdef __vita__
static FILE* fixture_log;
static FILE* fixture_stdout;
static FILE* fixture_stderr;
static char fixture_log_path[128];
static char fixture_log_token[33];
#endif
#ifdef __vita__
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr/thread.h>

int _newlib_heap_size_user = 96 * 1024 * 1024;

static int print_free_memory(void) {
    SceKernelFreeMemorySizeInfo info = {.size = sizeof info};
    int result = sceKernelGetFreeMemorySize(&info);
    if (result < 0) {
        fprintf(stderr, "sceKernelGetFreeMemorySize failed: %08X\n", (unsigned)result);
        return 1;
    }
    return printf("FREE user=%d cdram=%d phycont=%d (current process, not nominal RAM)\n",
                  info.size_user, info.size_cdram, info.size_phycont) < 0;
}
#endif

int fixture_log_open(const char* name, const char* token) {
#ifdef __vita__
    if (fixture_log != NULL || !diagnostic_hex(token, 32u))
        return 1;
    int result = sceIoMkdir("ux0:data/bluewake-ww-fixture", 0777);
    if (result < 0) {
        SceIoStat status;
        result = sceIoGetstat("ux0:data/bluewake-ww-fixture", &status);
        if (result < 0 || !SCE_S_ISDIR(status.st_mode))
            return 1;
    }
    if (snprintf(fixture_log_path, sizeof fixture_log_path, "ux0:data/bluewake-ww-fixture/%s.log", name) >= (int)sizeof fixture_log_path)
        return 1;
    fixture_log = diagnostic_open_candidate(fixture_log_path);
    if (fixture_log == NULL)
        return 1;
    memcpy(fixture_log_token, token, sizeof(fixture_log_token));
    if (fflush(stdout) != 0 || fflush(stderr) != 0 || setvbuf(fixture_log, NULL, _IONBF, 0) != 0) {
        const int closed = fclose(fixture_log);
        fixture_log = NULL;
        return closed != 0 ? 2 : 1;
    }
    fixture_stdout = stdout;
    fixture_stderr = stderr;
    stdout = fixture_log;
    stderr = fixture_log;
    const int initialized = print_free_memory();
    if (initialized != 0) {
        stdout = fixture_stdout;
        stderr = fixture_stderr;
        const int closed = fclose(fixture_log);
        fixture_log = NULL;
        return closed != 0 ? 2 : initialized;
    }
    return 0;
#else
    (void)name;
    (void)token;
    return 0;
#endif
}

int fixture_run(int argc, char** argv);

#ifdef __vita__
typedef struct FixtureArguments {
    int argc;
    char** argv;
} FixtureArguments;

static int worker(SceSize size, void* data) {
    if (size != sizeof(FixtureArguments))
        return 1;
    FixtureArguments* args = data;
    return fixture_run(args->argc, args->argv);
}
#endif

int main(int argc, char** argv) {
#ifdef __vita__
    FixtureArguments args = {argc, argv};
    SceUID thread = sceKernelCreateThread("ww-fixture", worker, 0x10000100, 256 * 1024, 0, 0, NULL);
    if (thread < 0) {
        fprintf(stderr, "sceKernelCreateThread failed: %08X\n", (unsigned)thread);
        return 1;
    }
    int status = 1;
    int result = sceKernelStartThread(thread, sizeof args, &args);
    if (result >= 0)
        result = sceKernelWaitThreadEnd(thread, &status, NULL);
    if (result < 0) {
        fprintf(stderr, "fixture thread start/wait failed: %08X\n", (unsigned)result);
        return 1;
    }
    result = sceKernelDeleteThread(thread);
    if (result < 0) {
        fprintf(stderr, "sceKernelDeleteThread failed: %08X\n", (unsigned)result);
        return 1;
    }
    return status;
#else
    return fixture_run(argc, argv);
#endif
}

static int flush_checked(FILE* stream) {
    int flushed = fflush(stream);
    int error = ferror(stream);
    return flushed != 0 || error;
}

int fixture_log_close(void) {
#ifdef __vita__
    int result = print_free_memory();
    int stdout_error = flush_checked(stdout);
    int stderr_error = flush_checked(stderr);
    int published = 0;
    if (fixture_log != NULL) {
        stdout = fixture_stdout;
        stderr = fixture_stderr;
        published = result || stdout_error || stderr_error ? fclose(fixture_log) :
            diagnostic_publish(fixture_log, fixture_log_path, fixture_log_path, fixture_log_token);
        fixture_log = NULL;
    }
    return result || stdout_error || stderr_error || published;
#else
    return 0;
#endif
}

int fixture_publish_report(FILE* report, const char* partial, const char* complete, const char* token) {
    return diagnostic_publish(report, partial, complete, token);
}
