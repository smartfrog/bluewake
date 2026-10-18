# WW88: skip the title logo for a crash-isolation run

## Question

The recomp title fault appears at the TlogoE boundary: the logo scene hands to the
opening/title scene, `TlogoE.arc` opens, and the guest then faults (PC0 after the
context corruption). Is the fault confined to that title logo/movie path?

## Call path

Retail GZLE01, from `config/GZLE01/symbols.txt` and `src/d/d_s_logo.cpp`:

- `dScnLogo_Draw` dispatches `dvdWaitDraw` (`.text:0x8022CF44`).
- After every logo preload command is ready, `dvdWaitDraw` does
  `li r4, 8` at `0x8022D16C` (`fpcNm_OPENING_SCENE_e`) and
  `bl dComIfG_changeOpeningScene` at `0x8022D170` (`LR 0x8022D174`).
- `dComIfG_changeOpeningScene` (`.text:0x800531A8`) sets the next stage to
  `sea_T:44` and re-requests the scene. `fpcNm_OPENING_SCENE_e` is
  `g_profile_OPENING_SCENE` in `d_s_play.cpp`: stage `sea_T`, room 44.
- Room 44 creates `daTitle`, which mounts `TlogoE.arc` and builds the title
  logo (ship, subtitle, `title_logo_e.blo`, `PRESS START`). This is the last
  resource the crashing trial opened.

## Skip

`WW_SKIP_TITLE_LOGO=ON` (off by default) adds one host-side rewrite in
`ww_host_dispatch`: when the dispatched address is `0x800531A8` and `r4` is `8`,
it sets `r4` to `12` (`fpcNm_NAME_SCENE_e`) and logs `[scene-skip]`. The return
address is not matched. The logo scene's preload still runs; only its final
transition changes, so the file-select menu opens instead of the title logo, the
title demo and the story movie. No ctx/guard code and no translated chunk is
changed, so the composite digest is unchanged.

## Checks

- `vita/host/scene_skip_probe.c` (CTest `scene-skip`) covers the exact call,
  several return addresses (none are matched), a wrong incoming scene, a wrong
  call address, a null CPU and a repeated call. It fails against a no-op stub
  and passes with the implementation.
- `vita/host/test_game_build.py` checks the builder passes
  `-DWW_SKIP_TITLE_LOGO=ON|OFF` explicitly.
- Host services compile with the option on and off; the on object contains the
  `[scene-skip]` string and the off object does not.
- Not run: Vita3K, a private package, or any visual check. The next trial owns
  that.

## Run plan

Build the next private package with `scripts/vita/ww05/game_build.py
--skip-title-logo` and run the unchanged ww56 harness. Expected proof: one
`[scene-skip]` line, no `TlogoE.arc` open, the `Stage` archive for the
file-select menu, and presentations past 297 with the file-select menu visible.
