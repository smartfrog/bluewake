// The static-initialisation check of milestone M0 (docs/NATIVE_PORT_PHASE4_6.md, step 3.9), out of
// the run harness so a build without the PC harness can run it: the Vita source main
// (native/vita/vita_static_init.cpp, P-A blocker 2) calls it with no Aurora, no disc and no heaps.
//
// It reads only the game's own data: g_fpcPf_ProfileList_p, g_fpcPfLst_ProfileList and fpcPf_Get
// (game/src/f_pc), which the static initialisers of the linked units fill before main.
#pragma once

namespace pc {

// Checks that g_fpcPf_ProfileList_p points at g_fpcPfLst_ProfileList, that the list has
// fpcNm_MAX_NUM_e entries and a NULL terminator, that each non-NULL entry i has mProcName == i,
// and that fpcPf_Get(i) returns entry i. Writes a line per mismatch and the summary
// "static-init: N of M profile slots filled, E error(s)" to fd, and returns E.
int staticInitCheck(int fd);

} // namespace pc
