#ifndef WW_NATIVE_CTX_H
#define WW_NATIVE_CTX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WW_NATIVE_RECORD_BYTES 128u
#define WW_NATIVE_HEADER_BYTES 4096u
#define WW_NATIVE_PRE_RECORDS 4096u
#define WW_NATIVE_POST_RECORDS 512u
#define WW_NATIVE_FILE_BYTES 593920u
#define WW_NATIVE_MAX_DEPTH 16u
#define WW_NATIVE_MAGIC 0x37315757u

enum WwNativeStage {
    WW_NATIVE_ENTRY = 1, WW_NATIVE_RETURN, WW_NATIVE_CHUNK, WW_NATIVE_BOUNDARY,
    WW_NATIVE_ARMED, WW_NATIVE_OPEN, WW_NATIVE_HEADER_READ, WW_NATIVE_BODY_READ,
    WW_NATIVE_FAILURE, WW_NATIVE_FP_ARGS, WW_NATIVE_JOURNAL_STATE
};

enum WwNativeSurface {
    WW_NATIVE_READ = 1, WW_NATIVE_WRITE, WW_NATIVE_SPR_READ, WW_NATIVE_SPR_WRITE,
    WW_NATIVE_CACHE, WW_NATIVE_FALLBACK, WW_NATIVE_HOST_CALL, WW_NATIVE_EDGE,
    WW_NATIVE_ALIAS, WW_NATIVE_CHUNK_ENTRY, WW_NATIVE_PREAD,
    WW_NATIVE_FADDS, WW_NATIVE_FCMP, WW_NATIVE_FCTIW, WW_NATIVE_FDIVS,
    WW_NATIVE_FMUL, WW_NATIVE_FMULS, WW_NATIVE_FP_AVAILABLE, WW_NATIVE_FRSP,
    WW_NATIVE_FRSQRTE, WW_NATIVE_FSUB, WW_NATIVE_FSUBS, WW_NATIVE_PSQ_LOAD,
    WW_NATIVE_PSQ_STORE, WW_NATIVE_JOURNAL, WW_NATIVE_OUTER_DISPATCH,
    WW_NATIVE_DISPATCH_ENTER, WW_NATIVE_DISPATCH_RETURN,
    WW_NATIVE_EDGE_ENTER, WW_NATIVE_EDGE_RETURN
};

enum WwNativeFlags {
    WW_NATIVE_PC_VALID = 1u, WW_NATIVE_EXPECTED_PC_VALID = 2u,
    WW_NATIVE_CTX_MISMATCH = 4u, WW_NATIVE_R4_CHANGED = 8u,
    WW_NATIVE_IO_FAILURE = 16u, WW_NATIVE_REENTRANCY = 32u,
    WW_NATIVE_DEPTH_FAILURE = 64u, WW_NATIVE_CONCURRENCY = 128u,
    WW_NATIVE_PAIR_FAILURE = 256u, WW_NATIVE_JOURNAL_CHANGED = 512u
};

/* Offsets are consumed by native_ctx_shims.S; the fixture checks this contract. */
typedef struct {
    uint32_t r[13], sp, lr, apsr;
    uint64_t d[8];
    uint32_t fpscr, target, surface, token, stack[3], wrapper;
} WwNativeSnapshot;

typedef struct {
    uint32_t magic, version, sequence, inverse_sequence, stage, surface, call_id, flags;
    uint32_t target, wrapper, expected_ctx, pc, expected_pc, sp, continuation;
    uint32_t r[13], stack[3], checksum;
} WwNativeRecord;

typedef int (*WwNativeWriteAt)(void* user, uint32_t offset, const void* bytes, uint32_t size);

void ww_native_trace_init(void* host, void* cpu, const uint32_t* pc,
                          WwNativeWriteAt write_at, void* user, uint32_t thread);
void ww_native_trace_arm(uint32_t module_id);
void ww_native_trace_marker(uint32_t stage, uint32_t first, uint32_t second, uint32_t third);
void ww_native_trace_enter(WwNativeSnapshot* snapshot);
void ww_native_trace_return(const WwNativeSnapshot* entry, const WwNativeSnapshot* returned);
void ww_native_trace_boundary(const void* pointer, uint32_t surface);
void ww_native_trace_failure(uint32_t flags);
void ww_native_trace_end(void);
uint32_t ww_native_trace_failures(void);
bool ww_native_record_valid(const WwNativeRecord* record);
uint32_t ww_native_thread_id(void);
int ww_native_failure_notify(uint32_t flags);

#if WW05_OBSERVE_NATIVE_CTX
#include "core/cpu.h"
void ww_native_install(void* host, CPUState* cpu);
void ww_native_close(void);
void ww_native_arm_title(uint32_t module_id);
void ww_native_dvd_open(bool entry_only, bool fast, const char* path, int32_t entry,
                        uint32_t file_info, bool success);
void ww_native_dvd_read(uint32_t file_info, uint32_t length);
extern void* ww_native_targets[8];
u64 ww_native_read(CPUState*, u32, u8);
void ww_native_write(CPUState*, u32, u64, u8);
u32 ww_native_spr_read(CPUState*, u16, u32);
void ww_native_spr_write(CPUState*, u16, u32, u32);
void ww_native_cache(CPUState*, u8, u32, u32);
void ww_native_fallback(CPUState*, u32, u32);
bool ww_native_host_call(CPUState*, u32);
#endif

#endif
