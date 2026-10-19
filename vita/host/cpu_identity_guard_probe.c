#include "probe_support.h"
#include "guest_ram_guard.h"

#include <stdbool.h>
#include <stdlib.h>

/* The game-free tests build has no module, so the probe owns the adoption
 * slot the guard checks (cpu_identity_guard.c reads g_bw_guest_cpu). */
CPUState* g_bw_guest_cpu;

static int s_host_call_invoked;
static int s_spr_read_invoked;
static int s_fallback_invoked;

static bool probe_host_call(CPUState* cpu, u32 address) {
    if (cpu->lr == 0x802CD120u && address == 0x80123456u)
        s_host_call_invoked++;
    return true;
}

static u32 probe_spr_read(CPUState* cpu, u16 spr, u32 cia) {
    if (cpu->lr == 0x802CD120u && spr == 268u && cia == 0x802CD580u)
        s_spr_read_invoked++;
    return 0x12345678u;
}

static void probe_fallback(CPUState* cpu, u32 raw, u32 cia) {
    if (cpu->lr == 0x802CD120u && raw == 0x80123456u && cia == 0x802CD580u)
        s_fallback_invoked++;
}

bool __wrap_ppc_host_call(CPUState* cpu, u32 address);
u32 __wrap_ppc_mftb(CPUState* cpu, u16 tbr, u32 cia);
void __wrap_ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia);

static WwHost* probe_host(void) {
    WwHost* host = calloc(1u, sizeof(*host));
    assert(host != NULL && ww_host_init(host));
    CPUState* cpu = &host->cpu;
    cpu->gpr[3] = 0x80123456u;
    cpu->pc = 0x802CD580u;
    cpu->lr = 0x802CD120u;
    cpu->host_call = probe_host_call;
    cpu->spr_read = probe_spr_read;
    cpu->instruction_fallback = probe_fallback;
    return host;
}

static void report(const char* tag, int invoked) {
    fprintf(stderr, "[%s] invoked=%d\n", tag, invoked);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    WwHost* host = probe_host();
    CPUState* cpu = &host->cpu;
    const u32 address = cpu->gpr[3];

    if (strcmp(scenario, "host-call-pass") == 0) {
        g_bw_guest_cpu = cpu;
        assert(__wrap_ppc_host_call(cpu, address));
        report("host_call", s_host_call_invoked);
        return 0;
    }
    if (strcmp(scenario, "host-call-twice") == 0) {
        g_bw_guest_cpu = cpu;
        assert(__wrap_ppc_host_call(cpu, address));
        assert(__wrap_ppc_host_call(cpu, address));
        report("host_call", s_host_call_invoked);
        return 0;
    }
    if (strcmp(scenario, "mftb-pass") == 0) {
        g_bw_guest_cpu = cpu;
        assert(__wrap_ppc_mftb(cpu, 268u, cpu->pc) == 0x12345678u);
        report("spr_read", s_spr_read_invoked);
        return 0;
    }
    if (strcmp(scenario, "fallback-pass") == 0) {
        g_bw_guest_cpu = cpu;
        __wrap_ppc_fallback_instruction(cpu, address, cpu->pc);
        report("instruction_fallback", s_fallback_invoked);
        return 0;
    }
    if (strcmp(scenario, "host-call-unadopted") == 0) {
        /* Before adoption the guard must not interfere: a host that owns its
         * own state reaches its own callback unchanged. */
        CPUState* owned = calloc(1u, sizeof(*owned));
        assert(owned != NULL);
        owned->lr = 0x802CD120u;
        owned->host_call = probe_host_call;
        assert(__wrap_ppc_host_call(owned, address));
        report("host_call", s_host_call_invoked);
        return 0;
    }
    if (strcmp(scenario, "host-call-wrong-ctx") == 0) {
        g_bw_guest_cpu = cpu;
        __wrap_ppc_host_call((CPUState*)((char*)cpu + 4), address);
        fprintf(stderr, "unreachable: wrong context returned\n");
        return 1;
    }
    if (strcmp(scenario, "mftb-wrong-ctx") == 0) {
        g_bw_guest_cpu = cpu;
        __wrap_ppc_mftb((CPUState*)((char*)cpu + 4), 268u, cpu->pc);
        fprintf(stderr, "unreachable: wrong context returned\n");
        return 1;
    }
    if (strcmp(scenario, "fallback-wrong-ctx") == 0) {
        g_bw_guest_cpu = cpu;
        __wrap_ppc_fallback_instruction((CPUState*)((char*)cpu + 4), address, cpu->pc);
        fprintf(stderr, "unreachable: wrong context returned\n");
        return 1;
    }
    fprintf(stderr, "unknown scenario %s\n", scenario);
    return 2;
}
