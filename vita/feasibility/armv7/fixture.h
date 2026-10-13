#ifndef WW_ARMV7_FIXTURE_H
#define WW_ARMV7_FIXTURE_H

#include "core/cpu.h"
#include <stdio.h>

typedef struct FixtureAbi {
    u32 pointer_bytes;
    u32 cpu_bytes;
    u32 cpu_abi;
    u32 ram_pointer_offset;
    u32 external_pointer_offset;
} FixtureAbi;

extern const FixtureAbi fixture_abi;
void fixture_print_abi(void);
int fixture_log_open(const char* name, const char* token);
int fixture_log_close(void);
int fixture_publish_report(FILE* report, const char* partial, const char* complete, const char* token);

#endif
