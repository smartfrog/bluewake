// The portable part of the Vita display clear/swap: fill a 32-bit buffer, hand it to the display,
// wait one vblank. The Vita glue (vita_display_vita.cpp) supplies sceDisplaySetFrameBuf and
// sceDisplayWaitVblankStart; the host test supplies fakes.
#ifndef COS_VITA_DISPLAY_H
#define COS_VITA_DISPLAY_H

#include <cstdint>

namespace cos_vita {

struct DisplayOps {
    // Return true when the display accepted the buffer.
    bool (*set_frame_buffer)(void* user, const void* pixels, uint32_t width, uint32_t height);
    // Return true when one vblank elapsed.
    bool (*wait_vblank)(void* user);
};

struct DisplayFrame {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
};

// Clears the frame to `color` and presents it for one vblank. `pixels` is width * height words,
// with no padding (pitch == width).
bool clearAndSwap(void* user, const DisplayOps& ops, const DisplayFrame& frame, uint32_t color);

} // namespace cos_vita

#endif
