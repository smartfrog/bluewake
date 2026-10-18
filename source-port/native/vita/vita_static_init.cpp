// P-A blocker 2 POC (build-f5/source-port-next-pa/REPORT.md): the smallest Vita main that reaches
// milestone M0. It runs pc::staticInitCheck and nothing else, so its link needs the game's profile
// list and the units that fill it, never pc_harness.cpp (Aurora DVD, window, heaps, threads) and
// never aurora::dvd, which the Vita source build does not have (AURORA_ENABLE_DVD=OFF).
//
// What it proves: the M0 check is reachable from a Vita main and links without the harness. What it
// is not: the milestone itself. M0's acceptance is "502 of 502 profile slots filled, 0 error(s)",
// which needs every REL actor unit compiled into this link (the g_profile_* definitions of the
// profile list), and the "[cos] MILESTONE static-init" line, which pc_milestone.cpp writes from
// Aurora's VI retrace counter and frame clock. Both arrive with the boot path, so this main does
// not print a marker it cannot fill in.
//
// Exit status: 0 when the check has no errors (the M0 condition apart from the link), 1 otherwise.
#include "pc/pc_static_init.h"

#include <unistd.h>

int main() {
    int errors = pc::staticInitCheck(STDERR_FILENO);
    return errors == 0 ? 0 : 1;
}
