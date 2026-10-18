#ifndef BLUEWAKE_SP_BALANCE_H
#define BLUEWAKE_SP_BALANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WW_SP_BALANCE_WINDOW_WORDS 16u
#define WW_SP_BALANCE_LINE_BYTES 256u
#define WW_SP_BALANCE_MEM1_LIMIT 0x80200000u
#define WW_SP_BALANCE_BAND_BASE 0x80170C00u
#define WW_SP_BALANCE_BAND_LIMIT 0x80170D00u
#define WW_SP_BALANCE_BAND_PCS 256u

typedef struct {
    uint32_t pc;
    uint32_t sp_in;
    uint32_t sp_out;
} WwSpBalanceRecord;

bool ww_sp_balance_mismatch(uint32_t sp_in, uint32_t sp_out);
bool ww_sp_balance_mem1(uint32_t sp);
bool ww_sp_balance_band(uint32_t sp);
int ww_sp_balance_format_band(char* out, size_t size, uint32_t pc, uint32_t sp_in,
                              uint32_t sp_out, uint32_t lr);
void ww_sp_balance_band_observe(uint32_t pc, uint32_t sp_in, uint32_t sp_out, uint32_t lr,
                                const uint8_t* ram, uint32_t ram_size);
int ww_sp_balance_format_record(char* out, size_t size, const WwSpBalanceRecord* record);
int ww_sp_balance_format_window(char* out, size_t size, uint32_t sp, uint32_t base,
                                const uint32_t* words, size_t count);
void ww_sp_balance_observe(uint32_t pc, uint32_t sp_in, uint32_t sp_out,
                           const uint8_t* ram, uint32_t ram_size);
uint64_t ww_sp_balance_mismatches(void);

#endif
