extern "C" {
#include "boot_host.h"
}
#include <cassert>

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

int main() {
    assert(ww_host_enable_frame_capture(nullptr) != nullptr);
    assert(ww_host_capture_mem1(nullptr) != nullptr);
    return 0;
}
