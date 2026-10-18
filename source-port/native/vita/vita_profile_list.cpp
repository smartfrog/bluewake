// P-A follow-up: the bounded stand-in for the game's profile list in the Vita boot path
// (native/vita/vita_boot.cpp).
//
// The real list (game/src/f_pc/f_pc_profile_lst.cpp) names all 502 g_profile_* definitions, which
// live in the REL actor units this build does not compile, so the executable cannot link that
// unit yet. This stub has the list's size and the NULL terminator pc::staticInitCheck looks for,
// and no entries: the check reports "0 of 502 profile slots filled" and verifies the list pointer
// and terminator, which is all the boot path can honestly prove.
//
// It is not the game's profile list: do not link it into a build that should reach M0, and do not
// fill it with made-up entries. The 502 real definitions are the compile sweep's job.
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_profile.h"

process_profile_definition* g_fpcPfLst_ProfileList[fpcNm_MAX_NUM_e + 1] = {};
