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
    if (fprintf(stderr, "[scheduler-idle] run_bits=%08X current_thread=%08X context=%08X lock=%u dvd_pending=%u r13=%08X poll=%08X poll_valid=%u poll_value=%08X gx_decoded=%llu renderer_available=%u fifo_bytes=%llu draws=%llu presented=%llu vi_enabled=%u vi_retraces=%llu pc=%08X cycles=%llu\n",
                idle.run_bits, idle.current_thread, idle.current_context, mem_read32(cpu, 0x803F7A38u),
                host->dvd_completions.count, cpu->gpr[13], poll, poll_valid,
                poll_valid ? mem_read32(cpu, poll) : 0u, (unsigned long long)host->stream.decoded,
                host->renderer != NULL, (unsigned long long)renderer.fifo_bytes,
                (unsigned long long)renderer.draws, (unsigned long long)renderer.presented,
                host->vi_clock_enabled, (unsigned long long)host->vi_retraces, cpu->pc,
                (unsigned long long)host->cycles.absolute_cycles) < 0)
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
