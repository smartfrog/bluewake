#include "code_guard.h"

#include <stdlib.h>
#include <string.h>

void ww_code_guard_free(WwCodeGuard* guard) {
    for (u32 index = 0u; index < DOL_NUM_TEXT; ++index) {
        free(guard->text[index]);
        guard->text[index] = NULL;
    }
    guard->bytes = 0u;
}

bool ww_code_guard_init(WwCodeGuard* guard, const CPUState* cpu, const DolLayout* layout) {
    if (guard->bytes != 0u || cpu->ram == NULL)
        return false;
    guard->layout = *layout;
    for (u32 index = 0u; index < DOL_NUM_TEXT; ++index) {
        const u32 size = layout->text_size[index];
        if (size == 0u)
            continue;
        const u32 address = layout->text_address[index];
        if (address < GC_RAM_BASE || (u64)address - GC_RAM_BASE + size > cpu->ram_size)
            goto failed;
        guard->text[index] = calloc(1u, size);
        if (guard->text[index] == NULL)
            goto failed;
        memcpy(guard->text[index], cpu->ram + address - GC_RAM_BASE, size);
        guard->bytes += size;
    }
    return true;

failed:
    ww_code_guard_free(guard);
    return false;
}

bool ww_code_guard_matches(const WwCodeGuard* guard, const CPUState* cpu, u32 address) {
    const u64 start = address & ~31u;
    const u64 end = start + 32u;
    if (cpu->ram == NULL || start < GC_RAM_BASE || end - GC_RAM_BASE > cpu->ram_size)
        return false;
    for (u32 index = 0u; index < DOL_NUM_TEXT; ++index) {
        const u64 text_start = guard->layout.text_address[index];
        const u64 text_end = text_start + guard->layout.text_size[index];
        const u64 overlap_start = start > text_start ? start : text_start;
        const u64 overlap_end = end < text_end ? end : text_end;
        if (overlap_start < overlap_end &&
            (guard->text[index] == NULL ||
             memcmp(cpu->ram + overlap_start - GC_RAM_BASE,
                    guard->text[index] + overlap_start - text_start, overlap_end - overlap_start) != 0))
            return false;
    }
    return true;
}
