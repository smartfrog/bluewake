#include "native_ctx.h"

/* Native lifecycle integration only; ARM capture is tested by the assembled QEMU fixture. */
static PPCHostCall original_host_call;

static int discard(void* user, uint32_t offset, const void* bytes, uint32_t size) {
    (void)user;
    (void)offset;
    (void)bytes;
    return (int)size;
}

uint32_t ww_native_thread_id(void) { return 1u; }
int ww_native_failure_notify(uint32_t flags) { (void)flags; return 0; }

bool ww_native_host_call(CPUState* cpu, u32 address) {
    return original_host_call(cpu, address);
}

void ww_native_install(void* host, CPUState* cpu) {
    original_host_call = cpu->host_call;
    cpu->host_call = ww_native_host_call;
    ww_native_trace_init(host, cpu, &cpu->pc, discard, NULL, 1u);
}

void ww_native_close(void) { ww_native_trace_end(); }
void ww_native_arm_title(uint32_t id) { ww_native_trace_arm(id); }
void ww_native_dvd_open(bool entry, bool fast, const char* path, int32_t id, uint32_t file, bool success) {
    (void)entry; (void)fast; (void)path; (void)id; (void)file; (void)success;
}
void ww_native_dvd_read(uint32_t file, uint32_t length) { (void)file; (void)length; }
