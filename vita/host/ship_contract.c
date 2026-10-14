#include "boot_host.h"
#include "rel_archive.h"

const char* ww_host_enable_ship_contract(WwHost* host, const BlueWakeShipContractV1* contract, const char* dol) {
    if (host == NULL || host->ship_contract != NULL || contract == NULL || contract->version != 1u ||
        contract->dol_bytes == NULL || contract->dol_size == 0u || contract->dol_size > 0x800000u ||
        contract->rel_bytes == NULL || contract->rel_size < 0x4Cu || contract->rel_size > 0x100000u ||
        contract->canonical_sections == NULL || contract->section_count == 0u || contract->section_count > 256u ||
        contract->build_identity == NULL || strlen(contract->build_identity) != 64u ||
        strspn(contract->build_identity, "0123456789abcdef") != 64u || dol == NULL)
        return "ship prolog requires a complete generated exact build contract";
    FILE* stream = fopen(dol, "rb");
    if (stream == NULL)
        return "ship contract raw DOL open failed";
    u8 buffer[4096];
    const char* error = NULL;
    for (u32 at = 0u; at < contract->dol_size && error == NULL;) {
        const u32 size = contract->dol_size - at < sizeof(buffer) ? contract->dol_size - at : sizeof(buffer);
        if (fread(buffer, 1u, size, stream) != size || memcmp(buffer, contract->dol_bytes + at, size) != 0)
            error = "ship contract full raw DOL identity mismatch";
        at += size;
    }
    if (error == NULL && (fgetc(stream) != EOF || ferror(stream)))
        error = "ship contract full raw DOL length or read mismatch";
    if (fclose(stream) != 0 && error == NULL)
        error = "ship contract raw DOL close failed";
    if (error == NULL)
        host->ship_contract = contract;
    return error;
}

bool ww_host_ship_raw_matches(WwHost* host, const u8* raw, u32 bytes) {
    const BlueWakeShipContractV1* contract = host->ship_contract;
    if (contract == NULL || bytes != contract->rel_size || bluewake_archive_be32(raw) != 168u)
        return false;
    const WwRelImage* live = ww_host_rel_live(host, 168u);
    if (live == NULL)
        return memcmp(raw, contract->rel_bytes, bytes) == 0;
    if (memcmp(raw, contract->rel_bytes, 0x34u) != 0 ||
        memcmp(raw + 0x40u, contract->rel_bytes + 0x40u, bytes - 0x40u) != 0)
        return false;
    for (u32 at = 0u; at < 3u; ++at) {
        const u32 section = contract->rel_bytes[0x30u + at];
        if (section >= live->section_count)
            return false;
        const u32 expected = live->guest + (live->sections[section].flags & ~1u) +
            bluewake_archive_be32(contract->rel_bytes + 0x34u + at * 4u);
        if (bluewake_archive_be32(raw + 0x34u + at * 4u) != expected)
            return false;
    }
    return true;
}

void ww_host_ship_publish(WwHost* host, WwRelImage* image) {
    for (u32 at = 0u; at < 3u; ++at) {
        const u8* raw = host->ship_contract->rel_bytes;
        const u32 section = raw[0x30u + at];
        mem_write32(&host->cpu, image->guest + 0x34u + at * 4u,
                    image->guest + (image->sections[section].flags & ~1u) +
                    bluewake_archive_be32(raw + 0x34u + at * 4u));
    }
    u8* raw = get_ram_ptr(&host->cpu, image->guest, image->bytes, NULL);
    image->raw_hash = ww_host_rel_raw_hash(raw, image->bytes);
}

const char* ww_host_ship_boundary(WwHost* host, u32 address) {
    const u32 canonical = address & ~0x40000000u;
    if (canonical != 0x80240EE8u)
        host->ship_prolog_return_ready = false;
    if (canonical == 0x81B10120u || canonical == 0x81B1014Cu)
        return "ship epilog or unresolved entry has no supported lifecycle contract";
    if (host->ship_prolog_owner != 0u) {
        const WwRelImage* image = ww_host_rel_live(host, 168u);
        if (image == NULL || image->owner != host->ship_prolog_owner || !ww_host_rel_raw_unchanged(image))
            return "ship prolog continuation lost its exact live owner";
        if (canonical == 0x80240EE8u) {
            if (host->cpu.lr != 0x80240EE8u || host->cpu.gpr[1] != host->ship_prolog_stack ||
                host->cpu.gpr[31] != image->owner || host->rel_constructor_owner != 0u ||
                host->cpu.gpr[2] != host->ship_prolog_r2 || host->cpu.gpr[13] != host->ship_prolog_r13 ||
                !host->ship_prolog_ctor_completed)
                return "ship prolog return did not preserve its translated caller";
            if (fprintf(stderr, "[rel-prolog] module=168 stage=caller-returned pc=%08X owner=%08X\n", address, image->owner) < 0)
                return "ship prolog return trace failed";
            host->ship_prolog_owner = 0u;
            host->ship_prolog_stack = 0u;
            host->ship_prolog_r2 = 0u;
            host->ship_prolog_r13 = 0u;
            host->ship_prolog_ctor_completed = false;
            host->ship_prolog_return_ready = true;
        }
    }
    if (canonical == 0x80240EE8u && host->ship_contract != NULL &&
        ww_host_rel_live(host, 168u) != NULL && !host->ship_prolog_return_ready)
        return "ship prolog return requires its completed translated call";
    if (canonical != 0x81B100F4u)
        return NULL;
    const WwRelImage* image = ww_host_rel_live(host, 168u);
    if (host->ship_contract == NULL || image == NULL ||
        host->cpu.lr != 0x80240EE8u || host->cpu.gpr[31] != image->owner ||
        !ww_host_rel_raw_unchanged(image))
        return "ship prolog entry requires its exact translated dynamic-link caller";
    if (host->ship_prolog_owner != 0u)
        return host->ship_prolog_owner == image->owner && host->ship_prolog_stack == host->cpu.gpr[1] ?
            NULL : "ship prolog does not support a nested entry";
    if (fprintf(stderr, "[rel-prolog] module=168 stage=entered pc=%08X owner=%08X lr=%08X\n", address, image->owner, host->cpu.lr) < 0)
        return "ship prolog entry trace failed";
    host->ship_prolog_owner = image->owner;
    host->ship_prolog_stack = host->cpu.gpr[1];
    host->ship_prolog_r2 = host->cpu.gpr[2];
    host->ship_prolog_r13 = host->cpu.gpr[13];
    host->ship_prolog_ctor_completed = false;
    return NULL;
}
