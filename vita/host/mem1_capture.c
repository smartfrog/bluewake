#include "boot_host.h"
#include "diagnostic_io.h"

const char* ww_host_capture_mem1(WwHost* host) {
    if (host == NULL)
        return "MEM1 capture requires a host";
    WwMem1Capture* capture = &host->mem1_capture;
    if (capture->limit == 0u)
        return NULL;
    if (capture->limit > WW_MEM1_MAX_CAPTURES || capture->captured > capture->limit)
        return "MEM1 capture exceeds its bounded request";
    if (capture->captured == capture->limit)
        return NULL;
    const WwGxCheckpoint* draw = &host->stream.command;
    if (!host->stream.enabled || host->stream.decoded == 0u || host->stream.decoded > host->stream.log_limit ||
        !draw->complete || draw->size < 3u || draw->size != draw->required ||
        (draw->bytes[0] & 0xF8u) < 0x80u || (draw->bytes[0] & 0xF8u) > 0xB8u ||
        (draw->bytes[0] & 0xF8u) == 0x88u || read_be16(draw->bytes + 1u) == 0u ||
        capture->token == NULL || capture->source == NULL || capture->package == NULL ||
        !diagnostic_hex(capture->token, 32u) || !diagnostic_hex(capture->source, 40u) ||
        !diagnostic_hex(capture->package, 64u))
        return "MEM1 capture requires a bound identity and a logged draw";
    if (host->cpu.ram == NULL || host->cpu.ram_size != WW_MEM1_CAPTURE_BYTES)
        return "MEM1 capture requires the actual 24 MiB RAM backing";
    u64 us;
    const char* error = ww_host_wall_us(&us);
    if (error != NULL)
        return error;
    if (capture->captured != 0u && us <= capture->last_us)
        return "MEM1 capture wall time did not advance";
    const u32 number = capture->captured + 1u;
    char binary[96], binary_partial[104], metadata[96], metadata_partial[104];
    const int binary_size = snprintf(binary, sizeof(binary), WW05_DIRECTORY "ww05.mem1-%u.bin", number);
    const int metadata_size = snprintf(metadata, sizeof(metadata), WW05_DIRECTORY "ww05.mem1-%u.json", number);
    if (binary_size < 0 || (size_t)binary_size >= sizeof(binary) || metadata_size < 0 ||
        (size_t)metadata_size >= sizeof(metadata) ||
        snprintf(binary_partial, sizeof(binary_partial), "%s.partial", binary) < 0 ||
        snprintf(metadata_partial, sizeof(metadata_partial), "%s.partial", metadata) < 0)
        return "MEM1 capture path formatting failed";
    if (!diagnostic_absent(binary) || !diagnostic_absent(binary_partial) ||
        !diagnostic_absent(metadata) || !diagnostic_absent(metadata_partial))
        return "MEM1 capture refuses preexisting output";
    FILE* stream = fopen(binary_partial, "wbx");
    if (stream == NULL)
        return "MEM1 capture binary open failed";
    for (u32 offset = 0u; offset < host->cpu.ram_size; offset += 65536u) {
        if (fwrite(host->cpu.ram + offset, 1u, 65536u, stream) != 65536u) {
            const int closed = fclose(stream);
            return closed != 0 ? "MEM1 capture binary write and close failed" : "MEM1 capture binary write failed";
        }
    }
    if (diagnostic_publish(stream, binary_partial, binary) != 0)
        return "MEM1 capture binary publish failed";
    stream = fopen(metadata_partial, "wx");
    if (stream == NULL)
        return "MEM1 capture metadata open failed";
    const int written = fprintf(stream,
        "{\"kind\":\"ww05-mem1-snapshot-v1\",\"token\":\"%s\",\"source\":\"%s\",\"vpk_sha256\":\"%s\","
        "\"snapshot\":%u,\"limit\":%u,\"file\":\"ww05.mem1-%u.bin\",\"physical_base\":\"00000000\","
        "\"guest_base\":\"80000000\",\"bytes\":%u,\"command\":%llu,\"opcode\":\"%02X\","
        "\"pc\":\"%08X\",\"completion_pc\":\"%08X\",\"cycles\":%llu,\"us\":%llu,\"presented\":%llu,"
        "\"phase\":\"before-renderer-write\",\"scope\":\"raw physical MEM1 only; external aliases and MEM2 are not captured\"}\n",
        capture->token, capture->source, capture->package, number, capture->limit, number, host->cpu.ram_size,
        (unsigned long long)host->stream.decoded, draw->bytes[0], draw->pc, draw->completion_pc,
        (unsigned long long)host->cycles.absolute_cycles, (unsigned long long)us,
        (unsigned long long)host->frame_timing.present.index);
    if (written < 0) {
        const int closed = fclose(stream);
        return closed != 0 ? "MEM1 capture metadata write and close failed" : "MEM1 capture metadata write failed";
    }
    if (diagnostic_publish(stream, metadata_partial, metadata) != 0)
        return "MEM1 capture metadata publish failed";
    capture->captured = number;
    capture->last_us = us;
    if (fprintf(stderr, "[mem1-snapshot] snapshot=%u command=%llu bytes=%u physical=00000000 us=%llu file=ww05.mem1-%u.bin phase=before-renderer-write\n",
                number, (unsigned long long)host->stream.decoded, host->cpu.ram_size,
                (unsigned long long)us, number) < 0)
        return "MEM1 capture trace write failed";
    return NULL;
}
