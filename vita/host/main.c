#define _POSIX_C_SOURCE 200809L
#include "boot_host.h"
#include "ww05_identity.h"
#include "diagnostic_io.h"
#include "fixture.h"

#include <stdlib.h>

#ifdef __vita__
#define WW05_DIRECTORY "ux0:data/bluewake-ww-fixture/"
#define WW05_PLATFORM "VITA"
#define WW05_DOL_PATH "app0:main.dol"
#elif defined(WW05_IO_TEST)
#define WW05_DIRECTORY ""
#define WW05_PLATFORM "HOST"
#define WW05_DOL_PATH "synthetic.dol"
#else
#error "Real game execution is Vita-only and requires separate authorization"
#endif

int fixture_run(int argc, char** argv) {
    (void)argv;
    char token[33], source[41], package[65];
    const char* partial = WW05_DIRECTORY "ww05.partial";
    const char* complete = WW05_DIRECTORY "ww05.report";
    if (argc > 1 || diagnostic_request(WW05_DIRECTORY "ww05.request", WW05_SOURCE, token, source, package) ||
        !diagnostic_absent(partial) || !diagnostic_absent(complete) ||
        !diagnostic_absent(WW05_DIRECTORY "ww05.log"))
        return 1;
    if (fixture_log_open("ww05"))
        return 1;
    FILE* report = fopen(partial, "wx");
    if (report == NULL)
        return 1;
    int error = fprintf(report, "WW05 %s %s %s method=startup-gx-v1 phase=guest platform=%s\n",
                        token, source, package, WW05_PLATFORM) < 0;
    if (error) {
        const int closed = fclose(report);
        const int logs = fixture_log_close();
        return closed != 0 || logs || error;
    }
    WwHost* host = calloc(1u, sizeof(*host));
    if (host == NULL) {
        error |= fprintf(report, "STOP reason=host allocation failed\nEND frontier=FAIL live=0 rendered=0\n") < 0;
        error |= fixture_log_close();
        if (error) {
            const int closed = fclose(report);
            return closed != 0 || error;
        }
        const int published = fixture_publish_report(report, partial, complete);
        return published != 0 ? published : 1;
    }
    const StaticRecompModuleDesc* module = staticrecomp_get_module();
    u32 count;
    const BlueWakeRelData* data = staticrecomp_get_rel_data(&count);
    bool initialized = ww_host_init(host);
    bool loaded = initialized && ww_host_load(host, module, data, count, WW05_DOL_PATH);
    if (!initialized)
        ww_host_fail(host, "CPU/VM/cache allocation failed", 0u, 0u);
    error |= fprintf(report, "ABI pointer=%u cpu=%u version=%u\n", fixture_abi.pointer_bytes,
                     fixture_abi.cpu_bytes, fixture_abi.cpu_abi) < 0;
    error |= fprintf(report, "ALLOC host=%u mem1=%u vm=%u cache=%u aliases=%llu alias_count=%u owned_live=%llu\n",
                     (unsigned)sizeof(*host), host->cpu.ram != NULL ? host->cpu.ram_size : 0u,
                     host->memory.vm != NULL ? host->memory.config.vm_size : 0u,
                     host->memory.locked_cache != NULL ? host->memory.config.locked_cache_size : 0u,
                     (unsigned long long)host->alias_bytes, host->alias_count,
                     (unsigned long long)(sizeof(*host) + host->backing_bytes + host->alias_bytes)) < 0;
    error |= fprintf(report, "UNKNOWN residency native_stack_highwater pool_peak GPU\n") < 0;
    bluewake_set_edge_service(ww_host_edge, host);
    while (loaded && !error && host->error == NULL && !host->edge_stopped) {
        if (ww_host_edge(host, &host->cpu, host->cpu.pc))
            break;
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        const int dispatched = module->dispatch(&host->cpu, host->cpu.pc);
        bluewake_cycle_domain_end_turn(&host->cycles, &host->cpu);
        if (!dispatched)
            ww_host_fail(host, "dispatch rejected guest PC", host->cpu.pc, host->cpu.pc);
        if (host->cpu.exception && host->error == NULL)
            ww_host_fail(host, "guest exception (FP defect remains accepted, never patched)", host->cpu.exception, host->cpu.pc);
        if (host->frontier.complete && !host->edge_stopped)
            ww_host_edge(host, &host->cpu, host->cpu.pc);
    }
    bluewake_set_edge_service(NULL, NULL);
    error |= fprintf(report, "STATE entry=%08X pc=%08X sp=%08X edges=%llu exception=%08X cycles=%llu\n",
                     module->entry_point, host->cpu.pc, host->cpu.gpr[1],
                     (unsigned long long)host->edges, host->cpu.exception,
                     (unsigned long long)host->cycles.absolute_cycles) < 0;
    WwGxCheckpoint* frontier = &host->frontier;
    error |= fprintf(report, "GX_FRONTIER complete=%u opcode=%02X bytes=%u pc=%08X completion_pc=%08X sp=%08X trailing=%llu edge_stop=%u hex=",
                     frontier->complete, frontier->size ? frontier->bytes[0] : 0u, frontier->size,
                     frontier->pc, frontier->completion_pc, frontier->sp,
                     (unsigned long long)frontier->trailing_bytes, host->edge_stopped) < 0;
    for (u32 i = 0u; i < frontier->size; ++i)
        error |= fprintf(report, "%02X", frontier->bytes[i]) < 0;
    error |= fprintf(report, "\nSTOP reason=%s address=%08X pc=%08X\n",
                     host->error != NULL ? host->error : "first complete GX command; no renderer",
                     host->error_address, host->error_pc) < 0;
    bool success = loaded && frontier->complete && host->edge_stopped && host->error == NULL;
    ww_host_shutdown(host);
    free(host);
    error |= fixture_log_close();
    if (!error)
        error = fprintf(report, "END frontier=%s live=0 rendered=0\n", success ? "PASS" : "FAIL") < 0;
    if (error) {
        const int closed = fclose(report);
        return closed != 0 || error;
    }
    return fixture_publish_report(report, partial, complete) || !success;
}
