extern "C" {
#include "probe_support.h"
}
#include "ww_renderer_backend.h"
#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdlib>
#include <cstring>

class RecordingDrainBackend final : public WwRendererBackend {
public:
    const char* init() override { return nullptr; }
    const char* submit_draw(const gx_vita::TranslatedDraw&, const gx_vita::CgProgram&) override {
        if (fail_draw)
            return "synthetic drain draw failed";
        ++draws;
        return nullptr;
    }
    const char* submit_copy(const gx_vita::TranslatedCopy&) override {
        return "unexpected synthetic drain texture copy";
    }
    const char* present() override {
        if (fail_present)
            return "synthetic drain present failed";
        ++presents;
        return nullptr;
    }
    void shutdown() override {}
    u32 draws = 0u;
    u32 presents = 0u;
    bool fail_draw = false;
    bool fail_present = false;
};

static const char* feed(WwHost* host, const u8* bytes, u32 size) {
    for (u32 index = 0u; index < size; ++index) {
        const WwFifoOrigin origin = {.pc = 0x80001000u + index * 4u, .sp = 0x817FFF00u, .byte = bytes[index]};
        const char* error = ww_gx_stream_write(host, bytes[index], &origin, 0u);
        if (error != nullptr)
            return error;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    WwHost* host = static_cast<WwHost*>(std::calloc(1u, sizeof(*host)));
    assert(host != nullptr && ww_host_init(host));
    gx_fixture::FixtureContext fixture;
    gx_fixture::fill_texture_zones(fixture);
    std::memcpy(host->cpu.ram, fixture.mem1.data(), fixture.mem1.size());
    RecordingDrainBackend backend;
    backend.fail_draw = std::strcmp(scenario, "draw-failure") == 0;
    backend.fail_present = std::strcmp(scenario, "present-failure") == 0;
    if (std::strcmp(scenario, "no-renderer") != 0) {
        const WwRendererConfig config = {.memory = &host->memory, .cpu = &host->cpu};
        assert(ww_renderer_create(&host->renderer, &config, &backend) == nullptr);
    }
    host->stream.enabled = true;
    host->stream.log_limit = 64u;
    const auto fifo = gx_fixture::build_textured_triangle();
    const char* error = feed(host, fifo.data(), fifo.size());
    if (backend.fail_draw) {
        assert(error != nullptr && std::strstr(error, "synthetic drain draw failed") != nullptr);
        assert(host->gx.trace_count != 0u && host->gx.fifo.size != 0u && backend.draws == 0u);
    } else if (std::strcmp(scenario, "no-renderer") == 0) {
        assert(error == nullptr && host->gx.trace_count != 0u && host->gx.fifo.size == fifo.size());
    } else {
        assert(error == nullptr && backend.draws == 1u);
        const WwGxCheckpoint frontier = host->frontier;
        DolGxRecompState* state = static_cast<DolGxRecompState*>(std::malloc(sizeof(*state)));
        assert(state != nullptr);
        std::memcpy(state, &host->gx, sizeof(*state));
        if (std::strcmp(scenario, "long-stream") == 0) {
            const u8 nop = 0u;
            for (u32 index = 0u; index < 300000u; ++index)
                assert(feed(host, &nop, 1u) == nullptr);
            WwRendererStats stats{};
            ww_renderer_stats(host->renderer, &stats);
            assert(stats.fifo_bytes == fifo.size() + 300000u && stats.events > DOL_GX_RECOMP_MAX_TRACE_EVENTS);
            assert(host->stream.logged == 64u && host->gx.trace_count == 0u && host->gx.fifo.size == 0u);
        }
        if (backend.fail_present) {
            const u8 display_copy[] = {0x61u, 0x52u, 0u, 0x40u, 0u};
            error = feed(host, display_copy, sizeof(display_copy));
            assert(error != nullptr && std::strstr(error, "synthetic drain present failed") != nullptr);
            assert(host->gx.trace_count != 0u && host->gx.fifo.size == sizeof(display_copy) && backend.presents == 0u);
        } else if (std::strcmp(scenario, "partial") == 0) {
            const u8 cp_prefix = 0x08u;
            assert(feed(host, &cp_prefix, 1u) == nullptr);
            assert(host->stream.command.size == 1u && !host->stream.command.complete);
            assert(host->gx.trace_count == 0u && host->gx.fifo.size == 0u && backend.draws == 1u);
        } else {
            std::vector<u8> draw;
            gx_fixture::push_triangle(draw);
            assert(feed(host, draw.data(), draw.size()) == nullptr && backend.draws == 2u);
            const size_t start = offsetof(DolGxRecompState, arrays);
            const size_t end = offsetof(DolGxRecompState, trace);
            assert(std::memcmp(reinterpret_cast<const u8*>(state) + start,
                               reinterpret_cast<const u8*>(&host->gx) + start, end - start) == 0);
            assert(std::memcmp(&state->resolver, &host->gx.resolver, sizeof(state->resolver)) == 0);
            assert(host->gx.trace_count == 0u && host->gx.fifo.size == 0u && host->stream.command.size == 0u);
        }
        assert(std::memcmp(&frontier, &host->frontier, sizeof(frontier)) == 0);
        std::free(state);
    }
    assert(host->cycles.absolute_cycles == 0u && host->cpu.pc == 0u && backend.presents == 0u);
    ww_host_shutdown(host);
    std::free(host);
    return 0;
}
