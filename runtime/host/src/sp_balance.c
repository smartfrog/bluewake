#include "sp_balance.h"

#include "core/cpu.h"

#include <stdio.h>
#include <string.h>

static uint64_t mismatch_count;
static bool window_dumped;

bool ww_sp_balance_mismatch(uint32_t sp_in, uint32_t sp_out) {
    return sp_in != sp_out;
}

bool ww_sp_balance_mem1(uint32_t sp) {
    return sp >= GC_RAM_BASE && sp < WW_SP_BALANCE_MEM1_LIMIT;
}

int ww_sp_balance_format_record(char* out, size_t size, const WwSpBalanceRecord* record) {
    if (out == NULL || size == 0u || record == NULL)
        return -1;
    const int written = snprintf(out, size, "[sp-balance] pc=%08X sp_in=%08X sp_out=%08X",
                                 record->pc, record->sp_in, record->sp_out);
    return written < 0 || (size_t)written >= size ? -1 : written;
}

int ww_sp_balance_format_window(char* out, size_t size, uint32_t sp, uint32_t base,
                                const uint32_t* words, size_t count) {
    if (out == NULL || size == 0u || (words == NULL && count != 0u))
        return -1;
    int written = snprintf(out, size, "[sp-balance] window sp=%08X base=%08X words=", sp, base);
    if (written < 0 || (size_t)written >= size)
        return -1;
    for (size_t index = 0u; index < count; ++index) {
        const int appended = snprintf(out + written, size - (size_t)written,
                                      " %08X", words[index]);
        if (appended < 0 || (size_t)appended >= size - (size_t)written)
            return -1;
        written += appended;
    }
    return written;
}

static bool read_guest_word(const uint8_t* ram, uint32_t ram_size, uint32_t address,
                            uint32_t* word) {
    if (ram == NULL || address < GC_RAM_BASE)
        return false;
    const uint32_t offset = address - GC_RAM_BASE;
    if (offset > ram_size || ram_size - offset < 4u)
        return false;
    const uint8_t* bytes = ram + offset;
    *word = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
            ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
    return true;
}

static void dump_window(uint32_t sp, const uint8_t* ram, uint32_t ram_size) {
    char line[WW_SP_BALANCE_LINE_BYTES];
    for (uint32_t half = 0u; half < 2u; ++half) {
        const uint32_t base = half == 0u
                                  ? sp - WW_SP_BALANCE_WINDOW_WORDS * 4u
                                  : sp;
        uint32_t words[WW_SP_BALANCE_WINDOW_WORDS];
        bool readable = true;
        for (uint32_t index = 0u; index < WW_SP_BALANCE_WINDOW_WORDS; ++index) {
            if (!read_guest_word(ram, ram_size, base + index * 4u, &words[index])) {
                readable = false;
                break;
            }
        }
        if (!readable) {
            (void)fprintf(stderr, "[sp-balance] window unavailable sp=%08X base=%08X\n",
                          sp, base);
            continue;
        }
        if (ww_sp_balance_format_window(line, sizeof(line), sp, base, words,
                                        WW_SP_BALANCE_WINDOW_WORDS) > 0)
            (void)fprintf(stderr, "%s\n", line);
    }
}

void ww_sp_balance_observe(uint32_t pc, uint32_t sp_in, uint32_t sp_out,
                           const uint8_t* ram, uint32_t ram_size) {
    if (!ww_sp_balance_mismatch(sp_in, sp_out))
        return;
    mismatch_count++;
    char line[WW_SP_BALANCE_LINE_BYTES];
    const WwSpBalanceRecord record = {pc, sp_in, sp_out};
    if (ww_sp_balance_format_record(line, sizeof(line), &record) > 0)
        (void)fprintf(stderr, "%s\n", line);
    if (!window_dumped || ww_sp_balance_mem1(sp_in) || ww_sp_balance_mem1(sp_out)) {
        window_dumped = true;
        const uint32_t window_sp = ww_sp_balance_mem1(sp_in)
                                       ? sp_in
                                       : (ww_sp_balance_mem1(sp_out) ? sp_out : sp_in);
        dump_window(window_sp, ram, ram_size);
    }
}

uint64_t ww_sp_balance_mismatches(void) {
    return mismatch_count;
}
