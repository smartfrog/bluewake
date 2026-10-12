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
