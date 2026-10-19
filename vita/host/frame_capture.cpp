extern "C" {
#include "boot_host.h"
#include "diagnostic_io.h"
}
#include "gx_frame_capture.h"
#include <initializer_list>
#include <new>
#include <cerrno>
#include <dirent.h>

static const char* trace_renderer_reasons(const WwRendererStats& stats, unsigned long long frame, const char* audio) {
    const struct {
        const char* category;
        u32 count;
        const char* const* reasons;
    } groups[] = {{"plan", stats.skip_count, stats.skip_reasons},
                  {"shader", stats.refusal_count, stats.refusal_reasons},
                  {"copy", stats.copy_refusal_count, stats.copy_refusal_reasons}};
    for (const auto& group : groups) {
        if (group.count > WW_RENDERER_MAX_REASONS)
            return "live frame renderer reason count exceeds its bound";
        for (u32 index = 0u; index < group.count; ++index) {
            if (group.reasons[index] == nullptr)
                return "live frame renderer reason is missing";
            if (fprintf(stderr, "[frame-renderer-refusal] frame=%llu category=%s audio=%s reason=%s\n",
                        frame, group.category, audio, group.reasons[index]) < 0)
                return "live frame renderer refusal trace write failed";
        }
    }
    if (stats.gap_count > WW_RENDERER_MAX_GAPS)
        return "live frame renderer gap count exceeds its bound";
    for (u32 index = 0u; index < stats.gap_count; ++index) {
        if (stats.gaps[index].name == nullptr)
            return "live frame renderer gap name is missing";
        if (fprintf(stderr, "[frame-renderer-gap] frame=%llu audio=%s name=%s value=%llu\n",
                    frame, audio, stats.gaps[index].name,
                    static_cast<unsigned long long>(stats.gaps[index].value)) < 0)
            return "live frame renderer gap trace write failed";
    }
    return nullptr;
}

static const char* capture_frame(const WwRendererFrame* frame, void* user) try {
    auto* host = static_cast<WwHost*>(user);
    WwRendererStats stats{};
    ww_renderer_stats(host->renderer, &stats);
    if (stats.presented == 0u)
        return "live frame evidence requires a completed presentation";
    if (stats.presented < host->frame_capture.start)
        return nullptr;
    if (stats.presented > host->frame_capture.limit)
        return "live frame evidence exhausted its requested present window";
    if (host->frame_capture.index != 0u && stats.presented != host->frame_capture.index + 1u)
        return "live frame evidence requires consecutive actual presentations within the requested window";
    if (frame == nullptr || !gx_capture::valid(*frame) || frame->size > 960u * 544u * 4u)
        return "live frame evidence exceeds the Vita framebuffer extent";
    u64 us;
    const char* error = ww_host_wall_us(&us);
    if (error != nullptr)
        return error;
    if (host->frame_capture.index != 0u && us <= host->frame_capture.us)
        return "live frame evidence wall time did not advance";
    char raw[128], display[128], metadata[128];
    const auto index = static_cast<unsigned long long>(stats.presented);
    if (snprintf(raw, sizeof(raw), WW05_DIRECTORY "frames/frame-%llu-rgba.png", index) < 0 ||
        snprintf(display, sizeof(display), WW05_DIRECTORY "frames/frame-%llu.png", index) < 0 ||
        snprintf(metadata, sizeof(metadata), WW05_DIRECTORY "frames/frame-%llu.json", index) < 0)
        return "live frame evidence path formatting failed";
    for (const char* path : {raw, display}) {
        char partial[136];
        if (snprintf(partial, sizeof(partial), "%s.partial", path) < 0 ||
            !diagnostic_absent(path) || !diagnostic_absent(partial))
            return "live frame evidence refuses preexisting PNG output";
        error = gx_capture::write_png(*frame, partial, path == display);
        if (error != nullptr)
            return error;
        if (!diagnostic_absent(path) || rename(partial, path) != 0)
            return "live frame evidence PNG publish failed";
    }
    char partial[136];
    if (snprintf(partial, sizeof(partial), "%s.partial", metadata) < 0 ||
        !diagnostic_absent(metadata) || !diagnostic_absent(partial))
        return "live frame evidence refuses preexisting metadata";
    FILE* stream = fopen(partial, "wx");
    if (stream == nullptr)
        return "live frame evidence metadata open failed";
    const WwMem1Capture* identity = &host->mem1_capture;
    const int written = fprintf(stream,
        "{\"kind\":\"ww05-live-frame-v1\",\"token\":\"%s\",\"source\":\"%s\",\"vpk_sha256\":\"%s\",\"audio\":\"%s\","
        "\"frame\":%llu,\"command\":%llu,\"draws\":%llu,\"width\":%u,\"height\":%u,\"us\":%llu,\"cycles\":%llu,"
        "\"mem1_snapshots\":%u,\"phase\":\"after-gx-present-before-next-frame\",\"row_order\":\"top-first\","
        "\"raw\":\"frame-%llu-rgba.png\",\"display\":\"frame-%llu.png\","
        "\"display_transform\":\"alpha=255; RGB unchanged from raw front-buffer readback\","
        "\"renderer\":{\"fifo_bytes\":%llu,\"events\":%llu,\"plans\":%llu,\"plans_skipped\":%llu,"
        "\"programs\":%llu,\"programs_distinct\":%llu,\"programs_refused\":%llu,\"copies\":%llu,"
        "\"copies_refused\":%llu,\"display_copies\":%llu,\"gaps\":%u}}\n",
        identity->token, identity->source, identity->package, host->dsp.audio_deferred ? "deferred" : "strict", index,
        static_cast<unsigned long long>(host->stream.decoded), static_cast<unsigned long long>(stats.draws),
        frame->width, frame->height, static_cast<unsigned long long>(us),
        static_cast<unsigned long long>(host->cycles.absolute_cycles), identity->captured, index, index,
        static_cast<unsigned long long>(stats.fifo_bytes), static_cast<unsigned long long>(stats.events),
        static_cast<unsigned long long>(stats.plans), static_cast<unsigned long long>(stats.plans_skipped),
        static_cast<unsigned long long>(stats.programs), static_cast<unsigned long long>(stats.programs_distinct),
        static_cast<unsigned long long>(stats.programs_refused), static_cast<unsigned long long>(stats.copies),
        static_cast<unsigned long long>(stats.copies_refused), static_cast<unsigned long long>(stats.display_copies),
        stats.gap_count);
    if (written < 0) {
        const int closed = fclose(stream);
        return closed != 0 ? "live frame metadata write and close failed" : "live frame metadata write failed";
    }
    if (diagnostic_publish(stream, partial, metadata) != 0)
        return "live frame metadata publish failed";
    host->frame_capture.index = stats.presented;
    host->frame_capture.us = us;
    if (fprintf(stderr, "[frame-capture] frame=%llu command=%llu us=%llu width=%u height=%u raw=frames/frame-%llu-rgba.png display=frames/frame-%llu.png phase=after-gx-present-before-next-frame\n",
                index, static_cast<unsigned long long>(host->stream.decoded), static_cast<unsigned long long>(us),
                frame->width, frame->height, index, index) < 0)
        return "live frame evidence trace write failed";
    return trace_renderer_reasons(stats, index, host->dsp.audio_deferred ? "deferred" : "strict");
} catch (const std::bad_alloc&) {
    return "live frame PNG allocation failed";
}

extern "C" const char* ww_host_enable_frame_capture(WwHost* host) {
    if (host == nullptr)
        return "live frame capture requires a host";
    const WwMem1Capture* identity = &host->mem1_capture;
    if (identity->limit == 0u)
        return nullptr;
    if (host->frame_capture.start == 0u)
        host->frame_capture.start = 1u;
    if (host->frame_capture.limit == 0u)
        host->frame_capture.limit = WW_FRAME_CAPTURE_DEFAULT_LIMIT;
    if (host->frame_capture.start > host->frame_capture.limit || host->frame_capture.limit > WW_FRAME_CAPTURE_MAX)
        return "live frame capture requires a bounded present window";
    if (identity->limit > WW_MEM1_MAX_CAPTURES || host->renderer == nullptr || identity->token == nullptr || identity->source == nullptr ||
        identity->package == nullptr || !diagnostic_hex(identity->token, 32u) ||
        !diagnostic_hex(identity->source, 40u) || !diagnostic_hex(identity->package, 64u))
        return "live frame capture requires a renderer and bound MEM1 request identity";
    if (mkdir(WW05_DIRECTORY "frames", 0700) != 0) {
        if (errno != EEXIST)
            return "live frame capture directory creation failed";
        struct stat info;
        if (lstat(WW05_DIRECTORY "frames", &info) != 0 || !S_ISDIR(info.st_mode))
            return "live frame capture requires a real frame directory";
        DIR* directory = opendir(WW05_DIRECTORY "frames");
        if (directory == nullptr)
            return "live frame capture directory inspection failed";
        errno = 0;
        bool empty = true;
        while (const dirent* entry = readdir(directory)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                empty = false;
                break;
            }
        }
        const int read_error = errno;
        const int closed = closedir(directory);
        if (!empty || read_error != 0 || closed != 0)
            return "live frame capture refuses nonempty or unreadable frame directory";
    }
    return ww_renderer_set_frame_capture(host->renderer, capture_frame, host);
}
