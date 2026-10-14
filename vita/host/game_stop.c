#include "boot_host.h"

static const char* panic_text(CPUState* cpu, u32 address, char* text, u32 capacity) {
    text[0] = '\0';
    if (address == 0u)
        return NULL;
    for (u32 index = 0u; index + 1u < capacity; ++index) {
        if (address > UINT32_MAX - index)
            return "OSPanic text address overflow";
        const u8* byte = get_ram_ptr(cpu, address + index, 1u, NULL);
        if (byte == NULL)
            return "OSPanic text requires mapped guest memory";
        text[index] = (char)*byte;
        if (*byte == 0u)
            return NULL;
    }
    text[capacity - 1u] = '\0';
    return NULL;
}

static const char* halt_evidence(WwHost* host, const char* event, u32 pc) {
    CPUState* cpu = &host->cpu;
    if (get_ram_ptr(cpu, 0x800000E4u, 4u, NULL) == NULL ||
        get_ram_ptr(cpu, 0x800000D4u, 4u, NULL) == NULL ||
        get_ram_ptr(cpu, 0x803F7A30u, 4u, NULL) == NULL)
        return "PPCHalt evidence requires mapped guest globals";
    if (fprintf(stderr,
                "[halt] %s pc=0x%08X lr=0x%08X r1=0x%08X "
                "r3=0x%08X msr=0x%08X exception=0x%08X "
                "srr0=0x%08X current_thread=0x%08X current_context=0x%08X "
                "run_bits=0x%08X\n",
                event, pc, cpu->lr, cpu->gpr[1], cpu->gpr[3], cpu->msr,
                cpu->exception, cpu->srr0, mem_read32(cpu, 0x800000E4u),
                mem_read32(cpu, 0x800000D4u), mem_read32(cpu, 0x803F7A30u)) < 0)
        return "halt entry trace write failed";
    u32 frame = cpu->gpr[1];
    for (u32 depth = 0u; depth < 8u && frame >= GC_RAM_BASE && frame < 0x82000000u; ++depth) {
        if (get_ram_ptr(cpu, frame, 8u, NULL) == NULL)
            return "PPCHalt evidence requires a mapped guest stack frame";
        const u32 next = mem_read32(cpu, frame);
        if (fprintf(stderr, "[halt] frame%u sp=0x%08X back=0x%08X lr=0x%08X\n",
                    depth, frame, next, mem_read32(cpu, frame + 4u)) < 0)
            return "halt frame trace write failed";
        if (next <= frame || next >= 0x82000000u)
            break;
        frame = next;
    }
    return NULL;
}

const char* ww_host_observe_game_stop(WwHost* host, u32 address) {
    CPUState* cpu = &host->cpu;
    if (address == 0x80006C4Cu && !host->os_panic_reported) {
        char file[160] = {0};
        char format[256] = {0};
        const char* error = panic_text(cpu, cpu->gpr[3], file, sizeof(file));
        if (error == NULL)
            error = panic_text(cpu, cpu->gpr[5], format, sizeof(format));
        if (error != NULL)
            return error;
        if (fprintf(stderr,
                    "[panic] OSPanic file=0x%08X line=%u format=0x%08X "
                    "r6=0x%08X r7=0x%08X r8=0x%08X lr=0x%08X "
                    "pc=0x%08X retraces=%llu edges=%llu "
                    "r26=0x%08X r27=0x%08X r28=0x%08X r29=0x%08X "
                    "r30=0x%08X r31=0x%08X file=\"%s\" format_text=\"%s\"\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6],
                    cpu->gpr[7], cpu->gpr[8], cpu->lr, address,
                    (unsigned long long)host->vi_retraces, (unsigned long long)host->edges,
                    cpu->gpr[26], cpu->gpr[27], cpu->gpr[28], cpu->gpr[29],
                    cpu->gpr[30], cpu->gpr[31], file, format) < 0)
            return "OSPanic entry trace write failed";
        host->os_panic_reported = true;
    }
    if (address == 0x8030150Cu || address == 0x80301510u) {
        const char* error = halt_evidence(host, "PPCHalt entry", address);
        return error != NULL ? error : host->os_panic_reported ? "game panic (OSPanic)" : "game halt (PPCHalt)";
    }
    return NULL;
}

const char* ww_host_panic_marker(WwHost* host, u32 address, u64 value, u8 width) {
    const u32 pc = host->cpu.pc;
    if (address != 0x01234567u || value != 0x01234567u || width != 4u ||
        pc < 0x80006C4Cu || pc >= 0x80006D84u)
        return NULL;
    if (fprintf(stderr, "[panic] OSPanic marker address=0x%08X value=0x%08X pc=0x%08X entry_captured=%u\n",
                address, (u32)value, pc, host->os_panic_reported) < 0)
        return "OSPanic marker trace write failed";
    const char* error = halt_evidence(host, "OSPanic marker", pc);
    return error != NULL ? error : "game panic (OSPanic)";
}
