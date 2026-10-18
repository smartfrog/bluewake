#include "vita_display.h"

#include <algorithm>
#include <cstddef>

namespace cos_vita {

bool clearAndSwap(void* user, const DisplayOps& ops, const DisplayFrame& frame, uint32_t color) {
    if (frame.pixels == nullptr || frame.width == 0 || frame.height == 0 ||
        ops.set_frame_buffer == nullptr || ops.wait_vblank == nullptr) {
        return false;
    }

    std::fill_n(frame.pixels, static_cast<std::size_t>(frame.width) * frame.height, color);
    if (!ops.set_frame_buffer(user, frame.pixels, frame.width, frame.height)) {
        return false;
    }
    return ops.wait_vblank(user);
}

} // namespace cos_vita
