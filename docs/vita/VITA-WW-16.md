# VITA-WW-16 — from the boot FIFO to the first visible pixel

The objective is the game running at 30+ FPS. Boot progress, command decoding and
a black pre-logo window do not meet it. Every unsupported operation remains an
explicit STOP; no guest instruction, FIFO data, GPU completion or frame is invented.

October 13 checkpoint: **the authentic Nintendo boot logo is visible**, first
at frame 11 (Unit 78). Gameplay and 30+ FPS are still unverified. The current
capture handoff targets complete later-draw FIFO state and synchronized MEM1
for lane 18 (Units 79-80), not another guessed renderer/audio fix. The first
extra-draw interval is now captured and passes the guarded native loader.
The latest CI attempt reaches 96 live images but not its requested draw-time
RAM window; Unit 85 records that incomplete outcome without extending bounds.

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

## Unit 6: the observed CP metrics reset is not an IRQ acknowledgement

Trial `0662c9511fad49b1b65d4b43d87ce23c` at `4294573` crosses PE configuration and
stops at **7,153,469 cycles** on a halfword `CC000004=4`, PC `80327C38`.
Installation takes 1.35 seconds, guest 8.18 seconds; the renderer counters remain
265 completed commands / 1,759 bytes / no draws or presents. The same post-report
Vita3K shutdown crash (133) remains unresolved and recorded.

At the unchanged RecompCore pin, `CommandProcessor.h:139-148` identifies bit 2 as
`ClearMetrics`, set by `GXClearGPMetric`; `CommandProcessor.cpp:210-217` has a
constant-zero halfword read and calls `SetCpClearRegister`, which intentionally
does nothing (`:597-601`). The host now accepts the observed **exact value 4** as
that sourced no-op, including while gather/FIFO input or a CP IRQ is pending. It
does not discard commands, acknowledge FIFO flags, clear renderer counters or
invent query results. Value 3 retains its earlier idle-only guard; unobserved
mixed masks still fail. The existing CP-clear probe is extended, not copied.

Focused metrics-reset tests fail before the seam (`logs/ww16-cp-metrics-red.log`)
and exercise the register through the normal host hooks, with pending input/IRQ
state, width refusals, repeated stores and exact executed-cycle credit.

Linux AArch64: **447 host tests pass** (`logs/ww16-cp-metrics-green.log`); audit,
attribution and whitespace checks pass. The next clean ARM build/trial follows.
No visible game pixel or FPS claim yet.

## Unit 7: route DVD open edges through the existing private-image seam

Trial `32d13d3823d043fd879118b851ab2bf1` at `a092d98` crosses the CP metrics reset
and reaches **8,191,107 cycles**, then stops explicitly at `8030F618` (`DVDOpen`).
Installation takes 1.17 seconds and guest 8.48 seconds. Still 265 real setup
commands, no draw/present; the post-report emulator teardown crash remains 133.

The existing shared desktop inventory identifies `8030F2B0` as path-to-entry,
`8030F618` as open and `8030F5A4` as fast-open (`edge_intercepts.c:51-53`,
`main.c:12142-12165`). The Vita host services only those module-owned edges,
using WW13's unchanged FST/seam and the pin's existing `dol_hle_DVDFastOpen`
(`GXRuntime/src/hle/hle_dvd.c:42-54`) for the four file-info writes. The native
returns to the real aligned LR with the actual found/not-found result.

Input guards refuse unmapped/unterminated paths, unaligned or out-of-range cached
MEM1 file-info pointers, aliases and unowned edges. A missing image or another
owner's image stops; trace failures stop before changing guest results or file
info. Image ownership/open/close is shared with the already wired DI-read path,
not duplicated. No read completion, callback, IRQ, FIFO data or frame is created
by opening a file. Async/synchronous reads remain separate seams if reached.

The game-free synthetic FST tests reuse WW13's image fixture, with real entry,
offset and length checks and preserved unrelated file-info bytes. The authentic
file bytes remain only in the read-only private image outside the VPK. The initial
DVDOpen edge failure is retained in `logs/ww16-dvd-open-red.log`.

Linux AArch64: **455 host tests pass** (`logs/ww16-dvd-open-green.log`), including
all existing DI-read and edge-guard regressions; audit, attribution and whitespace
checks pass. The next committed ARM build and authenticated continuation follow.
No visible pixel, physical Vita result or FPS measurement claimed.

The ARM link of `226e9ff` exposed the pin HLE's hidden registration dependencies:
the DVD HLE object pulled its HLE registry and memory-card code, ending in an
unsupported Vita `flock` symbol (`logs/vita-build-76.log`). No lock stub or save
behavior was invented. The existing desktop `host_dvd_fill_file_info` is instead
extracted into `runtime/host/src/dvd_file_info.h`, with its function body checked
byte-identical to the previous shared main; both desktop and Vita reuse it.
The four writes still match the pin's fast-open contract, without linking the
unrelated HLE registry. Desktop behavior is a refactor only; Windows CI/build is
unavailable here and is not claimed.

**455 host tests still pass** (`logs/ww16-dvd-file-info-tests.log`), the actual ARM
`ww05_host` now links (`logs/ww16-dvd-file-info-link.log`), and audit/attribution/
whitespace checks pass. The committed VPK build/trial follows.

## Unit 8: synchronous file reads copy real bytes before completing

Trial `3debd70ed0094262b76006e2cc4a4922` at `3924c80` proves authentic DVD open:
`/COPYDATE` resolves to FST entry 151 through **vita-sceio**, with the private
image's 2,391-entry FST. It then stops at **8,191,115 cycles**, `8030FBCC`
(`DVDReadPrio`). Installation takes 1.23 seconds, guest 8.48 seconds; the same
265 commands / 1,759 renderer bytes still contain no draws or presents.

The existing shared host's synchronous contract (`main.c`, DVD read bridge)
supplies r3=file-info, r4=destination, r5=length, r6=file-offset, returns the
actual completed length, and writes END/current/transferred state. Its existing
32-byte SDK tail allowance is preserved, but the bytes must come from the real
image, never a synthetic pad. The Vita host now services that module-owned sync
edge through the WW13 seam. Async reads/callback delivery are still unsupported.

Both DI DMA and synchronous API reads share the same owned-image and checked
copy path. Source ranges, signed disc limits, cached real MEM1, file bounds and
aliases are checked before copying. The seam's actual-byte counter must advance
by exactly the requested length; short/failed reads cannot set completion fields
or return success. No DI/PE interrupt, callback, GPU completion or cycle charge is
created by this synchronous operation. The shared transfer-state helper is
extracted from desktop's three unchanged writes to avoid duplicating the contract.

The initial sync-edge failure is in `logs/ww16-dvd-read-red.log`. **461 host tests
pass** (`logs/ww16-dvd-read-green.log`), including real synthetic bytes, SDK tail
reads, short-file refusal, input/alias guards, unchanged interrupts and trace-fail
preservation. DI regressions rerun after the final guard review pass too
(`logs/ww16-dvd-read-di-regressions.log`). Audit, attribution and whitespace checks
pass. Windows build/CI remains unavailable; shared desktop behavior is a helper
refactor only. The next committed ARM build/trial follows. No visible pixel or FPS.

## Unit 9: a narrowed no-callback path; trial checks the actual arguments

Trial `f38d5a186f804bc8a64c68a7b5429c24` at `001f87d` proves real synchronous reads
of `/COPYDATE` and the `RELS.arc` header/table, including a 6,816-byte copy through
vita-sceio. It reaches **14,448,441 cycles**, then stops at `8030FADC`
(`DVDReadAsyncPrio`). Installation takes 1.25 seconds, guest 9.18 seconds. Renderer
still sees 265 setup commands / 1,759 bytes; no draw or presentation yet.

One private translated call-site sets **r7=0**, but the subsequent trial below
proves that it is not the call-site reached at this frontier. The unchanged
pin's `GXRuntime/src/hle/hle_dvd.c:57-77` defines this no-callback path: copy real
bytes, write END/current/transferred state, return TRUE (queued). The host now
services exactly that path through the same checked copy used by synchronous
reads, with `kind=async-null` in the journal. Actual full-byte completion is still
required before updating state. No callback is requested or invented.

Any nonzero callback is refused **before copying or changing file-info state**,
and the STOP names the actual callback address. The shared callback delivery and
deferred completion queue are inspected, but not wired speculatively: the current
null-callback seam does not restore the pin's acknowledged uninvoked-callback bug
or pretend the later nonzero-callback case works. Synthetic tests reuse the existing
read probe and cover both null-callback normal/tail reads and the nonzero refusal.
The missing-edge red evidence is in `logs/ww16-dvd-async-null-red.log`.

Linux AArch64: **463 host tests pass** (`logs/ww16-dvd-async-null-green.log`),
including all synchronous/DI/open regressions. Audit, attribution and whitespace
checks pass. The next committed ARM build/trial follows; no visible pixel or FPS
claim, and no physical Vita result.

## Unit 10: defer the real JKRDvdFile callback and execute it

Trial `f4780be958bb485fa9033d766329b3d3` at `c3f5b42` correctly refuses the actual
nonzero callback **`802BCC78` (`JKRDvdFile::doneProcess`)** at `8030FADC`, still
14,448,441 cycles. That evidence corrects unit 9's candidate-call-site hypothesis;
no async read or callback was silently claimed successful.

The host now reuses the shared `BluewakeDvdCompletions` queue and
`bluewake_deliver_guest_callback`, the same real callback contract covered by
`tests/generated_dvd_callback_contract_test.c`. Nonzero callbacks must be owned
compiled module instructions. After a checked real copy, their file-info state
is BUSY and a completion is queued; actual guest-cycle progress reaches the
shared desktop's **one guest millisecond compatibility floor** (486,000 cycles,
not a measured disc-speed model). Only EE-enabled, exception-free, scheduler-safe
turn boundaries deliver it. No deadline or cycle credit is manufactured.

The callback runs through the existing host dispatcher and cycle credit/refund
path, with the shared interrupt-style scheduler lock, EE masking, register save/
restore and real retained memory effects. Its injected return sentinel is handled
only while this delivery is active. A full queue, deadline overflow, bad stack,
unowned callback, trace failure or incomplete callback is an explicit STOP; no
callback failure is swallowed. DI/PE interrupts, FIFO bytes and frames are not
created by this service. Null-callback reads retain their already checked path.

Synthetic dispatcher tests verify no early completion, EE and partial-context
gates, one-shot callback effects, restored caller registers/scheduler depth and
exact callback cycle credit, plus all failure paths. The real JKRDvdFile callback
is still to be proven in the next isolated authentic trial, not by host execution
of game code. Missing enqueue behavior is preserved in
`logs/ww16-dvd-callback-red.log`; the real ARM host link now succeeds in
`logs/ww16-dvd-callback-link.log`.

Linux AArch64: **472 host tests pass** (`logs/ww16-dvd-callback-green.log`); audit,
attribution and whitespace checks pass. Shared callback code only exposes its
existing sentinel constant; Windows CI/hardware remains untested here. The next
committed ARM build/trial follows. No first visible pixel or FPS claim yet.

## Unit 11: identify the real REL request without claiming a load

Authentic trial `ec5ac8f1e7054f6faf4b28af89a175bb` at `49caf81` proves **116
completed guest callbacks, zero failed callbacks**, and 123 real reads totalling
961,120 bytes through vita-sceio. It reaches **78,789,721 cycles**, then stops at
`80240744`, the archived REL materialization edge. Installation takes 1.26 seconds
and the guest phase 9.89 seconds. The closed report and 64 journaled commands
validate; the same post-report Vita3K teardown crash exits 133. Renderer counters
remain 265 completed GX setup commands, 1,759 bytes, zero draws/presentations.

The diagnostic now records the module name and object at that edge, then stops
without writing the module object, copying a REL, returning success, opening a
disc or emitting a `[rel-load]` marker. It reuses the bounded mapped string reader
from the DVD path. Invalid objects/strings and trace failures remain loud STOPs.
The guest object fields match the existing desktop dynamic-load observation
(`runtime/host/src/main.c`, object `+0x10` module and `+0x1C` name).

The next implementation needs a deliberate mapping/provider decision. The shared
desktop materializer reads privately prepared raw `.rel` files and reserves
scratch at `81820000..81F80000`, above this host's **24 MiB MEM1**. The current
private build has 415 raw RELs totalling 6,996,836 bytes (largest 154,876 bytes),
but neither a Vita provider for them nor a scratch mapping is wired. The
generated section aliases and shared first-fit allocator already exist; blindly
copying the desktop path would not establish safe ownership or mapped storage.
No source pin, translated module, MEM1 size or guest loading behavior is changed
by this diagnostic. Actual materialization waits for that decision.

Linux AArch64: **475 host tests pass** (`logs/ww16-rel-frontier-host-tests.log`),
including name logging, invalid input refusal, unchanged guest objects and no
disc access. The trace-failure injection check is recorded red before extending
the existing probe wrapper (`logs/ww16-rel-frontier-trace-red.log`); it does not
claim a test-first cycle for the earlier diagnostic edit. Audit, attribution and
whitespace checks pass. A clean committed ARM build and isolated diagnostic
trial follow; no visible game pixel, physical Vita result or FPS claimed.

The clean ARM/VPK rebuild at `b3f45f4` succeeds. Diagnostic trial
`a802fc25c74e443b9851877f52f3f802` identifies **`f_pc_profile_lst`**, object
`803A71C0`, module pointer zero, at `80240744`. Its existing private raw file is
16,500 bytes and header module id 1, matching the translator catalog. The host
still refuses materialization at 78,789,721 cycles; it emits no load event and
does not change the object. The same 116 callbacks complete with zero failures,
123 checked reads total 961,120 bytes, and draws/presentations remain zero.

This attempt was watched while live: `evidence/run-watch.jsonl` retains the
verified staged Vita3K PIDs, per-second CPU observations and log tails. Active
guest CPU samples range from 98.9% to 123.8% of one CPU with log progress.
Installation stops at its verified payload after 1.19 seconds; guest exits after
9.58 seconds. The brief zero-CPU tail precedes the already recorded post-report
shutdown crash (133), not a 30-second hang. Both observed Vita3K PIDs are gone.
The launcher validates the closed report and 64 journaled commands, preserves
the nonzero exit, and reaps its owned sandbox. All evidence stays private.

Provider/scratch integration remains pending: a read-only bind of verified raw
RELs plus bounded owned storage through the existing alias API would preserve
24 MiB MEM1; alternatively, loading from the existing archive would require its
resource extraction contract. Neither path is implemented or silently selected.

## Unit 12: read the requested raw REL from the ISO and map bounded scratch

The resumed scope selects an ISO-backed provider, not prepared loose REL files.
`rel_provider.c` resolves `/RELS.arc` with the unchanged WW13/pinned DVD FST,
opens the already verified private image with WW13's read-only transport and
requires exact reads. It selects the requested case-insensitive module name,
refuses ambiguous names, and decodes only the selected member. RARC parsing and
the importer's existing bounded Yaz0 decoder are extracted into
`apple/ios/src/rel_archive.h` and reused by the first-run importer, not copied.
There are no new dependencies or source-pin edits. Archive and raw-image bounds
are 16 MiB and 1 MiB respectively; no archive cache is retained.

The real archive is RARC, 943,360 bytes. The requested profile-list REL is version
3, 16,500 bytes, with 17 section entries and an import table. The layout is sourced
from `disc_import.c`'s archive extraction and the desktop REL materializer;
`docs/vita/VITA-WW-14.md` names the materialization/link/lifecycle boundaries.
Raw module id, version, file size and section-table metadata must match the
compiled module catalog; section/import extents are checked before mapping.

`rel_service.c` reuses the shared first-fit allocator over
`81820000..81F80000`, excluding every compiled code range and linked section
(including their tagged address form), and refuses any other alias owner. Only
the chosen 32-byte-rounded range is backed by a runtime-owned alias. **MEM1 stays
24 MiB**. Copy verification precedes recording the owned image; failed allocation
or logging rolls back the unpublished mapping. Shutdown frees it through the
existing alias cleanup and resets its accounting. The bounded single image is
appropriate while linking remains the next unsupported boundary; no speculative
slot cache, unload implementation or active-set measurement is added.

The desktop's `do_link` interception also binds the prelinked composite and, for
module 1, publishes the profile-list global. That is distinct from a raw copy.
This unit therefore emits `[rel-materialized]`, then explicitly stops for the
relocation/link/prolog contract, without setting the guest object's module pointer,
returning success, executing a fabricated prolog or emitting `[rel-load]`. A real
materialization is not yet a linked module. The next unit must source and test
that static-link contract before claiming a successful `do_link`.

Linux AArch64: **486 host tests pass** (`logs/ww16-rel-materialize-green.log`),
including raw and Yaz0 member/wrapper copies, case handling, catalog mismatch,
short/missing reads, bad ranges, exhausted/aliased scratch, allocation failure,
trace rollback, preserved guest object and unchanged MEM1. The initial missing
mapping test is red in `logs/ww16-rel-materialize-red.log`. The actual ARM host
links (`logs/ww16-rel-materialize-arm-link.log`). A fresh private importer run
checks `main.dol` and **all 415 REL files byte-identical** to the previous verified
import (`rel-importer-check-h211_gub/result.json`); it executes no guest code.
Audit, attribution and whitespace checks pass. Windows verification is tracked
in `docs/WINDOWS_TASKS.md`; it was not run. A clean committed VPK build and watched
authentic materialization trial follow, with the existing inactivity watchdog.

The committed VPK rebuild at `e8f6c6a` succeeds. Watched authentic trial
`30c30fe7ca064d528c372928120e4274` reads/decodes the requested ISO member but
refuses its catalog check before mapping: **`REL raw image metadata disagrees
with the compiled descriptor`**, at the same 78,789,721 cycles. Installation is
1.24 seconds, guest 9.54 seconds; zero draws/presents and the known post-report
teardown crash 133 remain. No successful materialization is claimed for this run.

This reveals a producer naming defect, not a different REL: the immutable
`scripts/generate_composite.py:118-127` reads the descriptor's field named
`version` from **raw +0x04 (next-link)**, not the actual version at **+0x1C**.
The existing composite exports zero in that field for module 1. The guard now
compares that legacy catalog word to its actual producer input and independently
validates the real version (1..3); file size, section count and table offset
remain exact catalog matches. Changing the generator/composite would invalidate
the private verified digest and is outside this seam: neither is rewritten.
The synthetic descriptor now models the real producer, and a changed catalog
word is still refused. This reproduction is red in `logs/ww16-rel-catalog-red.log`.

**486 host tests pass** after the producer-contract correction
(`logs/ww16-rel-catalog-green.log`); audit, attribution and whitespace checks pass.
The next clean committed rebuild and watched trial follow immediately.

Watched trial `f2be755920014d42a910d4ac1691834d` at `bf2d61e` proves the requested
raw module was materialized: **module 1, `f_pc_profile_lst`, 16,500 bytes at
`81822E80`, source=iso-RELS.arc, backend=vita-sceio**. The owned alias rounds to
16,512 bytes, outside 24 MiB MEM1 and outside linked section/code extents.
The next explicit STOP is **relocation/link/prolog**, at the same 78,789,721 cycles;
raw materialization is not claimed as successful `do_link`. Installation is 1.23
seconds, guest 10.31 seconds, with live CPU/log observations retained in
`evidence/run-watch.jsonl`. The known post-report crash remains 133. No draw or
presentation occurs; the closed report and 64 journaled GX commands validate.

## Unit 13: the sourced profile-list static-composite link contract

`runtime/host/src/main.c`'s materialization branch publishes the module pointer,
then `host_activate_rel_profile_list` binds module 1's prelinked section 4 to
**`g_fpcPf_ProfileList_p` at `803F6A68`** before returning TRUE to the actual
caller. The private translated module's publication routine confirms that same
global and prelinked data address. This is the shared desktop compatibility
contract, not a generic relocation engine or proof that a guest prolog executed.

The Vita lane now services **only the owned module-1 contract** after the proven
ISO materialization. Its linked data must be present, byte-identical to the
borrowed immutable composite export, and match the module's section descriptor
and raw section size. Unsupported BSS, absent/changed data, an unaligned return,
another object's pointer/global or a failed trace stops before publication.
Only after those gates does it publish the raw module pointer and the already
linked profile table, return TRUE and resume at the real LR. Repeated calls for
that same owned resident image do not read or materialize again. Other module
owners/link contracts remain explicit STOPs; no constructor, generic relocation,
unload, active-set measurement, frame or timing credit is fabricated.

The profile-data selection is extracted into shared `rel_profile_list.h` and
reused by desktop's existing publication routine; its selected entry, global and
return behavior are unchanged. The Vita lane borrows the already provided REL
exports until shutdown, without owning or freeing those static bytes.

Linux AArch64: **490 host tests pass** (`logs/ww16-rel-profile-link-green.log`),
including exact publication, preserved unrelated object bytes, repeated-link
no-read behavior, invalid binding/return refusal and trace-failure preservation.
The missing contract is red in `logs/ww16-rel-profile-link-red.log`. The real ARM
host links (`logs/ww16-rel-profile-link-arm.log`); audit, attribution and whitespace
checks pass. Windows/desktop gameplay and physical Vita remain untested. The next
committed VPK rebuild and watched authentic continuation follow immediately.

Watched authentic trial `64cfb05ca68748cb8e90dfe2bb8e8670` at `d091955` proves
the owned module-1 static contract: raw module at `81822E80`, prelinked profile
table **`C1F80178`, 2,012 bytes**, published before returning to the real guest.
Execution advances to **493,979,208 cycles**, then stops on the actual delivered
decrementer vector **`00000900`** (`srr0=80307EFC`, cause 8 in the vector).
Installation is 1.39 seconds; the guest phase is 19.14 seconds. CPU and log
progress were observed throughout; no 30-second inactivity occurred.

The original launcher result remains **INCOMPLETE/status 2**: its shared FP
configuration-log reader refuses the now 1,597,866-byte process log at the old
1 MiB bound before report certification. The emulator still exits 133 during
post-report shutdown. Separate `evidence/protocol-check.json` validates only the
closed report and 64 journaled commands, explicitly not full launcher readiness.
That preserves the failed result rather than rewriting it. Renderer counters
still show 265 commands / 1,759 bytes / zero draws or presentations.

## Unit 14: bounded validation for the longer authentic WW05 trace

WW05 now explicitly allows a **64 MiB process log**, matching its existing GX
journal bound, through optional limits on the shared configuration-log check.
The shared FP campaign's default remains **1 MiB**; its runtime-cache bound,
configuration schema, ownership/source checks, report validator and stop handling
are unchanged. Oversize WW05 logs still fail. A configuration error after the
first megabyte is still inspected and refused, never hidden by truncation.

The real oversized-log failure and the synthetic reproductions are retained;
`logs/ww16-long-launch-log-red.log` records the failed tests before the change.
Linux AArch64: **493 host tests and 80 shared FP tests pass**
(`logs/ww16-long-launch-log-green.log`, `logs/ww16-long-log-fp-regressions.log`),
including accepted long traces, late configuration errors, bounded-log refusal
and the unchanged FP default-limit test. Audit, attribution and whitespace checks
pass. The next clean rebuild and watched continuation will certify the report
through the complete launcher again; no pixel/FPS or physical Vita claim is made.

## Unit 15: include Vita3K's duplicate cache trace in the same bounded gate

Watched trial `e8da4458aea54d55b703ba96e6df912a` at `070e354` reproduces the same
successful ISO materialization/profile-list binding and honest `00000900` STOP.
The process-log check now passes, but Vita3K duplicates the trace into its cache:
**1,597,797 process-log bytes / 1,593,344 cache-log bytes**. The latter still hit
the shared FP 1 MiB default. Its original launcher result is preserved as
INCOMPLETE/status 2, with the exact cache-check traceback; no certification is
forced. Installation is 1.31 seconds and guest 18.11 seconds, continuously
observed, with the unchanged post-report emulator exit 133.

The optional phase limit now applies consistently to both traces: **WW05 64 MiB,
FP default 1 MiB**. Both logs retain their regular-file/range checks; cache logs
remain privately owned and unshared. Both are scanned fully for configuration
errors, including after the first megabyte. Cache evidence is retained instead
of trimmed. The default FP cache limit and oversize WW05 cache refusal are
explicit regression tests, alongside accepted large cache traces and late errors.

Linux AArch64: **496 host tests and 81 shared FP tests pass**
(`logs/ww16-long-cache-log-green.log`, `logs/ww16-cache-limit-fp-regressions.log`),
with red evidence in `logs/ww16-long-cache-log-red.log`. Audit, attribution and
whitespace checks pass. Another clean build and watched authenticated trial
follow; gameplay code is unchanged by this gate correction.

The subsequent physical decrementer-vector seam is already sourced, not executed:
the authentic 152-byte `00000900` observation from run `64cfb05c...` matches the
verified retail DOL template with **only exception-number immediate 8 changed**.
Private lineage evidence is `evidence/decrementer-vector-lineage.json` in that
run; SHA-256 `bc97b5f351867639d7c79721c1ed87d8670e4ac1ae741d89ef1782906f00e352`.
No vector code or disc bytes are committed, and no timer skip, fake IRQ, frame
or FPS result is claimed.

The clean build at `1277832` succeeds. Watched authentic trial
`6ee5339a723342c79b639e27fad8a8a1` now completes the entire launcher validation:
**closed report, 64 GX journal entries, rendered/presented=0**, with the real
`00000900` STOP at 493,979,208 cycles. Installation takes 1.25 seconds and guest
16.42 seconds. The process/cache bounds accept the intact traces without
truncation; result status remains **133**, not a passing emulator exit. Source,
input and preparation gates all run. The known shutdown crash remains unresolved.

## Unit 16: dispatch the exact observed decrementer vector

The authentic 152-byte vector at `00000900` has the same verified retail DOL
template lineage as the existing physical external/FP vectors, except for cause
**8**. The existing private emitter now accepts that observed base and emits its
function/reference pair; `game_build.py` validates the captured binary's exact
SHA and template lineage, includes it in the private build and snapshots its
input/source identities. The generated game-derived code stays private.

The host registers it in the existing observed-vector dispatcher. The same
full-byte comparison, physical MSR mode, aligned PC, exact memory extent and
exception-state guards apply before execution. The existing decrementer delivery,
deadline, saved SRR0/SRR1, cycle accounting and rebudget behavior are **unchanged**.
No timer skip, fake IRQ, synthetic return or host-call bypass is added: the real
translated stores and RFI run under the remaining instruction budget.

The existing vector probe is reused for cause 8. Tests cover each mutated byte,
invalid mode/PC/state/range, zero remaining quantum and one-cycle resumption with
actual saved context and RFI. Input tests refuse every unrelated observed byte
change, the wrong exception immediate and unrelated source changes; valid input
uses physical base `00000900`. The new emitter-base test is red before support
(`logs/ww16-decrementer-vector-red.log`). The full-suite run also exposed two
configuration fixtures still supplying only two vectors; they now provide the
required third synthetic source, preserving the real-input preflight gate.

Linux AArch64: **510 host tests pass** (`logs/ww16-decrementer-vector-final.log`),
and the actual ARM host links (`logs/ww16-decrementer-vector-arm-link.log`). Audit,
attribution and whitespace checks pass. No source pin, composite digest, runtime
default or desktop behavior changes. The next clean committed build and watched
authentic continuation follow; no visible pixel, FPS or physical Vita claimed.

Watched trial `4ce56e3c1971443dbe1dfee117cb525f` at `21f6a64` proves **eight real
decrementer deliveries and eight verified-vector RFI handoffs to `803025D4`**.
The last progress sample reaches **4,187,606,691 executed guest cycles**. It then
repeats `80307EF4`, with the timer rearmed and no further journaled GX entries.
There are 127 read requests (including four post-link audio archive reads) and
116 completed DVD callbacks. CPU/log progress stay high, so this is not an
inactivity-watchdog hang. The deliberately shortened **60-second phase ceiling**
stops/reaps it; installation took 1.18 seconds. The observed emulator is gone.

There is no closed report: result stays INCOMPLETE/UNAVAILABLE, process_status 124,
with status 2 for the missing report. The partial report, native log, CPU/log
watch and `evidence/active-plateau-analysis.json` are retained. No signal was sent
by the later manual-stop check because the process had already exited. There is
no newly certified renderer count or pixel/FPS claim from this unfinished run.

## Unit 17: observe the sourced SelectThread wait, without inventing a wakeup

The private translated `80307EF4` loop loads `r13-26288`, tests zero and loops;
the shared desktop labels it **SelectThread's run-queue wait** and already
observes the retail run bits/current thread/context, thread state/wait queue and
saved stack frames. This evidence does not establish a VI failure or permission
to wake a thread, so none is assumed.

The Vita lane now emits one post-link idle observation, reusing a shared pure
selection of the existing desktop's run bits/current thread/context values.
It records the actual r13-derived poll address/value, scheduler depth, pending
DVD work and live GX/renderer counters (with an explicit availability flag).
The sourced default/main/DVD thread records, bounded active-thread list and
four saved frames per thread identify the actual caller/wait, not a guessed
future event. Reads require bounded real MEM1; trace failures stop loudly.
It changes no guest RAM, register, runnable bit, queue, callback or clock budget.
The observer's once-only latch is host diagnostic state only.

Linux AArch64: **514 host tests pass** (`logs/ww16-scheduler-idle-green.log`),
with final observer checks in `logs/ww16-scheduler-idle-final-focused.log`; tests
compare all guest RAM before/after, preserve PC/cycles, exercise once-only and
unlinked guards, and refuse failed trace output. Red evidence is
`logs/ww16-scheduler-idle-red.log`. The actual ARM host links
(`logs/ww16-scheduler-idle-arm-link-final.log`); audit, attribution and whitespace
checks pass. The desktop's initial idle diagnostic uses the same three reads
through the shared helper; no desktop scheduling behavior changes. Windows
verification remains unavailable and is tracked. The next committed build and
short watched diagnostic trial follow immediately.

## Unit 18: keep an owned deadline without a closed report honest

When a verified owned guest phase reaches its timeout or inactivity stop before
closing `ww05.report`, the launcher now preserves **124 or 125 / INCOMPLETE /
UNAVAILABLE** and the partial-log/error context. This is not report certification.
Only the actual missing report filename is treated as expected after those
stops; a missing approved input or any other exception still returns status 2.
The earlier failed run results are not rewritten.

The no-report deadline test is red before the fix
(`logs/ww16-timeout-status-red.log`). Linux AArch64: **516 host tests pass**
(`logs/ww16-timeout-status-green.log`), including untouched partial logs and a
missing verified DOL after timeout that must not be hidden. Audit, attribution
and whitespace checks pass. The next clean build includes the read-only scheduler
observer and this status correction; a short watched trial will identify the
actual waiting thread/caller, without forcing a wakeup or claiming a frame.

Watched trial `c8e99533a39d458294b36ea1fff1ec6a` at `2e8deb1` identifies the wait:
at 78,796,192 cycles, run bits/current thread/lock/DVD pending are zero, and
`r13-26288` resolves to the real run bits at `803F7A30`. Main thread `803A2960`
waits on `80429EF0`; saved stack returns through `80305A28` to **`80255E18`**.
The read-only renderer snapshot proves **265 decoded commands / 1,759 FIFO bytes /
zero draws or presentations**. The run later executes two decrementer deliveries,
reaches 1,136,685,334 logged cycles, and is stopped/reaped at its 20-second ceiling.
Result is correctly **124 / INCOMPLETE / UNAVAILABLE**, not a closed report; the
snapshot and `evidence/scheduler-wait-analysis.json` preserve that distinction.
The observed emulator is gone. No runtime wakeup is inferred just from the wait.

## Unit 19: opt-in guest-cycle VI clock, using the existing interrupt model

The translated caller at `80255E18` receives from the video message queue;
the shared desktop traces `JUTVideo::postRetraceProc` at `802C8110`, and the
translated routine calls the real message-send path at `80305908`. Desktop's
`host_vi_advance_cycles` uses pinned `DolViClock` to assert the existing
`DolInterrupts` VI status. The Vita host already has those VI registers and the
real external-vector delivery, but **never advances a VI clock**. This sources
the missing producer rather than granting permission to fabricate a message.

The private boot host now advances that same pinned clock from **credited executed
guest cycles**, with the desktop cadence: **8,100,000 cycles per retrace (486 MHz /
60 Hz)**. Those unchanged constants move to shared `guest_timing.h`; desktop
behavior is unchanged. The next VI deadline bounds dispatch. Due events assert
the existing VI status/source; the existing VI/PI masks and MSR EE gate real
external-vector delivery. The translated handler/callback remains responsible
for acknowledgement, queue messages and waking threads. This unit does not call
the renderer's present function, count a retrace as a frame, alter guest RAM,
grant timebase credit, skip instructions or change the decrementer.

**Off by default:** `WW05_ENABLE_VI_CLOCK=OFF`; only the explicit private builder
`--vi-clock` enables it, resets the CMake option for each build and records the
choice in `build-evidence.json`. The native journal records the enabled state,
period, first 16 retraces and live idle-snapshot count. No pin/composite edits or
network operations are made. Windows verification of the unchanged shared timing
constants is tracked; desktop, Windows and physical Vita gameplay remain untested.

Linux AArch64: **530 host tests and 81 FP tests pass**
(`logs/ww16-vi-clock-green.log`, `logs/ww16-vi-clock-fp-regressions.log`). The actual
cycle-threshold test fails before implementation (`logs/ww16-vi-clock-red.log`).
Tests cover the default-off/build opt-in, exact threshold/deadline, VI/PI/EE masks,
prior exception preservation, real VI acknowledgement, elapsed periods, zero
charge, timebase writes and failed trace handling; every clock probe compares all
guest RAM before/after and observes no GX/message/frame work. The actual ARM host
links (`logs/ww16-vi-clock-arm-link.log`). Audit, attribution and whitespace checks
pass. A committed **opt-in** rebuild and fresh watched authentic trial follow.

## Unit 20: CPU OR guest-output growth prevents false inactivity

Coordinator priority supersedes the pending VI experiment: fix inactivity
accounting, then replay the **exact archived `2e8deb1` guest build** without VI.
The opt-in `c93951d` build completed, but no VI-enabled emulator trial was started.
The lane/branch and previous work remain intact; no source reset or data deletion.

The distinction is important: neither `4ce56e3c...` nor `c8e99533...` is a Vita3K
crash. Their actual `phases/guest/evidence/process.json` says **timeout / exit -15
(owned SIGTERM)**, at **60.006 s** and **20.001 s** respectively. They were killed
by their explicit phase ceilings, not an `inactive` observer result. The old
watchdog nevertheless had the CPU-only false-hang risk described by the
coordinator: real I/O progress must keep a low-CPU process alive.

Activity now means **CPU at least 5% OR positive size growth of an owned output**.
The 30-second idle window resets on either. The launcher samples the real phase
process log, Vita3K cache log, guest journal/partial/report/dispatch marker and,
while installing, the actual payload files. It does not read their contents or
walk broad trees. Symlinks, unowned/nonregular/multilink files and metadata errors
are refused; missing not-yet-created files are normal. A replaced regular log can
report new output; same-size overwrites are not growth.

Delegated choice: "any evidence" means the **process-written outputs above**, not
our observer's `run-watch.jsonl` or plan/result bookkeeping. Otherwise a monitor
would keep its own hung guest alive forever. The native monitor and CPU-tree
accounting remain active; true silence plus low CPU still TERM/KILL/reaps early.
The independent phase ceiling remains explicit and is not called inactivity.
The next baseline trial uses the longer authorized ceiling, not 20/60 seconds.

Linux AArch64: **540 host tests and 81 FP tests pass**
(`logs/ww16-output-watchdog-green.log`, `logs/ww16-output-watchdog-fp.log`). Real
subprocess regressions prove low-CPU stdout/native-journal writers exit normally
past the idle window; the original false-inactivity tests fail before the change
(`logs/ww16-output-watchdog-red.log`). Deterministic checks cover the 2%-CPU growth
case, silence after growth, each watched output, rotation, same-size writes,
symlink refusal and exclusion of our own watch journal. Explicit ceilings still
return timeout, not inactive. Audit, attribution and whitespace checks pass.
No guest code, ABI, graphics, timer, source pin or composite digest changes in
this unit. The baseline uses a preserved receipt/package, not a rebuild with a
different guest SHA; launcher and guest provenance will be recorded separately.

## Unit 21: replay a saved baseline without rewriting either source identity

Delegated choice: replay the **original archived VPK**, not a new rebuild bearing
a different guest commit label. `--baseline-build` selects an explicit preserved
`artifacts-*/build-evidence.json`. Its full guest commit must be an ancestor of the
current clean launcher checkpoint; errors/unrelated commits are refused. Pins,
composite digest, snapshot footprint, all artifact hashes and the packaged
SELF/SFO/DOL still pass their existing checks. The selected receipt is rehashed
before/after each phase; the launcher itself must remain at its recorded clean
SHA. Ordinary launches still require HEAD == build source, with both the ordinary
offline footprint and its preserved snapshot checked. No HEAD/source test is
bypassed or patched out.

Plan/execution/result record **guest source separately from launcher source**.
The guest request/report uses the real compiled guest source. Immutable snapshot
footprint replaces the moving latest-build footprint only for an explicit
baseline. The previous `2e8deb1cc2b04a221efbbfa60ab1e64bb109cd81` receipt is
`artifacts-26970c401eac46e5a2790f54f3f2dd93/build-evidence.json`; it predates VI
support, so this trial cannot enable the new clock. The finished VI build and
current source branch remain untouched. A new isolated profile is mandatory.

Linux AArch64: **551 host tests pass** (`logs/ww16-baseline-source-final.log`),
with red evidence in `logs/ww16-baseline-source-red.log`. Tests check explicit
ancestry, default stale-build refusal, matching default checkpoint, Git failure,
changed footprint/pins/composite, misplaced receipt, changed current launcher
and mutated selected receipt. Audit, attribution and whitespace checks pass.
This launcher-only correction deliberately does **not rebuild the old guest**:
the requested authentic replay must use its already verified exact artifact.
The watched trial follows at the longer authorized 600-second ceiling, with
inactivity requiring **both silence and low CPU**; an explicit deadline, if hit,
will still be reported as timeout, never a spontaneous crash or certified boot.

The exact baseline replay is `26b9c4f4d837452d83587bb4cdad7350`: guest source
**`2e8deb1`**, launcher **`356fb91`**, not a relabelled new guest build. The fixed
observer never reports inactive. The guest reaches its explicit **600.001 s
ceiling**, is TERM/reaped (exit -15, process_status 124), and remains in SelectThread
at `80307EF4`; no closed `ww05.report` exists. Global result is INCOMPLETE/status 2
because the 132,312,305-byte process trace exceeds the existing 64 MiB validation
bound. The result/logs are retained intact, not rewritten or trimmed.

`evidence/trace-baseline-analysis.json` records **46,165,817,388 last logged cycles,
94 real decrementer deliveries, 127 DVD reads, one raw materialization and one
module-1 link**. The native journal is 207,399,113 bytes / 1,769,311 lines;
the process trace contains **1,764,093 `sceIoWrite` TRACE lines**. The observed
emulator is gone. These counts are mostly the sourced scheduler wait, not evidence
that many RELs ran or that a frame was presented. The initial snapshot remains
265 GX commands / 1,759 bytes / zero draws or presentations.

The poll is specifically `lwz r0,-26288(r13); cmplwi r0,0; beq 80307EF4`.
Actual r13 `803FE0E0` resolves to run bits at **`803F7A30`**, not a VI MMIO register.
Main's saved receive caller `80255E18` waits on the video queue at `80429EF0`.
The timer is demonstrably delivered/rearmed; DVD pending is zero. Shared desktop
code identifies the normal producer as VI handler -> `JUTVideo::postRetraceProc`
-> real message send. The missing VI-clock producer is already implemented,
tested and default-off in `c93951d`; no artificial callback/wakeup will replace it.

## Unit 22: remove emulator TRACE amplification from future private profiles

Coordinator-approved profile change: **`log-level: 0` -> `log-level: 2` (Info)**.
Local read-only Vita3K source, `config/src/config.cpp:387,460-468`, explicitly maps
0=TRACE, 2=INFO and applies the selected level to the guest logging backend;
`util/src/logging.cpp:141-142` passes it to spdlog. This keeps Info/warnings/errors
instead of suppressing failure evidence. The installed executable's full source
mapping remains the existing UNVERIFIED provenance; the next real run must verify
the emitted config and observed syscall-trace suppression empirically.

The shared FP/WW05 template changes **one byte only**; all other bytes of the old
neutral schema are tested identical. Baseline and cpu-opt-off identities are
updated together, still 4,284 / 4,285 bytes and 154 unique keys. The alternate
continues to differ only in its cpu-opt line. Config metadata explicitly records
the authorized log-level change; unknown changes and late parse errors remain
refused. Old run configs/results are untouched. No renderer, guest code, IRQ,
frame counter, CPU optimization, pin or composite digest changes.

Linux AArch64: **551 host tests and 83 FP tests pass**
(`logs/ww16-info-config-host-green.log`, `logs/ww16-info-config-fp-green.log`), with
red/green profile checks in `logs/ww16-info-config-red.log` and
`logs/ww16-info-profile-{red,green}.log`. Audit, attribution and whitespace pass.
The change was deferred until the live TRACE run finished, preserving its clean
source/config checks. Reused immutable guest artifacts keep comparisons honest.

Baseline throughput receipt: in the fixed `80307EF4` wait, samples **10..70 seconds
after the first observed idle sample** give **77.744 M executed guest cycles per
wall second** over 59.074 seconds with TRACE. This is a single diagnostic-throughput
observation, **not FPS or gameplay performance**. The next Info-only run reuses
the same `2e8deb1` guest for that window, then the opt-in VI artifact is exercised
separately. The shared template benefits future lanes after integration; absolute
numbers from old TRACE runs remain log-contaminated and are not silently revised.

Watched Info replay `1ed418a3151e44e29caa3245a4283598` uses the exact same guest
`2e8deb1` and launcher `b368ad1`. Emitted config records **`log-level: 2`**; the
actual runtime announces **Info** and emits **zero `sceIoWrite` TRACE lines**.
The guest process log is **12,848 bytes**, versus 132 MB for the longer TRACE
run. Native guest journaling remains intact (39,956,520 bytes). The deliberate
120-second measurement ceiling stops/reaps the unchanged video wait; global
status is correctly **124 / INCOMPLETE**, no closed report, no inactive stop.

Same guest / `80307EF4` / relative 10..70-second window:
**77.744 -> 78.385 M cycles per wall second (+0.824%)**, with median emulator CPU
**156.2% -> 105.9%**. This single pair demonstrates eliminated syscall TRACE
amplification and reduced median CPU consumption, **not a significant speedup,
FPS gain or faster gameplay**. Source/phase are held constant, but host scheduling
and other activity are not controlled enough to certify a sub-percent difference.
Full receipts are `evidence/log-level-comparison.json` in the Info run and
`evidence/trace-baseline-analysis.json` in the TRACE run. The observed emulator is
gone; previous results are not relabelled. Info does not fabricate the missing VI.

The next authentic trial explicitly selects the preserved VI-enabled artifact
**`c93951d1e5cd56595fe2be9e591cda86d7cbec5e`**, receipt
`artifacts-3d147fbbfbe94d449979c7480a5ea917/build-evidence.json`, whose config says
`vi_clock: true`. It was built before the coordinator's watchdog priority change
and has not yet executed. Selecting that unchanged artifact avoids a redundant
rebuild/relabel and keeps guest and launcher provenance distinct. Info and the
corrected CPU/output-growth observer remain in the new profile; no synthetic
message, callback, retrace-as-frame or forced runnable bit is introduced.

## Unit 23: authentic VI delivery reaches the first primitive boundary

Watched trial `910ccc01648a41d891601bd42c1f0fa7`: guest **`c93951d`** (VI enabled),
launcher **`f769519`**, new isolated Info profile. The first due retrace occurs at
**8,100,000 executed cycles**, asserts real VI status/cause under the existing
guest masks, and immediately enters the verified physical `00000500` vector.
The translated vector saves the real context and hands off by RFI to `80304E24`.
This is a real guest interrupt path, not a host-invoked JUTVideo callback.

The guest progresses to its first primitive header. New closed STOP:
**`unsupported first GX opcode (draw requires preceding layout)`**, FIFO
`CC008000`, writer PC `80323194`, at **14,491,186 cycles**. The stream decoded
**989 commands** and the renderer received **6,941 bytes / 2,012 events**;
plans/draws/presentations are still **zero**. Installation is 1.23 s, guest 9.12 s.
The full launcher certifies the closed report and 64 logged commands, while
preserving the known post-report shutdown **SIGSEGV / exit 133**. This is not a
timeout/inactivity kill or gameplay success. The observed primitive is not yet
a submitted GPU draw or visible pixel.

## Unit 24: frame primitives using the already accepted CP layout

Source investigation corrects the error message's implication: the preceding
989 commands have been accepted; CP VCD/VAT state is retained in `host->gx` and
the pinned CP loader derives its vertex layouts. **The checkpoint's framing
switch had no primitive cases at all**. The missing piece is packet byte length,
not an invented layout or a replacement for renderer state interpretation.

`gx_checkpoint.c` now uses the pinned layout and wire shape from
`GXRuntime/src/gx_recomp_parser.c:266-285` and
`graphics/frontend/src/retail_gx_frontend.cpp:1010-1048`: retail primitive opcode,
big-endian u16 count, low-three-bit vertex format, then count * derived vertex
size bytes. Derivation reuses `dol_gx_recomp_derive_vertex_layout`; no VAT/attribute
math is reimplemented. Only a complete bounded packet is replayed by the same
pinned parser and forwarded unchanged to WW17. First-frontier bytes/provenance
remain intact. Reserved `0x88`, absent layout and packets above the existing
1,025-byte bound stop explicitly; zero-vertex draws remain refused by this
checkpoint's C parser rather than being declared a successful draw. No fake
vertices, padding, counters or presentations are added.

The original journal deliberately keeps only the **first 64 commands**, so the
commands immediately before number 990 cannot be reconstructed honestly from
that run. The new one-shot draw audit retains **16 recent complete commands** and
**64 recent CP writes**, with exact byte/PC/order provenance, and records the
effective VCD/VAT/validity/stride when the first actual three-byte draw header
arrives. It freezes after that audit and never becomes a per-frame logger.
This bounded private capture supplies the requested sequence on the next real
attempt without claiming that an observed header was accepted/submitted already.
Logging failures stop before completed-draw publication.

Linux AArch64: **561 host tests pass** (`logs/ww16-gx-draw-final.log`), including
actual preceding CP loads, split vertex payload, nonzero format, later VAT stride
change, following-command alignment, missing layout, reserved/zero/oversize
refusals, exact ordered history/wrap and failed audit handling. The draw test is
red before framing support (`logs/ww16-gx-draw-red.log`); focused and ARM link
evidence are `logs/ww16-gx-draw-audit-{focused,arm}.log`. Audit, attribution and
whitespace checks pass. MEM1, pins, composite and renderer source are unchanged.

The coordinator's VITA-WW-18 coverage commits are available locally through
`f734af1`, but this branch still carries the earlier WW17 renderer; its remaining
coverage must be reused from that lane, not reimplemented in the stream. This
unit changes only framing/capture. A fresh committed **VI-enabled** rebuild and
watched continuation follow; any nonzero plan/draw/present is reported immediately.

## Unit 25: first authentic primitive submitted to vitaGL (2026-10-13)

The committed VI-enabled build at **`10ac0ce`** succeeds. Watched authentic run
**`d98d1753b7904bfa82c72a883dc43af2`** proves the framing seam reaches WW17's
actual Vita backend: **plans=1, draws=1, programs=1**, no skipped plan, refused
program, copy refusal or gap. This is a real vitaGL submission with compiled Cg,
**not proof of GPU completion, presentation or a visible game pixel**.
Display copies/presentations/rendered remain zero.

The private first-draw audit identifies command **990**, **quad opcode `80`**, four
vertices, format 0, **16-byte stride / 67-byte packet**. Effective VCD is
`00002200/00000000`; VAT 0 is `5EB77009/B9DCEEF5/3B9DCEE7`, all validity flags set.
This means direct XYZ float positions (12 bytes) plus direct RGBA8 color (4 bytes),
using the pinned CP derivation. CP registers **50/60** (VCD) at commands 963/964
and **70/80/90** (VAT format 0) at 966..968 are captured before the draw; the last
16 commands configure the remaining VAT formats through format 7, ending at 989.
No unrecognized preceding state opcode had to be invented. Full exact private
order/bytes/provenance remain in `evidence/first-draw-layout-history.txt`, with
the verified summary in `evidence/first-draw-analysis.json`.

The closed report advances to **1,136 decoded commands / 8,189 renderer bytes /
2,288 events**, then stops at **`80308A9C`**, at 14,507,593 cycles. Installation
is 1.24 s, guest 11.60 s. Source/receipt/input/preparation/report gates certify the
closed frontier and 64 journal entries. The known post-report shutdown crash
remains exit **133**, not masked as emulator success; no watchdog kill occurs.
The observed emulator is gone. No physical Vita result or FPS measurement.

## Unit 26: let the observed retrace caller execute real guest sleep code

The new STOP is **OSSleepThread**, but not GXDrawDone's completion barrier:
actual LR **`80313A2C`**, queue **`803F7B44`**. Shared desktop observations name
`80313A04` as **VIWaitForRetrace**; its authentic translated code loads the retrace
counter, computes `r3=r13-26012`, and calls `80308A9C` before comparing the counter
again on return. The same desktop interception address is also used to publish
PE finish **only for LR `80322C38`**. Applying that service here would invent a GPU
completion unrelated to this observed caller and is explicitly not done.

The Vita edge gate now lets this one observed route reach its compiled guest
body, with **module ownership, exact LR, exact real-MEM1 queue and SDA relation**
checked. Other callers/queues/SDA bases and an aliased queue still stop. It does
not skip sleep, change a thread state/run bit, synthesize a return, send a message
or commit a PE interrupt. The already sourced due VI and its translated handler
must wake the thread normally. This reuses the existing guarded compiled-CPU
boundary pattern; renderer, clock, pins and desktop code are unchanged.

Linux AArch64: **564 host tests pass** (`logs/ww16-retrace-sleep-green.log`),
including preserved CPU registers/PC/time, unchanged queue/interrupts, ownership
refusal and caller/queue/SDA/alias negatives. The observed route is red before the
gate (`logs/ww16-retrace-sleep-red.log`). ARM host links
(`logs/ww16-retrace-sleep-arm-link.log`); audit, attribution and whitespace pass.
A clean committed VI-enabled rebuild and watched continuation follow immediately.

## Unit 27: the real VI handler wakes the retrace waiter

Committed VI-enabled build **`55177cb`** passes all private build stages. Fresh
watched run **`10c1b74cc7b647aab66bf18882e53a39`** passes the observed retrace
sleep route without a host wakeup: due VI interrupts occur at **8,100,000,
16,200,000 and 24,300,000 executed cycles**, with the latter two interrupting
the real scheduler idle loop. The guest progresses to **24,302,469 cycles**,
**1,171 decoded commands / 8,256 renderer bytes / 2,332 events**. The real backend
still records **one plan/draw/program**, zero refusals/gaps, zero copies/presents.

The next closed STOP is the same sleep entry **`80308A9C`**, but a different,
now-observed caller: **LR `80322C38`, FinishQueue `803F7C9C`**, the **GXDrawDone**
barrier. No extra route was admitted by the retrace-only guard. The report and
64-entry journal certify; install 1.29 s / guest 11.99 s, followed by the retained
shutdown crash **exit 133**. The monitor observes CPU and guest-output growth
throughout, and no inactivity kill occurs. Still no presentation, visible-pixel
verification, physical Vita test or FPS measurement.

## Unit 28: confirm backend completion before publishing the real PE finish

Source: the shared desktop barrier at `runtime/host/src/main.c:9776-9811` flushes
actual GX work and commits PE finish only for this GXDrawDone caller. The local
vitaGL source `source/gxm.c:962-969` implements **glFinish** by resetting the
scene and calling **sceGxmFinish**. Reuse that real blocking completion primitive;
do not wake FinishQueue, skip OSSleepThread, set sDrawDone or synthesize a return.

The small renderer/backend finish contract flushes existing stream work, rejects
partial commands, a held unpresented frame or refused/skipped rendering, then
waits for the backend. A backend without a real completion primitive explicitly
refuses. Vita uses **glFinish + the existing GL error check**, not a swap or a
counter increment. A failure is latched and propagates to the host.

The host admits only the owned observed **GXDrawDone LR/queue/SDA/real-MEM1**
shape, after all gather/FIFO/capture/command bytes have been consumed. Only a
successful, logged completion commits the pinned **dol_interrupts_commit_pe_finish**;
the guest's PE enable, PI mask and MSR EE still control delivery through the
existing real external-vector path. The actual guest sleep, PE handler, queue
wakeup and return must run. Repeated zero-charge entry does not refence or
republish the same consumed stream; later FIFO work requires a new completion.
Retrace sleep behavior remains unchanged. No pins, composite, desktop code or
draw/state coverage change; this seam is in the opt-in private continuing stream.

Linux AArch64: **574 host tests pass** (`logs/ww16-gpu-finish-green.log`), including
confirmed finish, failure/trace failure, missing renderer/disabled stream,
owned caller/queue/SDA/alias refusals, all unconsumed-work shapes, guest PI masking,
idempotence and later work. The observed success is red before implementation
(`logs/ww16-gpu-finish-red.log`). The renderer's **six new completion contracts**
plus existing bridge/translation/Cg/renderer executables pass; refusing an
unsubmitted program is separately red then green
(`logs/ww16-gpu-finish-refusal-red.log`, `logs/ww16-gpu-finish-renderer-green.log`).
These host backends record synthetic events, **not GPU completion on hardware**.
The actual ARM host links (`logs/ww16-gpu-finish-arm-link.log`); audit, attribution
and whitespace checks pass. A clean committed VI build and watched authentic
continuation are next. Do not claim a finished GPU job until that attempt reaches
the successful completion log, or a visible pixel from a submission alone.

## Unit 29: completion correctly refuses an unclassified retained tail

Committed VI build **`c198346`** succeeds. Watched attempt
**`555a9cabdde3450e8fcaef8f764612ef`** reaches the same GXDrawDone caller, then
closes with **`GX draw-done requires all captured FIFO work to be consumed`**.
No successful GPU-finish log is present and **no PE finish was published**.
The frontier remains **24,302,469 cycles / 1,171 decoded commands / one real
plan/draw/program / zero copies/presentations/refusals/gaps**. The closed report
and 64 journal entries certify; install 1.30 s / guest 12.05 s, followed by the
known shutdown exit **133**, not an inactivity kill.

The old completion guard grouped gather, CP distance, retained provenance and
partial command into one refusal. This report does **not** identify which count
is nonzero or prove the retained bytes are NOPs. Keep that uncertainty explicit.
The additional **83 FP launcher tests pass**
(`logs/ww16-gpu-finish-fp-tests.log`). Disassembly of the certified ARM artifact
confirms the linked glFinish actually branches through **sceGxmFinish**, rather
than relying only on a source-directory assumption.

## Unit 30: distinguish sourced inert GXFlush padding from unfinished work

The authentic unchanged composite provides the narrow candidate: **GXDrawDone**
writes BP `45000002`, then calls **GXFlush (`80322980`)** at `80322C10`. GXFlush
loads zero and performs eight FIFO **stw** writes at **`803229A8..803229C4`**,
then executes the real sync helper. Its stack is the caller's SP minus 8. The
pinned GX parser's `case 0x00` is NOP. With the existing real 32-byte gather
behavior, this guest-written 32-byte flush can leave up to 31 bytes of its own
NOP suffix gathered after all rendering commands have already been consumed.
Such NOPs have no unsubmitted rendering work; they need not be manufactured,
submitted early or discarded to wait for the actual GPU jobs.

The completion guard now permits **only** this inert suffix: every retained byte
and origin byte is zero, each writer PC matches its exact suffix position in
those eight stores, and each writer SP matches GXFlush's real stack relation.
All CP distance/capture/read/partial-command counts must still be zero. The
gather bytes, origins, count and transfer counters remain **unchanged**. Other
bytes, provenance or stack shapes stop before a backend finish or PE event.
No padding is added, bounds enlarged or commands replayed early.

A bounded **first-completion frontier capture** records all pending counts and
up to the existing 32 gather bytes with writer provenance; it also captures a
refusal if a later completion fails. It is not a per-frame byte logger. The next
authentic run must establish whether this candidate is the actual retained
tail; this unit does not retroactively claim that evidence for attempt 555a.

Linux AArch64: **577 host tests pass** (`logs/ww16-gpu-nop-tail-green.log`),
including all 31 possible suffix lengths with bitwise unchanged gather state,
non-NOP/origin/PC/SP refusals and frontier-log failure before backend work.
The suffix test is red before this guard (`logs/ww16-gpu-nop-tail-red.log`). ARM
host links (`logs/ww16-gpu-nop-tail-arm-link.log`); audit, attribution and whitespace
checks pass. The renderer completion primitive and pins are unchanged. Commit,
rebuild with VI and run a fresh watched continuation to classify the real tail.

## Unit 31: first authentic display-copy presentation (2026-10-13)

The committed VI build **`c47c76c`** succeeds. Watched authentic run
**`21c977bc89bf4cd88ab9658629295fe5`** establishes the candidate tail exactly:
**four zero NOP bytes**, all from the final GXFlush store **`803229C4`**, writer
SP **`8040CEA0`**, while CP distance/capture/read/partial-command counts are zero.
Those four bytes remain gathered, unchanged. The real vitaGL finish returns
success at decoded command 1,171 and publishes the actual PE finish. The guest
continues through its verified external vector and real interrupt/sleep path.

New milestone: **display_copies=1, presented=1, END rendered=1**, with **one
plan/draw/program**, zero gaps/refusals, **1,235 decoded commands / 8,416 renderer
bytes / 2,421 events**, at **24,315,678 cycles**. This is a genuine submitted draw
and a swap at the game's display copy; no synthetic frame or host-forced callback
was added. The closed report and 64 journal entries certify. Install 1.24 s / guest
12.65 s; the known post-report shutdown crash remains **exit 133**, separately
preserved. CPU and process-written logs were watched throughout; no hang kill.
Private summary: `evidence/first-presentation-analysis.json` in that attempt.

**Visible game pixels are not yet verified.** There is no visual screenshot in
this attempt. The prior WW02 demo documents this emulator's guest-memory readback
limitation; its black PNG is not evidence of this new frame's contents. A swap
counter is not a screenshot, a verified game scene, hardware performance or FPS.

## Unit 32: confirm the observed asynchronous draw-done return

The new STOP is **`80322BC8`**, GXSetDrawDone's register-restore return after
the display copy. Actual LR is `80322BC8`, r3 zero, SDA `803FE0E0`, SP `8040CC98`.
Shared desktop `main.c:9829-9844` sources this asynchronous finish boundary:
JUTVideo's drawDoneStart needs the PE callback to clear its draw-waiting flag
before the next retrace. The authentic composite writes BP `45000002` and eight
zero FIFO words at **`80322B98..80322BB4`**, then restores guest interrupts and
arrives at this still-unexecuted restore epilogue.

The Vita edge gate now admits only this owned observed return/SDA/real-MEM1
shape, using the **same actual backend completion and PE publication contract**
as synchronous GXDrawDone. Its own inert NOP suffix requires the exact store-PC
order and the current saved frame SP; it is kept unchanged. Other return states,
aliases or pending work still STOP. Guest registers, stack restores, return and
callback execute normally; no sDrawWaiting flag, callback, wakeup or GPU result
is manufactured. The finish log identifies the real SDA FinishQueue for both
boundaries, not asynchronous r3 (which is zero).

Linux AArch64: **583 host tests pass** (`logs/ww16-async-finish-green.log`), including
real-PE-path recording, failure, idempotence, exact owned return guards, all 31
retained suffix lengths and pending/bad-padding refusals. These are synthetic
backend contracts, not hardware pixel tests. The observed return is red before
the seam (`logs/ww16-async-finish-red.log`). Actual ARM host links
(`logs/ww16-async-finish-arm-link.log`); audit, attribution and whitespace pass.
Renderer draw coverage, pins and desktop code remain unchanged. Before the next
committed VI build/trial, extend the bounded command journal to the coordinator's
requested **2,048 commands**, so the authentic draw is available for WW18's
byte-exact journal-to-fixture replay; the existing 64-entry evidence is preserved.

## Unit 33: journal the draw-containing prefix without losing validation

Coordinator request: the next authentic launch uses **`--continue-gx 2048`**,
not 64, so WW18's extractor at local commit **`047d292`** can replay actual draw
records rather than the setup-only prefix. The host request/stream and launcher
plan/report contract previously capped this at 256. The limit is now **2,048**
throughout, with unchanged opt-in/default-frontier behavior and no execution cap.
After that many logged commands, decoding and guest execution continue normally.

The old Python journal validator also knew only fixed state-command lengths:
merely raising the limit would reject the first actual primitive after the game
ran. For each logged draw the producer now emits a separate
**`[gx-command-layout]`** witness of its **already derived pinned CP layout**:
index, format, vertex count, stride and exact packet bytes. The standard
**`[gx-command] ... hex=` record is unchanged**, so WW18's byte-exact extraction
remains compatible and never needs to reconstruct missing bytes. No VAT math
or new state parser is introduced.

Journal certification checks contiguous records, opcode/raw size, first-frontier
binding, each draw's positive actual count and format/stride/packet-size witness,
missing/duplicate/orphaned witnesses and total journal bytes versus actual FIFO
reads. Witness logging failure stops before publishing a logged draw. This is
**producer-evidence validation, not independent semantic renderer replay**;
the pinned stateful replay from WW18 remains the next coverage check.

Only the command-output count bound changes. The 1,025-byte command limit,
32-byte gather, pinned FIFO/trace capacities, bounded CP/history capture, 64 MiB
phase-log validation, memory owners and read-only pins are unchanged. Even the
worst allowed 2,048-command hex prefix plus witnesses is under 5 MiB. Old runs,
receipts and 64-entry journals are preserved; larger journals require the new
certified guest build, not a claim about a historical 256-bound artifact.

Linux AArch64: **592 host tests and 83 FP launcher tests pass**
(`logs/ww16-full-journal-{green,fp-tests}.log`). Tests include the real request
path, 2,048 logged commands with decoding beyond the cap, native CP framing and
the draw at index 990, malformed/missing layout witnesses, byte/read bounds and
failed draw-witness logging. Plan/report/native limit tests are red before the
extension (`logs/ww16-full-journal-red.log`). Actual ARM host links
(`logs/ww16-full-journal-arm-link.log`); audit, attribution and whitespace pass.
Commit, rebuild with VI, then launch the combined async-finish/full-prefix units
in a new continuously watched Info profile with **2,048** requested commands.

## Unit 34: four authentic presentations and a draw-containing journal

Committed VI build **`1c5e252`** succeeds. Watched attempt
**`5a4fb3b0ba4c46efa69ab545e225c8dd`**, with **2,048** requested journal entries,
passes both synchronous and asynchronous draw-done completion through the actual
backend and guest PE path. New result: **four plans/draws/programs and four display
copies/presentations**, one distinct program, **zero skips/refusals/gaps**. The
closed report reaches **48,608,267 cycles / 4,249 decoded commands / 28,385 renderer
bytes / 8,194 events**. All **2,048 journal commands**, including draw records
**990 and 1962**, certify with their pinned layout witnesses (four vertices,
16-byte stride, 67-byte quad packet).

The new honest STOP is **`pinned GX trace reached capacity; renderer drain not
implemented`**, FIFO `CC008000`, writer `80326554`, stopped guest PC `802D9174`.
The backend's streaming frontend already drains; this limit belongs to the
host's separate C-parser diagnostic mirror. Install 1.18 s / guest 18.86 s;
the retained post-report shutdown crash is still **exit 133**, not a hang kill.
The continuously recorded CPU/output observations and certified closed reports
remain private. This historical log has no `[frame-present]` wall stamps, so
**FPS is unmeasured**, not calculated from boot/teardown duration.

Reused WW18's delivered extractor after taking the repaired local integration
tree **`b2e06d7`** by fast-forward (no pin or history rewrite). The new private
fixture is under that attempt's **`evidence/gx-draw-prefix/`**: **14,464 exact
bytes / 2,048 commands / two captured draws**, FIFO SHA-256
`4f9b7592e088442ab9156410b9e5b1517e5a22e18f10d6623d2e67d6a1e13149`.
`capture.json` distinguishes the logged prefix from the 4,249 decoded commands
and five pending bytes. The extractor conservatively flags **requires_mem1=true**;
there is no matching historical memory snapshot. No memory is invented, no
zero-filled replay is attempted and no game data is committed or uploaded.
The verified summary is `evidence/four-presentations-analysis.json`.

## Unit 35: reuse consumed diagnostic trace/FIFO storage, preserving GX state

The pinned frontend already provides the source pattern:
`retail_gx_frontend.cpp:1479-1507` clears the **FIFO size and trace count** after
emitted packets are consumed, without resetting persistent CP/BP/XF/resolver
state. The C-parser mirror has no retained external consumer: its command bytes
are already journaled and handed unchanged to WW17, whose own stateful parser
and backend own rendering. Reuse that consumed-buffer lifecycle here instead of
enlarging either fixed capacity or resetting all GX state.

After a **complete command** is accepted by renderer write and the corresponding
present/no-present call succeeds, the host mirror now reuses only its FIFO/trace
cursors. First-frontier provenance, bounded history, journal, decoded counters,
resolver, CP arrays/VCD/VAT/layouts, BP registers and XF transforms stay intact.
Renderer failures keep the pending native bytes/events as evidence; partial
commands are neither cleared nor submitted. A host without a renderer retains
the original diagnostic buffers. A single command that reaches the pinned trace
capacity still stops before backend acceptance; no dropped trace event is
silently declared consumed. Bounds and memory allocations are unchanged.

Linux AArch64: **602 host tests pass** (`logs/ww16-gx-drain-green.log`), including
six real-interface recording-backend scenarios: persistent-state byte equality,
more than **300,000 FIFO bytes / 8,192 cumulative events** consumed without
changing the bounds, subsequent draw using retained layout, draw/present failure,
partial command and no-renderer preservation. Red before the lifecycle change:
`logs/ww16-gx-drain-red.log`; focused: `logs/ww16-gx-drain-focused.log`.
All four integrated renderer executables pass
(`logs/ww16-gx-drain-renderer-regressions.log`), and the actual ARM host links
(`logs/ww16-gx-drain-arm-link.log`). Audit, attribution and whitespace pass.
These recording tests are not GPU/pixel measurements. Commit this seam, then
add the sourced WW12 wall markers before the next rebuilt, watched authentic run.

## Unit 36: stamp actual presentations and due retraces with real wall time

Implemented the **existing WW12 contract**, without moving or manufacturing a
frame boundary: **`[frame-present] frame=N us=T`** appears only when WW17 reports
that its actual backend presentation succeeded for a GX display copy; N comes
from the renderer's actual presentation counter. **`[frame-timing] retrace=N us=T`**
is a separate secondary series, emitted only after the existing VI clock pops a
real due retrace. A draw, an unready/failed presentation or a disabled clock does
not become a frame marker. These marks remain in the private guest journal.

Vita timestamps use **sceKernelGetSystemTimeWide**, linked through the standard
SDK **SceKernelThreadMgr** stub. The locally sourced Vita3K implementation
(`SceThreadmgr.cpp:35-38,1210-1212`) returns host high-resolution wall time in
microseconds, not guest executed cycles or an emulator presentation counter.
Host contract tests use **CLOCK_MONOTONIC**. Each series requires consecutive
actual indices and strictly advancing, valid u64 microseconds; clock failure,
overflow, tied/backward time or failed marker output stops explicitly. There is
no fabricated timestamp increment, fallback to guest cycles or FPS extrapolation.
Guest registers, RAM, cycle credit/deadlines and interrupt delivery are unchanged.

Linux AArch64: **611 host tests pass** (`logs/ww16-frame-markers-green.log`),
including nine native-interface marker scenarios parsed by the **unchanged WW12
frame-event reader**: two confirmed display copies with exact deterministic
test stamps, absence for failed/unready presentation, clock/write failure,
invalid/overflowing/tied/backward time, and due/off VI series separation.
These injected test clocks and recording backends do not claim real GPU FPS.
The two-presentation test is red before the instrumentation
(`logs/ww16-frame-markers-red.log`); focused tests pass
(`logs/ww16-frame-markers-focused.log`). Actual ARM host links with the sourced
system-clock import (`logs/ww16-frame-markers-arm-link.log`).

The performance suite revealed its historical assertion that **every real run
must forever report rendered=0**. The new authentic one/four-presentation reports
properly invalidate that assumption. Its integration test now preserves each
real END counter, with a synthetic nonzero-counter test proving that a count
alone never creates an FPS field. **27 WW12 tests and 83 FP launcher tests pass**
(`logs/ww16-frame-markers-perf-green.log`, `logs/ww16-frame-markers-fp-tests.log`);
the original failed assertion is retained in `logs/ww16-frame-markers-perf-tests.log`.
Audit, attribution and whitespace pass. No renderer coverage, pin or player
default changes. Commit/rebuild and run the drain+marker units without a memory
dump first, to measure actual presentation growth without snapshot-write cost.

The coordinator's memory-proof request is accepted next: raw **24 MiB MEM1
starting at physical zero**, matching WW18's `--mem1` input, with a bounded private
manifest naming capture command/time and addresses/sizes. Preserve time-matched
evidence rather than relabel an old boot dump; keep that capture profile separate
from the no-dump FPS baseline. No memory or resources are invented or uploaded.

## Unit 37: eight authentic presentations and the first wall-stamped rate

Committed **VI / module O1** build **`07189db`** succeeds. Fresh continuously
watched **no-dump** run **`a4906dc5413141f5b4c1afc249f1382d`** passes the old
trace-capacity frontier: **8,963 decoded commands / 60,512 renderer bytes /
17,252 events**, with **nine plans/draws/programs and eight actual display
copies/presentations**, one distinct program, **zero skips/refusals/gaps**.
The first frontier, all **2,048 journal entries** and closed report certify.

All **eight `[frame-present]` stamps** match END/RENDER exactly, and ten actual
VI retrace stamps are a separate strictly advancing series. First/second/last:

```text
[frame-present] frame=1 us=1791848059554619
[frame-present] frame=2 us=1791848059827685
[frame-present] frame=8 us=1791848074637261
```

The unchanged WW12 harness now measures a **15.082642 s** presentation span:
**0.464110 mean FPS**, **0.60 FPS in the one complete 10 s window**, median frame
time **2,585.844 ms**, p95 **3,155.1604 ms**. Secondary: **0.509063 retraces/s**
over **17.679557 s**, 0.70 in its complete 10 s window. The incomplete tail is
excluded by the established harness. This is **one exploratory boot/clear-path
measurement under Vita3K/OpenGL llvmpipe**, not the three-run fixed-title
benchmark, verified visible pixels, sustained gameplay or physical-Vita FPS.
The 30 FPS floor is clearly missed. The long final high-CPU/no-new-frame segment
is outside the completed marker window; do not describe 0.60 as a sustained rate
throughout the whole guest lifetime.

New closed STOP: **AI volume register `CC006C04`**, writer/read PC **`80316E00`**,
stopped guest PC **`80316B9C`**, at **85,311,500 executed cycles**. Pinned Dolphin
`AudioInterface.cpp:59,280` names the register and direct u32 read; the AI sample
counter is instead at **+08**. Install 1.25 s / guest **88.93 s**, then the known
post-report shutdown **exit 133**. The watcher keeps a high-CPU process active
even during long silence, as required; it does not mislabel that segment as a
low-CPU hang or treat the teardown as gameplay success.

Private evidence in this attempt: **`evidence/frame-markers.json`** and
**`evidence/fps-no-dump-10s-verified/{baseline.json,summary.txt}`**. The initial
baseline's numeric metrics are identical; its preliminary mistaken sample-counter
register-name note is preserved and superseded by the sourced volume diagnosis.
Reused WW18 extraction yields **`evidence/gx-draw-prefix/stream.fifo`**: **14,464
bytes / two real draws / the exact 2,048-command prefix**, with the same FIFO hash
as the earlier run. Nine reported draws are not nine captured draws. Matching
MEM1 is still required; the requested bounded memory capture follows, separate
from this no-dump baseline. No opcode, timestamp, resource or presentation is
invented, and no private artifact is committed or uploaded.

## Unit 38: bounded, opt-in MEM1 evidence before the journal's actual draws

Added **`--mem1-snapshots 1|2`** to the isolated WW05 launcher, off by default
and valid only with the opt-in GX journal. Its identity-bound request is checked
before guest execution. The first one/two complete, logged primitive commands
capture the **actual raw 24 MiB physical MEM1 backing**, unchanged and without
endian conversion, **before the renderer consumes that draw**. No snapshots are
taken on an ordinary no-dump FPS profile, at shutdown, after the bound is met or
for unlogged later commands. Guest RAM/registers/cycles/interrupts are unchanged.
This is diagnostic I/O, not an optimization: its cost invalidates comparison with
the no-dump baseline, and the snapshot profile is explicitly separate.

Each guest capture writes **`ww05.mem1-N.bin`** and **`ww05.mem1-N.json`** in the
private run's fixture directory, recording token/source/package, physical base
**00000000**, cached guest base **80000000**, exact **25,165,824-byte** extent,
command/opcode/PC/completion PC, executed cycles, real wall microseconds and
preceding actual presentation count. Two captures can preserve changing resource
versions; neither is claimed to cover every earlier or later resource version.
External owned aliases and MEM2 are explicitly outside this raw-MEM1 capture.
No reconstruction, zero filling, missing resource or GPU completion is invented.

Binary and metadata are published from exclusive **`.partial`** files through
the reused diagnostic flush/error/close/rename checks. Existing outputs block
capture without replacement; a failed open/write/flush/close/publish is a STOP,
with incomplete evidence retained rather than promoted. The launch watchdog
includes binary/metadata/partial growth as activity alongside CPU and log growth.
The host requires genuine 24 MiB backing and a valid, logged complete draw.

Post-run verification ties the snapshot identity and writer PCs to the exact
unchanged journal, checks the full extent and advancing time, and computes hashes.
**`evidence/mem1-capture.json`** lists verified snapshots. Next to each binary,
**`ww05.mem1-N.snapshot.json`** uses lane 18's current
**`gx-mem1-snapshot-v1`** contract: adjacent binary filename, binary SHA-256,
SHA-256 of concatenated logged command bytes, and the single physical region
`{address:0, offset:0, size:25165824}`. Temporal producer annotations are retained.
Use lane 18's explicit **`--snapshot PATH`**, not an automatic whole-run snapshot
claim. The FIFO digest binds bytes, not resource-version correctness.

Linux AArch64: **629 host tests**, **27 WW12**, **83 FP launcher**, **21 GX parser**
tests and the four existing renderer executables pass. Request rejection was red
before implementation (`logs/ww16-mem1-request-red.log`); lane 18 manifest fields
were red before alignment (`logs/ww16-mem1-snapshot-contract-red.log`). Green:
`logs/ww16-mem1-snapshot-contract-green.log`, `logs/ww16-mem1-{perf,gx,fp}-final.log`,
`logs/ww16-mem1-renderer-regressions.log`. Actual ARM host links
(`logs/ww16-mem1-arm-link.log`; only existing unrelated DSP/generated warnings).
Audit, attribution and whitespace pass. Direct contract check with lane 18's
current **read-only trunk `memory_snapshot.py`** accepts both synthetic snapshots
and preserves every byte and the complete physical range
(`logs/ww16-mem1-format-contract/contract.json`). Those injected tests do not prove
authentic game resources; a fresh committed rebuild and watched capture trial
follows. No network, push, private data upload or pin modification.

## Unit 39: two authentic, time-matched MEM1 snapshots delivered for lane 18

Fresh committed VI/O1 build **`6d691a3`**, watched private capture run
**`33aa66c91b9d49a1b78cbabdeb1f86c3`**: **two actual 24 MiB MEM1 captures verify**,
before journal draws **990** and **1962**, at **14,491,219 / 24,342,927 executed
cycles**, respectively. Writer **80323190**, completion **802CD824**, opcode **80**.
First snapshot precedes any presentation; second follows the first. Bounded
capture stops there; the actual guest continues to **nine draws / eight
presentations**, **8,963 decoded / 2,048 journal commands**, zero skips/refusals/gaps.
All eight presentation and ten retrace wall stamps still parse and match the
report; the capture profile is **not an FPS baseline**. The no-dump Unit 37
baseline is preserved, not replaced or compared against dump-writing timings.

Closed STOP is unchanged: unsupported **AI volume read CC006C04**, PC **80316E00**,
with **85,311,500 cycles / 1,033,628 edges**, exactly as in the no-dump trial.
Install **1.23 s**, guest **91.23 s**, then the known closed-report shutdown
**exit 133**. The watch records **91 guest samples**, including 67 silent but
high-CPU samples; snapshot-file growth is activity too. No watchdog false hang.

**Private hand-off root**:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-33aa66c91b9d49a1b78cbabdeb1f86c3
```

- **`evidence/mem1-capture.json`**: verified identities, commands, times, mappings,
  binary/metadata hashes and explicit replay-manifest paths.
- **`data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/`**: original `ww05.log`,
  `ww05.report`, **`ww05.mem1-{1,2}.bin`**, guest provenance
  **`ww05.mem1-{1,2}.json`**, lane 18 contract
  **`ww05.mem1-{1,2}.snapshot.json`**. Supply the latter with **`--snapshot`**.
- **`evidence/gx-draw-prefix-snapshot{1,2}/`**: lane 18's read-only extractor
  accepts each manifest and stages **14,464 FIFO bytes / two captured draws**,
  original memory/manifest and coverage transport. Its available
  `build-f6/ww18-verify/gx-host/gx_bridge` is **stale and lacks guarded snapshot
  acknowledgement**, so both guarded replays explicitly reject; those refusals
  remain in `replay.txt`, console logs and `evidence/mem1-replay-attempts.json`.
  Tool/source/binary hashes are retained, not silently attributed to a new build.
- **`evidence/gx-draw-prefix-raw-mem1-{1,2}/`**: the existing WW16 bridge can use
  each **actual full, contiguous** MEM1 capture through the documented legacy
  **`--mem1`** contract (no sparse holes/zero-filled reconstruction). Both produce
  **2 plans / 2 accepted programmes / one distinct programme**, **4,045 events**,
  no skipped plan/refused programme. **Global replay still fails coverage**:
  its one display-copy is refused with **`display copy has no texture destination`**.
  Thus report **`verdict=ran-with-coverage-gaps`**, not a clean independent replay.
  Evidence: `evidence/mem1-full-image-replays.json` and the retained replay text.
- **`evidence/capture-run-checks.json`**, `evidence/result.json` and
  `evidence/run-watch.jsonl`: capture markers, actual frame/retrace counts,
  certified frontier/command/memory evidence and continuous activity checks.

MEM1 hashes, in capture order:

```text
72f9140a1fecd4f4e4022480457ce175252ffd37f8fcee6b5e82da407bc9a32f
8cc8414e03e8f8775ef493ac5db0bb7bc95fd7f84351e4f5f966e17773ee884b
```

FIFO hash remains **4f9b7592e088442ab9156410b9e5b1517e5a22e18f10d6623d2e67d6a1e13149**.
The snapshots differ, as expected for changing guest RAM. Each is time-matched
to its own draw, not proof of every resource version throughout the prefix.
Lane 18 now has real memory to investigate its guarded bridge/display-copy
coverage independently. No pixels, physical-Vita behavior, gameplay or FPS gain
is claimed, and no binary, disc data or captured memory is committed/uploaded.
Next host seam is the sourced stopped-AI volume register; audio execution and
unobserved interrupts remain explicit STOPs.

## Unit 40: reuse the pinned stopped-AI volume register, without an audio backend

The new frontier's **CC006C04 word read at 80316E00** is Dolphin's **AI volume**,
not its sample counter. Pinned `AudioInterface.cpp:59,198,280-287` defines zero
reset, direct u32 read, retained full u32 write and mixer volume application;
`AudioInterface.h:86-96` assigns the low-byte left/next-byte right fields while
the full register word is retained. The actual translated boot next clears each
channel with word read-modify-writes (`80316E10` / `80316E20`).

Host word reads/writes now reuse **dol_audio_dma_ai_mmio_read/write** and its
existing big-endian **ai_regs+4** storage (`GXRuntime/src/audio_dma.c:258-281`),
initialized to zero by the pinned audio state. No duplicated register, mask,
sample-counter algorithm or synthesized callback is introduced. Writes require
both AIS playback and audio DMA to be stopped; an active engine explicitly
refuses **`active AI volume change requires an audio backend`** before mutation.
Stream start/rate change, sample/timing registers and PCM execution are still
unsupported. Read widths other than the sourced word remain STOPs too.

Linux AArch64: **637 host tests and 27 WW12 tests pass**. Eight added scenarios
extend the existing retained-register probe: zero reset, exact whole-word
retention (including reserved bits), independent audio storage, only actual
executed-cycle credit, bad read/write widths, active AIS and active AID refusal.
Each compares the full audio state against only the permitted register delta;
FIFO/trace/frontier/interrupts and cycle budget cannot be invented by volume I/O.
The reset test was red before implementation (`logs/ww16-ai-volume-red.log`),
then green (`logs/ww16-ai-volume-green.log`). Actual ARM host links without new
warnings (`logs/ww16-ai-volume-arm-link.log`); WW12 regression evidence in
`logs/ww16-ai-volume-perf-tests.log`. Final full host suite remains green after
matching the volume probe's refusal context to **80316E00**
(`logs/ww16-ai-volume-final.log`). Audit/attribution/whitespace pass.
No shared runtime, pin, desktop default or rendering behavior changes. Fresh
committed VI/O1 rebuild and watched **no-dump** continuation follow to identify
the next actual STOP; host tests alone do not prove game audio or gameplay.

## Unit 41: authentic volume initialization passes; live capture integration is next

Committed VI/O1 build **478bef1**, fresh no-dump run
**ef922f40287146b1aaf213278762df48**: the real volume read and two channel-clear
word stores pass. New closed STOP is the next actual instruction:
**AI interrupt-timing CC006C0C = 0**, word store at **80316E24**. Still **nine
draws / eight presentations**, **8,963 decoded / 2,048 journal commands**, no
skips/refusals/gaps. All eight presentation and ten retrace stamps parse and
match the report. Executed cycles/edges remain **85,311,500 / 1,033,628**;
install **1.23 s**, guest **70.10 s**, known post-report exit **133**. No hang is
inferred from high-CPU silence.

Unchanged WW12 measurement, one exploratory no-dump boot run:
**16.439083 s** presentation span, **0.425815 mean FPS**, **0.50 FPS in the one
complete 10 s window**, median frame **2,312.227 ms**, p95 **2,774.8683 ms**.
Secondary retrace mean **0.475379/s**, complete-window **0.60/s**. Evidence:
`evidence/fps-no-dump-10s/{baseline.json,summary.txt}` and
`evidence/frame-markers.json` in this private attempt. Same caveats as Unit 37:
not sustained gameplay, physical-Vita timing or a three-run benchmark. No
renderer/FPS improvement is attributed to the stopped-volume register seam.

The coordinator independently verifies lane 18's new image pipeline: replay of
our authentic snapshot 2 produces uniform opaque black, consistent with the
two journaled initialization clears, not a visible game scene. Next, integrate
its committed default-off **ww_renderer_set_frame_capture** hook into the live
host's MEM1-capture profile. The boundary is after successful swap and before
next-frame drawing, never a post-run fabricated preview. Retain raw RGBA and
the declared display-alpha PNG in the run's private evidence. No-dump FPS
profiles must remain unchanged. The coordinator owns the trunk integration;
its staged/conflicted work is not edited or overwritten from this lane.

## Unit 42: enable actual live PNG capture for every MEM1 evidence profile

Reused lane 18's committed snapshot loader **c25ed79 → 082a4cf** and live-image
hook/encoder **161ebfd → b7447cd**, preserving their human authorship. Only those
delivery dependencies are imported; unrelated indirect/emboss/LOD parent-context
work is not pulled into this host unit. Conflict resolution retains all existing
WW16 GPU-finish methods/tests and draw coverage alongside the additive readback
method and five renderer capture contracts. Four renderer suites pass
(`logs/ww16-live-hook-import-renderer.log`); snapshot/image Python tests pass.
No edits are made to the coordinator's trunk worktree or its staged merge.

**Every `--mem1-snapshots 1|2` run now enables the delivered
ww_renderer_set_frame_capture hook**. No-dump runs leave it off. A real backend
swap must succeed first; the hook reads the actual shown front buffer before
held next-frame commands resume. The host reuses **gx_capture::write_png** and
the existing WW02 encoder: **`frame-N-rgba.png`** preserves actual readback RGBA,
flipped only to top-first rows; **`frame-N.png`** is the declared display view
with alpha 255 and every RGB byte unchanged. No fallback, CPU rasterization,
preview swap, fake command or inferred pixel is added.

The isolated guest maps only the new private **`evidence/frames`** directory
writable at its fixture frame path. Thus the renderer callback writes PNGs
**directly into the run's evidence while the guest lives**, not via post-run
replay. The installer does not receive that writable mount. Existing frame
evidence blocks a fresh request; the renderer accepts only a new/empty real
directory and never overwrites an existing PNG or metadata file. Each frame
keeps its own raw/display PNG pair and **`frame-N.json`**, binding actual
presentation/draw/command counts, token/source/package, real wall microseconds,
executed cycles and preceding MEM1 captures to the exact capture boundary.
Partial files remain evidence on failed PNG/metadata publication.

Capture profiles also enable **disable-surface-sync: false**, sourced by lane
18 from Vita3K's surface-writeback path. The existing approved-profile and
installer restoration checks now accept only that explicitly requested single
setting change; the neutral/no-dump schema and its original hash are unchanged.
An installer user-id rewrite cannot silently turn writeback off. The replay and
live paths share that setting helper, PNG parser, image statistics and declared
RGB-preserving display-alpha validation; no second encoder or image dependency.

The profile is diagnostic and bounded: **128 real presentations maximum**, each
at most the **960×544 pixel extent**; exhausting the capture bound is an explicit
STOP, not a silent missing-image success. This does not cap ordinary no-dump
continuations. The watchdog counts these process-written PNG/partial/metadata
files as output activity, never its own monitoring files. After the process is
reaped, the harness verifies identity/counts/times, PNG CRC/data/dimensions,
raw/display RGB equality and opaque display alpha, and writes hashes/pixel
statistics to **`evidence/live-images.json`** without rewriting the PNGs. A uniform
image is reported uniform, never assumed to show a logo or gameplay. Capture
profiles explicitly cannot replace the no-dump FPS baseline.

Linux AArch64: **647 host tests**, **83 FP launcher**, **51 GX snapshot/image**
and **27 WW12** tests pass. Ten live-host scenarios cover actual two-frame PNG
output/orientation/alpha, off-by-default, backend/readback/invalid-data failure,
preserved preexisting files, unpublished PNG write failure, direct mounted
evidence and collector integrity, the single profile change and installer
restoration. Red tests precede implementation
(`logs/ww16-live-frames-red.log`, `logs/ww16-live-profile-red.log`); final host
green in `logs/ww16-live-frames-final-host.log`, profile/GX/WW12 evidence in
`logs/ww16-live-profile-fp-tests.log`, `logs/ww16-live-frames-final-gx.log` and
`logs/ww16-live-frames-perf-tests.log`. Actual ARM host links without new warnings
(`logs/ww16-live-frames-final-arm.log`). Audit, attribution and whitespace pass.
No physical Vita, Windows CI or authentic live game PNG is claimed yet: fresh
committed rebuild and continuously watched two-MEM1/live-PNG trial follow.
The sourced next gameplay frontier remains stopped-AI timing **CC006C0C**.

## Unit 43: eight authentic live PNGs, with clean guarded draw-prefix replay

Fresh committed VI/O1 build **75c6f99**, watched MEM1/live-PNG run
**c96a4be9176f4a7694854a114db960ed**: **eight actual presentations produce eight
verified live PNG pairs and metadata records directly in evidence/frames**.
No replay or preview creates these images. Their command boundaries are
**1227, 2199, 3171, 4143, 5115, 6087, 7059, 8031**, with consecutive real frame
indices. The ninth draw is unpresented at the STOP and is not made into a ninth
PNG or measurement stamp. Report: **nine draws / eight presentations**,
**8,963 decoded / 2,048 journal commands**, zero skips/refusals/gaps. All eight
presentation and ten retrace wall stamps still parse and match the report.

**All eight display PNGs are numerically uniform opaque black**, **960×544 /
522,240 pixels each**, and the last was opened and visually inspected. No logo
or scene is visible. Every display RGB byte equals its preserved raw readback;
only the declared display-alpha conversion changes alpha. The raw images retain
**440,384 alpha-zero black pixels / 81,856 alpha-255 black pixels**, not invented
opaque RGBA. The identical display hash also matches lane 18's independently
verified initialization-clear replay; this is consistency, not pixel parity or
gameplay certification.

```text
display PNG SHA-256:
c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5
raw PNG SHA-256:
16afe13865182669ab43333868305b16c554029989ba9d68e7c57a5816739940
```

**Private evidence root**:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-c96a4be9176f4a7694854a114db960ed
```

- **`evidence/frames/frame-{1..8}.png`**, matching `-rgba.png` and `.json`:
  actual live front-buffer output. Open **`evidence/frames/frame-8.png`** for the
  latest presentation, not an extrapolated preview.
- **`evidence/live-images.json`**, **`evidence/live-capture-checks.json`**:
  exact PNG/metadata hashes, complete pixel statistics, frame/command/cycle/time
  provenance, raw/display equality, inspection and explicit no-gameplay/no-FPS
  verdict. The guest-only frame mount and surface-writeback configuration verify;
  final config SHA **a3d18d0c8536e633323b395d3cc56bdd300ac6f72482b8829867555d13d7b1d3**.
- **`evidence/mem1-capture.json`** and original numbered MEM1 files/manifests in
  the fixture directory: **two actual 24 MiB captures at draws 990 / 1962**.
  Both RAM hashes match Unit 39 exactly; the new capture hooks do not change
  those guest bytes or the final executed cycles/edges.
- **`evidence/gx-draw-prefix-snapshot{1,2}/`**: after importing the delivered
  guard/display-copy fix, **both independent guarded replays now pass**:
  **14,464 FIFO bytes / 4,045 events / 2 plans and programmes**, no skip/refusal
  or nonzero gap, **`verdict=ran`**, explicit **GX_MEM1_RANGES_V1** acknowledgement
  for the complete 24 MiB range. Original refusals from Unit 39 are preserved,
  not overwritten. Provenance in **`evidence/guarded-mem1-replays.json`**. Prefix
  translation is not whole-run replay, resource-version or GPU pixel parity.
- **`evidence/result.json`**, **`evidence/run-watch.jsonl`**, original guest log
  and report: certified commands/frontier, two memories and eight images;
  continuous CPU/log/snapshot/PNG-growth monitoring while the process lived.

Closed STOP remains **CC006C0C = 0**, word store **80316E24**, final
**85,311,500 cycles / 1,033,628 edges**. Install **1.24 s**, guest **99.97 s**,
known post-report shutdown **exit 133**. High-CPU silence is activity, not a
watchdog hang. This snapshot/readback/PNG-write profile is **not an FPS baseline**;
Unit 41's no-dump numbers are retained separately. No physical-Vita performance,
visible gameplay or FPS gain is claimed. Next: the sourced stopped-AI zero
interrupt-timing initialization, with active audio/timing still explicit STOPs.

## Unit 44: sourced stopped-AI zero interrupt-timing initialization

The actual **80316E24 word store of zero to CC006C0C** initializes Dolphin's
AI interrupt-timing register. Pinned `AudioInterface.cpp:61,199-200,306-315`
defines zero reset and retained word access. Its scheduled update and sample
increment explicitly do nothing while **PSTAT is clear** (`110-111,140-143`);
accepting this stopped initialization does not require invented samples, elapsed
audio time, completion or an interrupt. The host reuses the pinned AI register
file at **+0C**, sharing the existing stopped-engine gate with volume writes.

Only the observed **zero word store** is accepted, with AIS playback and audio
DMA both stopped. Nonzero timing, active audio or invalid width fails before
register mutation. Word reads return actual retained state. Playback, sample
clock/counter access and other unsupported audio behavior remain STOPs; no timer
callback, PCM output or PI source is synthesized. GPU/capture behavior, pins and
player defaults are unchanged.

Linux AArch64: **654 host tests pass**, including seven timing scenarios reusing
the retained-register probe: zero initialization, only executed-cycle credit,
nonzero refusal, read/write width refusal, active AIS and active AID refusal.
The full audio state, FIFO/frontier/trace, interrupts and budget remain checked
against their permitted deltas. The actual zero-store contract was red before
implementation (`logs/ww16-ai-timing-red.log`); green in
`logs/ww16-ai-timing-green.log`. Actual ARM host links without new warnings
(`logs/ww16-ai-timing-arm-link.log`); audit/attribution/whitespace pass. A clean
committed VI/O1 rebuild and watched **MEM1 + live-PNG** trial follows, retaining
actual images/counts and the next real STOP separately from no-dump FPS evidence.

## Unit 45: zero AI timing passes; the next frontier is idle DSP audio-DMA setup

Committed VI/O1 build **d4ca48e**, watched MEM1/live-PNG run
**24356e49e61d44c8b088bc9634afb278**: the real stopped-AI zero timing store passes.
New STOP is a **halfword read of CC005030 at 80316950**, sourced as DSP
**AUDIO_DMA_START_HI** (`DSP.cpp:63,314-321`), not the separate ARAM DMA block.
Final state **85,311,730 cycles / 1,033,646 edges**, advancing 230 cycles / 18 edges
beyond Unit 43. Still **nine draws / eight actual presentations**, **8,963 decoded
/ 2,048 journal commands**, zero skips/refusals/gaps.

**Eight live PNGs, two actual MEM1 captures and all wall markers verify**; the
last PNG was opened and remains uniform opaque black. Display hash unchanged:
**c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5**.
Both memory hashes still match Unit 39. Both independently guarded prefix
replays pass with the actual captured memories: **14,464 bytes / two plans and
programmes**, no gaps/refusals, explicit complete MEM1 coverage acknowledgement.
This is still an initialization-clear image, not a visible logo/game scene,
whole-run replay, physical-Vita parity or an FPS baseline.

Private hand-off root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-24356e49e61d44c8b088bc9634afb278
```

Latest image **`evidence/frames/frame-8.png`**; pixel/provenance manifests
**`evidence/live-images.json`**, **`evidence/live-capture-checks.json`**; memory
manifest **`evidence/mem1-capture.json`**; draw fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`** and
**`evidence/guarded-mem1-replays.json`**. Installer **1.29 s**, guest **92.97 s**,
known post-report shutdown **133**, no inactive/timeout termination. Next source
work is stopped audio-DMA register configuration only; DMA enable, PCM transfer
or audio completion cannot be invented to cross that frontier.

## Unit 46: retain sourced audio-DMA setup while both audio engines are stopped

The observed **80316950 halfword read of CC005030** begins the actual SDK source
address setup: high read/write at **5030**, low read/write at **5032**, then
length/control at **5036**. Pinned Dolphin `DSP.cpp:63-66,141,200-203,314-347`
defines zero reset, GameCube source-high **03FF** / aligned source-low **FFE0**
masks and the separate enable bit. The host now reuses the pinned
**dol_audio_dma_dsp_mmio_read/write** register file and source-address latching
for these three halfword configuration registers; there is no duplicate DMA
engine or new sample-data representation.

Writes require AIS and audio DMA both stopped. Source halves retain the sourced
masks; disabled control retains only its **7FFF** length bits. **Any DMA enable,
active-engine change or unobserved access width is an explicit STOP before
mutation**, because the enable path would require actual PCM transfer and AID
interrupt handling. No source memory is read, transfer consumed, time advanced,
completion published or interrupt asserted by this register-only setup. ARAM
DMA remains its separate existing implementation. Other unsupported registers
and audio execution remain refusals.

Linux AArch64: **662 host tests pass**, including eight added parameterized
register-family scenarios: zero reset, exact masks, coupled source halves,
disabled length, enable refusal, active-engine refusal, wrong-width refusal and
only executed-cycle credit. The reused probe checks the full audio state against
the permitted pinned register/latch delta, and preserves FIFO/frontier/trace,
interrupts and budget. Reset reads were red before implementation
(`logs/ww16-idle-aid-red.log`); green in `logs/ww16-idle-aid-green.log`. Actual
ARM host links without new warnings (`logs/ww16-idle-aid-arm-link.log`);
audit/attribution/whitespace pass. No runtime pin, desktop default or rendering
change. Fresh committed VI/O1 rebuild and watched MEM1/live-PNG trial follows;
configuring an idle source does not prove playing audio or game progress.

## Unit 47: idle DMA setup passes; the guest reaches the JAudio DSP-task boundary

Committed VI/O1 build **c6497b4**, watched MEM1/live-PNG run
**678e40d69c8e4e198c5f88abb3f89f6d**: the actual source-high/source-low/disabled
length setup passes. The guest advances **68,275 cycles / 1,813 edges** beyond
Unit 45 to **85,380,005 cycles / 1,035,459 edges**, then stops at **803193AC**:
**`desktop interception unsupported; guest not skipped`**. Shared
`edge_intercepts.c:47` and desktop `main.c:8497-8504` identify this as
**__DSP_boot_task**, the JAudio task's second DSP boot handshake. The desktop
arms its own reply bookkeeping there; that is not permission to inject mailbox
replies or skip real task instructions on this host.

Still **nine draws / eight presentations**, zero renderer skips/refusals/gaps,
**8,963 decoded / 2,048 journal commands**. Two actual MEM1 captures and **eight
live PNG pairs verify**; the last was opened and remains uniform opaque black,
with the same **c871acff...** display hash as Unit 43. All eight actual presentation
and ten retrace stamps match the report. Both guarded two-draw prefix replays
remain clean, using the same real memory hashes, not default zero RAM.

Private hand-off root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-678e40d69c8e4e198c5f88abb3f89f6d
```

Latest image **`evidence/frames/frame-8.png`**; image/check/memory manifests
**`evidence/live-images.json`**, **`evidence/live-capture-checks.json`** and
**`evidence/mem1-capture.json`**; guarded fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`** and their replay manifest. Install
**1.20 s**, guest **116.38 s**, closed report followed by known **exit 133**.
No inactivity/timeout termination, fabricated frame, logo/gameplay claim or FPS
comparison is made. The next seam is guarded observation of the actual DSP-task
entry and guest mailbox access, preserving the real body and explicitly
refusing unsupported task-peer behavior rather than adopting desktop replies.

## Unit 48: observe the actual DSP task and keep missing mailbox behavior explicit

The **803193AC __DSP_boot_task** boundary now reuses the existing SDK observation
path rather than the desktop reply-arming shortcut. Admission requires the
registered DOL entry, **32 original loaded text bytes** verified through the
existing code guard, an unaliased real-MEM1 task range and the actual prologue's
stack range (**SP-24 through SP+8**). It records entry/LR/SP/task address, **64
opaque actual task bytes**, DSP control/mailbox/ROM/init state and executed
cycles once, then lets the **real guest body run normally**. The task bytes are
bounded evidence, not a reconstructed descriptor, DSP program or scripted reply.
No guest register, RAM, DSP state, return, ready bit, callback or wakeup is changed
by observation. Every repeated entry is revalidated; logging failure is fatal.

At the ensuing real **CC005004** halfword mailbox poll, a missing busy reply
without supported **running** ROM/init work explicitly stops with
**`DSP task boot mailbox requires a DSP task backend; no reply injected`**.
This prevents an unsupported peer from becoming either a fabricated message or
an indefinite busy-wait. Existing busy mail, the verified running ROM source and
verified pending init work retain their existing behavior. A halted pending flag
is not a due event. No desktop task-mail flags or new response constants are
copied into the actual host path. The real loaded task still needs a sourced
DSP backend; this unit is guarded execution/diagnosis, not that implementation.

Linux AArch64: **672 host tests pass**, ten added observation/peer scenarios
extending the existing scheduler probe: original entry, changed/unregistered code,
invalid task/stack mapping, bounded repeat, logging failure, empty/stale mailbox,
actual busy mail, verified ROM/init work, and halted pending work. Full RAM and
general registers stay unchanged; DSP changes are limited to the already sourced
ROM read behavior. The admitted-entry test was red before the seam
(`logs/ww16-dsp-task-observation-red.log`); all tests green in
`logs/ww16-dsp-task-observation-final.log`. Actual ARM host links
(`logs/ww16-dsp-task-observation-final-arm.log`; only the existing DSP signedness
warning). Audit/attribution/whitespace pass. No pin, renderer or desktop behavior
change; no physical-Vita or authentic task execution is claimed from these tests.

The existing shared **dsp_adapter_c / dsp_hle_backend** is a source candidate for
the next real peer, but its current optional build consumes donor objects and
Mac frameworks (`runtime/host/CMakeLists.txt:702-755`), not an ARM/Vita backend.
It is not linked or silently substituted here. Commit/rebuild and a watched
MEM1/live-PNG diagnostic trial follow. The last healthy capture run needed
**116.38 s**, so the next explicit phase ceiling is **180 s**, still with the
same 30-second low-CPU **AND** silence abort and continuous live monitoring.

## Unit 49: authentic guarded DSP task body reaches the ROM upload command

Committed VI/O1 build **90fc919**, watched MEM1/live-PNG run
**1ff459b86c894d90938921c617ce2d26**: original registered task entry, descriptor
and stack checks pass. The actual guest body consumes the existing reset-selected
ROM ready mail and reaches **80F3A001**, the sourced ROM source-address command.
STOP is **`running ROM mail protocol not implemented`**, halfword store
**A001 to CC005002 at 803190B0**. No desktop reply arming or guest-body skip.

One actual task observation is retained privately: descriptor **803EAF60**,
LR **8028EC7C**, SP **803EA4D8**, control **0950**, reset-selected ROM pending,
loaded init already finished. Final **85,380,253 cycles / 1,035,459 edges**;
248 guest cycles beyond Unit 47. No extra presentation: still **nine draws /
eight presentations**, **8,963 decoded / 2,048 journal commands**, zero renderer
skips/refusals/gaps. **Eight live PNG pairs, two real MEM1 images, eight actual
presentation stamps and ten retrace stamps verify**. All shown PNGs remain
uniform opaque black; the last was opened. Both guarded two-draw-prefix replays
pass with the actual contemporaneous snapshots. No FPS baseline, logo/gameplay
or physical-Vita result is claimed.

Private evidence root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-1ff459b86c894d90938921c617ce2d26
```

Latest image **`evidence/frames/frame-8.png`**; image/check/memory manifests
**`evidence/live-images.json`**, **`evidence/live-capture-checks.json`**,
**`evidence/mem1-capture.json`**; fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`** and guarded replay provenance
**`evidence/guarded-mem1-replays.json`**. Install **1.34 s**, guest **105.12 s**;
closed report followed by known shutdown **133**, not timeout or inactivity.

Next source seam: pinned **DSPHLE/UCodes/ROM.cpp:40-113** and free ROM
**docs/DSP/free_dsp_rom/dsp_rom.ds:13-59** agree on the five command/parameter
pairs. **DSPHLE.cpp:179-186** consumes a low-word mail through HandleMail before
clearing the CPU busy flag. Receiving these actual parameters can be implemented
without an outgoing reply or simulated DSP instruction time. The final task
launch requires an executable peer and must remain an explicit STOP until one
exists; no Zelda init/interrupt mail is permitted merely because the guest asks.

## Unit 50: receive sourced ROM upload parameters, with task execution still refused

The actual **80F3A001** request now reaches a bounded source-backed ROM receiver
in **vita/host/dsp_rom_mail.c**, not a desktop interception or reply script.
Pinned **ROM.cpp:40-113**, corroborated by **free_dsp_rom/dsp_rom.ds:13-59**, defines
the source-address, instruction-length, data-length, instruction-destination
and start-PC command pairs. The receiver retains the actual source word and
only the documented low 16 bits of scalar parameters. A small owned record is
cleared only by a successful verified reset/init transition. It does not read
source memory, transfer code or allocate IRAM.

Explicit assumption: parameter intake uses the pinned **DSPHLE.cpp:167-186**
ABI, retaining the written high word in the running selected-ROM path and
consuming the completed low-word mail through the native receiver before
clearing the CPU busy flag. This is **HLE-style ROM input handling**, not proof
of DSP instruction execution or instruction-accurate time. The existing halted
LLE mailbox behavior remains unchanged. No outgoing reply, DSP/AI interrupt,
elapsed time, sample counter or callback is manufactured by receiving a word.

Only running reset-selected ROM with its prior outgoing mail already consumed
is admitted. Unselected execution, unknown commands, unexpected receiver state
and failed trace writes stop without consuming the failed low word or changing
the prior parameter record. Trace output names every accepted actual mail and
its retained fields. The final **D001 start-PC parameter** records the real task
request and explicitly refuses with **`DSP ROM task launch requires a DSP task
backend; transfer and execution not implemented`**. It never emits Zelda init
mail or pretends a task was downloaded. Nonzero data length is retained input,
not a silently completed transfer. All unsupported audio/DMA execution remains
an explicit refusal.

Linux AArch64: **681 host tests pass**, nine ROM scenarios reusing the existing
DSP init probe: actual command/parameter retention and masks, unsupported task
launch, reset clearing, unconsumed ready-mail refusal, non-ROM refusal, unknown
command refusal, command/parameter trace failures, and preservation across a
refused init transfer. RAM, complete DSP state outside the permitted input
record/latch/error, audio state, cycles/timebase, interrupts and GX frontier stay
unchanged. Test-first intake failure in **logs/ww16-dsp-rom-red.log**; an additional
red regression caught early metadata clearing on a refused init transfer
(**logs/ww16-dsp-rom-refused-init-red.log**), fixed before commit. Final suite
**logs/ww16-dsp-rom-verified-host.log**. ARM host links
(**logs/ww16-dsp-rom-verified-arm.log**, only the existing DSP signedness warning).
Audit, attribution and whitespace pass. No runtime pin or desktop behavior is
changed. Windows CI and physical Vita are not tested here. Clean committed VI/O1
rebuild and watched **MEM1 + live-PNG** trial follow; host tests alone do not
certify authentic protocol progress, task execution or visible gameplay.

## Unit 51: the authentic ROM sequence reaches the real task launch request

Committed VI/O1 build **8824a11**, watched MEM1/live-PNG run
**5befd4ad675848c2bf257377f60116c8**: **nine actual accepted ROM mails** reach the
final start-PC parameter. The complete four received parameter fields are
**source 00399420 / 7,456 bytes / data length zero / IRAM destination 0000**;
requested task entry **0000**, field-presence mask **0F**. Source, destination,
length and entry are producer observations, not inferred from a known CRC.
The last parameter is refused at **CC005002 / 803190B0**, word-low value
**0000**, with **`DSP ROM task launch requires a DSP task backend; transfer and
execution not implemented`**. Nothing reports a code transfer, task execution,
Zelda handshake, DSP interrupt or fabricated completion.

Still **nine draws / eight presentations**, **8,963 decoded / 2,048 journal
commands**, zero skips/refusals/gaps. Two actual MEM1 captures, eight actual live
PNG pairs, eight presentation stamps and ten retrace stamps verify. All display
PNGs are uniform opaque black; the last was opened. Both contemporaneous guarded
two-draw-prefix replays remain clean. The final precharged guest cycle/edge
totals remain **85,380,253 / 1,035,459**; no added-time or FPS claim is inferred
from the newly accepted input sequence. Owned host state grows by the actual
**16-byte** ROM parameter record; memory/GPU residency remains explicitly unknown.

Private hand-off root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-5befd4ad675848c2bf257377f60116c8
```

Latest actual image **`evidence/frames/frame-8.png`**; image/check/memory manifests
**`evidence/live-images.json`**, **`evidence/live-capture-checks.json`**,
**`evidence/mem1-capture.json`**; extracted fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`** and exact bridge provenance in
**`evidence/guarded-mem1-replays.json`**. Original ROM request/parameters remain
in the private guest log. Install **1.25 s**, guest **92.20 s**, closed report
followed by known exit **133**, not timeout/inactivity. This is not a no-dump FPS
baseline, visible gameplay, physical-Vita measurement or whole-run GPU replay.

The next dependency is an executable sourced DSP task backend. The existing
shared adapter and pinned interpreter/HLE are being checked for ARM source
reuse; their desktop build does not establish Vita support. The launch STOP
stays intact until a real transfer/execution path is proved.

## Unit 52: coordinator-approved audio deferral keeps the video trial explicit

The coordinator now authorizes a **declared no-op DSP task backend** to prioritize
visible pixels, then video FPS. This changes the prior no-synthetic-DSP-mail rule
**only in an explicit audio-deferred profile**; it does not authorize fabricated
GPU completions, guest instructions, callbacks, RAM, frames, timestamps or FPS.
The real DSP port is paused. Its five source candidates passed syntax and actual
ELF32 ARM object compilation with VitaSDK GCC **15.2.0**; none was linked or run.
Private compiler/source/object hashes and unresolved symbols remain under Unit
51's **evidence/dsp-arm-source-probe/**, including **arm-objects/objects.json**.
Read-only local fmt headers were used for that probe; no pin, dependency or other
repository was modified, and no network was used.

### What this game's boot actually checks

The actual translated guest body, rather than the older notes' approximate
token spelling, establishes the required bootstrap:

- **8028ECA0 interrupt handler** acknowledges **DSPINT 0080** through the DSP
  control register, then polls and consumes a busy mailbox word. This is a DSP
  task interrupt, **not AID 0008 PCM-DMA completion or ARAM 0020 completion**.
- The **DCD10000** branch at **8028ED78-8028EDA4** writes task state **1** and
  calls its **+28 / 40-byte init callback**, the actual **8028E860 DspHandShake**.
  The host must not perform either guest write or callback for it.
- **8028E870-8028E880** polls **DSPCheckMailFromDSP** until the busy bit is set,
  reads and discards a second mailbox word, then calls **8028F080**, which writes
  the JAudio running flag. **It does not compare that second word's content or
  verify any PCM output at this step.**
- Pinned **Zelda.cpp:45-55** and **UCodes.h:58** source the startup pair
  **DCD10000 / F3551111**, first mail interrupting. **DSP.h:27-31,50-60** sources
  DSPINT **0080**, mask **0100**, separately from AID and ARAM. The existing
  interrupt source/guest vector path and normal guest control-register W1C
  acknowledgement are retained, following **MailHandler.cpp:21-68**.

### Explicit stub and report contract

**scripts/vita/ww05/launch.py --defer-audio** is off by default and requires the
real continuation journal. It creates an exclusive **ww05.audio** request bound
to the new run's token/source/package. The host accepts only value **1** and marks
the report header **audio=deferred**, even on early failure. Strict reports and
their prior behavior remain unchanged; a profile/header mismatch fails validation.
Execution/request/result, live-frame and MEM1 provenance also retain the declared
audio mode, and collectors refuse relabelling strict evidence as deferred. WW12
accepts the declared header and retains audio modes in a deferred run series;
presentation/retrace stamps and FPS arithmetic are unchanged.

**vita/host/dsp_deferred.c** accepts one complete, bounded, even-length IRAM-shaped
bootstrap request with zero data transfer. Its task body is **no-op**: **no code
copy, DSP instructions, PCM, sample progress or audio-DMA completion**. It supplies
only the two **explicitly synthetic** startup words and a **synthetic DSPINT**,
masked and acknowledged normally. Reading the first low word exposes the second;
reading the second clears its busy bit without reissuing either word or an IRQ.
The actual guest handler, state write, init callback and running-flag write still
execute as guest instructions. Traces declare the synthetic origin and log real
guest mailbox PC/LR, allowing the next trial to prove which callers consumed them.
Failed trace publication produces no stub bootstrap state. Reset clears the stub
task state while preserving the profile's declared audio policy.

Post-bootstrap task commands and further empty-mail waits remain explicit,
audio-labelled frontiers; this small stub is **not a Zelda mixer or an audio
engine**. They will be classified from actual guest behavior, not automatically
called proof that real audio is required. If a real DSP-processing wait blocks
video, preserve/report that dependency for the coordinator's proposed DSP lane.

Linux AArch64: **693 host tests**, **29 WW12**, **83 FP launcher**, **51 GX** pass;
ARM host links. Evidence **logs/ww16-audio-deferred-certified-{host,arm}.log**,
**logs/ww16-audio-deferred-perf-green.log**, **logs/ww16-audio-deferred-{fp,gx}.log**.
The authenticated strict launch refusal in Unit 51 is the pre-change baseline.
The initial new IO report test used an incomplete synthetic fixture and timed
out; that preserved log is **not counted as a meaningful red feature test**.
The corrected consumer fixture, request/mode integrity, strict-default refusal,
bounded stub inputs, masked/W1C interrupt, two-word queue, early acknowledgement,
trace failure and unsupported later commands all verify. RAM/audio buffers and
guest cycles/timebase remain unchanged in the synthetic stub tests. Audit,
attribution and whitespace pass. No Windows CI, physical Vita, sound or new game
pixels are claimed. Committed rebuild and a fresh watched **MEM1 + PNG live +
audio=deferred** trial follow immediately.

## Unit 53: real guest handler and handshake run; a nonwaiting probe is misclassified

Committed VI/O1 build **5c4483a**, watched live-PNG/MEM1/audio-deferred run
**5f2be0ca0d2d475daaed1a6419ff62f7**: the declared no-op bootstrap accepts the
task request, and the **actual guest DSP interrupt handler consumes DCD10000**
(`CC005006 / 80319098 / LR 8028ED10`). The **actual 8028E860 init callback
consumes F3551111** (`CC005006 / 80319098 / LR 8028E880`). No callback body,
guest state write or guest instruction was skipped. The two mail values and
bootstrap DSPINT are explicitly synthetic, **audio=deferred**, no PCM claim.

The next STOP is **CC005004 / 80319080**, with actual exception caller LR and
parent PC **8028E884**. Source at **8028E880** performs one more **DSPCheckMail**
after consuming the handshake, then **unconditionally calls 8028F080** to set the
running flag. There is no compare/branch waiting on that third query. Thus the
host's generic empty-mail refusal is **too broad**: this run is **not evidence
that real audio/DSP processing blocks video**. The genuine busy-wait is the
earlier **8028E870 → 8028E874 compare/branch**, not this probe. Next fix will admit
only the guarded original nonwaiting query, returning the actual retained
busy-clear mailbox state, **without another synthetic reply or IRQ**.

Final **85,380,652 cycles / 1,035,494 edges**, +399 / +35 beyond Unit 51. Still
**nine draws / eight presentations**, **8,963 decoded / 2,048 journal commands**,
zero renderer skips/refusals/gaps. **Eight actual live PNG pairs**, two actual
MEM1 images, eight presentation and ten retrace stamps verify. All image/memory
provenance and run/report headers retain **audio=deferred**. Display PNGs remain
uniform opaque black, the last opened and inspected; no logo, gameplay or FPS
baseline claim. Both captured, guarded two-draw-prefix replays remain clean.

Private hand-off root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-5f2be0ca0d2d475daaed1a6419ff62f7
```

Latest image **`evidence/frames/frame-8.png`**; image/check/memory manifests
**`evidence/live-images.json`**, **`evidence/live-capture-checks.json`**,
**`evidence/mem1-capture.json`**; fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`** and guarded replay provenance. Original
mail-read PC/LR and exception registers remain in the private guest log. Install
**1.25 s**, guest **128.29 s**, closed report followed by known shutdown **133**;
no inactive/timeout termination. The real-DSP source port remains paused under
the coordinator's video-first decision; this probe correction needs no new DSP
response, task execution, sample clock or audio-DMA completion.

## Unit 54: admit only the original nonwaiting post-handshake mailbox query

The authentic Unit 53 exception proves **PC/LR 8028E884**, after the real callback
consumed its handshake. The fix recognizes only a deferred, already bootstrapped
host reading **DSPCheckMail at 80319080 with LR 8028E884**, whose caller block
**8028E880** is both registered in the actual module and matches **16 original
loaded DOL bytes** through the existing code guard. That is the source's
nonwaiting post-consumption query before **8028F080**, not the earlier polling
loop. Changed/unregistered caller text or the real waiter LR **8028E874** is still
refused. Strict-mode behavior is unchanged.

The permitted query goes through the ordinary device read, returning the actual
retained **7355 high half** with its busy bit clear; the guest's own helper
extracts zero. **No new mailbox word, interrupt, callback, running-flag write,
guest PC jump, memory, time or DSP execution is synthesized.** It merely removes
an erroneous STOP so the next actual guest instruction can execute. Later task
processing remains explicitly deferred and must not be mistaken for a completed
audio operation.

Linux AArch64: **696 host tests pass**, three added contract scenarios (one
parameterized): registered original query, changed/unregistered code, genuine
waiter. The existing synthetic DSP probe checks complete DSP state, RAM/audio
buffers, clocks/timebase, interrupts and frontier remain unchanged by the query.
The original-query test was genuinely red before the guard correction
(**logs/ww16-deferred-empty-probe-red.log**) and green with the whole suite
(**logs/ww16-deferred-empty-probe-green.log**). Actual ARM host links without new
warnings (**logs/ww16-deferred-empty-probe-arm.log**); audit/attribution/whitespace
pass. No hardware, new visible pixels or FPS gain is claimed from these tests.
Commit/rebuild and a fresh watched **MEM1 + PNG live + audio=deferred** trial
follow, keeping video progress separate from any audio-processing dependency.

## Unit 55: nonwaiting query passes; the next guard stops command-list intake

Committed VI/O1 build **553ea1b**, watched live-PNG/MEM1/audio-deferred run
**3e4f864272234ed9b3f69683480bc143**: the actual post-handshake query succeeds,
returning the retained busy-clear mailbox at **80319080 / LR 8028E884**. The guest
returns from its handler/callback path and reaches the ordinary command-list
sender. New STOP: **00000005 to CC005002 at 803190B0**, caller **8028E9B8**,
**`deferred DSP post-bootstrap task command not modeled; audio=deferred`**.

Source **8028E9B0-8028EA20** sends the payload count, asserts the DSP's input
attention bit, waits for the **CPU→DSP mailbox busy bit to clear**, and sends
that many payload words. This first guard failure is at **input intake**, not
an observed wait for PCM, mixing or an audio-DMA completion. Pinned
**Zelda.cpp:163-167,194-202,376-411** sources a five-word setup packet (command
01 plus four pointer words). Its real implementation initializes mixing tables;
a declared no-op can accept/discard that bounded input without reading tables,
mixing or claiming execution. First payload type still needs authentic
confirmation. Unknown commands remain refusals; no processing ACK is assumed
from this producer-count observation alone.

Final **85,389,645 cycles / 1,035,977 edges**, +8,993 / +483 beyond Unit 53.
Still **nine draws / eight presentations**, **8,963 decoded / 2,048 journal
commands**, zero renderer skips/refusals/gaps. Eight actual PNG pairs, two actual
MEM1 captures, eight presentation and ten retrace stamps verify, all labelled
**audio=deferred**. All shown images remain uniform opaque black; the last was
opened. Both guarded two-draw-prefix replays pass with these actual captures.

Private evidence root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-3e4f864272234ed9b3f69683480bc143
```

Latest image **`evidence/frames/frame-8.png`**; image/check/memory manifests
**`evidence/live-images.json`**, **`evidence/live-capture-checks.json`**,
**`evidence/mem1-capture.json`**; extracted fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`** and guarded replay provenance.
Install **1.25 s**, guest **86.10 s**, closed report followed by known shutdown
**133**, no inactive/timeout termination. No logo, gameplay, real audio,
physical-Vita behavior or FPS improvement is claimed. Continue the declared
no-op input protocol rather than the paused full DSP port; a real processing
dependency will be reported only when the guest actually waits for one.

## Unit 56: bounded setup input is discarded by the declared no-op, not executed

The deferred task now accepts the observed **count word 5**, followed by **five
payload words only if the first word is command 01 (81xxxxxx)**, matching pinned
**Zelda.cpp:163-167,194-202,376-411**. The incoming high word is retained in this
declared HLE-style input path so command **81** is not lost when its CPU busy bit
is cleared after actual stub intake (**DSPHLE.cpp:167-186**). Existing halted and
strict-mode mailbox semantics remain unchanged. Only one setup list is accepted
per reset; incomplete/unexpected packet types and later command lists remain
explicit refusals. The first actual payload still needs confirmation in a run.

The **803190B8/803190C0-803190E4** input assertion following the count sets
**DSPAssertInt bit 0002** (`DSP.h:50`). For an explicitly deferred, bootstrapped
task this is a logged **assert-to-no-op / ignored** operation: its transient input
bit is not retained and **no CPU IRQ or task response is raised**. The sender's
mailbox wait can then observe consumption normally; no guest branch is skipped.

Every payload is logged **discarded / configuration=not-executed / reply=none /
pcm_output=none**. No pointer is dereferenced, coefficient table copied, voice
configured, audio buffer written, task executed or processing ACK generated.
The small remaining-word counter denotes **input intake only**, not a completed
audio operation. Failed trace writes preserve the partial input state; successful
reset/init transitions clear the intake record. Audio mode remains visibly
**audio=deferred** in all trial/report/image/memory provenance.

Linux AArch64: **701 host tests pass**, five added setup scenarios including
parameterized trace failures: complete packet with high-bit-preserving command
type, ignored input assertion, wrong processing packet type, count/payload
trace failure, and rejected repeated setup. The reused probe checks full DSP
state outside the allowed intake/latch/error delta, RAM/audio buffers, clocks,
interrupts and GX frontier. Setup intake was red before implementation
(**logs/ww16-deferred-setup-red.log**); final full green in
**logs/ww16-deferred-setup-verified-host.log**. ARM host links
(**logs/ww16-deferred-setup-verified-arm.log**); the new stub's unsigned bound
comparison warning was corrected, leaving the separate preexisting DSP-init
warning unchanged. Audit/attribution/whitespace pass. No sound, new game pixels,
hardware behavior or FPS gain is claimed. Commit/rebuild and watched **MEM1 +
PNG live + audio=deferred** trial follow; source-backed minimal deferral continues
instead of the paused real-DSP port.

## Unit 57: actual setup packet confirms a metadata-ACK wait, not PCM verification

Committed VI/O1 build **6c5b6e0**, watched live-PNG/MEM1/audio-deferred run
**4e6a06f79bda45d6811006ef4218d657** accepts/discards the actual setup packet:
**81000040**, followed by **806B19E0 / 80398F20 / 80398EE0 / 806B79E0**. Its
input attention pulse is actual **0952 at 803190E0**, logged ignored. These are
private producer pointer observations, not copied tables or initialized voices.

The guest now **really busy-waits at 8028E708**, reading **R13-27528**, comparing
against zero and branching back while nonzero. **8028E700** set this setup-pending
flag to 1 before sending the packet; the actual completion callback **8028E6A0**
clears it at **8028E6A4**. This is a meaningful video-blocking dependency, not
the prior nonwaiting probe or a low-CPU host hang.

Source identifies the missing minimal protocol state: **Zelda.cpp:411,501-527**
sends a standard setup ACK consisting of **DSP_SYNC DCD10004** (interrupting),
then **F3550000 | sync_value**, where `sync_value = cmd_mail >> 16` gives **8100**
for this actual packet. The guest handler's +34 / 52-byte request callback is
**80289130**. It checks the second word's F355 prefix and sends its low 16 bits
through **8028EB00**, whose tag comparison and indirect callback at **8028EB44**
dispatch the registered **8028E6A0** completion. **No PCM or coefficient-table
output is inspected by that setup wait.** A declared synthetic acceptance ACK
can satisfy this metadata handshake without real DSP table setup; it must not
be described as actual configuration/mixing completion. The coordinator is
informed of the real wait; the full DSP port remains paused. Further actual
rendering/sample-processing waits will be reported separately.

The process remained high-CPU **and** output-active until the explicit ceiling:
guest **180.01 s**, timeout **124**, TERM/reaped. This attempt stays **INCOMPLETE**;
**no closed guest report exists**, and the original partial file remains
preserved. Last **observed**, not final, progress: **6,169,996,090 cycles /
24,759,442 edges**, PC **8028E708**. Final cycle/edge/decoded/draw totals and
certified frontier verdict are **UNKNOWN/UNAVAILABLE**, not borrowed from a
previous run. No fake closing report, PASS or FPS is manufactured.

Independently closed artifacts remain verifiable: **eight actual presentation
markers and PNG pairs**, **761 real retrace markers**, **two actual MEM1 images**;
all declare **audio=deferred**. The shown images are numerically uniform opaque
black. Existing collectors verify them post-timeout without rewriting PNGs or
changing the original result; both guarded, captured two-draw-prefix replays pass.
These artifact checks do **not** turn the trial into a completed game run.

Private root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-4e6a06f79bda45d6811006ef4218d657
```

Images **`evidence/frames/frame-{1..8}.png`**; explicitly incomplete verification
**`evidence/live-images-post-timeout.json`**, **`evidence/mem1-post-timeout.json`**,
**`evidence/timeout-evidence-checks.json`**; draw fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`**. Preserve the original timeout result,
log and monitor. Next: only the declared no-op setup-intake ACK, with normal
guest interrupt/callback execution and no direct write to the waiting flag.

## Unit 58: declared setup-intake ACK reuses the bounded two-word mailbox

The actual Unit 57 wait is an ACK dependency. In the explicit **audio=deferred**
no-op profile only, the fifth successfully discarded setup word now publishes
**synthetic DCD10004 / F3550000 | received command's upper half**, plus a synthetic
**DSPINT 0080**. Pinned **Zelda.cpp:333,411,501-527** and **UCodes.h:73** source
the protocol; the observed **81000040** command therefore receives **F3558100**,
not a fixed guessed callback token. The actual handler's **8028EEE8** branch
calls the task's **+0x34 / 52-byte request callback**, **80289130**; that callback
validates the F355 prefix and dispatches the tag through **8028EB00**. Its real
registered completion callback, **8028E6A0**, must clear the guest pending flag.
The host does not write that flag, jump the guest PC, call the callback natively,
or manufacture instruction execution.

The bootstrap's hard-coded second-word boolean becomes one owned, bounded
**deferred_reply_mail** slot shared by bootstrap and setup. Reading the first
low word advances to the second; reading the second clears busy. Only the first
word raises the declared task interrupt. Guest masking/W1C acknowledgement is
unchanged, including acknowledgement before mailbox consumption. No outstanding
word, follow-up or IRQ may be overwritten; reset clears queued reply and sync
tag. The setup tag is latched only after a successfully logged command word.
Failed final-word/ACK publication preserves the partial input and emits no reply.

This ACK means **accepted/discarded by the no-op**, not initialized coefficients,
configured voices, completed mixing, DSP execution, PCM, sample progress or
audio-DMA completion. The trace spells out **origin=no-op-intake**,
**configuration=not-executed**, synthetic mail/IRQ and no PCM. Startup/task/mail
diagnostics now declare **bootstrap-and-setup** and **synthetic-no-op-mailbox**,
not bootstrap-only. Every run/report/image/memory remains **audio=deferred**.
Strict behavior, the source-backed ROM receiver and all later unsupported task
commands remain unchanged explicit refusals. The full DSP port stays paused.

Linux AArch64: **707 host tests**, **29 WW12**, **83 FP**, **51 GX** pass; ARM
host links. Setup ACK test was genuinely red on the missing IRQ before the
implementation (**logs/ww16-deferred-setup-ack-red.log**); whole-suite green
**logs/ww16-deferred-setup-ack-reviewed-host.log**. Six added tests cover derived
tag, early interrupt ACK, masking, failed ACK trace, busy-mail refusal and reset
of a queued setup reply. Existing partial intake/unknown command/strict refusal
tests stay green. Complete MEM1, audio state, DSP state outside the declared
mail/intake/IRQ delta, clocks/timebase and GX frontier stay unchanged in the
synthetic probe. ARM logs **logs/ww16-deferred-setup-ack-{final,reviewed}-arm.log**;
only the prior DSP-init signedness warning remains. Audit, attribution and
whitespace pass. No Windows CI, physical Vita, real sound, visible scene or FPS
gain is claimed. Commit/rebuild, then a fresh continuously watched **MEM1 + PNG
live + audio=deferred** trial must prove the actual callback releases the wait.

## Unit 59: real setup callback consumes the ACK and releases the pending wait

Committed VI/O1 build **4f5c074**, watched live-PNG/MEM1/audio-deferred run
**1351063a0837427b87d242d99ace55ec**: the actual handler consumes synthetic
**DCD10004** at **80319098 / LR 8028ED10**. The actual **80289130** request
callback consumes synthetic **F3558100** at **80319098 / LR 8028914C**. Guest
execution then advances to the next command-list sender, proving that the
original setup-pending wait was released. The **8028E6A0** flag-clear path is
inferred from the unchanged original code and progress, not a separately traced
callback entry. No host write to the pending flag or skipped guest instruction.

New STOP: **count 00000002 to CC005002 at 803190B0**, caller **8028E9B8**,
**`deferred DSP post-bootstrap task command not modeled; audio=deferred`**.
Original **8028E800-8028E838** constructs **8D00xxxx + one parameter**, registers
**8028E7E0** as completion, and sends count **2**. The pinned command-0D handler
**Zelda.cpp:469-480** either NOPs or reads/logs the one parameter, then sends
the same standard ACK; it does not mix PCM. Only the count has been observed
authentically so far: do not claim the actual packet is 0D until its next payload
word is witnessed. A bounded no-op can accept count 2 after setup, require a real
**8Dxxxxxx** first word, discard the parameter and declare an intake ACK. Other
packet types remain refusals; no audio-rendering completion is assumed.

Final **85,390,497 cycles / 1,036,079 edges**, +852 / +102 beyond Unit 55's
closed report. **Nine draws / eight presentations**, **8,963 decoded / 2,048
journal commands**, zero renderer skips/refusals/gaps. Eight live PNG pairs,
two actual MEM1 images, eight presentation and ten retrace markers verify, all
**audio=deferred**. All display images remain numerically uniform opaque black;
last image opened. Both guarded captured two-draw-prefix replays pass. No game
scene, real sound, physical-Vita test or FPS baseline is claimed.

Private root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-1351063a0837427b87d242d99ace55ec
```

Image **`evidence/frames/frame-8.png`**; **`evidence/live-images.json`**,
**`evidence/live-capture-checks.json`**, **`evidence/mem1-capture.json`**; draw
fixtures **`evidence/gx-draw-prefix-snapshot{1,2}/`** and guarded replay provenance.
Install **1.26 s**, guest **95.97 s**, closed certified frontier report followed
by known shutdown **133**; no inactivity/timeout termination. Preserve Unit 57's
incomplete run as the genuine pre-ACK wait evidence. The real-DSP port stays
paused; continue only the sourced, declared no-op command-0D intake/ACK seam.

## Unit 60: one bounded post-setup command-0D packet reuses the no-op intake

Only in **audio=deferred**, after a complete setup and fully consumed/acknowledged
reply, the no-op now accepts count **2**, requires first word **8Dxxxxxx**, and
discards exactly one following parameter. Source **8028E800-8028E838** constructs
that packet and registers **8028E7E0**, which clears **R13-27524**; the caller's
remaining wait is for this completion flag, not PCM. Pinned
**Zelda.cpp:469-480** reads/logs this parameter and sends the standard ACK without
mixing audio. The actual next payload is still unconfirmed until the next run.

The setup-only names become **deferred_words_left / deferred_word_count /
deferred_command_sync**. The same bounded input/trace/tag/ACK code handles setup
and control; no duplicate packet engine. At most one setup and one command-0D
packet may be accepted per reset. A command-02 rendering opcode, out-of-order
control count, repeated setup/control, other counts or later commands are
explicit STOPs. No queue collision or partial/failed trace can publish an ACK.
Reset clears both intake records and reply/tag state while preserving the
declared policy.

Control acceptance emits the same **explicitly synthetic DCD10004 / F355xxxx**
pair, tagged from the received command upper half, with **synthetic DSPINT 0080**.
Every word/ACK trace distinguishes **setup** from **control**, and declares
**no-op-intake / configuration=not-executed / pcm_output=none**. No parameter is
dereferenced, DSP body run, RAM/audio buffer changed, completion callback skipped
or guest flag written by the host. This remains a metadata-intake stub, **not a
PCM-processing or audio-DMA-completion backend**. Initial/task policy diagnostics
now include **bootstrap-setup-control**; reports and capture provenance stay
**audio=deferred**, strict default behavior unchanged.

Linux AArch64: **715 host tests pass**, eight new control tests: completed
packet, received tag, control-before-setup refusal, rendering-opcode refusal,
parameterized count/parameter/ACK trace failures, repeated control refusal,
preserved pending setup ACK, and reset of completed control state. Command-0D
input was genuinely red before implementation
(**logs/ww16-deferred-control-red.log**); reviewed full green
**logs/ww16-deferred-control-reviewed-host.log**. The existing complete DSP/RAM/
audio/clocks/GX invariant checks and all setup, strict-mode and guarded-mail
tests remain green. ARM host links (**logs/ww16-deferred-control-arm.log**),
only the preexisting DSP-init warning; audit/attribution/whitespace pass. No
Windows CI, physical Vita, sound, new scene or FPS gain is claimed. Commit,
rebuild and fresh watched **MEM1 + PNG live + audio=deferred** trial follow to
confirm the actual packet and its real guest callback path.

## Unit 61: actual command-0D ACK passes; next frontier is PCM-DMA activation

Committed VI/O1 build **fe73d51**, watched live-PNG/MEM1/audio-deferred run
**162cb9a0a41f461b84e4a1fe1ed61914** witnesses the actual control packet
**8D00000A / 803EA5E0**, deliberately discarded without reading its parameter
target. The actual request callback consumes synthetic **F3558D00** at
**80319098 / LR 8028914C**, after the actual handler consumes **DCD10004**.
Execution leaves the original **8028E838** pending wait and advances to the
audio thread's DMA start. The **8028E7E0** flag-clear path is inferred from the
unchanged original code and forward progress, not a separately logged entry.

New STOP: **8046 to CC005036 at 803169C4**, caller **8028903C**:
**`audio DMA configuration requires stopped engines; enable/transfer needs a PCM backend`**.
Original **803169B4-803169C8 AIStartDMA** reads the length, ORs enable **8000**,
writes it, then returns **without any completion/PCM-result check**. The audio
thread next enters **OSReceiveMessage** at **80289048**, blocking on its own
message queue; this does not by itself prove the other threads/video require
real audio. Length **0046** is **70 32-byte blocks**, not an observed transfer.

The reusable real device's **audio_dma.c:127-145** would latch an active source,
remaining blocks and an AID interrupt when enabled. Do **not** invoke that enable
path in the declared no-op: it would imply transfers/completions the stub never
performs. Next seam suppresses only the requested enable bit in the explicit
deferred profile, retaining the sourced stopped length/source configuration,
**DMA disabled / AID IRQ none / PCM none**, and logs the suppression. If the
guest checks the enable bit it must read disabled, not a fake running state.
No initial DMA completion, sample clock, silence samples or audio callback is
invented. Actual subsequent guest scheduling/video progress must be observed.

Final **85,553,773 cycles / 1,051,944 edges**, +163,276 / +15,865 beyond Unit 59.
**Nine draws / eight presentations**, **8,963 decoded / 2,048 journal commands**,
zero renderer skips/refusals/gaps. Eight live PNG pairs, two actual MEM1 images,
eight presentation and ten retrace markers verify, all **audio=deferred**.
Display PNGs remain numerically opaque black; last opened. Both guarded captured
two-draw-prefix replays pass. No scene, sound, physical Vita or FPS baseline claim.

Private root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-162cb9a0a41f461b84e4a1fe1ed61914
```

Image **`evidence/frames/frame-8.png`**; **`evidence/live-images.json`**,
**`evidence/live-capture-checks.json`**, **`evidence/mem1-capture.json`**; fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`**. Install **1.27 s**, guest **16.08 s**,
closed report followed by known shutdown **133**; no inactive/timeout termination.
This shorter wall duration is **not** an isolated performance measurement or an
FPS improvement claim. The full DSP/PCM port stays paused under video-first
coordination; investigate only honest DMA suppression and actual guest progress.

## Unit 62: PCM-DMA enable is explicitly suppressed, never falsely activated

The original **AIStartDMA 803169B4-803169C8** merely sets enable **8000** and
returns. In **audio=deferred** only, a request matching the existing stopped,
nonzero length is now a logged **audio-dma-enable-suppressed** no-op. This is
admitted only after the declared bootstrap, setup and control packets have
completed intake and their replies/interrupts are consumed/acknowledged. Active
AI/DMA engines, pending replies, zero/changed length, outstanding blocks/work or
an audio interrupt are still explicit refusals. Strict mode keeps its previous
PCM-backend STOP unchanged.

For the actual request **8046**, the existing audio device remains **0046**,
source/configuration untouched, **enable clear**. Reads therefore report the
real disabled state, not a fabricated running DMA. The real device enable
routine (**audio_dma.c:127-145**) is never called; no source is read, remaining
block or sample progress created, PCM buffer written, audio sink invoked or
AID interrupt raised. There is **no synthetic DMA-completion callback**. Normal
guest instruction/cycle accounting and other interrupt sources are retained.
Repeated start requests remain individually logged no-ops while quiescent.
Failed suppression trace is a contextual MMIO STOP with no audio mutation.

This permits the next original **AIStartDMA** return and normal guest
**OSReceiveMessage**/scheduler path to execute, rather than assuming the audio
thread's queue must be artificially fed. Whether other threads/video advance
without AID callbacks remains an authentic-run question. A real video-blocking
sample-processing wait must be recorded for coordination; the full PCM/DSP port
is still paused. Every report and capture remains **audio=deferred**; no sound,
silence samples, sample clock or completion is claimed.

Linux AArch64: **720 host tests pass**, five new DMA scenarios: suppressed
observed enable/readback, failed trace, incomplete bootstrap refusal,
parameterized zero/changed length refusals, repeated no-op. The reused probe
checks the **entire audio structure byte-for-byte unchanged**, complete RAM,
DSP state outside declared prior intake, interrupts, clocks/timebase and GX
frontier. Genuine red before implementation
**logs/ww16-deferred-dma-red.log**; full green
**logs/ww16-deferred-dma-green-host.log**. ARM host links without new warnings
(**logs/ww16-deferred-dma-arm.log**); audit/attribution/whitespace pass. No
Windows CI, physical Vita, new visible pixels, sound or FPS gain is claimed.
Commit/rebuild and fresh watched **MEM1 + PNG live + audio=deferred** trial follow.

## Unit 63: silent trial was killed by the phase ceiling, not a proved module crash

Committed VI/O1 build **6ec634d**, watched MEM1/live-PNG/audio-deferred run
**d5f68d96c8e94c0daa165efd11cd9ee0** has no closed guest report. Its actual
**`phases/guest/evidence/process.json`** records **stop_reason=timeout**, **exit=-15
(SIGTERM)**, **waited=true**, **181.61 s**. The owned runner's phase ceiling sent
TERM and reaped it. The final **process.log** records successful publication of
frame 8, not a trap/abort/module-crash diagnostic. Normal missing-destination
messages around exclusive PNG renames are not crash evidence. The shell harness
subsequently reports SIGTERM too; its sender/cause is **not established**. There
is no root **result.json**: do not manufacture a guest report or completed result.

The audio-DMA request really passes as logged **8046 → retained 0046 / disabled**.
Other guest work advances beyond the prior audio-thread frontier: DVD callbacks,
resource construction, **16 retrace markers** and last observed
**134,504,761 cycles / 3,314,813 edges / 528,663 turns**, PC **80328B30**,
exception **0**. This is **not the final PC/counter snapshot**. That PC belongs
to the original array-construction callback loop; silence alone cannot tell
whether the later stall is guest code, native JIT/render work or host pressure.
Last watched aggregate CPU is about **593%** with silent logs: the run never
met the low-CPU **AND** silence idle rule. No AID completion/PCM is synthesized.

The independently closed artifacts verify after termination: eight actual
presentation markers and PNG pairs, two actual MEM1 images, **audio=deferred**,
all display PNGs numerically opaque black. Original log/partial/process evidence
stays intact. Post-timeout verification is explicitly incomplete and **does not
certify the guest frontier**; final counters and final decoded/draw totals stay
UNKNOWN. An owned-profile process scan finds **no surviving Vita3K process**,
including no Vita3K zombie. Other lanes/builds are not touched.

Private root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-d5f68d96c8e94c0daa165efd11cd9ee0
```

**`evidence/run-diagnosis.json`**, **`evidence/live-images-post-timeout.json`**,
**`evidence/mem1-post-timeout.json`** retain the independent checks and exact
process disposition; original **`evidence/run-watch.jsonl`** retains CPU/log
samples. The DMA wall is crossed, but visible logo/gameplay remains unverified.
Next correction publishes the measured guest disposition before expensive
post-run identity validation, so later launcher interruption cannot erase that
checkpoint. A fresh watched diagnostic trial must locate the CPU-active silent
frontier before changing guest behavior. Under the user's memory-pressure
constraint: **O1, -j2**, no O2 work started/resumed, no leftover owned emulator
before launching. No module-crash, real-audio dependency or FPS claim is made.

## Unit 64: persist guest disposition before post-validation; relaunch with native diagnostics

The launcher now writes exclusive **`evidence/guest-disposition.json`** immediately
after collecting the actual **process.json**, **before** post-run configuration,
input/package hashes and guest-report parsing. It preserves measured status,
exit/signal, reason, waited/duration, run/source/package/launcher identity and
**audio** mode. It explicitly says **validation=PENDING / frontier=UNAVAILABLE /
frontier_verified=false**: this is not a substitute guest report or certified
result. Later errors still fail loudly; exclusive publication cannot overwrite
a prior checkpoint. A later launcher interruption can no longer remove this
already closed disposition file, although it can still prevent final validation.

Linux AArch64: **722 host tests pass**. The timeout/post-validation interruption
test was genuinely red because the checkpoint was absent
(**logs/ww16-guest-disposition-red.log**), then green; the second test verifies
the explicit deferred profile identity remains labelled even before report
validation. Full green **logs/ww16-guest-disposition-host.log**, launcher subset
**logs/ww16-guest-disposition-green-launch.log**. Existing timeout/missing-input,
idle CPU-or-output, strict/deferred report checks and all 720 prior host scenarios
remain green. Audit/attribution/whitespace pass. No guest DMA/audio/rendering or
timing behavior was changed by this launcher correction.

For the next private diagnostic trial, the external watched helper adds at most
two **native AArch64 thread register/maps/instruction snapshots** after at least
20 seconds of silence beyond 90M observed guest cycles. Target identity must
match that trial's owned Vita3K executable. Selected native threads are briefly
ptrace-stopped/read/detached, with no target memory/register writes. Capture
errors are recorded, and a failed live-thread detach aborts only the verified
owned emulator rather than leaving it traced/stopped. This is explicitly
**diagnostic perturbation, not an FPS baseline**, and native PCs are not labelled
as final guest PPC state. Original activity/ceiling policy remains unchanged.

The private helper/sampler sources are preserved under **logs/**, including
the prior helper content/hash and **ww16_native_snapshot.py**. Two synthetic
owned-child checks verify actual registers/maps/instruction reads and detach;
the reviewed check is **logs/ww16-native-sampler-reviewed-check.json**. No game
diagnosis is claimed from that synthetic test. The next run preserves its actual
**native-silent-snapshot-{1,2}.json** if the silent CPU-active frontier recurs.
This observation is needed before guessing a guest loop/JIT/render cause or
adding further audio behavior. Commit/rebuild and one fresh continuously watched
**O1 -j2 / MEM1 + live PNG / audio=deferred** trial follow, with no owned Vita3K
survivor before launch. Other lanes' O2 work remains untouched.

## Unit 65: timeout record survives; CPU activity belongs to software raster workers

Committed VI/O1 **fd552de**, **-j2**, fresh watched diagnostic run
**eaaf26a456a341bea856c169a5ccefde**: both the early disposition and final
launcher result survive. Actual status **124 / timeout / exit -15 / waited=true**,
guest **180.00 s**. Still no closed guest report; the run remains **INCOMPLETE /
frontier UNAVAILABLE**. The launcher evidence-publication correction is verified
authentically, not a game-boot fix. No surviving Vita3K remains after termination.

Actual native snapshot **1** takes **25 ms**, after **20.07 s** silence at the
last observed **134,504,761 cycles / PC 80328B30**. Ten **llvmpipe-0..9** threads
are running and account for the high aggregate CPU. Four sampled worker PCs
execute the same anonymous executable mapping, with actual native LR addresses
inside **`/usr/lib64/libgallium-26.2.3.so`**. Both native **ww-fixture** threads
are sleeping in **futex_do_wait**. Thus aggregate CPU is **software rendering
activity**, not evidence that the guest is spinning on DSP/PCM. Neither a stuck
shader nor the exact guest blocking call is proved yet: the first sampler chose
hot workers and did not capture ww-fixture registers/call chains.

The DMA no-op is witnessed and guest resource work advances as in Unit 63.
Last observed counts match that earlier trial, but are **not final state**. Eight
actual PNG pairs/presentation markers, two actual MEM1 images and **16 retrace
markers** verify post-timeout, all **audio=deferred**; display images remain
numerically opaque black. No logo, gameplay, sound, physical-Vita result or FPS
baseline is claimed. The input snapshots/log/frames remain available to lane 18.

Private root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-eaaf26a456a341bea856c169a5ccefde
```

**`evidence/guest-disposition.json`**, **`evidence/result.json`**,
**`evidence/native-silent-snapshot-1.json`**, **`evidence/native-run-diagnosis.json`**,
**`evidence/live-images-post-timeout.json`**, **`evidence/mem1-post-timeout.json`**;
original native maps/register/instruction bytes and watch samples are retained.

Next observation prioritizes the **actual ww-fixture native threads**, plus
raster/renderer threads, and reads at most **12 native frame-pointer entries**
within each thread's mapped stack. No guessed return address or guest PC is
invented; unwind failure stays explicit. The private sampler's synthetic actual
stack-read/detach check passes (**logs/ww16-native-frame-chain-check.json**).
One fresh **O1 -j2 / MEM1 + live PNG / audio=deferred** diagnostic uses a **300 s
phase ceiling**, within the existing bounded 600 s contract, because much of
the 180 s attempt was spent on earlier CPU-active rendering and left only a
short observation at the new frontier. CPU/output are still watched continuously,
low CPU **AND** silence still aborts at 30 s, and extra time is **not** treated
as a fix or proof of healthy progress. Do not add an audio wakeup or fake GPU
finish to hide this software-render wait. Other O2 work remains untouched.

## Unit 66: real GXM finish wait releases; ninth frame and CARDProbe frontier

Committed VI/O1 **328ccaa**, **-j2**, watched 300 s-ceiling diagnostic run
**14dbc3f44e0446cd9a130675b74e5528** finishes guest execution at **178.96 s**,
before the ceiling. Both actual native snapshots capture the ww-fixture wait;
symbolizing the recorded, mapped Vita3K return addresses against that trial's
owned debug ELF identifies **renderer::finish → export_sceGxmFinish → bridge →
ThreadState::run_loop**, not a DSP-processing loop. Sleeping guest threads wait
while software raster workers execute. Signed/tagged unmapped return values
remain uninterpreted raw evidence, not invented addresses.

The actual GXM wait later releases with **no new DSP/GPU completion or guest
instruction bypass**. A **ninth actual presentation / live PNG pair**, command
**9003**, is published. Therefore the earlier silent trials were observing a
long software-render wait interrupted by their ceilings, not a proved permanent
module crash/deadlock or real-PCM dependency. The longer diagnostic ceiling is
not a rendering fix and does not prove this wall duration on other runs/devices.

New explicit STOP: **8031D400 CARDProbe**, **`desktop interception unsupported;
guest not skipped`**. Existing **runtime/host/src/card_runtime.c:123-126** maps
this desktop entry to CARDProbe; original **8031D400-8031D438** is a 56-byte guest
wrapper that checks retail byte **800030E3** and otherwise calls original
**EXIProbe 8030A448**. Next step admits this registered, guarded original guest
body rather than a desktop memory-card shim. No real HOME, save/card image,
native success/no-card return, callback or device completion will be supplied.
Unsupported later card/device work must remain an explicit STOP.

Closed report: **134,612,247 cycles / 3,322,635 edges**, **9,933 decoded / 2,048
journal commands**, **ten draws / nine presentations**, zero renderer
skips/refusals/gaps. **Nine actual live PNG pairs**, **two actual MEM1 images**,
**nine presentation / 16 retrace markers** verify, **audio=deferred**. The tenth
draw is not presented and is not a tenth image/timing marker. Every displayed
PNG is still numerically opaque black; **frame-9.png** opened. Both guarded
captured two-draw-prefix replays pass, not whole-run resource-version/GPU parity
certification. No logo, gameplay, sound, physical Vita or FPS baseline claim.

Private root for lane 18:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-14dbc3f44e0446cd9a130675b74e5528
```

**`evidence/frames/frame-9.png`**, **`evidence/live-images.json`**,
**`evidence/live-capture-checks.json`**, **`evidence/mem1-capture.json`**,
**`evidence/native-silent-snapshot-{1,2}.json`**,
**`evidence/native-guest-wait-symbols.txt`**, private sampler/helper source bytes;
fixtures **`evidence/gx-draw-prefix-snapshot{1,2}/`**. Install **1.26 s**, closed
certified frontier report, then known shutdown **133**; no timeout/inactive
termination. No surviving Vita3K. Continue the boot/render path at CARDProbe,
with **O1 -j2**, fresh isolated profile, real guest execution and live images.

## Unit 67: CARDProbe is a guarded original guest wrapper, not a desktop card shim

The observed **8031D400 CARDProbe** entry is admitted only when its entry and
last instruction are registered and all **56 original loaded DOL bytes** match
the existing code guard. Text/stack/retail-flag ranges must be owned real MEM1
and not overlap linked aliases. The original **8-byte prologue** needs the
16-byte extent from **SP-8 through SP+7**, covering its saved LR store. Retail
byte **800030E3** remains actual RAM, only observed, never replaced.

The observer emits one bounded **card-probe / mode=guest-body /
result_injection=none / storage=none** trace after validation, then the existing
dispatcher executes the original wrapper. It does **not** set r3, advance PC,
call EXIProbe natively, create/read a memory card or save image, open real HOME,
or inject an attach/detach/callback/completion. Original **EXIProbe 8030A448**
and its device/MMIO behavior remain on the normal guest path. All other desktop
card interceptions stay refused until observed and sourced. Repeated calls still
validate original code/ownership but do not repeat the initial trace; failed
publication stops before admission and does not mark the observation complete.

The reused scheduler/DSP observation probe covers **five added tests**: original
entry admission, parameterized changed entry/tail text and missing/truncated
registration, parameterized low/high stack range refusal, trace failure, and
bounded repeated observation. It checks unchanged guest registers, PC, RAM,
DSP state, cycles/timebase and no fabricated callback/result. The first valid
entry was genuinely red on the desktop refusal before implementation
(**logs/ww16-card-probe-red.log**). During implementation a last-byte registration
check was corrected to the existing API's aligned last instruction; the inherited
DSP low-stack fixture also needed CARDProbe's actual smaller 8-byte prologue
bound. Both failed attempts remain preserved, not counted as separate feature
red tests.

Linux AArch64: **727 host tests pass**
(**logs/ww16-card-probe-certified-host.log**), all 722 previous scenarios green;
ARM host links (**logs/ww16-card-probe-reviewed-arm.log**) with **-j2** and no new
warnings. Audit/attribution/whitespace pass. Synthetic admission is not a game
run or proof of card presence/absence. No Windows CI, physical Vita, sound,
logo/gameplay or FPS gain is claimed. Commit/rebuild, then one fresh watched
**O1 -j2 / 300 s ceiling / MEM1 + PNG live / audio=deferred** trial follows to
observe what the original probe and later boot code actually do. No stale
emulator or player's data is reused; real-DSP/PCM work remains paused.

## Unit 68: original CARDProbe passes; 45 black presentations, no semantic STOP

Committed VI/O1 **feff0a6**, **-j2**, watched run
**ed8c366cf38e42c99ebc2c07f9f73207** records the guarded original CARDProbe
entry at **134,612,247 cycles**, channel **0**, LR **80018F30**, SP **8040CF40**,
actual retail flag **00**. Guest execution subsequently advances and presents
more frames without another card interception STOP. Its return value was not
directly sampled; do not infer a native card result or card presence/absence.

The actual process is stopped by the **300.00 s ceiling**, **exit -15 / waited**,
launcher status **124**, with early disposition and final result retained.
There is no closed guest report or final semantic STOP. Last observed progress
is **434,289,673 cycles / 5,135,976 edges / 1,696,736 turns**, PC **80307EF4**,
exception **0**, not final counters. Actual live captures verify **45 PNG pairs /
45 presentation markers / 53 retrace markers / two MEM1 images**, all
**audio=deferred**. All display images remain numerically opaque black, hash
**c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5**;
frames **27 / 45** opened. Both guarded captured draw-prefix replays pass.
Later frames have no later contemporaneous MEM1 snapshot and must not be
replayed with either earlier snapshot. This run remains **INCOMPLETE**; neither
final decoded/draw totals nor a certified whole-run renderer result is available.

No surviving Vita3K is found. Real GXM waits again release and actual rendering
continues. Repeated scheduler-idle PCs while frames advance do not establish a
new deadlock. Only **53 genuine retraces** have occurred under the retained
clock contract; startup/fade timing remains a possible explanation, not a
diagnosis. The existing single scheduler snapshot predates CARDProbe, so it
cannot explain the later guest queues. Next observation reuses that read-only
snapshot once after **32 actual presentations** and the guarded CARDProbe
entry, in explicitly deferred continuation only. It will name actual wait
queues and saved stacks without waking a thread or changing guest time/RAM.
No new audio behavior, fabricated GPU finish or forced logo draw is justified.

Private root for lane 18:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-ed8c366cf38e42c99ebc2c07f9f73207
```

Actual journal: **`data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`**;
images **`evidence/frames/`**, independent checks
**`evidence/live-images-post-timeout.json`**, **`evidence/mem1-post-timeout.json`**,
**`evidence/card-run-diagnosis.json`**, guarded fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`**. Original disposition/result/native
snapshots/watch log remain intact. No logo, gameplay, sound, physical Vita or
FPS baseline is claimed. Continue test-first observation, commit and certified
**O1 -j2** rebuild before another isolated, continuously watched capture trial.

## Unit 69: bounded post-card queue observation, no guest wakeup or time change

The existing scheduler-idle observer now has one additional observation in
**explicit audio-deferred continuation**, after the original guarded CARDProbe
entry and at least **32 actual presentation markers**. It still requires the
linked REL and the observed **80307EF4** idle boundary. The original single
post-link observation is preserved for strict/default runs. Later iterations
cannot produce unbounded queue/stack dumps; only successful publication sets
the additional observation flag.

Both snapshots identify **stage / audio / actual present-marker count** alongside
the existing run bits, current context, real wait queues, saved PPC stacks,
pending DVD work and renderer/VI counters. The same validated real-MEM1 reads
are reused. No guest PC/register, RAM, audio/device state, timebase/cycle budget,
callback or thread wakeup is supplied. This is diagnostic observation, not a
fix for the black image or a claim that a blocked queue exists.

Test-first: the focused test genuinely failed **one snapshot instead of two**
(**logs/ww16-post-card-idle-red.log**). Five new tests verify the observed queue
contents at the milestone, bounded repetition, parameterized strict/no-
continuation/no-card/31-presentations exclusions, unlinked exclusion, and loud
trace failure without marking the late observation complete. The reused probe
checks unchanged RAM, registers, PC, DSP state, cycles and timebase.
**732 host tests pass** on Linux AArch64
(**logs/ww16-post-card-idle-host.log**); ARM host links with **-j2**
(**logs/ww16-post-card-idle-arm.log**). Audit/attribution/whitespace pass. No
Windows CI or physical Vita run is available, and no logo/gameplay, sound or FPS
result is claimed. Commit and certified **VI/O1 -j2** rebuild, then one fresh
continuously watched **300 s ceiling / journal 2048 / MEM1=2 + PNG live /
audio=deferred** trial. Keep actual images and incomplete provenance even if
the run reaches its phase ceiling; do not infer a DSP dependency from silence
or an idle PC alone.

## Unit 70: post-card snapshot shows a ready main thread, not a main-thread DSP wait

Committed VI/O1 **d7fc157**, **-j2**, continuously watched run
**97cc3eafb3924536ac41f576938faf89** retains two actual scheduler snapshots.
The added one occurs at **320,934,640 cycles / 32 presentations / 39 retraces**:
run bits **00008000**, main **803A2960 state=1 / suspend=0**, queue **803F0338**.
Pinned **GXRuntime/graphics/aurora/include/dolphin/os/OSThread.h:59-63** defines
**1=READY**, **4=WAITING**. Main's actual saved stack returns via **80322C38**
and **802563EC**: original **GXDrawDone** sleep/test wrapper and its guest
caller, not a DSP setup/control or PCM wait. Immediately preceding genuine
**gx-gpu-finish decoded=33588** records a confirmed finish, followed by original
external-vector execution. This snapshot is a runnable scheduling transition,
not proof of a permanent sleep; frames subsequently continue.

There is **one pending DVD completion**. Waiting thread **80425680** has queue
**806AD6D8**, return **802BCC50**; original **802BCC24-802BCC78** locks its
object, calls **OSReceiveMessage 803059D0**, then unlocks. The preceding actual
DVD request uses file pointer **806AD66C**, **0x6C** below the observed queue.
The audio thread remains waiting separately. No main-thread dependency on real
PCM processing is established, and no synthetic AID callback/wakeup is added.

Actual artifacts independently verify **46 PNG pairs / presentation markers**,
**54 retrace markers**, **two MEM1 captures**, all **audio=deferred**, all display
pixels opaque black. **frame-46.png** opened. Its contemporaneous metadata says
**74 draws / 434,302,035 cycles** at capture, not final totals. Both guarded
early draw-prefix replays pass; neither early snapshot can replay later frames.
Last observed guest progress is **442,379,700 cycles / 5,184,100 edges /
1,728,231 turns**, PC **80307EF4**, exception **0**, not final state.

Actual termination: **300.00 s timeout / exit -15 / waited=true**, launcher
**124**, early disposition and final result saved, no closed guest report,
**INCOMPLETE / frontier UNAVAILABLE**. No owned Vita3K survivor remains. No logo,
gameplay, physical Vita, sound or FPS baseline is claimed. The new observation
is authentically verified; it does not fix the black image.

Private root for lane 18:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-97cc3eafb3924536ac41f576938faf89
```

Actual draw journal **`data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`**;
**`evidence/frames/`**, **`evidence/live-images-post-timeout.json`**,
**`evidence/mem1-post-timeout.json`**, **`evidence/post-card-idle-run-diagnosis.json`**,
**`evidence/gx-draw-prefix-snapshot{1,2}/`** retain checks, original identities
and incomplete provenance. Native snapshots and monitor evidence remain intact.

Next is one unchanged-code exploratory capture with the existing **600 s
maximum phase ceiling**, after a committed **VI/O1 -j2** rebuild. Earlier
ceilings interrupt genuine rendering with only **54 retraces**: observe further
startup/resource progress before inventing a new seam. A longer ceiling is
**not** a video fix, a timing change, a promise of logo appearance or proof that
all CPU-active intervals are healthy. The same CPU **OR** process-written
output rule, 30 s low-CPU **AND** silence abort, **128-presentation / 960×544**
capture bounds, journal **2048**, fresh isolated profile, **MEM1=2 + live PNG**,
**audio=deferred** remain mandatory. Do not resume the full DSP/PCM port, alter
guest cycles/retraces or force a wakeup; keep other builds and O2 work untouched.

## Unit 71: 121 genuine black captures; longer observation is not a video fix

Committed VI/O1 **b9dd934**, **-j2**, continuously watched exploratory run
**ec41283c31c74ab1a84f0ed8f772723e** reaches its existing **600.00 s maximum
ceiling**, **exit -15 / waited**, launcher **124**, no closed guest report.
Early disposition and final result survive: **INCOMPLETE / frontier UNAVAILABLE**,
no proved module crash, no final state/counter claim. No Vita3K survivor remains.

Independent checks verify **121 real presentation markers / live PNG pairs**,
**128 real retrace markers**, **two real MEM1 captures**, **audio=deferred**.
Every display image remains numerically opaque black, same RGB/hash as prior
black frames; **frame-121.png** opened. Its actual metadata records **192
submitted draws / 1,041,885,143 cycles** at capture, not final totals. Last
progress sampled earlier is **1,041,880,030 cycles / 8,514,392 edges / 4,060,483
turns**, PC **80307EF4**, exception **0**. Both guarded early draw-prefix replays
pass. Later frames remain unreplayable with these early MEM1 images.

The extended run advances resource work and genuine DVD callbacks. The last
logged request, **56D96078 / 5792 bytes / file 806AD6DC**, has an actual callback
delivery/return at **896,259,929 cycles**. Read-only FST inventory against the
unchanged, identity-verified private disc maps it to **Audiores/Banks/objSE_0.aw**;
other logged bank reads include SystemSE, n_zelda, w_zelda, voice, LinkMotion,
Footnote, Atmos and CharVoice. These are actual resource reads and original
callbacks, **not DSP execution or PCM processing**. Repeated/rounded reads are
not claimed as unique bytes or whole-resource completion certification.
Seven cross-file rounded requests are retained explicitly in the inventory;
the first stricter full-extent check correctly refused COPYDATE's 32-byte read
across its packed 19-byte file, before the inventory was narrowed to request-
start ownership. No disc bytes or game assets are published or changed.

Further wall time did **not** produce a visible logo. It is no longer sufficient
to describe the whole observed black interval as fewer than 60 retraces.
No main-thread real-audio processing dependency is proved. The original bounded
journal covers early draws only, and live image metadata lacks later renderer
**skips / shader refusals / copy refusals / gaps**. Without a closed report,
those counts are UNKNOWN: never borrow the earlier ten-draw report's zeros.
Next diagnostic retains the renderer's actual counters contemporaneously with
each already-authorized live image, including refusal/skip/gap observations.
This is read-only evidence, not a renderer fallback or an audio wakeup.

Private root for lane 18:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-ec41283c31c74ab1a84f0ed8f772723e
```

Actual journal **`data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`**;
**`evidence/frames/`**, **`evidence/live-images-post-timeout.json`**,
**`evidence/mem1-post-timeout.json`**, **`evidence/extended-run-diagnosis.json`**,
**`evidence/dvd-resource-inventory.json`**, guarded fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`**. All incomplete provenance/native
snapshots/monitor samples stay intact. No logo, gameplay, physical Vita, sound,
FPS baseline or 30+ FPS claim. Do not raise the phase ceiling or capture bounds
to hide the unresolved black picture; preserve **O1 -j2** and pause full DSP/PCM.

## Unit 72: retain actual renderer health at the live image boundary

Each already-authorized live PNG metadata record now retains its actual
**renderer fifo bytes / events / plans / skipped plans / submitted programs /
distinct program keys / shader refusals / copies / refused copies / display
copies / gap count**, sampled by the same existing stats call **after actual
presentation and before next-frame parsing**. Draw count, identity, explicit
**audio**, original capture clock/cycles and raw/display image policy stay
unchanged. Actual bounded skip/shader/copy reasons and named nonzero gaps are
also emitted with the frame index and audio mode; trace failure remains fatal.
No shader, draw, device completion or pixel is replaced.

The existing private collector validates the optional complete numeric schema
and bounded gap count. Earlier metadata without these counters stays accepted
as **unknown**, never augmented with invented zeros. The counters belong to
that capture instant only and do not turn a later timeout into a certified
final report or provide missing contemporaneous late MEM1 for replay. No-dump
profiles still do no readback and emit no frame metadata; renderer code and
guest instruction/timing/audio behavior are unchanged.

Test-first: actual two-presentation synthetic metadata was genuinely red with
missing **renderer** fields (**logs/ww16-frame-renderer-red.log**). Six added
tests verify contemporaneous counters, real fixture-driven shader refusal with
no extra submitted draw, loud refusal-trace failure preserving already
published images, parameterized malformed integers, partial schema rejection,
and absent legacy counters staying absent. The synthetic triangle's existing
**unresolved_tex_matrix** gap was initially mistaken for zero by the new test;
inspection proved actual values **1 / 2** in the two frames. The test now
asserts that evidence rather than hiding the gap or altering the renderer.
Failed check **logs/ww16-frame-renderer-green-live.log** and exact synthetic
files **logs/ww16-frame-renderer-synthetic-inspection/** remain preserved.
That synthetic gap is **not** a diagnosis of the game's black image.

Linux AArch64: **738 host tests pass**
(**logs/ww16-frame-renderer-host.log**), all 732 prior scenarios green; ARM host
links with **-j2** (**logs/ww16-frame-renderer-arm.log**), no new warnings.
Audit/attribution/whitespace pass. No physical Vita, Windows CI, sound, logo,
gameplay or FPS claim. Commit and certified **VI/O1 -j2** rebuild, then a fresh
continuously watched **300 s ceiling / journal 2048 / MEM1=2 + live PNG /
audio=deferred** trial to expose the actual later renderer counters without
another blind extension of the ceiling. Full DSP/PCM remains paused and all
prior captures/incomplete evidence remain untouched.

## Unit 73: 83 submitted draws, two programs, no observed refusal, still black RGB

Committed VI/O1 **508f611**, **-j2**, watched diagnostic run
**3080b87e2f50436c98fa8efeca7eefae** authentically verifies the new live renderer
metadata. At all **55 actual capture instants**, **plans_skipped / programs_refused /
copies_refused / gaps are zero**. Frame **55**, command **56076**, records
**83 plans / submitted draws / programs**, **two distinct program keys**,
**379,563 renderer bytes / 108,293 events / 55 display copies**, no EFB copies.
These are actual counters at that image boundary, not final totals or pixel
parity proof. Zero refusals are **not** retroactively assigned to the earlier
121-frame trial or to work after this last capture.

Independent artifact checks: **55 live PNG pairs / presentation markers**,
**63 real retrace markers**, **two MEM1 images**, **audio=deferred**; every
display image still opaque black, same RGB/hash. **frame-55.png** opened. Both
guarded early draw-prefix replays pass; there is no valid late-frame replay
using those earlier memories. Last observed progress is **515,297,154 cycles /
5,617,494 edges / 2,012,144 turns**, PC **80307EF4**, exception **0**.
Actual termination remains **300.02 s timeout / exit -15 / waited**, launcher
**124**, early disposition and final result intact, no closed guest report,
**INCOMPLETE / frontier UNAVAILABLE**. No Vita3K survivor remains.

Private root for lane 18:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-3080b87e2f50436c98fa8efeca7eefae
```

Actual journal **`data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`**;
images and per-image renderer metadata **`evidence/frames/`**, independent checks
**`evidence/live-images-post-timeout.json`**, **`evidence/mem1-post-timeout.json`**,
**`evidence/renderer-health-run-diagnosis.json`**, guarded fixtures
**`evidence/gx-draw-prefix-snapshot{1,2}/`**. Original snapshots, source receipts
and monitor records remain private and preserved.

The unresolved question is now **actual pixel output/readback**, not an observed
parser/Cg refusal or main-thread DSP wait. Next verification reuses the existing
**synthetic** WW02 demo's known red default-surface clear, front/back reads,
green FBO clear and known fixture draws, with surface synchronization enabled.
This is separate from authentic guest execution: no game RAM, journal, frame,
callback, timestamp or FPS may be manufactured from a synthetic control. It
contains no disc or DSP/audio workload and cannot establish game rendering.
Its source **vita/feasibility/gx/gx_vita_main.cpp** already implements those
positive controls; do not duplicate them or alter the read-only vitaGL/emulator
pins to obtain an expected pixel. Use a fresh private **O1 -j2** control build
and isolated, continuously watched profile, bounded phase ceiling and immediate
stop on its actual closed report. Keep its package/sources/config/hash/CPU/log
evidence, and report a failed pixel control plainly. Full DSP/PCM stays paused;
no logo/gameplay, physical Vita or FPS claim is justified by these counters.

Private control build wiring **logs/ww16-readback-control-source/CMakeLists.txt**
reuses the committed demo/fixture/PNG and renderer translation sources without
editing them or the pins. Only demo packaging and optimization are private:
**50 generated translation-unit commands all have effective -O1**, verified
before building, **-j2** maximum. Configuration succeeds
(**logs/ww16-readback-control-configure.log**). No game module/disc/MEM1 image
is staged in this synthetic profile. Its result must stay separate from WW05's
explicit audio-deferred guest trials; the synthetic control has no audio/DSP
workload, not a sound claim. Repository audit/attribution/whitespace pass;
the existing **738 host tests** remain the last checked game-host baseline.

## Integration checkpoint: full union with the October 13 trunk

Resolved the pending merge of **508f611** into **vita-integration**, whose
pre-merge head is **0a576a6**. All **12 conflicted files** were reviewed against
both indexed parents, with original versions/diffs retained privately under
**/tmp/opencode/ww16-trunk-merge-508f611/**. No side was accepted wholesale and
no branch, pin, capture or player's data was discarded.

The union keeps lane 18's TexGen sink, color-channel ordering/post-normalization,
indirect/emboss and original-LOD checks; read-only staging deduplication and
deadline validation; selected command-prefix/snapshot binding and honest
coverage verdicts; live frame/readback callbacks, genuine GPU finish tests,
WW05 timing/MEM1/explicit deferred-audio seams, and the trunk's residency,
profile and hot-measurement protocol. Duplicate equivalent PNG/surface-sync
helpers now reuse the shared implementations. Function/test inventories from
both parents were checked after conflict resolution.

The newer trunk supports the original indirect fixture, so old capture-refusal
tests now use the existing **destination-alpha dual-source** refusal fixture
instead. Positive indirect checks remain, along with the explicit refusal
reason and fail-loud capture-publication checks. Cg smoke output also retains
actual unsupported-state reasons. Real-evidence parsing checks the recorded
frontier bytes/complete flag rather than assuming every historical report is
empty. These are compatibility updates to tests, not renderer fallbacks or
invented frames. Initial failures and reviewed/final logs remain preserved.

Linux AArch64, **CC=gcc**, builds **-j2** maximum, isolated test output trees:

- Requested **`python3 -m pytest vita/host/test_launch.py -q`**: **33 passed**.
- Complete **vita/host** suite including DVD tests: **767 passed**.
- **scripts/vita/gx** Python suite: **111 passed**.
- Rebuilt native renderer/bridge/Cg fixtures, **vita/feasibility/gx** wrappers:
  **75 passed** (the preliminary progress message's 76 was a counting error).
- **vita/feasibility/perf**: **29 passed**.
- **vita/feasibility/armv7/test_fp_*.py**: **83 passed**.
- Repository audit, attribution and whitespace checks pass.

No emulator/game trial was launched during the merge intervention. No Windows
CI or physical Vita test was available; local contract-test success is not a
gameplay or FPS claim. No network/push. The ww16 checkout and its pending
synthetic pixel-control work remain preserved for resumption after this merge.

## Unit 74: sync the verified renderer trunk before the next authentic scene trial

The ww16 worktree merges verified integration **c3262c6**, preserving the
separately committed Unit 73 record **7ba92e0**. Only the appended history
conflicts; both histories are retained in full. **vita/** and **scripts/** match
the integration tree exactly before this documentation entry. No pin, private
attempt, player's data or other lane's build is discarded.

The next rebuilt guest therefore receives the trunk's TexGen sink/matrix and
color-channel ordering fixes, indirect/emboss support and explicit original-LOD
refusal, rather than continuing on an older renderer. The genuine finish,
frame-boundary callback, raw/display RGB preservation, live renderer counters
and explicit **audio=deferred** protocol remain intact. This merge is not a
claim that any previous black image now becomes a scene.

Re-plan the pending synthetic control: lane 18 already has a positively colored
authored checker under **/tmp/opencode/ww18/build-f6/ww18/images/
synthetic-checker-visible/**. Read-only verification of the existing PNG gives
the exact recorded SHA **97ed8bae1cf7694508b8d2ccb621e16f85a40cd8df4b4dce7712c97179320da8**,
four colors / **130,560 pixels each**, raw/display RGB identical. Its original
revision **e0b0993** and synthetic-no-game-assets source remain declared.
**logs/ww16-existing-positive-control-check.json** records the check. It is
historical synthetic evidence, not a new emulator trial, a game frame, pixel
parity or a physical-Vita result. Do not repeat that control blindly or borrow
its pixels for the game. The prepared, unbuilt private control project remains
preserved; the immediate next step is the real guest on the synced renderer.

Linux AArch64, **CC=gcc / builds -j2**: **767 host/DVD tests** pass in isolated
**/tmp/opencode/ww16-sync-c3262c6/** outputs
(**logs/ww16-trunk-sync-host.log**); the **111 GX Python tests** also pass
(**logs/ww16-trunk-sync-gx-python.log**). Audit/attribution/whitespace pass.
The integration's native renderer/Cg/bridge and perf/FP results remain the
separate checks recorded above. No owned Vita3K remains before trial preparation.

Commit the sync, then certified **VI/O1 -j2** rebuild and one fresh continuously
watched **600 s maximum ceiling / journal 2048 / MEM1=2 + live PNG /
audio=deferred** trial. Keep genuine presentation/retrace markers and the
**128-presentation / 960×544** capture bounds. The target remains the first
actual nonblack frame and scene content after the clears, not merely more draw
counters. Capture timing is not an FPS baseline; keep the FPS watch's hot-run
protocol separate. If it remains black or hits a refusal, source that actual
wall before changing a seam. No fabricated GPU/audio completion, real DSP/PCM,
guest-time alteration, logo/gameplay, physical Vita or 30+ FPS claim.

## Unit 75: bounded black prefix on the synced renderer and a mixed pixel control

Certified **b8e2048 / VI / O1 / -j2** authentic run
**a3eb11fc6882448c93a4688ca5bc46a9** retains **128 actual live PNG pairs**, all
**960x544 / 522,240 opaque black display pixels**, SHA
**c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5**.
Raw/display RGB is checked for every pair. **129 actual presentations / 136
real `[frame-timing]` retrace markers / two MEM1 snapshots** are observed.
Presentation 129 has **no PNG**: the unchanged 128-image bound stopped the host.
The closed report says **133,260 decoded commands / 2048 logged / 216 submitted
draws / three distinct programs / zero plans skipped, shader or copy refusals**.
Its final stopped state is **1,106,693,761 cycles / 8,796,068 edges**, PC
**80301504**, exception **00000001**; the STOP reason is the capture bound,
not a newly admitted gameplay/audio dependency.

The launcher timed out at **600.001 s**, **SIGTERM / exit=-15 / waited=true**.
It validated the closed guest report and journal but then rejected **129** as
outside the image collector's bound. Original **result.json status=2 /
INCOMPLETE / frontier UNAVAILABLE** and the **PENDING** disposition remain
untouched. Separate **evidence/renderer-postrun-check.json** checks report
identity/framing (**frontier-only PASS**, not gameplay); **verified-live-prefix-
128.json** independently validates exactly the existing 128 images, explicitly
leaving presentation 129's pixels unavailable. **actual-retrace-marker-check.json**
uses the real `[frame-timing] retrace=` prefix, not the nonexistent
`[frame-retrace]` prefix initially searched in the post-run check. No capture
bound, report, PNG, guest RAM or timestamp is rewritten to promote the failed
launcher result. No owned Vita3K remains after the run.

There is a real change in submitted work at **frame 101 / command 102936 /
879,863,624 cycles**: the third program appears and **tlut_texture=2** begins.
Frame 128 records **213 submitted draws / three programs / tlut_texture=56**.
The read-only pin's **GXRuntime/graphics/gxcore/src/gxcore.cpp:1044-1047** increments
that counter for **every valid CI-format texture**, independent of palette
resolution. It is **not evidence that palette decoding failed**; the Vita
translator already uses the pinned CI/TLUT decoder. No full DSP/PCM work is
warranted by that counter. **1,498 actual DVD callback deliveries and returns**
occur in this run. The two early MEM1 images still cannot certify later scene
textures or geometry.

The prepared private WW02 control is now built from clean committed
**b8e2048**, **50 effective-O1 translation units / -j2**, unchanged pins and
vitaGL archive. Build identity is **logs/ww16-readback-control-b8e2048-build.json**;
VPK SHA **226c03f3ab93ae56b70d0cdec1214241c84bb2a25c1ce078f4389118be57f789**.
An absent output-parent error happened **before any emulator started**; its
record remains under **build-f5/ww16-readback-control-runs-b8e2048/**.
Fresh watched synthetic run **2415b6df25574a0f9a7009b6f67a0c8d** uses surface
sync and stops at the actual closed **demo done 2** report after **3.106 s**;
the owned process is terminated/reaped rather than waiting for the 90 s ceiling.

This control is **mixed, not green**: red front **255,0,0,255** is correct;
back and the complete/error-free FBO read **0,0,0,0**; the first triangle and
its clear both read black and fail two pixel assertions. The later synthetic
HUD produces **29 distinct RGBA values** in the actual PNG, SHA
**1fca81c349a1a2ff272f35b252827dc86e48ddeb312c3ccecc15c2d1f5c8e78f**,
opened and numerically checked (**evidence/pixel-check.json**). The demo's
sampled distinct-color count is 11, not the whole-PNG count of 29. This is raw
synthetic PNG evidence with its original alpha, not an opaque game-frame pair.
It proves some current GPU/readback paths yield nonblack RGB, not that the
FBO, triangle, real host's surface or GX transforms are correct.

Re-plan: source the real backend's surface/matrix seam with an authored
synthetic control through **WwRenderer**, which unlike the demo does not
override the captured projection. Keep this separate from authentic frames;
do not guess a TLUT/DSP fix or extend the frame/deadline bounds to hide black.
Existing **767 host/DVD / 111 GX Python** checks for b8e2048 remain the baseline;
this evidence-only unit is verified by the real artifact checks above.
Linux AArch64/Vita3K only; no Windows CI, physical Vita, visible logo/gameplay,
pixel parity, sound or 30+ FPS claim. No network or push.

## Unit 76: transpose captured GX dot rows for Cg's row-vector multiply

The source seam is now reproduced independently of game assets. Clean
**8c05f2d / O1 / -j2** controls use the actual **WwRenderer / vitaGL backend /
Cg**, with lane 18's authored checker copied read-only into fresh private
fixtures. No scene RAM is borrowed. **build-f5/ww16-matrix-control-8c05f2d/**
retains package/config/FIFO identities, watched profiles and exact PNG checks.

Identity run **56ceb89c142f4fe796033d646adb05c4** reproduces the known checker
SHA **97ed8bae1cf7694508b8d2ccb621e16f85a40cd8df4b4dce7712c97179320da8**:
four exact **130,560-pixel** quadrants. The second control only changes the
authored XF position rows: X/Y scale **0.5**, X translation **+0.25**.
Run **d56e92854bc2418c9cbfcaac57d73fe4** still closes **demo done 0**, but
independent pixel verification is **RED**: colored bounds are
**[160,672) x [91,453)**, not **[360,840) x [136,408)**, with color populations
**50,771 / 50,771 / 23,503 / 23,503** instead of **32,640 each**. PNG SHA
**ba1cba7067d41e09a29832b092e3ef07d382a4c82e2b7a4b796d1fddf838f360**.
Raw/display RGB agrees. The report's submission success is not pixel success.

Sourced cause: pinned **gxcore.cpp:1560-1562 / 1641-1658** builds XF and
projection **dot rows**; pinned **gxcore_shader.cpp:946-948** dots those rows
with the view position. Our generated Cg instead uses **mul(vector, matrix)**,
while **translate_transform** previously copied the rows without transposing.
The real VitaGL uniform upload uses **GL_FALSE** and copies its floats as-is
(read-only **custom_shaders.c:2908-2928**). The authored pixel failure matches
translation entering **W** rather than X. No DSP or GPU completion hypothesis
is needed to explain this demonstrated transform error.

**gx_vita.cpp** now transposes both the padded XF position matrix and projection
into the Cg row-vector uniform layout. Per-vertex PNMTXIDX remains transformed
on the CPU with an identity position uniform; it is not transformed twice.
The header states the actual layout. Existing direct-value checks are updated;
a new parameterized renderer check compares non-symmetric translated/scaled
position and orthographic/perspective clip results against the GX row-dot
reference. It fails on the old translation at **view == expected_view**
(**logs/ww16-matrix-native-red.log**) and passes after the change.

Linux AArch64, builds **-j2**: **767 host/DVD**, **75 GX native-wrapper** and
**111 GX Python** tests pass; native framebuffer PNG checks pass too
(**logs/ww16-matrix-host.log / ww16-matrix-native-suites-certified.log**).
The initial suite invocation used the wrong renderer-output environment and
was rejected as stale; that log and the separate missing PNG CLI-argument
invocation remain preserved. The final suite explicitly selects the fresh
outputs for both **WW02_BUILD** and **WW17_BUILD**. Whitespace/audit/attribution
pass. No source pin or production desktop path changes.

Commit this unit, rebuild the committed control **O1 / -j2**, and require the
actual translated checker pixels to match before the next fresh watched
authentic **VI/O1 / journal 2048 / MEM1=2 / live PNG / audio=deferred** trial.
Keep the 128-image and 600 s maximum bounds; stop a diagnostic at its closed
report rather than wasting its remaining ceiling. The transform seam is
proved on synthetic/native evidence only so far; no game scene, hardware
parity, physical Vita, Windows CI, sound or FPS result is claimed.

## Unit 77: the committed transform fix passes exact GPU pixels before game rebuild

Clean committed **5f3e179**, **56 effective-O1 control translation units / -j2**,
rebuilt in a new private directory **build-f5/ww16-matrix-control-5f3e179/**.
The input bytes are the unchanged authored identity/translated checker
fixtures from Unit 76, not expected-image files or any captured game scene.
Package SHA is **9217f835171d00a28a511847d1fa3fb888adff3f013dc77d928b462183d89977**;
**build/control-build-identity.json** records source/config/compiler identities.

Fresh continuously watched control **c9daa81a2adc40729788acc336342d87** keeps
the exact identity-checker SHA and four **130,560-pixel** quadrants. The corrected
translated control **a1641f325649471abd9c751556a4af49** now gives exactly
**[360,840) x [136,408)** colored bounds and **32,640 pixels of each authored
color**, with **391,680 black background pixels**. Raw/display RGB is identical.
PNG SHA **df16dd48e7ad776b40bb3c5d0854264fa57edd76c71e03b3d8d6be372c7bbd4d**;
opened and inspected, with exact checks in each run's
**evidence/matrix-pixel-check.json**. The same translation control was pixel-red
before the fix; it is pixel-green now, not merely a successful draw submission.
Its actual closed report is observed and the owned process terminated/reaped
after **2.563 s**, rather than waiting for the 90 s ceiling. No owned Vita3K
survivor remains before game trial preparation.

This establishes the demonstrated GX-to-Cg transform seam on Linux AArch64 /
Vita3K with the current renderer. It does **not** turn the previous 128 black
game frames into a scene, certify the mixed FBO/WW02 triangle control, establish
later game resource versions, or prove physical-Vita/pixel parity or FPS.
The **767 host/DVD / 75 GX native-wrapper / 111 GX Python** results apply to
this exact code; this unit only appends evidence, without another renderer fix.

Next: certified committed **VI/O1 -j2** game rebuild, fresh watched authentic
trial with unchanged **journal 2048 / MEM1=2 / live PNG / audio=deferred** and
**128-image / 960x544 / 600 s maximum** bounds. The private copied watcher
**logs/watch-matrix-run.py** retains the original watcher and adds an explicit
owned SIGTERM once the actual bounded, identity-validated guest **END** report
exists, to avoid waiting through active shutdown after the diagnostic is
already over. It records **evidence/closed-report-stop.json**, does not write a
guest report or inject a completion, and leaves termination and collector
failures visible. Its syntax is checked; that new stop behavior still awaits
the real trial. CPU-or-process-output growth monitoring and the 30 s
low-CPU-plus-silence abort remain unchanged. Native ptrace sampling is not
needed to retest this sourced pixel seam. Full DSP/PCM stays paused; no network,
push, player data change, game scene or 30+ FPS claim.

## Unit 78: first authentic nonblack frame, the Nintendo boot logo

Certified **98c99e1 / VI / O1 / -j2** run
**b0f643a2cf6f4ad5916c791df6b87365** verifies the matrix fix on actual game output:
frames **1-10** retain the exact opaque-black hash, then **frame 11 / command
11004 / 150,764,580 cycles** shows the real **Nintendo boot logo**. Its PNG has
**25,616 nonblack RGB pixels**, first visible red values up to 15, SHA
**0411aab11be85ba3df03fd4cf62c1640f010774c10378cef6800c60308d9cda6**.
Frames **38 and 93** show the clear red logo, each **26,422 nonblack pixels**;
frame 93 PNG SHA is
**686603975980849d2b6e60b5605fb16f009655beaa6946e95cbdce2043af1f2e**.
All three PNGs are opened and visually inspected; every preserved raw/display
pair has identical RGB, not synthetic replacement pixels or a replay image.

**93 genuine live PNG pairs / 101 retrace markers / two initial MEM1 images**
are independently verified in **evidence/postrun-image-memory-check.json**;
**first-visible-logo-check.json** records exact first/logo-frame pixel counts
and identity. Frame 93's actual counters are **121 cumulative submitted draws /
two programs / zero recorded skips, refusals or gaps**, command **94722**,
**815,055,002 cycles**. This is not a 121-draw scene or a whole-run final total.
The last bounded image is a logo, not gameplay.

The authoritative runner disposition remains **600.001 s timeout / SIGTERM /
exit=-15 / waited=true / status 124 / INCOMPLETE / frontier UNAVAILABLE**, with
no closed guest report or final guest counters. The owner-directed capture
handoff also led to a controller TERM request near this run's end; its separate
record does **not** override or explain the runner's measured timeout. Cleanup
finds no survivor requiring KILL. The logo observation is real even though the
attempt is incomplete. The private root and every prior attempt remain intact.
No physical Vita, Windows CI, gameplay, sound, parity or FPS claim.

## Unit 79: complete state prefix and draw-time MEM1 in an explicit scene window

Read lane 18's **gx_black_frame_audit.md** from its worktree at delivery
**2cef9ce** (the local integration checkout does not yet contain that file).
Its **b8e2048** diagnosis is accurate: only 2048/133260 commands and init-time
MEM1 cannot replay later work. Its first extra-draw interval is **9976-11004**;
the first CI interval is **101854-102936**. The new logo result above is a
different, rebuilt source; it does not retroactively change the audited inputs.

The capture seam now accepts up to **196,608 complete commands**, streamed to
the private journal rather than accumulated in guest RAM, with a separate
hard **8 MiB raw-FIFO-byte budget**. Exhaustion fails before certifying another
command; the existing **64 MiB text-log read bound** remains. A native test
really feeds/logs **133,260 commands**, reuses drained native buffers, and
continues decoding command 133261 without falsely logging it. The 2048-prefix
option still works; these bounds do not invent unobserved command bytes.

New opt-in **--mem1-command-window START END**, with **--mem1-snapshots 1..8**,
takes the actual full **24 MiB MEM1 immediately before every draw** in that
inclusive interval. Earlier draw snapshots are skipped, but every preceding
CP/BP/XF/FIFO command is retained from command 1. A missing/inverted/unlogged
endpoint or endpoint without snapshots is rejected before guest execution.
Identity-bound **ww05.mem1-start / ww05.mem1-end** reuse the existing strict
request reader. Eight full files bound disk cost to **192 MiB**; capture writes
the existing MEM1 in **64 KiB chunks**, with no shadow MEM1 allocation or dirty-
write tracker. Incremental snapshots are unnecessary for this bounded first
delivery. More selected draws than the requested snapshot budget explicitly
STOP instead of silently losing a resource version.

Each selected snapshot has its original token/source/package/command/PC/frame/
cycle/time provenance and a native command-window witness. Its lane 18
**gx-mem1-snapshot-v1** manifest binds the exact FIFO **prefix through that
specific draw**, not future commands or an init-time digest. Use **extract.py
--through-command DRAW --snapshot ww05.mem1-N.snapshot.json**. The collector
labels earlier resource versions and external aliases/MEM2 **uncertified**;
neither old RAM nor zero padding supplies missing late resources. Every selected
draw in the interval gets its own resource version. No claim that late MEM1
certifies the resources of all earlier draws.

The explicit window ends the diagnostic only **after the actual completed end
command and its renderer/presentation processing**, with a declared
**STOP reason=scene MEM1 draw window capture complete**. This produces a real
closed guest report rather than waiting through an unrelated remainder of the
600 s ceiling. It is off by default; ordinary continuation still has no guest
execution cap. The 128-live-image / 960x544 bounds, real VI clock/guest handlers,
CPU-or-output activity watch and **audio=deferred** semantics are unchanged.

The acceptance tests were red on the old 2048 ceiling before implementation.
Linux AArch64 **-j2**: **779 host/DVD tests / 1911 subtests**, including streamed
large prefix, raw-byte exhaustion, late RAM mutations, per-draw digest binding,
snapshot-budget failure, identity-bound endpoints and strict main-loop window
termination; **111 GX Python tests / 78 subtests** pass. Initial focused suite:
**159 tests**, then the added request/window guards pass in the full suite
(**logs/ww16-scene-capture-focused.log / ww16-scene-capture-host-final.log**).
The **75 GX native-wrapper / 111 GX Python** checks are also rerun together:
**186 tests / 109 subtests** pass. Snapshot verification stores provenance only
for the selected at-most-eight draws and hashes FIFO prefixes incrementally,
instead of retaining a per-command provenance dictionary for the large prefix.
Audit/attribution/whitespace pass. No renderer, audio, pin or production default
is changed by this capture unit; no Windows CI or physical Vita available.

Commit/rebuild **VI/O1 -j2**, then fresh watched capture of **9976-11004** with
the complete prefix through 11004 and at most eight scene-time snapshots.
Validate the real artifacts before the separate CI-window trial through
**102936**. Lane 18 receives the unchanged private journal, each matching
binary/manifest and actual live PNG/report identities; no public game resources
or push. Nintendo visibility is established; scene replay and 30+ FPS still
need their own evidence.

## Unit 80: first extra-draw window delivered with actual late RAM

Certified **39ee655 / VI / O1 / -j2**, fresh watched run
**b403a0647ca6495cbe8edcd415c0b76f**, reaches the requested **9976-11004**
interval and writes a real closed report at its explicit window-end STOP.
**11,004 / 11,004 decoded/logged commands**, **74,190 exact FIFO bytes**,
**12 cumulative submitted draws / two programs / 11 presentations / 18 retraces**;
zero recorded skipped plans, shader/copy refusals or gaps. **11 live raw/display
PNG pairs** validate, and the opened last image is the actual Nintendo logo,
with the same first-visible SHA as Unit 78. It is not a substituted replay.

The **two actual 24 MiB MEM1 snapshots** are before the two draws in this
window, not the initial draws 990/1962. Both precede presentation 11:

| Snapshot | Draw command | Actual cycles | MEM1 SHA-256 | Exact bound FIFO prefix SHA-256 |
| --- | ---: | ---: | --- | --- |
| 1 | 10716 | 142689111 | `4a0503bfa9fd53ac9b868522df742ef95ac5d38b0d992b5ec79c3dc92aea355a` | `f8869c428817908fe3a6773fd351f8f070ac0ee5f48a6065f39fb8a76d3274e6` |
| 2 | 10767 | 142693052 | `beb257c9dfa5ffa39022c4cdd3f1e161640d7d4dc80f259dcbf17f680da661ea` | `9c7b233a00bbd8dc49a8fbac367f9f4b4ada68fcab1e97dc8230654207e7bb9e` |

Lane 18's existing strict **memory_snapshot.load** accepts each original
manifest only with its matching selected draw prefix: **72,094 bytes / 11
draws** and **72,806 bytes / 12 draws**. Asking either snapshot to certify the
later full-window FIFO is explicitly rejected, rather than rebinding its
digest. Each binary supplies exactly the complete physical MEM1 region;
the reconstructed loader image is byte-identical, with no holes or zero padding.
The differing RAM hashes are kept as separate resource versions.

The same-source GX native bridge (renderer code unchanged from the already
tested matrix fix) replays both guarded inputs: **verdict=ran**, all **11 / 12
plans/programs** accepted, zero recorded skips/refusals/gaps. This checks the
loader, state prefix and translation chain, **not GPU pixel parity** or earlier
resource versions. External aliases/MEM2 remain uncaptured and explicitly
uncertified. No new emulator/GPU replay is started for this handoff.

Private root (read-only original inputs for lane 18):

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-b403a0647ca6495cbe8edcd415c0b76f/
```

- **evidence/lane18-scene-window-handoff.json**: exact source/package/report/log
  identities, snapshot timing, selector commands and guarded replay results.
- **data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/**: unchanged **ww05.log /
  ww05.report / ww05.mem1-{1,2}.bin / .json / .snapshot.json**.
- **evidence/late-draw-prefix-snapshot{1,2}/**: extracted FIFO, original snapshot
  provenance, physical coverage and **guarded-native-replay.txt**.
- **evidence/frames/frame-11{,-rgba}.png / frame-11.json**: actual visible frame,
  original RGB and contemporaneous cumulative counters.

For fresh extraction, use **--through-command 10716** with snapshot 1 or
**--through-command 10767** with snapshot 2, never the full-window FIFO digest.
The handoff JSON contains exact commands with a new private output placeholder.
No source/journal/manifest from another run is borrowed, overwritten or published.

Actual closed STOP is **scene MEM1 draw window capture complete**, final state
**150,764,595 cycles / 3,416,100 edges / PC 80301504 / exception 00000001**.
The copied watcher observes the identity-validated real END report and signals
only the owned emulator. Measured process disposition is **exit=133 /
waited=true / 224.440 s**; the launcher's validation says **COMPLETE / frontier-
only PASS**, while its **status=133** remains visible. This is a closed capture
diagnostic, **not a clean normal game exit**. No owned Vita3K survivor remains.
Journal SHA **e87a504a01aba4f64658b45907022e38a2ffea5f4174faf5f3fcdc869bbfb75e**;
report SHA **bd65e89317e0d51d3fba7cb901d47153e40a8863e5ed1ecfbc911879da2585ed**.

Next: commit this evidence, certified **VI/O1 -j2** rebuild, then a separate
fresh continuously watched **101854-102936** CI-window capture, capacity
**133260 commands / at most eight draw-time MEM1 images**, unchanged 600 s
maximum / 128 live images / actual CPU-or-output watch / **audio=deferred**.
The first interval's delivery gate is met; a genuine CI input, later scene
content, gameplay, physical Vita, Windows CI, sound, parity and 30+ FPS still
need their own evidence. No pin change, network or push.

## Unit 81: the first CI attempt misses its window without borrowing earlier RAM

Certified **b276f38 / VI / O1 / -j2**, watched attempt
**a31f77e552114075806cf6ec06f6c536**, keeps executing/rendering but stops at the
unchanged **600.001 s ceiling**, before **101854-102936**. **85 actual live PNG
pairs / 92 retrace markers** validate independently; the complete structurally
framed journal prefix contains **87,529 commands / 590,240 raw FIFO bytes**.
No snapshot binary is written: only the identity-bound window request files
exist. **captured=0 / window_complete=false** is independently checked; the
Nintendo-window or init RAM is not substituted for the missing CI resources.

Frame 85 / command **86586** records **113 cumulative submitted draws / two
programs / zero observed skips, refusals or gaps**, **750,246,461 cycles**.
Last observed progress is **750,285,684 cycles / 7,016,288 edges / 2,927,097
turns**, PC **802EC8BC**, exception **0**. These are observation instants, not
unavailable final counters. The process remains active with CPU or output
growth during the diagnostic; no low-CPU-and-silent hang is proved.

Original **result.json / guest-disposition.json** remain untouched:
**timeout / SIGTERM / exit=-15 / waited=true / status 124 / INCOMPLETE /
frontier UNAVAILABLE**, no closed guest report. Private
**evidence/ci-window-incomplete-check.json** preserves exact journal and PNG
checks without inventing an END report. No owned Vita3K remains after reap.
The first extra-draw handoff from Unit 80 is still valid; **the CI handoff is
not ready**. Do not extend the deadline merely to conceal this missing data.

## Unit 82: byte-identical command records without per-byte diagnostic formatting

The capture writer has a sourced, avoidable cost: **gx_stream.c** writes the
header, then calls **fprintf("%02X") once per raw byte**, then writes a newline.
**main.c** explicitly sets this redirected journal's **stderr to _IONBF**;
**vita/feasibility/armv7/platform.c:23-40** redirects it into the private log.
The preceding CI prefix therefore implies **765,298 command-record formatting
calls**, plus separate draw-layout lines. This is a count derived from the
source and actual byte/command counts, **not a measured syscall total, FPS or
proof that formatting caused the missed window**. Real GPU waits still exist.

Isolate a small journal helper: encode the already framed bytes into one fixed
**2,051-byte hex buffer** and make **one formatted command-record call**, with
the original header/provenance/hex/newline unchanged. The separate layout
witness, command/raw-byte limits and fail-loud write checks remain. Counters
advance only after that record succeeds. No guest instructions, cycle charging,
RAM/resources, FIFO payload, framebuffer, GPU completion or audio policy changes.
No full-prefix buffer, shadow MEM1, new dependency or default opt-in is added.
It does not assume the C library performs only one kernel write internally.

Test-first native instrumentation is **red** on the old writer: the short mixed
CP/BP/XF fixture emits **54 formatted calls for 11 commands**, not 11
(**logs/ww16-journal-record-red.log**). After the helper, it emits **11 calls**.
Before/after stderr transcripts are **byte-identical** for that fixture, the
maximum supported **1,025-byte / 255-word XF** command, and the **2048-command**
prefix. Independent saved hashes and transcripts live under
**logs/ww16-journal-record-byte-check/**. Existing first/second write-failure,
raw-byte-budget, continuation, large-journal and snapshot-window tests remain
green; failures cannot certify a partial/unlogged command.

Linux AArch64, builds **-j2**: **781 host/DVD tests / 1911 subtests** pass
(**logs/ww16-journal-record-host.log**); **75 GX native-wrapper + 111 GX Python
tests / 109 subtests** pass together. Audit/attribution/whitespace pass.
The improvement is a proved reduction in formatted calls with unchanged
bytes, **not yet a measured gain on the game diagnostic**. Commit, certified
**VI/O1 -j2** rebuild, then one fresh watched attempt at the **same CI window /
133260-command capacity / eight-snapshot budget / 600 s maximum / 128 live
images / audio=deferred**. Preserve any failure; no guessed renderer/DSP seam,
fake completion, Windows/physical-Vita check, gameplay, parity or FPS claim.

## Unit 83: reduced formatter reaches 93 images but still misses the CI window

Certified **54f1a6e / VI / O1 / -j2**, watched run
**4cf8acd00eef494c806b5347c3497e88**, reaches **93 actual live PNG pairs /
101 real retrace markers / 95,738 structurally verified commands / 644,797 raw
FIFO bytes**. The first **87,529 commands** are byte-identical to the preceding
CI attempt's FIFO. This is more retained diagnostic output at the same ceiling,
not a controlled formatter-only speed attribution or game FPS measurement.

All **frames 1-10 have zero RGB; every frame 11-93 is nonblack**. The opened
last PNG still shows the Nintendo logo and has **26,422 nonblack RGB pixels**,
SHA **686603975980849d2b6e60b5605fb16f009655beaa6946e95cbdce2043af1f2e**.
Frame 93 / command **94722** records **121 cumulative submissions / two
programs / zero recorded skips, refusals or gaps**, **815,055,002 cycles**.
Last observed progress is **823,140,517 cycles / 7,446,716 edges / 3,210,741
turns**, PC **80307EF4**, exception **0**; these are not final guest counters.

The requested **101854-102936** window is still unreached. Exactly **zero MEM1
binaries** exist, with **captured=0 / window_complete=false**. The unchanged
**600.001 s timeout / SIGTERM / exit=-15 / waited=true / status 124 /
INCOMPLETE / frontier UNAVAILABLE**, missing guest report, and per-second
CPU-or-output watch remain preserved. There is no low-CPU-and-silent hang,
closed-report stop or post-logo black scene established here. No owned Vita3K
survivor remains. Read-only verification and all original evidence live under:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-4cf8acd00eef494c806b5347c3497e88/
  evidence/ci-window-incomplete-check.json
```

Read lane 18's follow-up **vita/renderer/gx_logo_replay.md**, delivery
**0bcbf4d**, without editing that worktree. It independently GPU-replays Unit
80's exact selected prefixes: draw **10716** is the textured Nintendo quad;
**10767** is its captured black fade overlay, alpha **238/255**, yielding
**RGB_before × 17/255**. The resulting raw and display PNGs match the actual
live frame 11 byte-for-byte, with zero RGB-channel error. That lane explicitly
labels the selected-prefix final preview **not a guest GX presentation**.
This is strong parity for this one logo/fade image, not scene or whole-game
parity. Its former black audit remains a valid record of the old **b8e2048**
inputs; neither missing CI RAM nor a later black transition is inferred.

## Unit 84: deliver each complete journal record to stdio contiguously

Unit 82 removed the per-byte **fprintf** loop, but its final **fprintf** still
formats directly into unbuffered **stderr**. One formatted call is not a
guarantee about the library's underlying I/O. Refine only this diagnostic seam:
format the original header into a fixed **2,210-byte record**, append the exact
uppercase hex and newline in that same buffer, then call **fwrite** once for
the complete record. There is no full-prefix buffering or delayed flush.
The existing explicit **_IONBF** mode stays; a short write immediately fails
before **decoded/logged/logged_bytes/frontier** certify that command.
Header and framed-command extents are checked before writing the buffer.
The layout witness, snapshot timing, guest cycles/handlers, renderer and real
GPU/VI completion behavior are unchanged. This is not proof of one kernel
write internally or of game FPS improvement.

The new contiguous-write/short-write cases are red on the previous formatter
(**logs/ww16-contiguous-journal-red.log**). The new writer emits **11 whole
records in 11 stdio writes** for the short mixed CP/BP/XF fixture. Existing
first/second-record write failures remain fail-loud, and an injected short
write leaves all command-certification counters at zero. The old per-byte
writer's saved stderr remains **byte-identical** for the short fixture,
maximum **1,025-byte XF** command and **2048-command prefix**; exact checks are
preserved in **logs/ww16-journal-record-byte-check/contiguous-final.json**, with
the matching synthetic proof executable retained separately from later builds.

The first full suite caught two overly broad test-only **fwrite** injections:
the MEM1/PNG failure scenarios also intercepted the new journal write before
reaching their intended capture surface. Scope those existing injections to
non-stdout/stderr capture files, as their flush test already does; do not hide
or ignore either failure. The full rerun is green: Linux AArch64, **-j2**,
**783 host/DVD tests / 1911 subtests**, **75 GX native-wrapper + 111 GX Python
tests / 109 subtests**. Logs: **ww16-contiguous-journal-host-final.log**;
audit/attribution/whitespace also pass. No Windows CI or physical Vita available.

After removing obsolete probe-only formatted-call bookkeeping, the final
stream/drain suite is rerun: **21 tests / two subtests** pass, including every
record failure and the actual 133260-command native prefix.

Commit and certify a fresh **VI/O1 -j2** rebuild, then retry the **same
101854-102936 window / 133260-command capacity / eight draw-time MEM1 maximum /
600 s ceiling / 128 actual live images / audio=deferred**. Neither failed CI
attempt is replaced or promoted. Full earlier FIFO state and per-draw resource
versions remain the delivery gate. No raise of bounds, borrowed initial/logo
RAM, guessed TLUT/DSP/renderer fix, fake frames/completion, sound, gameplay,
whole-game parity or 30+ FPS claim. No network or push.

## Unit 85: contiguous records are authentic, but the CI handoff is still incomplete

The committed **51c5668** rebuild passes all seven private builder stages,
certified **VI / O1 / -j2**. Fresh watched attempt
**1957a0b2c9ff4911b2fe17dd1a59db33** preserves **96 actual live raw/display PNG
pairs / 104 actual retrace markers / 98,789 structurally verified complete
commands / 665,200 exact FIFO bytes**. Its first **95,738 commands** are
byte-identical to Unit 83's already-verified prefix. Every draw-layout witness
is consumed by its matching whole command. The contiguous writer did not
truncate, reorder, replace or invent captured bytes.

All **frames 1-10 are RGB black; every frame 11-96 is nonblack**. Raw/display
RGB is identical for all pairs. The opened frame 96 still shows the Nintendo
logo, **26,422 nonblack RGB pixels**, PNG SHA
**419041854b1f6a82bb046d378aef68ce46d53161079f441226043fe32df94464**.
Frame 96 / command **97773** has **124 cumulative submitted draws / two
programs / zero recorded skips, refusals or gaps**, **839,358,258 cycles**.
Last observed progress is **847,398,697 cycles / 7,590,683 edges / 3,305,190
turns**, PC **80307EF4**, exception **0**, resume **2784**. Those are actual
observation instants, not unavailable final guest totals. The images do not
establish an intervening return to black, a later scene or gameplay.

**101854-102936** remains unreached. Exactly **zero CI snapshot binaries**
exist; only the identity-bound window request files are present. The strict
collector independently verifies **captured=0 / window_complete=false**.
No initialization/logo RAM, alternate run's resource versions, synthesized END
report or unobserved CI bytes are substituted. The original disposition stays
**600.001 s timeout / SIGTERM / exit=-15 / waited=true / status 124 /
INCOMPLETE / frontier UNAVAILABLE**, with no closed guest report. Independent
post-run verification does not overwrite or promote that result.

The continuous watcher preserves **594 guest samples**, actual whole-process
CPU **99.53%-995.27%** over measured intervals, and process-written output
growth. High aggregate CPU is not a main-thread progress or GPU-wait diagnosis;
the low-CPU **AND** silence abort rule was never satisfied. No owned Vita3K
survivor remains after reap. The actual measured live-image wall instants
show **184.106528 s from frame 1 to frame 10**, including **145.707328 s from
frame 8 to frame 9** (81,004,451 to 134,562,378 actual cycles), and
**109.218110 s over the last 20 image intervals**. These are diagnostic wall
observations, including capture/emulator/guest/rendering costs, **not fabricated
guest time, an FPS baseline or attribution to a particular thread**.

Private root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-1957a0b2c9ff4911b2fe17dd1a59db33/
```

- **evidence/ci-window-incomplete-check.json**: exact identity, original failed
  result/digest, complete prefix framing, memory absence, all 96 RGB counts,
  matching PNGs, actual counters and byte-identical prior prefix.
- **evidence/capture-wall-observations.json**: original per-image wall/cycle/
  command differences, no synthetic timestamps or FPS claim.
- **data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log**: unchanged journal
  SHA **287dbe601877985d528779d9d3f973da6202e5b9c2c7f2d09f36dd6e1c987422**.
- **evidence/frames/frame-96{,-rgba}.png / frame-96.json**, original
  **result.json / guest-disposition.json / run-watch.jsonl**, and the copied
  watcher source remain intact.

Do not start another identical CI attempt solely hoping to pass the ceiling.
The source-complete journal and draw-time snapshot implementation are checked,
and the first logo/fade delivery is verified and independently GPU-replayed by
lane 18. **The late CI delivery gate is not met.** The next justified step is a
bounded measurement of the actual guest/main-thread/renderer waits behind the
long diagnostic intervals, before choosing another capture-cost or performance
change. The 600 s / 128 image / 960x544 bounds and **audio=deferred** remain;
there is no newly proved DSP/PCM dependency, TLUT failure or renderer defect.
No further rebuild or emulator is started at this checkpoint. The tested code
remains **51c5668**; this unit records evidence only. Audit/attribution/whitespace
pass on Linux AArch64; no Windows CI, physical Vita, gameplay, sound, whole-game
parity or 30+ FPS is claimed. No network or push.

## Unit 86: owner-requested rebuild at 85ef54f and watched authentic verdict

Inspect the reported leftover compilers before signalling anything. The actual
**ps / /proc** inspection finds **zero compiler, Ninja or CMake processes**,
including no visible compiler zombie. The last existing **vita-build-121.log**
already has **status 0 / 113.500 s**, source **51c5668**; its unchanged timestamp
is not evidence of an ongoing blocked build. **85ef54f** had not yet been
rebuilt at the previous checkpoint. No process is killed without an owned,
blocked identity. Initial evidence is preserved in
**logs/ww16-85ef54f-recovery-initial-inspection.json**.

Run the requested incremental build from clean, committed **85ef54f**, with
**VI / O1 / -j2**, preserving all existing objects. The copied private recovery
controller emits a **15 s build heartbeat**, recording owned process state,
**wchan**, CPU and actual build-output changes; inaccessible kernel stacks are
explicitly recorded rather than guessed. Its declared inactivity stop requires
both low CPU and unchanged process-written outputs for **60 s**. Root-process
CPU remains active during the later artifact inspection/publication interval,
even when no object changes. No inactivity stop or compiler cleanup is needed.
The original trial's separate **30 s CPU-AND-output inactivity rule** remains.

**All seven builder stages pass / status 0 / 120.226 s observed completion**;
the actual incremental Vita build is **113.386 s**, **vita-build-122.log**.
Of **848 prior objects**, **843 retain their exact tracked inode/size/mtime
signatures**, five are rebuilt and **none is missing**. No clean, deletion,
source rollback or unrelated O2/O3 build is performed. All **750 module compile
commands** have effective final **-O1**. The new identity-bound receipt and
private artifacts certify **85ef54f**, not relabelled binaries from **51c5668**.
Recovery evidence, watcher source, build logs and object-preservation result:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/logs/ww16-recovery-85ef54f-e1e57900f02d44548c27d395d25e5fc9/
```

The controller then launches the requested fresh isolated authentic trial,
**a85b38504ed3435db17cf9c43f856bf0**, source **85ef54f**, with unchanged
**133260-command capacity / MEM1=8 / window 101854-102936 / 600 s ceiling /
128-image bound / audio=deferred**. The verified result is **real execution and
Nintendo-logo progression, then the diagnostic deadline**, not a captured new
unsupported-operation wall or low-CPU/silent hang.

**92 actual live raw/display PNG pairs / 100 actual retrace markers / 94,721
complete structurally verified commands / 637,996 exact FIFO bytes** are
preserved. This whole FIFO extent is byte-identical to the corresponding
prefix of Unit 85's run. All **frames 1-10 are RGB black; 11-92 are nonblack**;
all raw/display RGB pairs match. The opened last PNG is the Nintendo logo,
**26,422 nonblack pixels**, SHA
**dad5a19e99c8fdbbc2915c6b9411bf7b44e057e1172640ca5f433fe2991a6a05**.
Frame 92 / command **93705** records **120 cumulative submitted draws / two
programs / zero recorded skips, refusals or gaps**, **806,953,919 cycles**.
Last observed progress is **815,054,247 cycles / 7,398,448 edges / 3,179,257
turns**, PC **80255A04**, exception **0**, resume **2709**. These are not final
guest totals. The watcher preserves **593 guest samples**, measured aggregate
CPU **106.90%-983.41%**, and actual output growth, not a game FPS baseline.

The CI window is **not reached**: **zero snapshot binaries / captured=0 /
window_complete=false**. Preserve **600.001 s timeout / SIGTERM / exit=-15 /
waited=true / status 124 / INCOMPLETE / frontier UNAVAILABLE**, with no closed
guest report. No earlier RAM or fabricated END/report/completion supplies the
missing CI resources. Private result:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-a85b38504ed3435db17cf9c43f856bf0/evidence/owner-requested-recovery-verdict.json
```

That independent verification binds the actual build/source/package, preserved
original result, journal, PNGs, resource absence and watcher measurements.
**postrun-process-check.json** records no compiler or Vita3K survivor and zero
cleanup signals. Journal SHA:
**1d407f214a0995e416b655a1698b63a151684d51bcaf4612062c24312e458a5f**.
Original result/journal hashes are rechecked unchanged after collection.
Audit/attribution/whitespace pass on Linux AArch64. No repository code change,
physical Vita, Windows CI, gameplay, sound, whole-game parity or 30+ FPS claim;
no network or push. The requested rebuild and authentic verdict are delivered;
the late CI capture gate remains open and is not disguised as a success.

## Unit 87: owner-authorized extended CI deadline with actual user-RAM protection

Take the direct, sourced path **(a)** after integrating clean trunk **52a8935**:
extend the **external diagnostic deadline**, not guest time, VI pacing, cycles,
interrupts or GPU/DSP completion. The previous complete 600 s observations
prove continued real execution but not the requested CI extent. Their original
failed dispositions stay unchanged. A new **1200 s guest ceiling**, ending
earlier at the actual **101854-102936** window STOP, is the chosen single trial;
an actual title-screen image is still to be inspected, not inferred from a CI
texture counter or a command number.

The existing **stage_plan.validate_timeout** is the only 600 s launcher cap.
Allow up to **1800 s only for an explicitly validated logged MEM1 command
window**, with the CPU/output watchdog still at **30 s or shorter**. Ordinary
profiles remain capped at 600 s and all existing defaults stay unchanged;
the installer remains at most **600 s**. Invalid window endpoints, no window,
snapshots without a window, disabled/lengthened watchdog or >1800 s still fail
before staging/execution. No renderer, guest-host code, source pin or shader is
changed to obtain the later bytes.

At initial inspection, Linux **MemAvailable is 6.716 GiB** and swap free is
only about **281 MiB**. Use the real kernel **/proc/meminfo:MemAvailable**, not
MemFree, swap or guessed guest heap totals. The extended profile records a
**2.5 GiB minimum** (2,684,354,560 bytes), checks it before installer launch and
again after reaping the installer, before creating the guest request. Below
the floor, execution is explicitly withheld with preserved readiness/error
evidence. A missing/invalid memory measurement fails loud; it is not invented.

The same owned **ProcessIdleObserver** now optionally checks available RAM on
its existing bounded sampling interval. Actual live RAM pressure stops/reaps
only the owned process group, records **stop_reason=memory-pressure**, and keeps
the **125 / INCOMPLETE** disposition when no report exists. Growing guest output
cannot override the RAM guard. A resident **SIGSTOP** is not used: it would
retain the roughly 1.8 GiB emulator allocation instead of relieving pressure.
The guard is off for ordinary profiles; the original **CPU OR process-written
output** activity rule, and **low CPU AND silence** hang detection, are unchanged.

Selected deadline and RAM-gate cases are red before implementation on the old
600 s validator/missing optional guard. Linux AArch64 verification is green:
**86 focused tests / 39 subtests**, then **794 host/DVD tests / 1918 subtests**;
**75 GX native-wrapper + 111 GX Python tests / 109 subtests**. Tests include
1200/1800 window acceptance, retained 600 s installer/default cap, immutable
128-image bound, short-watchdog enforcement, exact available-memory units,
bad/missing readings, no install at low RAM, a second guest preflight after
installer reap, and actual owned synthetic-process cleanup on live pressure.
The synthetic fixtures are not game/MEM1/frame evidence. Logs:
**ww16-extended-ci-focused.log / ww16-extended-ci-host.log**.

Commit, then rebuild the committed source **VI/O1 -j2**, rechecking RAM before
trial launch. The copied private build controller keeps its **15 s CPU/state/
output heartbeat**, preserves objects, and does not resume other O2/O3 work.
The copied extended-trial watcher retains original watchers and records actual
**RAM available / owned emulator VmRSS**, CPU and process-written growth; the
launcher's RAM guard is authoritative. It stops the owned emulator after an
identity-validated **actual closed guest report**, not a synthetic END or frame.

Keep **133260-command capacity / 8 MiB raw journal bound / at most eight actual
24 MiB draw-time snapshots / 128 images / 960x544 / fresh isolated profile /
readonly verified disc / audio=deferred** unchanged. At most **192 MiB snapshot
files**, no additional shadow MEM1 or dirty tracker: longer wall residency,
not an expanded guest memory or capture allocation. Preserve any RAM pause,
timeout, native/shader/resource refusal or real preceding wall. Audit,
attribution and whitespace pass; no Windows CI, physical Vita, sound, gameplay,
title-screen parity or 30+ FPS claim. No network or push. The authentic extended
trial outcome is pending until its real artifacts are collected.
