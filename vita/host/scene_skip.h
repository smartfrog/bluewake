#ifndef WW_SCENE_SKIP_H
#define WW_SCENE_SKIP_H

#include "core/cpu.h"

// Diagnostic skip for the boot/title path. The retail opening-scene request is
// dComIfG_changeOpeningScene(0x800531A8) with fpcNm_OPENING_SCENE_e (8). It is
// reached before TlogoE from d_s_title's dScnTitle_Execute (0x8023743C and
// 0x80237498, LR 0x80237440/0x8023749C) and, when the logo follows, from
// d_s_logo's dvdWaitDraw (0x8022D170, LR 0x8022D174). Both request sea_T room
// 44, which creates daTitle and mounts TlogoE. Rewriting that scene name to
// fpcNm_NAME_SCENE_e (12) opens the file-select menu and never creates the
// title actor. Matching only the request address and scene name keeps every
// other opening-scene caller untouched.
#define WW_SCENE_SKIP_CHANGE_OPENING_SCENE 0x800531A8u
#define WW_SCENE_SKIP_OPENING_SCENE 8u
#define WW_SCENE_SKIP_NAME_SCENE 12u

// True when this was an opening-scene request and it was rewritten to NAME_SCENE.
bool ww_scene_skip_logo_rewrite(CPUState* cpu, u32 address);

#endif
