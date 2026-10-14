#ifndef WW_REL_SDK_FIXTURE_H
#define WW_REL_SDK_FIXTURE_H

static void ww_probe_sdk_append(WwHost* host, const WwRelImage* image, bool fixed) {
    CPUState* cpu = &host->cpu;
    const u8* raw = image->certificate->rel_bytes;
    const u32 tail = mem_read32(cpu, 0x800030CCu);
    if (tail == 0u)
        mem_write32(cpu, 0x800030C8u, image->guest);
    else
        mem_write32(cpu, tail + 4u, image->guest);
    mem_write32(cpu, image->guest + 8u, tail);
    mem_write32(cpu, image->guest + 4u, 0u);
    mem_write32(cpu, 0x800030CCu, image->guest);
    const u32 fields[] = {0x10u, 0x24u, 0x28u, 0x48u};
    for (u32 at = 0u; at < 4u; ++at)
        mem_write32(cpu, image->guest + fields[at], image->guest + read_be32(raw + fields[at]));
    mem_write8(cpu, image->guest + 0x33u, 0u);
    for (u32 at = 1u; at < image->section_count; ++at) {
        const WwRelSection* section = &image->sections[at];
        if (section->flags != 0u)
            mem_write32(cpu, image->guest + image->section_table + at * 8u, image->guest + section->flags);
        else if (section->size != 0u) {
            mem_write32(cpu, image->guest + image->section_table + at * 8u, mem_read32(cpu, image->owner + 0x14u));
            mem_write8(cpu, image->guest + 0x33u, (u8)at);
        }
    }
    for (u32 at = 0u; at < 3u; ++at) {
        const u32 index = raw[0x30u + at];
        if (index != 0u) {
            const u32 start = mem_read32(cpu, image->guest + image->section_table + index * 8u) & ~1u;
            mem_write32(cpu, image->guest + 0x34u + at * 4u, start + read_be32(raw + 0x34u + at * 4u));
        }
    }
    mem_write32(cpu, image->guest + 0x14u, mem_read32(cpu, 0x800030D0u) + read_be32(raw + 0x14u));
    const u32 imports = read_be32(raw + 0x28u), import_bytes = read_be32(raw + 0x2Cu);
    bool trimmed = false;
    for (u32 at = imports; at < imports + import_bytes; at += 8u) {
        const u32 module = read_be32(raw + at);
        assert(module == 0u || module == image->module_id);
        u32 patch = 0u;
        for (u32 cursor = read_be32(raw + at + 4u);; cursor += 8u) {
            assert(cursor + 8u <= image->bytes);
            const u8* entry = raw + cursor;
            const u32 type = entry[2];
            if (type == 203u) break;
            if (type == 202u) {
                patch = image->guest + (image->sections[entry[3]].flags & ~1u);
                continue;
            }
            patch += ((u32)entry[0] << 8u) | entry[1];
            if (type == 201u) continue;
            const u32 section = module == 0u ? 0u :
                mem_read32(cpu, image->guest + image->section_table + entry[3] * 8u) & ~1u;
            const u32 value = section + read_be32(entry + 4u);
            if (type == 1u) mem_write32(cpu, patch, value);
            else if (type == 4u) mem_write16(cpu, patch, (u16)value);
            else if (type == 6u) mem_write16(cpu, patch, (u16)((value + 0x8000u) >> 16u));
            else {
                assert(type == 10u);
                mem_write32(cpu, patch, (mem_read32(cpu, patch) & ~0x03FFFFFCu) | ((value - patch) & 0x03FFFFFCu));
            }
        }
        mem_write32(cpu, image->guest + at + 4u, image->guest + read_be32(raw + at + 4u));
        if (fixed && !trimmed && (module == 0u || module == image->module_id)) {
            mem_write32(cpu, image->guest + 0x2Cu, at - imports);
            trimmed = true;
        }
    }
}

#endif
