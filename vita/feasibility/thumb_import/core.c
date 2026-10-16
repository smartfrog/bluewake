#include "probe.h"

typedef struct {
    unsigned char before[16];
    unsigned char data[PROBE_BYTES];
    unsigned char after[16];
} ProbeBuffer;

static _Alignas(16) ProbeBuffer source;
static _Alignas(16) ProbeBuffer destination;

static unsigned char pattern(size_t index) {
    return (unsigned char)(index * 37u + 19u);
}

#define RECORD(text) do { if (!probe_record(text "\n", sizeof(text "\n") - 1)) return 4; } while (0)

int probe_run(void) {
    RECORD("control-enter");
    if (probe_control() < 0) {
        RECORD("control-fail");
        return 2;
    }
    RECORD("control-pass");
    volatile unsigned char *in = (volatile unsigned char *)&source;
    volatile unsigned char *out = (volatile unsigned char *)&destination;
    for (size_t i = 0; i < sizeof(source); ++i) {
        in[i] = 0xa5;
        out[i] = 0x5a;
    }
    for (size_t i = 0; i < PROBE_BYTES; ++i) {
        source.data[i] = pattern(i);
        destination.data[i] = (unsigned char)~pattern(i);
    }
    RECORD("copy-enter");
    probe_copy(destination.data, source.data);
    RECORD("copy-leave");
    bool valid = true;
    for (size_t i = 0; i < PROBE_BYTES; ++i)
        if (destination.data[i] != pattern(i) || source.data[i] != pattern(i))
            valid = false;
    for (size_t i = 0; i < sizeof(source.before); ++i)
        if (source.before[i] != 0xa5 || source.after[i] != 0xa5 ||
            destination.before[i] != 0x5a || destination.after[i] != 0x5a)
            valid = false;
    if (!valid) {
        RECORD("status-fail");
        return 3;
    }
    RECORD("status-pass");
    return 0;
}
