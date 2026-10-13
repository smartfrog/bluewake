extern "C" {
#include "boot_host.h"
#include "diagnostic_io.h"
}
#include "gx_frame_capture.h"
#include <initializer_list>
#include <new>
#include <cerrno>
#include <dirent.h>

static const char* capture_frame(const WwRendererFrame* frame, void* user) try {
    auto* host = static_cast<WwHost*>(user);
    WwRendererStats stats{};
    ww_renderer_stats(host->renderer, &stats);
    if (stats.presented == 0u || stats.presented != host->frame_capture.index + 1u || stats.presented > 128u)
        return "live frame evidence requires consecutive actual presentations, at most 128 per diagnostic run";
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
        "\"display_transform\":\"alpha=255; RGB unchanged from raw front-buffer readback\"}\n",
        identity->token, identity->source, identity->package, host->dsp.audio_deferred ? "deferred" : "strict", index,
        static_cast<unsigned long long>(host->stream.decoded), static_cast<unsigned long long>(stats.draws),
        frame->width, frame->height, static_cast<unsigned long long>(us),
        static_cast<unsigned long long>(host->cycles.absolute_cycles), identity->captured, index, index);
    if (written < 0) {
        const int closed = fclose(stream);
        return closed != 0 ? "live frame metadata write and close failed" : "live frame metadata write failed";
    }
    if (diagnostic_publish(stream, partial, metadata) != 0)
        return "live frame metadata publish failed";
    host->frame_capture = {stats.presented, us};
    if (fprintf(stderr, "[frame-capture] frame=%llu command=%llu us=%llu width=%u height=%u raw=frames/frame-%llu-rgba.png display=frames/frame-%llu.png phase=after-gx-present-before-next-frame\n",
                index, static_cast<unsigned long long>(host->stream.decoded), static_cast<unsigned long long>(us),
                frame->width, frame->height, index, index) < 0)
        return "live frame evidence trace write failed";
    return nullptr;
} catch (const std::bad_alloc&) {
    return "live frame PNG allocation failed";
}

extern "C" const char* ww_host_enable_frame_capture(WwHost* host) {
    if (host == nullptr)
        return "live frame capture requires a host";
    const WwMem1Capture* identity = &host->mem1_capture;
    if (identity->limit == 0u)
        return nullptr;
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
