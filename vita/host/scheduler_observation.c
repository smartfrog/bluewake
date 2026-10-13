#include "boot_host.h"
#include "scheduler_contract.h"

static bool real_mem1(CPUState* cpu, u32 address, u32 size) {
    const u32 offset = address - GC_RAM_BASE;
    return (address & 3u) == 0u && cpu->ram != NULL && offset <= cpu->ram_size &&
           size <= cpu->ram_size - offset && get_ram_ptr(cpu, address, size, NULL) == cpu->ram + offset;
}

static const char* trace_thread(CPUState* cpu, u32 thread) {
    if (!real_mem1(cpu, thread, 0x300u))
        return "scheduler observation requires a mapped real MEM1 thread";
    const u32 queue = mem_read32(cpu, thread + 0x2DCu);
    if (queue != 0u && !real_mem1(cpu, queue, 8u))
        return "scheduler observation found an unmapped wait queue";
    if (fprintf(stderr, "[scheduler-idle-thread] thread=%08X state=%u suspend=%d queue=%08X head=%08X tail=%08X saved_pc=%08X saved_lr=%08X sp=%08X\n",
                thread, mem_read16(cpu, thread + 0x2C8u), (s32)mem_read32(cpu, thread + 0x2CCu), queue,
                queue != 0u ? mem_read32(cpu, queue) : 0u, queue != 0u ? mem_read32(cpu, queue + 4u) : 0u,
                mem_read32(cpu, thread + 0x198u), mem_read32(cpu, thread + 0x84u),
                mem_read32(cpu, thread + 4u)) < 0)
        return "scheduler thread observation trace write failed";
    u32 frame = mem_read32(cpu, thread + 4u);
    for (u32 depth = 0u; depth < 4u && frame != 0u; ++depth) {
        if (!real_mem1(cpu, frame, 8u))
            return "scheduler observation found an unmapped saved stack frame";
        if (fprintf(stderr, "[scheduler-idle-frame] thread=%08X level=%u sp=%08X lr=%08X\n",
                    thread, depth, frame, mem_read32(cpu, frame + 4u)) < 0)
            return "scheduler stack observation trace write failed";
        const u32 next = mem_read32(cpu, frame);
        if (next <= frame)
            break;
        frame = next;
    }
    return NULL;
}

const char* ww_host_observe_scheduler_idle(WwHost* host) {
    CPUState* cpu = &host->cpu;
    if (!real_mem1(cpu, 0x803F7A30u, 12u) || !real_mem1(cpu, 0x800000C0u, 0x28u))
        return "scheduler observation requires mapped retail globals";
    const BluewakeSchedulerIdleState idle = bluewake_scheduler_idle_state(cpu);
    const u32 poll = cpu->gpr[13] - 26288u;
    const bool poll_valid = real_mem1(cpu, poll, 4u);
    WwRendererStats renderer = {0};
    if (host->renderer != NULL)
        ww_renderer_stats(host->renderer, &renderer);
    if (fprintf(stderr, "[scheduler-idle] run_bits=%08X current_thread=%08X context=%08X lock=%u dvd_pending=%u r13=%08X poll=%08X poll_valid=%u poll_value=%08X gx_decoded=%llu renderer_available=%u fifo_bytes=%llu draws=%llu presented=%llu vi_enabled=%u vi_retraces=%llu pc=%08X cycles=%llu stage=%s audio=%s present_markers=%llu\n",
                idle.run_bits, idle.current_thread, idle.current_context, mem_read32(cpu, 0x803F7A38u),
                host->dvd_completions.count, cpu->gpr[13], poll, poll_valid,
                poll_valid ? mem_read32(cpu, poll) : 0u, (unsigned long long)host->stream.decoded,
                host->renderer != NULL, (unsigned long long)renderer.fifo_bytes,
                (unsigned long long)renderer.draws, (unsigned long long)renderer.presented,
                host->vi_clock_enabled, (unsigned long long)host->vi_retraces, cpu->pc,
                 (unsigned long long)host->cycles.absolute_cycles,
                 host->scheduler_idle_observed ? "post-card" : "post-link",
                 host->dsp.audio_deferred ? "deferred" : "strict",
                 (unsigned long long)host->frame_timing.present.index) < 0)
        return "scheduler idle observation trace write failed";
    const u32 known[] = {0x803F06D0u, 0x803A2960u, 0x803A5F90u};
    for (u32 index = 0u; index < sizeof(known) / sizeof(known[0]); ++index) {
        const char* error = trace_thread(cpu, known[index]);
        if (error != NULL)
            return error;
    }
    u32 active = mem_read32(cpu, 0x800000DCu);
    for (u32 count = 0u; count < 8u && active != 0u; ++count) {
        bool known_thread = false;
        for (u32 index = 0u; index < sizeof(known) / sizeof(known[0]); ++index)
            known_thread |= active == known[index];
        if (!known_thread) {
            const char* error = trace_thread(cpu, active);
            if (error != NULL)
                return error;
        }
        if (!real_mem1(cpu, active, 0x300u))
            return "scheduler observation found an unmapped active thread";
        const u32 next = mem_read32(cpu, active + 0x2FCu);
        if (next == active)
            return "scheduler observation found a self-linked active thread";
        active = next;
    }
    return NULL;
}

const char* ww_host_observe_dsp_task_boot(WwHost* host) {
    if (host == NULL)
        return "DSP task observation requires a host";
    CPUState* cpu = &host->cpu;
    const u32 entry = 0x803193ACu;
    const u32 task = cpu->gpr[3];
    const u32 stack = cpu->gpr[1];
    if (cpu->pc != entry || !ww_host_module_contains_address(host->module, entry) ||
        !ww_code_guard_copied_text_matches(&host->code_guard, cpu, entry, entry, 32u))
        return "DSP task observation requires the registered original DOL entry";
    if (!real_mem1(cpu, task, 64u) || ww_host_linked_alias_overlaps(task, 64u))
        return "DSP task observation requires a real MEM1 task descriptor";
    if (stack < GC_RAM_BASE + 24u || !real_mem1(cpu, stack - 24u, 32u) ||
        ww_host_linked_alias_overlaps(stack - 24u, 32u))
        return "DSP task observation requires the real guest prologue stack range";
    if (host->dsp_task_boot_observed)
        return NULL;
    const WwDspInterface* dsp = &host->dsp;
    if (fprintf(stderr, "[dsp-task-boot] pc=%08X lr=%08X sp=%08X task=%08X task_bytes=64 control=%04X cpu_mail=%08X dsp_mail=%08X rom_selected=%u rom_pending=%u init_loaded=%u init_cycles=%llu cycles=%llu mode=guest-body audio=%s reply_injection=%s task_hex=",
                entry, cpu->lr, stack, task, dsp->control, dsp->cpu_mailbox, dsp->dsp_mailbox,
                dsp->rom_selected, dsp->rom_mail_pending, dsp->init_loaded,
                (unsigned long long)dsp->init_cycles_remaining, (unsigned long long)host->cycles.absolute_cycles,
                dsp->audio_deferred ? "deferred" : "strict", dsp->audio_deferred ? "deferred-bootstrap-setup-control" : "none") < 0)
        return "DSP task observation trace write failed";
    const u8* bytes = cpu->ram + (task - GC_RAM_BASE);
    for (u32 index = 0u; index < 64u; ++index) {
        if (fprintf(stderr, "%02X", bytes[index]) < 0)
            return "DSP task observation descriptor trace write failed";
    }
    if (fprintf(stderr, "\n") < 0)
        return "DSP task observation trace closure failed";
    host->dsp_task_boot_observed = true;
    return NULL;
}

const char* ww_host_observe_card_probe(WwHost* host) {
    if (host == NULL)
        return "CARDProbe observation requires a host";
    CPUState* cpu = &host->cpu;
    const u32 entry = 0x8031D400u;
    const u32 bytes = 56u;
    const u32 stack = cpu->gpr[1];
    if (cpu->pc != entry || !ww_host_module_contains_address(host->module, entry) ||
        !ww_host_module_contains_address(host->module, entry + bytes - 4u) ||
        !ww_code_guard_copied_text_matches(&host->code_guard, cpu, entry, entry, bytes) ||
        ww_host_linked_alias_overlaps(entry, bytes))
        return "CARDProbe observation requires the registered original DOL wrapper";
    if (stack < GC_RAM_BASE + 8u || !real_mem1(cpu, stack - 8u, 16u) ||
        ww_host_linked_alias_overlaps(stack - 8u, 16u))
        return "CARDProbe observation requires the real guest prologue stack range";
    if (!real_mem1(cpu, 0x800030E0u, 4u) || ww_host_linked_alias_overlaps(0x800030E0u, 4u))
        return "CARDProbe observation requires the original retail flag range";
    if (host->card_probe_observed)
        return NULL;
    if (fprintf(stderr, "[card-probe] pc=%08X lr=%08X sp=%08X channel=%u retail_flag=%02X cycles=%llu mode=guest-body result_injection=none storage=none\n",
                entry, cpu->lr, stack, cpu->gpr[3], cpu->ram[0x30E3u],
                (unsigned long long)host->cycles.absolute_cycles) < 0)
        return "CARDProbe observation trace write failed";
    host->card_probe_observed = true;
    return NULL;
}
