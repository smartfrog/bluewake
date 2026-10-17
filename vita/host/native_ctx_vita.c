#include "native_ctx.h"

#include "boot_host.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

_Static_assert(offsetof(CPUState, external_read) == 3400u, "inventory callback ABI");
_Static_assert(offsetof(CPUState, instruction_fallback) == 3416u, "inventory fallback ABI");
_Static_assert(offsetof(CPUState, spr_read) == 3464u, "inventory SPR ABI");
_Static_assert(offsetof(CPUState, cache_control) == 3472u, "inventory cache ABI");

void* ww_native_targets[8];
static SceUID trace_fd = -1;
static bool armed;
static WwHost* native_host;
static int32_t logo_entry = -1;
static uint32_t logo_file;
extern void ww_native_journal(u32, u32, void*);
void __real_ppc_set_mem_write_journal(PPCMemWriteJournal, void*);

uint32_t ww_native_thread_id(void) {
    return (uint32_t)sceKernelGetThreadId();
}

static int write_at(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    if (trace_fd < 0 || offset > WW_NATIVE_FILE_BYTES || size > WW_NATIVE_FILE_BYTES - offset ||
        sceIoLseek(trace_fd, offset, SCE_SEEK_SET) != offset)
        return -1;
    return sceIoWrite(trace_fd, bytes, size);
}

static void event(const char* name) {
    struct timeval now;
    if (gettimeofday(&now, NULL) != 0 ||
        fprintf(stderr, "[native-ctx] event=%s us=%llu fd=%d flags=%u\n", name,
                (unsigned long long)now.tv_sec * 1000000u + (unsigned long long)now.tv_usec,
                trace_fd, ww_native_trace_failures()) < 0)
        ww_native_trace_failure(WW_NATIVE_IO_FAILURE);
}

int ww_native_failure_notify(uint32_t flags) {
    struct timeval now = {0};
    if (gettimeofday(&now, NULL) != 0)
        flags |= WW_NATIVE_IO_FAILURE;
    const int written = fprintf(stderr, "[native-ctx] event=failure us=%llu fd=%d flags=%u\n",
                                (unsigned long long)now.tv_sec * 1000000u + (unsigned long long)now.tv_usec,
                                trace_fd, flags);
    if ((flags & WW_NATIVE_CTX_GUARD) != 0u) {
        if (fprintf(stderr, "[ctx-guard] event=stop hits=%u\n", WW_NATIVE_CTX_GUARD_HITS + 1u) < 0)
            return -1;
        if (native_host != NULL)
            ww_host_fail(native_host, "native-context guard limit", 0u, native_host->cpu.pc);
    }
    return written;
}

int ww_native_guard_marker(uint32_t hit, uint32_t pc, uint32_t live, uint32_t seen,
                           uint32_t lr, uint32_t sp) {
    return fprintf(stderr, "[ctx-guard] hit=%u pc=%08X seen=%08X live=%08X lr=%08X sp=%08X\n",
                   hit, pc, seen, live, lr, sp);
}

void __wrap_ppc_set_mem_write_journal(PPCMemWriteJournal callback, void* user) {
    ww_native_targets[7] = (void*)callback;
    __real_ppc_set_mem_write_journal(callback != NULL ? ww_native_journal : NULL, user);
}

void ww_native_install(void* host, CPUState* cpu) {
    armed = false;
    native_host = host;
    logo_entry = -1;
    logo_file = 0u;
    trace_fd = sceIoOpen("ux0:data/bluewake-ww-fixture/ww117-native-ctx.bin",
                         SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0600);
    ww_native_trace_init(host, cpu, &cpu->pc, write_at, NULL, ww_native_thread_id());
    ww_native_targets[0] = (void*)cpu->external_read;
    ww_native_targets[1] = (void*)cpu->external_write;
    ww_native_targets[2] = (void*)cpu->spr_read;
    ww_native_targets[3] = (void*)cpu->spr_write;
    ww_native_targets[4] = (void*)cpu->cache_control;
    ww_native_targets[5] = (void*)cpu->instruction_fallback;
    ww_native_targets[6] = (void*)cpu->host_call;
    cpu->external_read = ww_native_read;
    cpu->external_write = ww_native_write;
    cpu->spr_read = ww_native_spr_read;
    cpu->spr_write = ww_native_spr_write;
    cpu->cache_control = ww_native_cache;
    cpu->instruction_fallback = ww_native_fallback;
    cpu->host_call = ww_native_host_call;
    if (g_mem_write_journal != NULL)
        __wrap_ppc_set_mem_write_journal(g_mem_write_journal, g_mem_write_journal_user);
    event("ready");
}

void ww_native_arm_title(uint32_t module_id) {
    if (module_id != 400u || armed)
        return;
    armed = true;
    event("armed");
    if (g_mem_write_journal != NULL && g_mem_write_journal != ww_native_journal)
        ww_native_trace_failure(WW_NATIVE_JOURNAL_CHANGED);
    ww_native_trace_arm(module_id);
    ww_native_trace_marker(WW_NATIVE_JOURNAL_STATE, (uint32_t)(uintptr_t)g_mem_write_journal,
                           (uint32_t)(uintptr_t)ww_native_targets[7], g_mem_write_journal == NULL);
}

void ww_native_dvd_open(bool entry_only, bool fast, const char* path, int32_t entry,
                        uint32_t file_info, bool success) {
    if (!armed)
        return;
    const bool logo = !fast && strcmp(path, "/res/Object/TlogoE.arc") == 0;
    if (entry_only) {
        if (logo && entry >= 0)
            logo_entry = entry;
        return;
    }
    if (success && (logo || (fast && entry >= 0 && entry == logo_entry))) {
        logo_file = file_info;
        event("tlogoe-open");
        ww_native_trace_marker(WW_NATIVE_OPEN, file_info, (uint32_t)entry, 0u);
    } else if (file_info == logo_file) {
        logo_file = 0u;
    }
}

void ww_native_dvd_read(uint32_t file_info, uint32_t length) {
    if (!armed || logo_file == 0u || file_info != logo_file || (length != 32u && length != 382080u))
        return;
    event(length == 32u ? "tlogoe-header" : "tlogoe-body");
    ww_native_trace_marker(length == 32u ? WW_NATIVE_HEADER_READ : WW_NATIVE_BODY_READ, file_info, length, 0u);
}

void ww_native_close(void) {
    ww_native_trace_end();
    if (trace_fd >= 0 && sceIoClose(trace_fd) < 0)
        ww_native_trace_failure(WW_NATIVE_IO_FAILURE);
    trace_fd = -1;
}
