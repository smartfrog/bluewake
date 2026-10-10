#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned stdout_flushes;
static unsigned stderr_flushes;

static int observed_fflush(FILE* stream) {
    if (stream == stdout)
        stdout_flushes++;
    if (stream == stderr)
        stderr_flushes++;
    return fflush(stream);
}

#define main fixture_platform_main
#define fflush observed_fflush
#include "platform.c"
#undef fflush
#undef main

/* Vita stat fields must precede glibc's stat field macros. */
#include <fcntl.h>

static int memory_query_result;

int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo* info) {
    info->size_user = info->size_cdram = info->size_phycont = 0;
    return memory_query_result;
}

static int check_close(FILE* proof, int expect_failure) {
    int status = fixture_log_close();
    fprintf(proof, "CLOSE status=%d stdout_error=%d stderr_error=%d "
                   "stdout_flushes=%u stderr_flushes=%u\n",
            status, ferror(stdout), ferror(stderr), stdout_flushes, stderr_flushes);
    return (status != 0) != expect_failure || stdout_flushes != 1 || stderr_flushes != 1;
}

int main(int argc, char** argv) {
    if (argc == 4 && (strcmp(argv[1], "publish-close") == 0 ||
                      strcmp(argv[1], "publish-healthy") == 0)) {
        FILE* report = fopen(argv[2], "a");
        if (!report)
            return 2;
        bool failure = strcmp(argv[1], "publish-close") == 0;
        if (failure && close(fileno(report)) != 0)
            return 2;
        int status = fixture_publish_report(report, argv[2], argv[3]);
        fprintf(stderr, "PUBLISH %s status=%d\n", argv[1], status);
        return (status != 0) != failure;
    }
    if (argc == 4 && strcmp(argv[1], "publish-write") == 0) {
        FILE* report = fopen("/dev/full", "w");
        if (!report || setvbuf(report, NULL, _IONBF, 0) != 0)
            return 2;
        if (fprintf(report, "lost case\n") >= 0 || !ferror(report))
            return 2;
        int recovered = open("/dev/null", O_WRONLY);
        if (recovered < 0 || dup2(recovered, fileno(report)) < 0 || close(recovered) != 0)
            return 2;
        int status = fixture_publish_report(report, argv[2], argv[3]);
        fprintf(stderr, "PUBLISH failed-write status=%d\n", status);
        return status == 0;
    }
    if (argc != 2)
        return 2;
    if (strcmp(argv[1], "healthy") == 0)
        return check_close(stderr, 0);
    if (strcmp(argv[1], "query-error") == 0) {
        memory_query_result = -1;
        return check_close(stderr, 1);
    }
    if (strcmp(argv[1], "flush-both") == 0) {
        static char buffer[4096];
        if (freopen("/dev/full", "w", stdout) == NULL ||
            setvbuf(stdout, buffer, _IOFBF, sizeof buffer) != 0)
            return 2;
        int result = check_close(stderr, 1);
        return result || !ferror(stdout);
    }
    if (strcmp(argv[1], "snapshot") == 0) {
        if (freopen("/dev/full", "w", stdout) == NULL || setvbuf(stdout, NULL, _IONBF, 0) != 0)
            return 2;
        errno = 0;
        int status = print_free_memory();
        int write_errno = errno;
        int error = ferror(stdout);
        fprintf(stderr, "SNAPSHOT errno=%d error=%d status=%d\n", write_errno, error, status);
        return status == 0 || write_errno != ENOSPC || !error;
    }
    if (strcmp(argv[1], "stdout") != 0 && strcmp(argv[1], "stderr") != 0)
        return 2;
    FILE* stream = strcmp(argv[1], "stdout") == 0 ? stdout : stderr;
    FILE* proof = stream == stdout ? stderr : stdout;
    if (freopen("/dev/full", "w", stream) == NULL || setvbuf(stream, NULL, _IONBF, 0) != 0)
        return 2;
    errno = 0;
    int written = fprintf(stream, "lost diagnostic\n");
    int write_errno = errno;
    int error_before = ferror(stream);
    if (written >= 0 || write_errno != ENOSPC || !error_before)
        return 2;

    int recovered = open("/dev/null", O_WRONLY);
    if (recovered < 0)
        return 2;
    int redirected = dup2(recovered, fileno(stream));
    int closed = close(recovered);
    if (redirected < 0 || closed != 0)
        return 2;
    int flushed = fflush(stream);
    if (flushed != 0 || !ferror(stream))
        return 2;

    fprintf(proof, "FAILED-WRITE stream=%s write=%d errno=%d error_before=%d fflush=%d "
                   "error_after_flush=%d\n",
            argv[1], written, write_errno, error_before, flushed, ferror(stream));
    int result = check_close(proof, 1);
    return result || !ferror(stream);
}
