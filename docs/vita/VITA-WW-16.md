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
