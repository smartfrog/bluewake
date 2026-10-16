#include "probe.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int corrupt = -2;
static int control_status;
static bool record_failure;

int probe_control(void) { return control_status; }

bool probe_record(const char *text, size_t bytes) {
    if (record_failure)
        return false;
    return fwrite(text, 1, bytes, stdout) == bytes;
}

void *__wrap_memcpy(void *destination, const void *source, size_t bytes) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    if (bytes != PROBE_BYTES || destination == source ||
        (uintptr_t)destination % 16 || (uintptr_t)source % 16)
        exit(90);
    for (size_t i = 0; i < bytes; ++i)
        out[i] = in[i];
    if (corrupt != -2)
        out[corrupt] ^= 1;
    return destination;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "control-fail") == 0)
        control_status = -1;
    if (argc > 1 && strcmp(argv[1], "record-fail") == 0)
        record_failure = true;
    if (argc == 3 && strcmp(argv[1], "corrupt") == 0)
        corrupt = atoi(argv[2]);
    return probe_run();
}
