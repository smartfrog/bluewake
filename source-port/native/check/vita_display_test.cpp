// Host test for the Vita display clear/swap sequence (native/vita/vita_display.cpp).
//
// The Vita path is a CPU clear into a display buffer, sceDisplaySetFrameBuf(NEXTFRAME), then
// sceDisplayWaitVblankStart. No emulator or display is needed here: the test supplies the two
// callbacks and checks the order, the pixels and the failure paths.
#include "vita_display.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int sFailures;

void check(bool ok, const char* what) {
    std::printf("vita-display: %-45s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        sFailures++;
    }
}

struct Context {
    int setCalls;
    int waitCalls;
    int nextStep;
    int setStep;
    int waitStep;
    const void* setPixels;
    uint32_t setWidth;
    uint32_t setHeight;
    bool setResult;
    bool waitResult;
};

bool SetFrameBuffer(void* user, const void* pixels, uint32_t width, uint32_t height) {
    auto* context = static_cast<Context*>(user);
    context->setCalls++;
    context->setStep = context->nextStep++;
    context->setPixels = pixels;
    context->setWidth = width;
    context->setHeight = height;
    return context->setResult;
}

bool WaitVblank(void* user) {
    auto* context = static_cast<Context*>(user);
    context->waitCalls++;
    context->waitStep = context->nextStep++;
    return context->waitResult;
}

} // namespace

int main() {
    using namespace cos_vita;

    uint32_t pixels[6] = {0};
    Context context{};
    context.setResult = true;
    context.waitResult = true;
    const DisplayOps ops{SetFrameBuffer, WaitVblank};
    const DisplayFrame frame{pixels, 3, 2};

    check(clearAndSwap(&context, ops, frame, 0x11223344u), "clear and swap succeeds");
    check(context.setCalls == 1, "one frame-buffer set");
    check(context.waitCalls == 1, "one vblank wait");
    check(context.setStep == 0 && context.waitStep == 1, "set before wait");
    check(context.setPixels == pixels, "the cleared buffer is presented");
    check(context.setWidth == 3 && context.setHeight == 2, "width and height are passed");
    bool filled = true;
    for (uint32_t value : pixels) {
        filled = filled && value == 0x11223344u;
    }
    check(filled, "every pixel has the clear colour");

    Context setFailed{};
    setFailed.setResult = false;
    setFailed.waitResult = true;
    check(!clearAndSwap(&setFailed, ops, frame, 0xFFFFFFFFu), "a failed set is reported");
    check(setFailed.setCalls == 1 && setFailed.waitCalls == 0, "no wait after a failed set");

    Context waitFailed{};
    waitFailed.setResult = true;
    waitFailed.waitResult = false;
    check(!clearAndSwap(&waitFailed, ops, frame, 0xFFFFFFFFu), "a failed wait is reported");
    check(waitFailed.setCalls == 1 && waitFailed.waitCalls == 1, "set and wait both ran");

    Context nullPixels{};
    nullPixels.setResult = true;
    nullPixels.waitResult = true;
    check(!clearAndSwap(&nullPixels, ops, DisplayFrame{nullptr, 3, 2}, 0),
          "a null buffer is rejected");
    check(nullPixels.setCalls == 0 && nullPixels.waitCalls == 0, "no callbacks for a null buffer");

    Context zeroSize{};
    zeroSize.setResult = true;
    zeroSize.waitResult = true;
    check(!clearAndSwap(&zeroSize, ops, DisplayFrame{pixels, 0, 2}, 0),
          "a zero width is rejected");
    check(!clearAndSwap(&zeroSize, ops, DisplayFrame{pixels, 3, 0}, 0),
          "a zero height is rejected");
    check(zeroSize.setCalls == 0 && zeroSize.waitCalls == 0, "no callbacks for a zero size");

    check(!clearAndSwap(&context, DisplayOps{nullptr, WaitVblank}, frame, 0),
          "a missing set callback is rejected");
    check(!clearAndSwap(&context, DisplayOps{SetFrameBuffer, nullptr}, frame, 0),
          "a missing wait callback is rejected");

    if (sFailures != 0) {
        std::printf("vita-display: %d failure(s)\n", sFailures);
        return 1;
    }
    std::printf("vita-display: ok\n");
    return 0;
}
