#define _POSIX_C_SOURCE 200809L
#include "boot_host.h"
#include "gxruntime/aram.h"
#include "ww05_identity.h"
#include "diagnostic_io.h"
#include "fixture.h"

#include <stdlib.h>

#ifdef __vita__
#define WW05_PLATFORM "VITA"
#define WW05_DOL_PATH "app0:main.dol"
#elif defined(WW05_IO_TEST)
#define WW05_PLATFORM "HOST"
#define WW05_DOL_PATH "synthetic.dol"
WwRendererBackend* ww05_io_renderer_backend(void);
#else
#error "Real game execution is Vita-only and requires separate authorization"
#endif

static bool log_handler_boundary(const WwHost* host, const char* stage) {
    if (host->cpu.pc < 0x803077D0u || host->cpu.pc >= 0x803077ECu ||
        !ww_host_trace_syscall(host->syscall_resumes))
        return false;
    return fprintf(stderr, "[handler-boundary] stage=%s pc=%08X cycles=%llu exception=%08X msr=%08X hid0=%08X downcount=%lld budget=%lld dec=%08X pending=%u tb=%llu resume=%llu srr0=%08X\n",
                   stage, host->cpu.pc, (unsigned long long)host->cycles.absolute_cycles,
                   host->cpu.exception, host->cpu.msr, host->hid0,
                   (long long)host->cpu.downcount, (long long)host->cpu.cycle_budget,
                   host->decrementer, host->decrementer_pending,
                   (unsigned long long)host->cpu.timebase,
                   (unsigned long long)host->syscall_resumes, host->cpu.srr0) < 0;
}

static int dispatch_marker(char marker) {
    FILE* stream = fopen(WW05_DIRECTORY "ww05.dispatch", marker == 'L' ? "wx" : "a");
    if (stream == NULL)
        return 1;
    int error = fprintf(stream, "%c", marker) < 0;
    error |= fflush(stream) != 0 || ferror(stream) != 0;
    error |= fclose(stream) != 0;
    return error;
}

static int requested_limit(const char* path, u32 maximum, const char* token, const char* source,
                           const char* package, u32* limit) {
    *limit = 0u;
    if (diagnostic_absent(path))
        return 0;
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
        return 1;
    FILE* stream = fopen(path, "r");
    if (stream == NULL)
        return 1;
    char content[160] = {0}, expected[160];
    const size_t size = fread(content, 1u, sizeof(content) - 1u, stream);
    int error = ferror(stream) != 0 || !feof(stream);
    error |= fclose(stream) != 0;
    unsigned count = 0u;
    if (error || strlen(content) != size || sscanf(content, "%*32s %*40s %*64s %u", &count) != 1 ||
        count == 0u || count > maximum)
        return 1;
    if (snprintf(expected, sizeof(expected), "%s %s %s %u\n", token, source, package, count) < 0 ||
        strcmp(content, expected) != 0)
        return 1;
    *limit = count;
    return 0;
}

int fixture_run(int argc, char** argv) {
    (void)argv;
    char token[33], source[41], package[65];
    u32 command_log_limit;
    u32 memory_capture_limit;
    u32 memory_start_command;
    u32 memory_end_command;
    u32 deferred_audio;
    const char* partial = WW05_DIRECTORY "ww05.partial";
    const char* complete = WW05_DIRECTORY "ww05.report";
    if (argc > 1 || diagnostic_request(WW05_DIRECTORY "ww05.request", WW05_SOURCE, token, source, package) ||
        requested_limit(WW05_DIRECTORY "ww05.continue", WW_GX_MAX_JOURNAL_COMMANDS,
                        token, source, package, &command_log_limit) ||
        requested_limit(WW05_DIRECTORY "ww05.mem1", WW_MEM1_MAX_CAPTURES,
                         token, source, package, &memory_capture_limit) ||
        requested_limit(WW05_DIRECTORY "ww05.mem1-start", WW_GX_MAX_JOURNAL_COMMANDS,
                         token, source, package, &memory_start_command) ||
        requested_limit(WW05_DIRECTORY "ww05.mem1-end", WW_GX_MAX_JOURNAL_COMMANDS,
                         token, source, package, &memory_end_command) ||
        requested_limit(WW05_DIRECTORY "ww05.audio", 1u, token, source, package, &deferred_audio) ||
        (deferred_audio != 0u && command_log_limit == 0u) ||
        (memory_capture_limit != 0u && command_log_limit == 0u) ||
        ((memory_start_command == 0u) != (memory_end_command == 0u)) ||
        (memory_start_command != 0u && (memory_capture_limit == 0u || memory_start_command > memory_end_command ||
                                      memory_end_command > command_log_limit)) ||
        !diagnostic_absent(partial) || !diagnostic_absent(complete) ||
        !diagnostic_absent(WW05_DIRECTORY "ww05.log") ||
        !diagnostic_absent(WW05_DIRECTORY "ww05.dispatch"))
        return 1;
    if (fixture_log_open("ww05"))
        return 1;
    if (setvbuf(stderr, NULL, _IONBF, 0) != 0)
        return fixture_log_close() != 0 ? 2 : 1;
    FILE* report = fopen(partial, "wx");
    if (report == NULL)
        return 1;
    int error = fprintf(report, "WW05 %s %s %s method=startup-gx-v1 phase=guest platform=%s%s\n",
                          token, source, package, WW05_PLATFORM, deferred_audio ? " audio=deferred" : "") < 0;
    error |= fflush(report) != 0 || ferror(report) != 0;
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
    error |= fprintf(stderr, "[host] stage=initialize\n") < 0;
    bool initialized = !error && ww_host_init(host);
#if WW05_ENABLE_TITLE_CONTRACT
    if (initialized) {
        host->module = module;
        host->rel_data = data;
        host->rel_data_count = count;
    }
#endif
#if WW05_ENABLE_SHIP_PROLOG
    if (initialized) {
        const char* contract_error = ww_host_enable_ship_contract(host, staticrecomp_get_ship_contract_v1(), WW05_DOL_PATH);
        if (contract_error != NULL) {
            ww_host_fail(host, contract_error, 0u, host->cpu.pc);
            initialized = false;
        }
    }
#endif
#if WW05_ENABLE_TITLE_CONTRACT
    if (initialized) {
        const char* contract_error = ww_host_register_rel_contracts(host, staticrecomp_get_rel_init_contracts_v1(), WW05_DOL_PATH);
        if (contract_error != NULL) {
            ww_host_fail(host, contract_error, 0u, host->cpu.pc);
            initialized = false;
        } else {
            host->rel_title_admission = true;
        }
    }
#endif
#if WW05_ENABLE_IKADA_CONTRACT
    if (initialized) {
        const char* contract_error = ww_host_enable_ikada_contract(host);
        if (contract_error != NULL) {
            ww_host_fail(host, contract_error, 0u, host->cpu.pc);
            initialized = false;
        }
    }
#endif
#if WW05_ENABLE_ACTOR_BATCH_CONTRACTS
    if (initialized) {
        const char* contract_error = ww_host_enable_actor_batch_contracts(host);
        if (contract_error != NULL) {
            ww_host_fail(host, contract_error, 0u, host->cpu.pc);
            initialized = false;
        }
    }
#endif
#if defined(__vita__) && WW05_ENABLE_PAD_INPUT
    if (initialized && ww_pad_vita_enable(host) < 0) {
        const int formatted = snprintf(host->pad_error, sizeof(host->pad_error),
                                       "controller analog initialization failed: result=%08X",
                                       (unsigned)host->pad_source_result);
        ww_host_fail(host, formatted < 0 ? "controller init error formatting failed" : host->pad_error,
                     0u, host->cpu.pc);
        initialized = false;
    }
#endif
    error |= fprintf(stderr, "[pad-input] build_opt_in=%u active=%u source=%s sampling=guest-SI-only init_result=%08X\n",
                     WW05_ENABLE_PAD_INPUT, host->pad_read != NULL,
                     host->pad_read != NULL ? "sceCtrlPeekBufferPositive" : "empty-legacy",
                     (unsigned)host->pad_source_result) < 0;
    host->dsp.audio_deferred = deferred_audio != 0u;
    error |= fprintf(stderr, "[audio-policy] audio=%s pcm_output=none deferred_task_body=no-op synthetic_mail=%s\n",
                     deferred_audio ? "deferred" : "strict", deferred_audio ? "explicit-opt-in-bootstrap-setup-control" : "disabled") < 0;
    host->stream.enabled = command_log_limit != 0u;
    host->stream.log_limit = command_log_limit;
    host->mem1_capture = (WwMem1Capture){.limit = memory_capture_limit,
                                        .start_command = memory_start_command, .end_command = memory_end_command, .token = token,
                                        .source = source, .package = package};
    if (initialized)
        error |= fprintf(stderr, "[vi-clock] enabled=%u period_cycles=%llu refresh_hz=%u\n",
                         host->vi_clock_enabled,
                         (unsigned long long)dol_vi_clock_work_units_per_retrace(&host->vi_clock),
                         dol_vi_clock_refresh_hz(&host->vi_clock)) < 0;
    // VITA-WW-17: attach the renderer to the captured stream over the same MEM1
    // view the pinned trace resolves against. A create failure is fatal: the run
    // must not continue with no renderer while claiming to render.
    if (initialized) {
        WwRendererConfig renderer_config = {.memory = &host->memory, .cpu = &host->cpu};
        WwRendererBackend* backend = NULL;
#ifdef WW05_IO_TEST
        backend = ww05_io_renderer_backend();
#endif
        const char* renderer_error = ww_renderer_create(&host->renderer, &renderer_config, backend);
        if (renderer_error == NULL)
            renderer_error = ww_host_enable_frame_capture(host);
        if (renderer_error != NULL) {
            error |= fprintf(stderr, "[renderer] init failed: %s\n", renderer_error) < 0;
            ww_host_fail(host, renderer_error, 0u, host->cpu.pc);
        }
    }
    error |= fprintf(stderr, "[host] stage=load initialized=%u\n", initialized) < 0;
    bool loaded = !error && initialized && ww_host_load(host, module, data, count, WW05_DOL_PATH);
    error |= fprintf(stderr, "[host] stage=loaded loaded=%u pc=%08X\n", loaded, host->cpu.pc) < 0;
    if (!initialized)
        ww_host_fail(host, "CPU/VM/cache/ARAM allocation or ownership failed", 0u, 0u);
    error |= fprintf(report, "ABI pointer=%u cpu=%u version=%u\n", fixture_abi.pointer_bytes,
                     fixture_abi.cpu_bytes, fixture_abi.cpu_abi) < 0;
    error |= fprintf(report, "ALLOC host=%u mem1=%u vm=%u cache=%u code_snapshot=%llu aram=%u aliases=%llu alias_count=%u owned_live=%llu\n",
                     (unsigned)sizeof(*host), host->cpu.ram != NULL ? host->cpu.ram_size : 0u,
                     host->memory.vm != NULL ? host->memory.config.vm_size : 0u,
                      host->memory.locked_cache != NULL ? host->memory.config.locked_cache_size : 0u,
                       (unsigned long long)host->code_guard.bytes,
                      host->aram_owned ? ARAM_SIZE : 0u,
                     (unsigned long long)host->alias_bytes, host->alias_count,
                      (unsigned long long)(sizeof(*host) + host->backing_bytes + host->code_guard.bytes + host->alias_bytes)) < 0;
    error |= fprintf(report, "UNKNOWN residency native_stack_highwater pool_peak GPU\n") < 0;
    error |= fflush(report) != 0 || ferror(report) != 0;
    bluewake_set_edge_service(ww_host_edge, host);
    u64 turns = 0u;
    u64 next_progress = 0u;
    bool traced_dispatch = false;
    while (loaded && !error && host->error == NULL && !host->edge_stopped) {
        if (log_handler_boundary(host, "edge-enter")) {
            error = 1;
            break;
        }
        const bool intercepted = ww_host_edge(host, &host->cpu, host->cpu.pc);
        if (log_handler_boundary(host, "edge-return")) {
            error = 1;
            break;
        }
        if (intercepted) {
            if (host->edge_stopped)
                break;
            continue;
        }
        if (log_handler_boundary(host, "turn-enter")) {
            error = 1;
            break;
        }
        bluewake_cycle_domain_begin_turn(&host->cycles, &host->cpu);
        if (log_handler_boundary(host, "turn-begun")) {
            error = 1;
            break;
        }
        bluewake_cycle_domain_prepare_dispatch(&host->cycles, &host->cpu);
        if (log_handler_boundary(host, "dispatch-ready")) {
            error = 1;
            break;
        }
        const bool probe_dispatch = !traced_dispatch && host->cpu.pc == 0x803077D4u;
        if (probe_dispatch && dispatch_marker('L')) {
            error = 1;
            break;
        }
        const bool trace_dispatch = probe_dispatch || turns < 8u || (turns + 1u) % 1000u == 0u;
        if (!WW05_QUIET_LOGGING && trace_dispatch && fprintf(stderr, "[dispatch] stage=enter turn=%llu pc=%08X cycles=%llu\n",
                                      (unsigned long long)(turns + 1u), host->cpu.pc,
                                      (unsigned long long)host->cycles.absolute_cycles) < 0) {
            error = 1;
            break;
        }
        if (probe_dispatch) {
            if (dispatch_marker('E')) {
                error = 1;
                break;
            }
            traced_dispatch = true;
            host->dispatch_trace_remaining = WW_EDGE_TRACE_BUDGET;
        }
        const int dispatched = ww_host_dispatch(host, host->cpu.pc);
        error |= dispatched < 0;
        if (probe_dispatch) {
            host->dispatch_trace_remaining = 0u;
            if (dispatch_marker('R')) {
                error = 1;
                break;
            }
        }
        ww_host_end_dispatch(host);
        if (dispatched == 1 && !error && host->error == NULL)
            ww_host_dvd_complete(host);
        ++turns;
        if (trace_dispatch && (!WW05_QUIET_LOGGING || dispatched <= 0 || error ||
                               host->error != NULL || host->cpu.exception != 0u))
            error |= fprintf(stderr, "[dispatch] stage=return turn=%llu pc=%08X cycles=%llu exception=%08X\n",
                             (unsigned long long)turns, host->cpu.pc,
                             (unsigned long long)host->cycles.absolute_cycles, host->cpu.exception) < 0;
        error |= log_handler_boundary(host, "returned");
        if (host->cycles.absolute_cycles >= next_progress || (turns & 0xFFFFu) == 0u) {
            if (!WW05_QUIET_LOGGING) {
                u64 wall_us = 0u;
                error |= ww_host_wall_us(&wall_us) != NULL;
                if (!error)
                    error |= fprintf(stderr, "[progress] turns=%llu edges=%llu cycles=%llu pc=%08X msr=%08X srr0=%08X exception=%08X resume=%llu us=%llu\n",
                                     (unsigned long long)turns, (unsigned long long)host->edges,
                                     (unsigned long long)host->cycles.absolute_cycles,
                                      host->cpu.pc, host->cpu.msr, host->cpu.srr0, host->cpu.exception,
                                      (unsigned long long)host->syscall_resumes,
                                      (unsigned long long)wall_us) < 0;
            }
            next_progress = host->cycles.absolute_cycles + 65536u;
        }
        if (!dispatched)
            ww_host_fail(host, "dispatch rejected guest PC", host->cpu.pc, host->cpu.pc);
        if (host->cpu.exception && (host->error != NULL || !ww_host_resume_exception(host))) {
            CPUState* cpu = &host->cpu;
            error |= fprintf(stderr, "[exception] bits=%08X pc=%08X srr0=%08X srr1=%08X msr=%08X lr=%08X r0=%08X r3=%08X r4=%08X r12=%08X fpscr=%08X program=%08X pi_cause=%08X pi_mask=%08X\n",
                              cpu->exception, cpu->pc, cpu->srr0, cpu->srr1, cpu->msr,
                              cpu->lr, cpu->gpr[0], cpu->gpr[3], cpu->gpr[4], cpu->gpr[12],
                              cpu->fpscr, cpu->program_exception,
                              host->interrupts.pi_cause, host->interrupts.pi_mask) < 0;
            const u32 vector = cpu->pc >= GC_RAM_BASE ? cpu->pc - GC_RAM_BASE : cpu->pc;
            if (vector <= cpu->ram_size && 256u <= cpu->ram_size - vector) {
                error |= fprintf(stderr, "[exception-vector] address=%08X bytes=256 hex=", cpu->pc) < 0;
                for (u32 offset = 0u; offset < 256u; ++offset)
                    error |= fprintf(stderr, "%02X", cpu->ram[vector + offset]) < 0;
                error |= fprintf(stderr, "\n") < 0;
            }
            ww_host_fail(host, "unhandled guest exception", cpu->exception, cpu->pc);
        }
        if (host->frontier.complete && !host->stream.enabled && !host->edge_stopped)
            ww_host_edge(host, &host->cpu, host->cpu.pc);
        error |= log_handler_boundary(host, "checked");
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
    error |= fprintf(report, "\n") < 0;
    if (host->stream.enabled)
        error |= fprintf(report, "GX_STREAM mode=continue decoded=%llu logged=%u limit=%u pending=%u read_bytes=%llu\n",
                         (unsigned long long)host->stream.decoded, host->stream.logged, host->stream.log_limit,
                         host->stream.command.size, (unsigned long long)host->fifo_capture.total_read_bytes) < 0;
    error |= fprintf(report, "STOP reason=%s address=%08X pc=%08X",
                      host->error != NULL ? host->error : "first complete GX command; no renderer",
                      host->error_address, host->error_pc) < 0;
    if (host->error_width != 0u)
        error |= fprintf(report, " width=%u value=%016llX", host->error_width,
                         (unsigned long long)host->error_value) < 0;
    error |= fprintf(report, "\n") < 0;
    // VITA-WW-17: what the renderer saw and showed. The line sits between STOP
    // and END so the strict report records keep their positions.
    WwRendererStats renderer_stats = {0};
    ww_renderer_stats(host->renderer, &renderer_stats);
    error |= fprintf(report, "RENDER fifo_bytes=%llu events=%llu plans=%llu plans_skipped=%llu "
                             "draws=%llu programs=%llu programs_distinct=%llu programs_refused=%llu "
                             "copies=%llu copies_refused=%llu display_copies=%llu presented=%llu gaps=%u\n",
                     (unsigned long long)renderer_stats.fifo_bytes, (unsigned long long)renderer_stats.events,
                     (unsigned long long)renderer_stats.plans, (unsigned long long)renderer_stats.plans_skipped,
                     (unsigned long long)renderer_stats.draws, (unsigned long long)renderer_stats.programs,
                     (unsigned long long)renderer_stats.programs_distinct,
                     (unsigned long long)renderer_stats.programs_refused,
                     (unsigned long long)renderer_stats.copies,
                     (unsigned long long)renderer_stats.copies_refused,
                     (unsigned long long)renderer_stats.display_copies,
                     (unsigned long long)renderer_stats.presented, renderer_stats.gap_count) < 0;
    // The kept refusal and skip reasons plus the nonzero gap names. Diagnostic
    // output only: the counters above already gate the finish and nothing here
    // changes a decision. The records sit between RENDER and END.
    for (u32 at = 0u; at < renderer_stats.skip_count; ++at)
        error |= fprintf(report, "RENDER-REFUSAL group=plan reason=%s\n",
                         renderer_stats.skip_reasons[at] != NULL ? renderer_stats.skip_reasons[at] : "unknown") < 0;
    for (u32 at = 0u; at < renderer_stats.refusal_count; ++at)
        error |= fprintf(report, "RENDER-REFUSAL group=shader reason=%s\n",
                         renderer_stats.refusal_reasons[at] != NULL ? renderer_stats.refusal_reasons[at] : "unknown") < 0;
    for (u32 at = 0u; at < renderer_stats.copy_refusal_count; ++at)
        error |= fprintf(report, "RENDER-REFUSAL group=copy reason=%s\n",
                         renderer_stats.copy_refusal_reasons[at] != NULL ? renderer_stats.copy_refusal_reasons[at] : "unknown") < 0;
    for (u32 at = 0u; at < renderer_stats.gap_count; ++at)
        error |= fprintf(report, "RENDER-GAP name=%s value=%llu\n",
                         renderer_stats.gaps[at].name != NULL ? renderer_stats.gaps[at].name : "unknown",
                         (unsigned long long)renderer_stats.gaps[at].value) < 0;
    const bool success = loaded && frontier->complete &&
        (host->stream.enabled || (host->edge_stopped && host->error == NULL));
    const bool failed = !success || host->error != NULL;
    ww_host_shutdown(host);
    free(host);
    error |= fixture_log_close();
    if (!error)
        error = fprintf(report, "END frontier=%s live=0 rendered=%llu\n", success ? "PASS" : "FAIL",
                        (unsigned long long)renderer_stats.presented) < 0;
    if (error) {
        const int closed = fclose(report);
        return closed != 0 || error;
    }
    return fixture_publish_report(report, partial, complete) || failed;
}
