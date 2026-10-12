# VITA-WW-16 — from the boot FIFO to the first visible pixel

The objective is the game running at 30+ FPS. Boot progress, command decoding and
a black pre-logo window do not meet it. Every unsupported operation remains an
explicit STOP; no guest instruction, FIFO data, GPU completion or frame is invented.

## Unit 1: CP performance selector

The authentic WW05 continuation stops at `CC000006`, halfword zero, PC `803202C4`
after eleven complete commands. At the unchanged RecompCore pin
`ddd031b2a7f45fcbda37a49daac9b308b8d95da1`,
`Source/Core/VideoCommon/CommandProcessor.h:62,200` identifies `PERF_SELECT=06`
and zero-initialized halfword storage. `CommandProcessor.cpp:221-222,608` registers
a direct halfword read, a direct write masked with `0007`, and reset to zero.

The Vita host implements exactly that retained selector. It does not implement
performance counters, consume FIFO bytes or acknowledge interrupts. Other widths
remain unsupported. Focused tests cover the source mask, pending FIFO/CP IRQ state,
width refusals and real cycle credit without renewing an exhausted dispatch quantum.
The initial unsupported-register failures are retained in private
`build-f5/ww05/logs/ww16-cp-perf-red.log`.

Linux AArch64: **421 host tests pass**, repository audit and attribution check pass.
The continuation fixture now uses genuinely unsupported offset `08`, not the newly
implemented selector, as its later service refusal. No shared runtime source or pin
changed. Windows compilation/CI is unavailable here. The next authentic STOP is
recorded after the clean committed build/trial. No presentation, physical Vita result
or FPS claim yet.

## Unit 2: host renderer lifecycle and truthful reports

Merged the local WW17 delivery `69a4177` without changing either read-only pin,
then applied `VITA-WW-17-host-hook.patch`. The host creates the renderer over its
own memory/CPU, forwards completed continuation commands, presents only completed
display copies, and destroys the renderer before freeing the guest storage.
The report includes `RENDER`; `END rendered` and the launcher's `presented`/`rendered`
come from actual renderer presentations. The validator rejects malformed renderer
fields and disagreement between the summary and the end count.

The private build selects the same no-splash vitaGL archive as WW02 and records its
digest. The isolated launcher copies the locally owned Cg compiler into its fresh
profile and rechecks both identities. No firmware or game data is committed.
The host-only integration fixture explicitly supplies a backend with no GPU or
display: unexpected draws/presents fail instead of inventing rendered frames.

Linux AArch64: **428 host tests pass**, including all captured bytes reaching the
renderer with no invented presentation and create failures stopping execution.
Evidence: private `build-f5/ww05/logs/ww16-renderer-host-tests-3.log`.
Repository audit, attribution and whitespace checks pass. ARM VPK rebuild and
isolated authentic continuation are the next checks; there is still no visible
pixel, physical Vita test or FPS claim.

The first ARM rebuild failed explicitly at `vita_gl_backend.cpp`: WW05's local
SDK lacks `vitaGL.h` and the third-party renderer support libraries (private
`logs/vita-build-68.log`). The build now supplies the read-only Cloudpunk SDK
target include/library paths, the same headers/libraries used by WW02/WW17,
without changing WW05's compiler or either pin. The build receipt also truthfully
records `aurora_recomp=true` for the pinned frontend (Aurora itself remains off).
The renderer/GX regression suite rebuilt from this worktree passes **73 tests**
in private `build-f5/ww16-gx`; this is synthetic, not a visible game frame.

## Unit 3: link the real backend through the C interface

The restart preserved the compiled game objects. The resumed build reached the
real ARM link and stopped at an undefined `ww_renderer_vita_backend` (private
`logs/vita-build-71.log`): the backend did not include its C-facing declaration,
so it exported a mangled C++ name. Including `ww_renderer.h` fixes the ABI.
The renderer's vitaGL support libraries are now its transitive dependencies,
after its archive on the host link line, rather than preceding its unresolved
references.

A focused ARM compile/symbol regression test failed with zero C symbols before
the fix and passes with exactly one afterward. **429 host tests pass**, and
`cmake --build .../ww05/vita --target ww05_host -j 4` now links the actual ARM
host successfully (`logs/ww16-renderer-link-smoke.log`). The committed private
VPK rebuild and isolated continuation remain the next gates. No pixel claimed.

## Unit 4: stop at installation readiness and kill idle guests early

The committed ARM VPK build at `dcb7e73` completed successfully. Authentic attempt
`5d65e06d7b3b4686a52a5af9e7a48963` was stopped on request after roughly 252 seconds
waiting in the install phase. It never issued a guest request. All three installed
payloads pass `verify_installed` against the staged VPK: installation was complete,
but the GUI process was left running while the launcher waited for its 600-second
phase deadline. The older launcher used the same environment/arguments/runtime
digest and accepted this timeout (e.g. run `2770acb8...`, 30 seconds).

Missing Wayland variables were not the cause. A diagnostic `/usr/bin/env` probe
using the exact recorded sandbox command confirms `QT_QPA_PLATFORM=wayland`,
`WAYLAND_DISPLAY=wayland-0` and the private `XDG_RUNTIME_DIR`; its evidence is under
that attempt's `phases/environment-probe`. No emulator or game ran in the probe.
The local Qt source constructs the GUI after archive installation, consistent
with this state; executable/source mapping remains unverified.

The launcher now stops and reaps an installer once its exact installed payload is
verified, before creating the guest request. A CPU watchdog samples the owned
sandbox **and its descendants**, so an idle bwrap parent cannot hide a busy game.
Below 5% of one CPU for 30 consecutive seconds (`--idle-timeout` overrides the
interval), it stops with `inactive`, status 125 and INCOMPLETE, preserving partial
logs, CPU observations and process cleanup evidence. Installation also gets this
watchdog if no complete payload appears. Phase start/stop messages flush immediately.
The existing owned TERM/KILL/wait path is reused; timeout/report behavior is not
replaced. Focused fake-sandbox tests cover early idle cleanup with partial logs,
immediate installed-payload readiness and a busy child that must not be killed.

Linux AArch64: **437 host tests and 80 shared FP launcher tests pass** (including
the existing exact-deadline checks). Evidence: private
`logs/ww16-inactivity-host-tests.log` and `logs/ww16-inactivity-fp-tests.log`.
Repository audit, attribution and whitespace checks pass. A new clean committed
build/trial follows; the game still has not drawn or presented an authentic frame.

## Unit 5: the renderer sees the real stream; PE direct configuration

Authentic trial `5b4a7353a17847288b16f3873a34c80c` at `fb2800e` proves the new
installation gate: 1.28 seconds, `installed_payload`, exact verified bytes, then
guest execution. The guest phase ends after 8.03 seconds at **7,153,307 cycles**,
with **265 completed authentic GX commands**, 64 journaled and verified. `RENDER`
reports **1,759 FIFO bytes, 521 frontend events, zero draws and zero presentations**;
`END rendered=0` agrees. This is real renderer initialization and streaming,
not synthetic pixels or a visible scene.

The next honest STOP is a halfword read at `CC001002`, PC `80322D58`. The unchanged
RecompCore pin's `Source/Core/VideoCommon/PixelEngine.h:31-35` names the five
direct configuration halfwords at offsets `00,02,04,06,08`. `PixelEngine.cpp:59-63`
resets them to zero; `:85-95` registers direct reads/writes of the full halfword.
The host retains exactly those five independent values. Other widths remain
unsupported; no EFB readback, GPU completion, rendering state or interrupt is
invented. The CP selector probe is generalized and reused rather than duplicated.

The closed report and complete journal were validated before the emulator itself
crashed during app shutdown (`Unhandled SIGSEGV`, outer exit 133, after report
rename). The crash is preserved in the trial's `phases/guest/evidence/process.log`;
it is a separate unresolved teardown issue, not a passing emulator exit.
The PE test's unsupported-register failures are preserved in private
`logs/ww16-pe-config-red.log`; the focused source seam is under host regression checks.

Linux AArch64: **443 host tests pass** (`logs/ww16-pe-config-green.log`), including
PE retention, independence, width refusals, unchanged pending FIFO/IRQ state and
cycle credit without quantum renewal. Audit, attribution and whitespace checks pass.
The next committed ARM build and authentic continuation follow. Still no draws,
shown pixels, physical Vita test or FPS measurement.
