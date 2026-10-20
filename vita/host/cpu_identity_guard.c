/* Fail-closed identity guard on the runtime's CPUState-field tail-call
 * trampolines: ppc_host_call, ppc_mftb and ppc_fallback_instruction.
 *
 * Each trampoline loads a function pointer through the caller's CPUState
 * argument and branches to it. A caller whose state pointer has drifted reads
 * a neighbouring field instead - external_user_data sits right after
 * host_call and holds the host itself, so the branch reaches host data and
 * runs it as ARM code. The wrappers below run before that field load: this
 * host runs the guest on exactly one CPUState, the one the RAM guard adopted
 * (g_bw_guest_cpu, guest_ram_guard.c), so any other pointer is reported with
 * its delta from the adopted state, the field offset involved, the guest
 * pc/lr and the caller, and the process stops instead of branching.
 *
 * Before adoption the wrappers are transparent, matching guest_ram_guard.h:
 * host code and tests that own their state keep the runtime's own behaviour.
 * Wired in vita/host/CMakeLists.txt next to --wrap=ppc_rfi; the bogus state's
 * own fields are never read (guest_ram_guard.c prints the same way). */
#include "guest_ram_guard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

bool __real_ppc_host_call(CPUState* cpu, u32 address);
u32 __real_ppc_mftb(CPUState* cpu, u16 tbr, u32 cia);
void __real_ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia);

static CPUState* ww_cpu_identity(CPUState* cpu, const char* service, size_t field_offset,
                                 CPUState* adopted) {
    if (adopted == NULL || adopted == cpu)
        return cpu;
    fprintf(stderr,
            "[cpu-identity] service=%s ctx=%p expected=%p delta=%+ld field_offset=0x%zX "
            "pc=%08X lr=%08X caller=%p\n",
            service, (void*)cpu, (void*)adopted, (long)((char*)cpu - (char*)adopted),
            field_offset, adopted->pc, adopted->lr, __builtin_return_address(0));
    abort();
}

static void ww_cpu_identity_live(const char* service, CPUState* cpu, CPUState* adopted,
                                 bool* noted) {
    if (*noted || adopted == NULL)
        return;
    *noted = true;
    fprintf(stderr, "[cpu-identity] live service=%s ctx=%p\n", service, (void*)cpu);
}

bool __wrap_ppc_host_call(CPUState* cpu, u32 address) {
    static bool noted;
    CPUState* const adopted = g_bw_guest_cpu;
    cpu = ww_cpu_identity(cpu, "ppc_host_call", offsetof(CPUState, host_call), adopted);
    ww_cpu_identity_live("ppc_host_call", cpu, adopted, &noted);
    return __real_ppc_host_call(cpu, address);
}

u32 __wrap_ppc_mftb(CPUState* cpu, u16 tbr, u32 cia) {
    static bool noted;
    CPUState* const adopted = g_bw_guest_cpu;
    cpu = ww_cpu_identity(cpu, "ppc_mftb", offsetof(CPUState, spr_read), adopted);
    ww_cpu_identity_live("ppc_mftb", cpu, adopted, &noted);
    return __real_ppc_mftb(cpu, tbr, cia);
}

void __wrap_ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia) {
    static bool noted;
    CPUState* const adopted = g_bw_guest_cpu;
    cpu = ww_cpu_identity(cpu, "ppc_fallback_instruction", offsetof(CPUState, instruction_fallback),
                          adopted);
    ww_cpu_identity_live("ppc_fallback_instruction", cpu, adopted, &noted);
    __real_ppc_fallback_instruction(cpu, raw, cia);
}
