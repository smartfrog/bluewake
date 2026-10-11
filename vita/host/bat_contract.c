#include "bat_contract.h"

#define IBAT0U 528u
#define DBAT0U 536u
#define DBAT3L 543u
#define BOOT_CACHED_UPPER 0x80001FFFu
#define BOOT_UNCACHED_UPPER 0xC0001FFFu
#define BOOT_UNCACHED_LOWER 0x0000002Au

static const u32 split_upper[4] = {0x800001FFu, 0u, 0x810000FFu, 0u};
static const u32 split_lower[4] = {2u, 0u, 0x01000002u, 0u};

void ww_bat_init(WwBatContract* bat) {
    *bat = (WwBatContract){.regs = {[0] = BOOT_CACHED_UPPER, [1] = 2u,
                                  [8] = BOOT_CACHED_UPPER, [9] = 2u,
                                  [10] = BOOT_UNCACHED_UPPER, [11] = BOOT_UNCACHED_LOWER}};
}

bool ww_bat_contains(u16 spr) {
    return spr >= IBAT0U && spr <= DBAT3L;
}

bool ww_bat_read(const WwBatContract* bat, u16 spr, u32* value) {
    if (!ww_bat_contains(spr) || value == NULL)
        return false;
    *value = bat->regs[spr - IBAT0U];
    return true;
}

static u32 expected_upper(bool data, u32 pair) {
    return data && pair == 1u ? BOOT_UNCACHED_UPPER : split_upper[pair];
}

static u32 expected_lower(bool data, u32 pair) {
    return data && pair == 1u ? BOOT_UNCACHED_LOWER : split_lower[pair];
}

const char* ww_bat_write(WwBatContract* bat, u16 spr, u32 value, u32 msr) {
    if (!ww_bat_contains(spr))
        return "unsupported BAT SPR";
    const u32 index = spr - IBAT0U;
    const bool data = spr >= DBAT0U;
    const u32 pair = (index % 8u) / 2u;
    if (bat->regs[index] == value)
        return NULL;
    if (msr & (data ? PPC_MSR_DR : PPC_MSR_IR))
        return "BAT change with translation active requires an MMU backend";
    if (index & 1u) {
        if (bat->regs[index - 1u] & 3u)
            return "BAT lower change requires invalidated upper";
        if (value != 0u && value != expected_lower(data, pair))
            return "BAT lower outside the supported fixed memory view";
    } else if (value != 0u) {
        if (value != expected_upper(data, pair) && !(pair == 0u && value == BOOT_CACHED_UPPER))
            return "BAT upper outside the supported fixed memory view";
        if (bat->regs[index + 1u] != expected_lower(data, pair))
            return "BAT upper requires matching physical base and attributes";
    }
    bat->regs[index] = value;
    return NULL;
}

bool ww_bat_memory_supported(const WwBatContract* bat, u32 msr) {
    for (u32 bank = 0u; bank < 2u; ++bank) {
        if (!(msr & (bank != 0u ? PPC_MSR_DR : PPC_MSR_IR)))
            continue;
        const u32* regs = bat->regs + bank * 8u;
        if (regs[1] != 2u)
            return false;
        if (regs[0] != BOOT_CACHED_UPPER &&
            !(regs[0] == split_upper[0] && regs[4] == split_upper[2] && regs[5] == split_lower[2]))
            return false;
        if (bank != 0u && (regs[2] != BOOT_UNCACHED_UPPER || regs[3] != BOOT_UNCACHED_LOWER))
            return false;
    }
    return true;
}
