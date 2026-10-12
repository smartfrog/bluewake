#include "ww_renderer.h"
#include "ww_renderer_backend.h"

#include <cstdlib>
#include <cstring>

class IoRendererBackend final : public WwRendererBackend {
public:
    const char* init() override {
        const char* failure = std::getenv("WW05_IO_FAILURE");
        return failure != nullptr && std::strcmp(failure, "renderer-init") == 0
            ? "synthetic renderer init failure" : nullptr;
    }

    const char* submit_draw(const gx_vita::TranslatedDraw&, const gx_vita::CgProgram&) override {
        return "synthetic renderer has no GPU";
    }

    const char* submit_copy(const gx_vita::TranslatedCopy&) override {
        return "synthetic renderer has no GPU";
    }

    const char* present() override { return "synthetic renderer has no display"; }
    void shutdown() override {}
};

extern "C" WwRendererBackend* ww05_io_renderer_backend(void) {
    static IoRendererBackend backend;
    return &backend;
}
