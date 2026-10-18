#include "vita_display.h"
#include "vita_display_vita.h"

#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kDisplayWidth = 960;
constexpr uint32_t kDisplayHeight = 544;
constexpr uint32_t kFrameBytes = kDisplayWidth * kDisplayHeight * sizeof(uint32_t);
constexpr uint32_t kAllocBytes = (kFrameBytes + 0x3FFFFu) & ~0x3FFFFu;
constexpr uint32_t kHoldVblanks = 30;

bool SetFrameBuffer(void* user, const void* pixels, uint32_t width, uint32_t height) {
    (void)user;
    SceDisplayFrameBuf frame{};
    frame.size = sizeof(frame);
    frame.base = const_cast<void*>(pixels);
    frame.pitch = width;
    frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    frame.width = width;
    frame.height = height;
    const int result = sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (result != 0) {
        std::fprintf(stderr, "[cos] vita-boot: sceDisplaySetFrameBuf failed 0x%08x\n",
                     static_cast<unsigned int>(result));
        return false;
    }
    return true;
}

bool WaitVblank(void* user) {
    (void)user;
    const int result = sceDisplayWaitVblankStart();
    if (result != 0) {
        std::fprintf(stderr, "[cos] vita-boot: sceDisplayWaitVblankStart failed 0x%08x\n",
                     static_cast<unsigned int>(result));
        return false;
    }
    return true;
}

bool AllocFrame(void*& base, SceUID& uid) {
    uid = sceKernelAllocMemBlock("bw_pa_frame", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                 kAllocBytes, nullptr);
    if (uid < 0) {
        std::fprintf(stderr, "[cos] vita-boot: frame allocation failed 0x%08x\n",
                     static_cast<unsigned int>(uid));
        return false;
    }
    const int result = sceKernelGetMemBlockBase(uid, &base);
    if (result != 0) {
        std::fprintf(stderr, "[cos] vita-boot: frame base failed 0x%08x\n",
                     static_cast<unsigned int>(result));
        return false;
    }
    return true;
}

} // namespace

bool vitaDisplayBringUp() {
    void* first = nullptr;
    void* second = nullptr;
    SceUID firstUid = -1;
    SceUID secondUid = -1;
    if (!AllocFrame(first, firstUid) || !AllocFrame(second, secondUid)) {
        return false;
    }

    const cos_vita::DisplayOps ops{SetFrameBuffer, WaitVblank};
    if (!cos_vita::clearAndSwap(nullptr, ops,
                                 cos_vita::DisplayFrame{static_cast<uint32_t*>(first),
                                                         kDisplayWidth, kDisplayHeight},
                                 0xFF102030u)) {
        return false;
    }
    if (!cos_vita::clearAndSwap(nullptr, ops,
                                 cos_vita::DisplayFrame{static_cast<uint32_t*>(second),
                                                         kDisplayWidth, kDisplayHeight},
                                 0xFF20C040u)) {
        return false;
    }
    // Keep the second frame on screen long enough to see or capture before M1 exits the app.
    for (uint32_t i = 0; i < kHoldVblanks; i++) {
        if (!WaitVblank(nullptr)) {
            return false;
        }
    }
    return true;
}
