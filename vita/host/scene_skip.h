#ifndef WW_SCENE_SKIP_H
#define WW_SCENE_SKIP_H

#include "core/cpu.h"

// Diagnostic skip for the title path. The logo scene's last draw handler
// (dScnLogo_c's dvdWaitDraw) calls dComIfG_changeOpeningScene with
// fpcNm_OPENING_SCENE_e (8) at 8022D170; the title scene then builds TlogoE.
// Rewriting that one argument to fpcNm_NAME_SCENE_e (12) opens the file-select
// menu instead, skipping the TlogoE logo and every movie transition.
#define WW_SCENE_SKIP_CHANGE_OPENING_SCENE 0x800531A8u
#define WW_SCENE_SKIP_LOGO_CALL_RETURN 0x8022D174u
#define WW_SCENE_SKIP_OPENING_SCENE 8u
#define WW_SCENE_SKIP_NAME_SCENE 12u

// True when the call is the logo scene's opening-scene transition and this
// rewrote its scene name to the file-select menu.
bool ww_scene_skip_logo_rewrite(CPUState* cpu, u32 address);

#endif
