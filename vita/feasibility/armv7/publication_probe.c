#define _POSIX_C_SOURCE 200809L
#include "diagnostic_io.h"
#include <assert.h>
#include <unistd.h>

static const char token[] = "cccccccccccccccccccccccccccccccc";

int main(void) {
    FILE* stream = diagnostic_open_candidate("payload.partial");
    assert(stream != NULL);
    assert(fwrite("abc", 1u, 3u, stream) == 3u);
    const int result = diagnostic_publish(stream, "payload.partial", "payload", token);
    assert(result == 0);
    assert(access("payload", F_OK) != 0);
    assert(access("payload.partial", F_OK) == 0);
    return 0;
}
