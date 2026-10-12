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
