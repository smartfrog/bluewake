extern "C" {
#include "probe_support.h"
}
#include "ww_renderer_backend.h"
#include "../feasibility/gx/gx_fixture.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <cerrno>

static const char* marker_scenario;
static u64 marker_us = 1000000u;
static const char* capture_scenario;

extern "C" size_t __real_fwrite(const void* data, size_t size, size_t count, FILE* stream);
extern "C" int __real_fflush(FILE* stream);
extern "C" int __real_fclose(FILE* stream);
extern "C" int __real_rename(const char* source, const char* destination);

extern "C" size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    if (stream != stdout && stream != stderr && capture_scenario != nullptr &&
        (std::strcmp(capture_scenario, "capture-write-failure") == 0 ||
         std::strcmp(capture_scenario, "image-png-write-failure") == 0)) {
        errno = ENOSPC;
        return 0u;
    }
    return __real_fwrite(data, size, count, stream);
}

extern "C" int __wrap_fflush(FILE* stream) {
    if (capture_scenario != nullptr && std::strcmp(capture_scenario, "capture-flush-failure") == 0 &&
        stream != stdout && stream != stderr) {
        errno = EIO;
        return EOF;
    }
    return __real_fflush(stream);
}

extern "C" int __wrap_fclose(FILE* stream) {
    const int result = __real_fclose(stream);
    if (capture_scenario != nullptr && std::strcmp(capture_scenario, "capture-close-failure") == 0) {
        errno = EIO;
        return EOF;
    }
    return result;
}

extern "C" int __wrap_rename(const char* source, const char* destination) {
    if (capture_scenario != nullptr &&
        (std::strcmp(capture_scenario, "capture-publish-failure") == 0 ||
         (std::strcmp(capture_scenario, "capture-json-publish-failure") == 0 && std::strstr(source, ".json.partial") != nullptr))) {
        errno = EIO;
        return -1;
    }
    return __real_rename(source, destination);
}

extern "C" int __real_clock_gettime(clockid_t clock, struct timespec* stamp);

extern "C" int __wrap_clock_gettime(clockid_t clock, struct timespec* stamp) {
    if (marker_scenario == nullptr)
        return __real_clock_gettime(clock, stamp);
    assert(clock == CLOCK_MONOTONIC);
    if (std::strcmp(marker_scenario, "marker-clock-failure") == 0) {
        errno = EIO;
        return -1;
    }
    stamp->tv_sec = marker_us / 1000000u;
    stamp->tv_nsec = (marker_us % 1000000u) * 1000u;
    if (std::strcmp(marker_scenario, "marker-invalid-clock") == 0)
        stamp->tv_nsec = -1;
    if (std::strcmp(marker_scenario, "marker-overflow") == 0)
        stamp->tv_sec = INT64_MAX;
    return 0;
}

extern "C" int __wrap_fprintf(FILE* stream, const char* format, ...) {
    if (capture_scenario != nullptr && std::strcmp(capture_scenario, "capture-json-write-failure") == 0 &&
        std::strncmp(format, "{\"kind\":\"ww05-mem1-snapshot-v1\"", 31u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    if (marker_scenario != nullptr && std::strcmp(marker_scenario, "marker-write-failure") == 0 &&
        stream == stderr && std::strncmp(format, "[frame-present]", 15u) == 0) {
        errno = ENOSPC;
        return -1;
    }
    if (capture_scenario != nullptr && std::strcmp(capture_scenario, "image-refusal-trace-failure") == 0 &&
        stream == stderr && std::strstr(format, "[frame-renderer-refusal]") == format) {
        errno = ENOSPC;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

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
    const char* readback(WwRendererFramePixels& frame) override {
        ++readbacks;
        if (fail_readback)
            return "synthetic live readback failed";
        frame.width = 2u;
        frame.height = 2u;
        frame.rgba = {10, 20, 30, 0, 40, 50, 60, 128, 70, 80, 90, 255, 100, 110, 120, 0};
        if (invalid_readback)
            frame.rgba.pop_back();
        return nullptr;
    }
    void shutdown() override {}
    u32 draws = 0u;
    u32 presents = 0u;
    bool fail_draw = false;
    bool fail_present = false;
    bool fail_readback = false;
    bool invalid_readback = false;
    u32 readbacks = 0u;
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
    const bool markers = std::strncmp(scenario, "marker-", 7u) == 0;
    marker_scenario = markers ? scenario : nullptr;
    const bool captures = std::strncmp(scenario, "capture-", 8u) == 0;
    const bool images = std::strncmp(scenario, "image-", 6u) == 0;
    const bool capture_window = std::strcmp(scenario, "capture-window") == 0 ||
                                std::strcmp(scenario, "capture-window-limit") == 0 ||
                                std::strcmp(scenario, "capture-window-sixteen") == 0;
    const bool capture_success = std::strcmp(scenario, "capture-one") == 0 || std::strcmp(scenario, "capture-two") == 0 || capture_window;
    capture_scenario = captures || images ? scenario : nullptr;
    WwHost* host = static_cast<WwHost*>(std::calloc(1u, sizeof(*host)));
    assert(host != nullptr && ww_host_init(host));
    gx_fixture::FixtureContext fixture;
    gx_fixture::fill_texture_zones(fixture);
    std::memcpy(host->cpu.ram, fixture.mem1.data(), fixture.mem1.size());
    RecordingDrainBackend backend;
    backend.fail_draw = std::strcmp(scenario, "draw-failure") == 0;
    backend.fail_present = std::strcmp(scenario, "present-failure") == 0 ||
                           std::strcmp(scenario, "marker-failed-present") == 0 ||
                           std::strcmp(scenario, "image-failed-present") == 0;
    backend.fail_readback = std::strcmp(scenario, "image-readback-failure") == 0;
    backend.invalid_readback = std::strcmp(scenario, "image-invalid-readback") == 0;
    if (std::strcmp(scenario, "no-renderer") != 0) {
        const WwRendererConfig config = {.memory = &host->memory, .cpu = &host->cpu};
        assert(ww_renderer_create(&host->renderer, &config, &backend) == nullptr);
    }
    host->stream.enabled = true;
    host->stream.log_limit = 64u;
    if (std::strcmp(scenario, "scene-journal") == 0)
        host->stream.log_limit = 133260u;
    if (captures) {
        host->mem1_capture = {.limit = std::strcmp(scenario, "capture-one") == 0 ? 1u : 2u,
            .token = "cccccccccccccccccccccccccccccccc",
            .source = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            .package = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
        if (capture_window) {
            host->mem1_capture.start_command = 15u;
            host->mem1_capture.end_command = 16u;
            if (std::strcmp(scenario, "capture-window-limit") == 0)
                host->mem1_capture.limit = 1u;
            if (std::strcmp(scenario, "capture-window-sixteen") == 0) {
                host->mem1_capture.limit = 16u;
                host->mem1_capture.end_command = 30u;
            }
        }
        if (std::strcmp(scenario, "capture-bad-identity") == 0)
            host->mem1_capture.source = "invalid";
        if (std::strcmp(scenario, "capture-bad-ram") == 0)
            --host->cpu.ram_size;
    }
    const auto fifo = gx_fixture::build_textured_triangle();
    const char* error = feed(host, fifo.data(), fifo.size());
    if (captures && !capture_success) {
        assert(error != nullptr && host->mem1_capture.captured == 0u && backend.draws == 0u);
        assert(host->gx.trace_count != 0u && host->gx.fifo.size != 0u);
    } else if (backend.fail_draw) {
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
        if (std::strcmp(scenario, "scene-journal") == 0) {
            const u8 nop = 0u;
            while (host->stream.decoded <= host->stream.log_limit)
                assert(feed(host, &nop, 1u) == nullptr);
            assert(host->stream.logged == 133260u && host->stream.decoded == 133261u);
            assert(host->stream.logged_bytes < WW_GX_MAX_JOURNAL_BYTES);
            assert(host->gx.trace_count == 0u && host->gx.fifo.size == 0u);
        }
        if (images) {
            if (std::strcmp(scenario, "image-off") != 0) {
                host->mem1_capture = {.limit = 2u, .captured = 2u,
                    .token = "cccccccccccccccccccccccccccccccc",
                    .source = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    .package = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
            }
            const char* enabled = ww_host_enable_frame_capture(host);
            if (std::strcmp(scenario, "image-preexisting") == 0) {
                assert(enabled != nullptr && backend.readbacks == 0u && backend.presents == 0u);
                ww_host_shutdown(host);
                std::free(state);
                std::free(host);
                return 0;
            }
            assert(enabled == nullptr);
            assert(backend.readbacks == 0u);
            for (u32 frame = 1u; frame <= 2u; ++frame) {
                if (frame == 2u) {
                    std::vector<u8> draw;
                    if (std::strcmp(scenario, "image-refused") == 0 || std::strcmp(scenario, "image-refusal-trace-failure") == 0)
                        draw = gx_fixture::build_dstalpha_triangle();
                    else
                        gx_fixture::push_triangle(draw);
                    assert(feed(host, draw.data(), draw.size()) == nullptr);
                }
                const u8 display_copy[] = {0x61u, 0x52u, 0u, 0x40u, 0u};
                error = feed(host, display_copy, sizeof(display_copy));
                if (backend.fail_present) {
                    assert(error != nullptr && backend.presents == 0u && backend.readbacks == 0u);
                    break;
                }
                if (backend.fail_readback || backend.invalid_readback || std::strcmp(scenario, "image-png-write-failure") == 0) {
                    assert(error != nullptr && backend.presents == 1u && backend.readbacks == 1u);
                    break;
                }
                if (frame == 2u && std::strcmp(scenario, "image-refusal-trace-failure") == 0) {
                    assert(error != nullptr && std::strstr(error, "refusal trace write failed") != nullptr);
                    assert(backend.presents == 2u && backend.readbacks == 2u && backend.draws == 1u);
                    break;
                }
                assert(error == nullptr && backend.presents == frame);
                assert(backend.readbacks == (std::strcmp(scenario, "image-off") == 0 ? 0u : frame));
            }
        } else if (capture_window) {
            assert(host->mem1_capture.captured == 0u);
            for (u32 command = host->mem1_capture.start_command; command <= host->mem1_capture.end_command; ++command) {
                host->cpu.ram[gx_fixture::kTextureBase] = std::strcmp(scenario, "capture-window-sixteen") == 0
                    ? command : command == 15u ? 0x55u : 0xAAu;
                std::vector<u8> draw;
                gx_fixture::push_triangle(draw);
                error = feed(host, draw.data(), draw.size());
                if (command == 16u && std::strcmp(scenario, "capture-window-limit") == 0) {
                    assert(error != nullptr && std::strstr(error, "scene MEM1 draw window exhausted its snapshot budget") != nullptr);
                    assert(host->mem1_capture.captured == 1u);
                    std::fprintf(stderr, "%s\n", error);
                    break;
                }
                if (command == host->mem1_capture.end_command) {
                    assert(error != nullptr && std::strcmp(error, "scene MEM1 draw window capture complete") == 0);
                    assert(host->mem1_capture.captured == host->mem1_capture.limit && host->stream.decoded == command);
                    std::fprintf(stderr, "%s\n", error);
                    break;
                }
                assert(error == nullptr && host->mem1_capture.captured == command - host->mem1_capture.start_command + 1u);
            }
        } else if (capture_success) {
            assert(host->mem1_capture.captured == 1u && host->cpu.ram[gx_fixture::kTextureBase] != 0x55u);
            host->cpu.ram[gx_fixture::kTextureBase] = 0x55u;
            std::vector<u8> draw;
            gx_fixture::push_triangle(draw);
            assert(feed(host, draw.data(), draw.size()) == nullptr && backend.draws == 2u);
            assert(host->mem1_capture.captured == host->mem1_capture.limit);
            assert(feed(host, draw.data(), draw.size()) == nullptr && backend.draws == 3u);
            assert(host->mem1_capture.captured == host->mem1_capture.limit);
            assert(host->cpu.ram[gx_fixture::kTextureBase] == 0x55u);
        } else if (markers && !backend.fail_present) {
            for (u32 frame = 1u; frame <= 2u; ++frame) {
                if (frame == 2u) {
                    std::vector<u8> draw;
                    gx_fixture::push_triangle(draw);
                    assert(feed(host, draw.data(), draw.size()) == nullptr);
                    marker_us = std::strcmp(scenario, "marker-backward") == 0 ? 999999u :
                                std::strcmp(scenario, "marker-repeated-time") == 0 ? 1000000u : 2000000u;
                }
                const u8 display_copy[] = {0x61u, 0x52u, 0u, 0x40u, 0u};
                error = feed(host, display_copy, sizeof(display_copy));
                WwRendererStats stats{};
                ww_renderer_stats(host->renderer, &stats);
                assert(stats.presented == frame && backend.presents == frame);
                if (std::strcmp(scenario, "marker-present") != 0 &&
                    (frame == 2u || (std::strcmp(scenario, "marker-backward") != 0 &&
                                     std::strcmp(scenario, "marker-repeated-time") != 0))) {
                    assert(error != nullptr);
                    break;
                }
                assert(error == nullptr);
            }
        } else if (backend.fail_present) {
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
    assert(host->cycles.absolute_cycles == 0u && host->cpu.pc == 0u && (backend.presents == 0u || markers || images));
    if (std::strcmp(scenario, "capture-bad-ram") == 0)
        ++host->cpu.ram_size;
    ww_host_shutdown(host);
    std::free(host);
    return 0;
}
