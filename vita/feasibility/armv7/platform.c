#include "fixture.h"
#include <stdio.h>
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

int fixture_log_open(const char* name) {
#ifdef __vita__
    int result = sceIoMkdir("ux0:data/bluewake-ww-fixture", 0777);
    if (result < 0) {
        SceIoStat status;
        result = sceIoGetstat("ux0:data/bluewake-ww-fixture", &status);
        if (result < 0 || !SCE_S_ISDIR(status.st_mode))
            return 1;
    }
    char path[128];
    if (snprintf(path, sizeof path, "ux0:data/bluewake-ww-fixture/%s.log", name) >= (int)sizeof path)
        return 1;
    FILE* reset = fopen(path, "w");
    if (reset == NULL)
        return 1;
    if (fclose(reset) != 0 || freopen(path, "a", stdout) == NULL || freopen(path, "a", stderr) == NULL)
        return 1;
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
        return 1;
    return print_free_memory();
#else
    (void)name;
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
    return result || stdout_error || stderr_error;
#else
    return 0;
#endif
}

int fixture_publish_report(FILE* report, const char* partial, const char* complete) {
    int error = flush_checked(report);
    int closed = fclose(report);
    return error || closed != 0 || rename(partial, complete) != 0;
}
