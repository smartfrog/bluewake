#include "rel_lifecycle.h"

#include <assert.h>
#include <string.h>

int main(void) {
    WwRelPendingUnlink pending;
    CPUState cpu;
    memset(&pending, 0, sizeof pending);
    memset(&cpu, 0, sizeof cpu);
    pending.r1 = 0x8040CD40u;
    pending.r2 = 0x8024560Cu;
    pending.r13 = 0x803FE0E0u;
    pending.r31 = 0x80AE1940u;
    cpu.gpr[1] = pending.r1;
    cpu.gpr[2] = pending.r2;
    cpu.gpr[13] = pending.r13;
    cpu.gpr[31] = pending.r31;
    assert(ww_rel_unlink_caller_matches(&pending, &cpu));
    cpu.gpr[1] = 0x00065E20u;
    assert(!ww_rel_unlink_caller_matches(&pending, &cpu));
    cpu.gpr[1] = pending.r1;
    cpu.gpr[2] = 0u;
    assert(!ww_rel_unlink_caller_matches(&pending, &cpu));
    cpu.gpr[2] = pending.r2;
    cpu.gpr[13] = 0u;
    assert(!ww_rel_unlink_caller_matches(&pending, &cpu));
    cpu.gpr[13] = pending.r13;
    cpu.gpr[31] = 0u;
    assert(!ww_rel_unlink_caller_matches(&pending, &cpu));
    assert(!ww_rel_unlink_caller_matches(NULL, &cpu));
    assert(!ww_rel_unlink_caller_matches(&pending, NULL));
    return 0;
}
