# VITA-WW-21 — When the title screen appears: projection from the captured CI evidence

2026-10-13 · estimation only — **no new run was started, and `build-f5` was read only.**

**Headline.** The captured CI run `3f5aaa45e50f4c35b8785969f3b5bf5c` stops at command
**102936 / frame 101** (879,863,630 cycles / 615.04 s) with the Nintendo logo only starting to
fade out. The game code needs **≈ 400 more guest frames after frame 101 (range ≈ 390–430) for
the title logo to be fully visible**, and **≈ 490–530 frames to the first full "PRESS START"
title**. In emulator wall time that is **≈ 39–40 min at the steady cadence measured in that run
(5.81 s/frame)**, **≈ 60 min at the slower 9 s/frame cadence seen in the sibling runs**, and
**≈ 80 s in the burst cadence that occurred exactly once (frames 12–25)**. On a retail GameCube
the same stretch is **≈ 5–7 s** (frame-exact part 4.5–5.3 s + loads). The next capture must
therefore budget **≈ 40–60 min of extra window after command 102936; use ≥ 60 min (≥ 75 min to
reach PRESS START) as the working bound** — which the current machine cannot sustain under the
2.5 GiB RAM floor; §7 lists the options.

## 1. Inputs (all read-only)

| Input | Role |
| --- | --- |
| `build-f5/ww05/vita3k-run-3f5aaa45e50f4c35b8785969f3b5bf5c` | the CI run: report (`102936 decoded/logged`, `132 draws`, `101 presented`, `879863630` cycles, STOP `scene MEM1 draw window capture complete`), 101 `frame-*.json`, 101 `frame-present` + 108 `frame-timing` lines, `693346` FIFO bytes, 18.2 MB `ww05.log` |
| `a31f77e…` (85 frames / ~764 s), `e6a42f8…` (frame 30 / 263.34 s), `362b25b…`, `79112c7…` | cadence controls, see [VITA-WW-16](VITA-WW-16.md) Units 88–92 |
| `vita/renderer/gx_boot_101_replay.md`, `gx_logo_replay.md`, `gx_black_frame_audit.md` | byte-exact frame/state facts used below |
| zeldaret/tww decompilation (read via GitHub; **not present in this checkout**): `src/d/d_s_logo.cpp`, `include/d/d_s_logo.h`, `include/f_pc/f_pc_name.h`, `src/d/d_s_play.cpp`, `src/d/actor/d_a_title.cpp`, `src/d/d_s_title.cpp`, `src/d/d_s_open.cpp` | the frame-exact state machine |
| Native (Mac) milestones: LOGO actions at frames 5/95/125/215/245 and opening-scene start `sea_T:44` at frame 287 (`docs/status/CURRENT.md`); title-ready retrace 333 (`docs/status/PERFORMANCE.md`); title card visible at retrace 700 (`docs/archive/GOAL_PROMPT_V7_2026-09-14.md`) | confirms the retail frame counts and the OPENING scene identity |
| Local disc-image FST (read-only) | request-set sizes in §4 |

## 2. What the captured run shows (state at frame 101)

Frame numbering is exact from frame 11 on: each presented frame advances the guest by exactly
8,100,000 cycles = one NTSC VI = one game frame (verified frame 12→101; the early frames are
sparser — frame 8→9 skips ≈6 VIs during the REL load). The visual sequence is documented and
GPU-replayed byte-exact in `gx_boot_101_replay.md`:

| Frames | Content |
| --- | --- |
| 1–10 | opaque black initialization |
| 11–38 | Nintendo logo fades in (overlay alpha 238→9) |
| 39–99 | Nintendo logo, no overlay |
| 100 | fade-out starts (overlay alpha 8, max red 213) |
| 101 (cmd 102936) | fade-out continues (alpha 17, max 205) + transparent CI8 notice (alpha 0) |

**Frame 101 is the 3rd of the 30 `nintendoOutDraw` frames** (alpha 8→17 = fade-out steps 1→2
of 30). The fade-out completes at frame ≈ 128; the visible logo is gone from then on.

Measured cadence in this run (from the 101 `frame-present` microsecond stamps):

| Segment | Intervals | Total | Per frame | Rate |
| --- | ---: | ---: | ---: | ---: |
| frame 12→25 ("burst") | 14 | 2.693 s | 0.192 s | 5.20 fps |
| frame 26→101 (steady "queue") | 75 | 435.77 s | 5.810 s | 0.172 fps |
| frame 1→101 (whole run) | 100 | 601.45 s | 6.015 s | 0.166 fps |
| worst single interval (frame 8→9, REL load) | 1 | 113.06 s | — | — |

The user-facing approximations (≈5.5 fps burst / ≈0.2 fps queue) match these within rounding.
**86 of 100 intervals are "queue"**; the burst is a single 14-interval episode that has not
repeated, so the queue cadence is the projection basis.

## 3. The game-code sequence after frame 101 (USA, cold boot, non-progressive)

`d_s_logo.cpp` is a fixed state machine; each state counts down `mTimer` once per drawn frame
(`cLibCalcTimer` in `dScnLogo_Draw`). The native composed probe independently observed the
action changes at frames **5/95/125/215/245** = exactly the 90/30/90/30 pattern
(`docs/status/CURRENT.md`), and `d_a_title.cpp` gives the title timeline.

| Emulator frame (est.) | Event | Basis |
| --- | --- | --- |
| 102–128 | rest of `nintendoOutDraw` (fade to black completes) | code; 27 of 30 frames left |
| 129–218 | `dolbyInDraw` (Dolby logo fades in, holds) | code: 90 frames |
| 219–248 | `dolbyOutDraw` (fade to black) | code: 30 frames |
| 249 | `dvdWaitDraw` first check; syncs already true → `changeOpeningScene(fpcNm_OPENING_SCENE_e)` | code + §4 |
| 249–254 | transition request + 5-frame overlap | `fopScnM_ChangeReq(..., OVERLAP0, 5)` |
| ~255–305 | `g_profile_OPENING_SCENE` (a play-type scene, `d_s_play.cpp`) starts stage **`sea_T` room 44**, loads Stage/Room44/TlogoE/wave, creates the `daTitle` actor, `offFade` | native analog: scene start 287 → actor 333 (46 frames) + ~12 frames of measured I/O (§4) |
| actor+120 | `m01C` countdown ends → title alpha timeline starts | `d_a_title.cpp` |
| actor+150…200 | "zeld" logo fades in (50 frames); ship/subtitle at m018=80 | `d_a_title.cpp` |
| actor+160…180 | "PRESS START" (`pres`) fades in | `d_a_title.cpp` |
| actor+200 | input window opens (`enterMode=1`) | `d_a_title.cpp` |

Central actor frame ≈ 305 → **title logo fully visible ≈ frame 505** (range 490–530) and
**PRESS START ≈ frame 605** (range 590–630). The title scene's first visible content (the sea,
after `offFade`) is ≈ frame 305.

## 4. The emulator's loading side: `dvdWaitDraw` is not a long wait

Measured from the run's own log (all read-only):

- DVD completion pace: **one 8 KiB chunk per 555,613 guest cycles** (median over 1,468
  `[dvd-completion]` lines, constant from the first to the last) = **0.114 MiB per guest
  frame**, i.e. 14.6 chunks/frame.
- The `dvdWaitDraw` sync set is `dComIfG_syncAllObjectRes()` + the 26 `l_*` archives + 4 raw
  files + `LODALL.arc`. Its exact size from the disc FST: **4,985,107 B (4.75 MiB / 4.98 MB)**
  = 3.54 MiB archives + 0.41 MiB raw + 0.81 MiB object archives. Issued around the scene
  create (frame ≈ 9–11), it is delivered by ≈ frame 40–55 (cumulative delivery crosses it
  there; by frame 101 **11.36 MiB** had been delivered).
- The stream still running at frame 101 (`file=806AD6DC`) is the **audio bank loader** (the
  FST entries opened on it are Atmos/CharVoice/Footnote/LinkMotion/n_zelda/objSE/voice/
  w_zelda — 7.7 MiB of banks, and the stream continues). The remaining boot I/O after frame
  101 is ≈ 23 MiB of wave data (of the ≈ 30 MiB total) + ≈ 1.4 MiB of scene data (Stage
  149 KB + Room44 729 KB + TlogoE 382 KB + scene wave) ≈ 210 DVD-frames of guest time,
  draining at ≈ frame 300 (range 290–315).
- Consequence: the archives are complete long before frame 249, so **`dvdWaitDraw` returns on
  its first check (frame 249)**. The only residual risk is the opening scene's
  `load1stDynamicWave` step if that read is queued behind the banks (bound in §5, H2).

## 5. Projection: remaining frames and wall time after command 102936

Frames are exact for the logo states and for the actor timeline; the only real uncertainty is
the scene-load frame count (H1/H2 below) and the wall-clock cadence.

| After cmd 102936 (frame 101) | Δ frames | Burst 0.192 s/f | Queue 5.81 s/f (this run) | Queue 9 s/f (sibling runs) |
| --- | ---: | ---: | ---: | ---: |
| visible logo gone (frame 128) | +27 | 5 s | 2.6 min | 4 min |
| fade-out + Dolby logo complete (frame 248) | +147 | 28 s | 14.2 min | 22.1 min |
| scene change (frame ~250) | +149 | 29 s | 14.4 min | 22.4 min |
| opening scene up / actor created (frame ~305; H1) | +204 | 39 s | 19.8 min | 30.6 min |
| **title logo fully visible (frame ~505; H1)** | **+404** | **78 s** | **39.1 min** | **60.6 min** |
| **PRESS START (frame ~605; H1)** | **+504** | **97 s** | **48.8 min** | **75.6 min** |
| input window open (frame ~625) | +524 | 101 s | 50.7 min | 78.6 min |

H1 = all data already delivered (archives + scene files + dynamic wave fit the measured
delivery). H2 = the opening scene's `load1stDynamicWave` read is queued behind the ≈ 23 MiB of
wave data: the DVD queue drains at ≈ frame 300, which shifts the actor to ≈ frame 315–325 and
everything after by **+10–20 frames** (≈ +1–3 min at 5.81–9 s/f). H2 stays inside the
recommended margin. A third, bounded risk is the scene transition's JIT burst: the
observed early-boot analogue (frame 8→9, REL load + scene create) ran ~7 frames at ≈ 16 s per
frame, totaling 113 s; if the sea/title transition behaves the same, add ≈ 2–5 min.

## 6. The retail-GameCube gap (the answer to question 2)

Frame-exact part (60 fps, from the code), anchored at the moment the Nintendo logo is fully
gone (retail frame ≈ 119 of the logo scene, ≈ 2.0 s after the logo first appears):

| Retail segment | Frames / time |
| --- | --- |
| fade-out completes → Dolby in + hold + out | 120 frames = **2.0 s** |
| `dvdWaitDraw` | ≈ **0 s** — the 4.75 MiB request set reads at 2–3 MB/s in ≈ 1.7–2.5 s, overlapping the 4 s of logo display; it was verified complete well before the wait in the emulator evidence |
| scene change + `sea_T:44` load + actor create | ≈ **0.5–1.5 s** — 0.87 MB Stage+Room44 + 0.38 MB TlogoE + scene wave ≈ 0.5 s of disc data + engine start |
| actor → title logo fully visible | actor+150…200 frames = **2.5–3.3 s** |
| actor → PRESS START | actor+300 frames = **5.0 s** |

**Gap, Nintendo logo gone → title logo fully visible: ≈ 5–7 s (central ≈ 6 s).**
**Gap → full PRESS START title: ≈ 7.5–9 s.** No physical GameCube measurement exists in this
repository; the frame part is from the game's code, the load part is estimated from file sizes
and the emulator's measured I/O accounting. (The preliminaries — Nintendo logo, then Dolby
logo — are what make the retail boot feel longer than this gap.)

## 7. Window recommendation for the capture (the answer to question 4)

**Additional window after command 102936 (the direct answer):**

- **≈ 2,300–2,500 s (39–41 min)** to the fully visible title logo at the cadence measured in
  the captured run;
- **≈ 2,900–3,000 s (49–51 min)** to PRESS START;
- plan for **3,600–4,500 s (60–75 min)** to absorb the 9 s/frame cadence seen in sibling runs
  and the H2/transition margin;
- the burst cadence floor is ≈ 80–100 s, but it appeared in only 14 of 100 observed intervals
  and must not be the sizing basis.

**A fresh boot to the title needs ≈ 3,000–5,100 s** (add the 615 s already observed to frame
101). If the consolidated capture is the whole boot, use a **≥ 5,400 s (90 min) guest ceiling**.

**Host configuration the next capture needs (currently not set):**

| Setting | Now | Needed | Why |
| --- | --- | --- | --- |
| guest ceiling | 1,200 s (extended) | ≥ 5,400 s | wall time above |
| `continue_gx` | 133,260 | ≥ 1,500,000 (safe 2,097,152) | at the measured ≈1,017–1,083 commands/frame the title logo lands near command 0.5–1.0 M; the sea scene may raise the density |
| journal byte limit | 8 MiB | ≥ 16 MiB | 6.74 FIFO bytes/command → ≈ 10–13.5 MB at 1.5–2 M commands |
| `frame_capture_limit` | 128 | ≥ 640 (or a late-window image start) | frames 1–128 are logos; the title is at frame ≈ 490–630. ~0.7–1.4 GB of PNG pairs if the limit is raised naively |
| MEM1 snapshots | 8, window 101854–102936 | 8, window bound to the title draw | snapshots are consumed at the first eligible draws inside the window; find the title command with the novelty watcher, then re-run with the tight window (two-step, as `scripts/vita/gx/EXTRACTION.md` prescribes) |

**RAM is the binding constraint, not the ceiling.** The emulator's measured RSS plateau is
3.78–4.12 GiB; with the 2.5 GiB floor that needs **≈ 7–8 GiB of host availability sustained for
40–75 min**. The post-CI attempts stopped on host RAM at 70 s, 158 s and 263 s, and the
successful run dipped to 2.493 GiB available at 312 s (it reached its window end at 615 s only
because the 1 s sampling guard missed that crossing; Unit 90 then moved the check to ≈50 ms). So:

1. Either do the emulator speed work first (VITA-WW-20-PROFILE: cold Dynarmic compilation was
   82.25% of the guest thread; a 4–6× wall gain would bring the title inside a ~10–15 min
   window), or
2. run on a machine/session with the sustained ≈8 GiB headroom the docs already name, or
3. take the owner's explicit opening for a 60–75 min run — do **not** lower the 2.5 GiB floor
   or terminate other applications unasked.

Keep `audio=deferred`, keep the novelty watcher (`gallery.py --after-command 102936
--known-logo …`), and do not promote any logo-envelope frame to "title screen" without the
resource inspection the extraction doc requires.

## 8. Hypotheses and limits

1. **Cadence**: central estimate uses the run's own steady 5.81 s/frame; 9 s/frame is the
   observed sibling-run variant; the burst is real but was not repeated. The post-101 phases
   are new guest code (Dolby draws, scene transition, sea/play-scene machinery, `daTitle`), so
   the queue cadence is the defensible basis; the burst is an explicit upside, not a plan.
2. **`dvdWaitDraw` ≈ 0**: measured, not assumed — the sync set (4.75 MiB) is delivered by
   ≈ frame 40–55 at the measured 0.114 MiB/frame, and 11.36 MiB had been delivered by frame
   101. H2 (dynamic wave queued behind the wave data) shifts the projection by +10–20 frames
   at most, inside the margin.
3. **Scene-load frame count** (≈ 50 frames H1) is the largest frame uncertainty; its worst
   bounded case is H2 + the JIT-burst analogue (+2–5 min).
4. **The title actor frame math is exact** for the actor timeline (120/150/200/300/320) but the
   actor's creation frame is not directly observed in any Vita3K run yet.
5. **The retail-GameCube load estimate** (§6) is derived from file sizes and disc rates; this
   repository has no physical GameCube measurement.
6. **The title screen has still not been captured.** No frame after command 102936 exists in
   any run; every number after frame 101 is a projection from code, FST sizes and the run's
   own measured pacing.

## 9. Evidence pointers

- Run and frames: `build-f5/ww05/vita3k-run-3f5aaa45e50f4c35b8785969f3b5bf5c/` (report,
  `evidence/frames/frame-{11,100,101}.json`, `ww05.log`; read-only).
- Replay/proof of the captured states:
  `vita/renderer/gx_boot_101_replay.md`, `gx_logo_replay.md`, `gx_black_frame_audit.md`,
  `gx_boot_alpha_regressions.md`.
- Runtime history and RAM stops: `docs/vita/VITA-WW-16.md` Units 88–92; cost model:
  `docs/vita/VITA-WW-20-PROFILE.md`.
- Native milestones: `docs/status/CURRENT.md` (LOGO actions; `sea_T:44`),
  `docs/status/PERFORMANCE.md` (title-ready 333),
  `docs/archive/GOAL_PROMPT_V7_2026-09-14.md` (title card at retrace 700).
- Decompilation used for the state machine (CC0, fetched read-only):
  zeldaret/tww `src/d/d_s_logo.cpp`, `src/d/d_s_play.cpp`, `src/d/actor/d_a_title.cpp`,
  `include/f_pc/f_pc_name.h`.
