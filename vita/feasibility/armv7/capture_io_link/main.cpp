extern "C" {
#include "boot_host.h"
#include "diagnostic_io.h"
}
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdio>

extern "C" const char* ww_host_wall_us(u64* us) {
    *us = 1u;
    return nullptr;
}

extern "C" void ww_renderer_stats(WwRenderer*, WwRendererStats* stats) {
    *stats = {};
}

extern "C" const char* ww_renderer_set_frame_capture(WwRenderer*, WwRendererFrameCapture, void*) {
    return nullptr;
}

extern "C" int fixture_run(int, char**) { return 1; }

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--collisions") == 0) {
        for (int index = 2; index < 4; ++index) {
            FILE* stream = diagnostic_open_candidate(argv[index]);
            const int cause = errno;
            if (stream != nullptr) {
                const int closed = fclose(stream);
                return closed != 0 ? 2 : 1;
            }
            if (cause != EEXIST && cause != ELOOP)
                return 1;
            if (std::printf("COLLISION path=%s errno=%d refused=1\n", argv[index], cause) < 0)
                return 1;
        }
        return 0;
    }
    if (argc != 1)
        return 1;
    assert(ww_host_enable_frame_capture(nullptr) != nullptr);
    assert(ww_host_capture_mem1(nullptr) != nullptr);
    return 0;
}
