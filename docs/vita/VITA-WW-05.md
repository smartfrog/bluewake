# VITA-WW-05 — private Wind Waker startup host

**Private, opt-in startup experiment.** The product owner superseded the original execution gate:
WW05 may run repeatedly in fresh isolated private profiles. There is no process-count budget,
authorization token or permanent reservation. Every attempt, including failures, must retain its
source/package identity, exact argv, logs, process disposition and result. No shared profile,
player data, network, publication or FP repair is permitted.
The acceptance frontier is one complete command from the real guest's GX FIFO, not a rendered
scene, title screen, playable Outset, physical Vita result or frame-rate measurement. This ticket
does not adopt any 08 patch. Vita3K's accepted directed-rounding/double-denormal defect remains;
an exception or unsupported service is negative evidence, never permission to patch or skip it.

## Source and opt-in shape

- Integration base: `8583ba88666f6131bebbb6354a3f412861201738`, branch `vita-ww-05-host`.
- Pristine RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1` and DolRecomp
  `b8b534591cba8ca7cd43943a655ee6e2591cf5de`; no pin/source/FP edits.
- The existing importer verifies the privately supplied USA GZLE01 rev0 disc and DOL hash before
  any generation. A hash override is refused. The pinned C generator emits DOL plus 415 RELs;
  the generated tree must match `54f54434c3f9c899d43a96373dc0b4c1aed0e50db8b820b9698dfa76571a770a`.
- `cmake/composite/CMakeLists.txt`: new `BLUEWAKE_STATIC_MODULE`, default **OFF**. Only ON changes
  the target to STATIC and excludes exactly the six `src/core/cpu*.c` sources. OFF retains the
  original SHARED source list, order, flags, definitions and CPU ownership.
- `module_export.c`: the two existing REL export types move to `module_exports.h`, which exposes
  the existing static descriptor/REL/edge-service functions without `dlopen` or a new ABI.
  The default AArch64 export object is byte-identical to the base: 1,069,512 bytes, SHA-256
  `daa263a3ecb0370bd0b8eb7d59edeab3c0e703d3cb827b153bd69e6557a21c11`.
  The comparison recipe/proof is private `build-f5/default-equivalence/proof.json`.
- `vita/host/CMakeLists.txt`: a separate Vita-only real-game target, one `GXRuntime::runtime`
  instance, exactly `GXRUNTIME_ENABLE_AURORA=OFF` and `GXRUNTIME_ENABLE_AURORA_RECOMP=OFF`.
  The private module uses composite O1 with GCC's `tree-dominator-opts` pass disabled, Cortex-A9/NEON/hard-float,
  `-ffp-contract=off`, `-fno-fast-math`, stack-usage files and a link map. Desktop defaults are untouched.
- New build/inspection/staging/report entries are in `scripts/vita/ww05/`. Nothing in
  `scripts/vita/armv7/` or the feasibility fixtures is modified. Existing pin checking, platform
  worker/log/report publication, owned runner, private profile, firmware copying and bwrap construction are reused.
- Shared host seams are reused read-only: `cycle_domain.c`, `external_memory.c`,
  `interrupt_sources.c`, `cache_flush_fallback.h`, `edge_intercept_table.h`, plus the pinned
  loader/boot/MMIO/SPR/cache/guest-memory/GX APIs. No gameplay or shared desktop host behavior changes.

## Startup and explicit stops

The ordinary module ABI/CPU layout/game identity and descriptor/DOL entry agreement are checked.
MEM1/auxiliary VM/locked cache are owned once. Writable relocated REL data and BSS use the
single runtime's owned aliases, excluding module 336 section 6 at initial boot; executable REL
sections are not shadowed. Every acquired alias contributes once to the owner ledger and all
owners are released on terminal success/failure. Alias allocation failure propagates.

The original OS boot globals/SP and architectural FP enable policy are retained. Cycle-domain
observations charge guest time exactly once, with the host's 12 cycles/time-base tick convention.
The PVR remains `0x00083214`; supported SPR and dirty-cache writes use the existing runtime seams.
L2CR (SPR 1017) starts disabled and stores only L2E/L2I control flags. The host has no L2 lines:
global invalidation fences coherent memory without changing RAM, and L2IP remains clear because
there are no pending lines. L2I stays set until the guest clears it. Other flags fail explicitly;
the pinned runtime still enforces supervisor privilege. This empty-cache model is not evidence
of physical cache timing, performance or a completed hardware transfer.
`icbi` reaches the existing cache-control callback with its real effective address. The host
retains owned copies of the loaded DOL text and compares the overlapping instruction bytes before
fencing an invalidation. Unchanged text and data in mapped MEM1 are supported; changed text or
unverified mappings stop explicitly without advancing PC. There is no silent stale-code execution
or retranslation claim. Snapshot bytes are a separate owner inside the 96 MiB heap and appear in
the allocation ledger; partial allocation and shutdown release them. This adds no translated CPU.
The MI interrupt mask at `CC00401C` retains its five documented source bits through halfword
read/write access. Other widths, reserved bits and all other MI services remain explicit failures;
this does not enable memory-protection enforcement or fabricate an interrupt source.
DSP control at `CC00500A` initially exposes the stopped, initialization-pending state `0804` and retains
the AID/ARAM/DSP interrupt masks. Acknowledgements cannot create pending interrupts. The
initial halt/init bits follow the pristine donor's `HW/DSP.cpp`, `HW/DSP.h` and
`HW/DSPHLE/DSPHLE.cpp`; they are not a task-completion token. Reset now has a fixed, guarded
ROM selection model: it requires all 128 live init bytes to match the identified private reference.
The reset bit clears without altering guest time. A stopped ROM does not expose its pending mail;
verified ROM release exposes the documented `8071FEED`, whose busy latch clears on low-word read.
Unknown code, external DSP interrupts and running/queued CPU-mail protocols still refuse
explicitly. The increments below add real ARAM copies and the verified fixed init transfer,
with separate credit-driven state transitions. No general DSP core or audio is implemented. ARAM configuration
halfwords are implemented separately: AR_INFO (`CC005012`, mask `007F`, reset zero),
read-only AR_MODE (`CC005016`, reset one), and AR_REFRESH (`CC00501A`, mask `07FF`, reset 156).
The refresh image is a register value, not a new emulated clock frequency.
AI control at `CC006C00` reuses the pinned C audio-DMA register model with its initial `42` clock
state. Only stopped-clock masks, acknowledgement and sample-counter reset are accepted; start,
frequency changes, reserved bits, other registers and incorrect access widths fail explicitly.
Acknowledgement strips the write-one-clear bit before storing the idle image. No streaming time,
sample, DMA or AI interrupt completion is fabricated.
The runtime offers ordinary guest PCs to the optional host-call hook before trying compiled code.
An aligned address in the loaded descriptor's code ranges (including the dispatcher's physical/
uncached aliases) is declined without changing PC or exceptions, so the original guest code runs.
An address outside those declared ranges, including an unsupported native-query selector, still
fails explicitly. This classification does not implement or skip a guest service.
Known PI/VI/PE/DI/EXI/SI register operations use the pinned C models with width/range guards.
An unimplemented register, SPR, cache operation, instruction, host call, desktop interception,
interrupt delivery, DVD command or EXI/SI transfer stops with reason/address/PC. There is no
fabricated DSP/controller/DVD completion, arbitrary zero-valued service or instruction advance.
Audio/DSP execution, CP hardware service and later rendering are not implemented. These are material risks
of stopping **before** GX, which only actual startup evidence can resolve.

`bluewake_set_edge_service(ww_host_edge, host)` installs the stop latch. The outer dispatcher also
checks the latch and errors on return, including budget boundaries. FIFO writes at
`0xCC008000..0xCC00801F` are converted to big-endian bytes. No padding is inserted and the pinned
non-incremental `dol_gx_recomp_replay_fifo` is called only once a complete first command is framed:

| Command | Complete bytes |
| --- | ---: |
| NOP `00`, invalidate vertex cache `48` | 1 |
| CP register load `08` | 6 |
| BP register load `61`, indexed XF `20/28/30/38` | 5 |
| Display-list call `40` | 9 |
| XF register load `10` | `5 + 4 * count`, count 1–255 |

Draw-without-prior-layout, unknown opcode, unsupported XF count or parser rejection fails loudly.
An actual first NOP is identified as NOP, not upgraded to a register write or draw. The exact
opcode cannot be promised before execution. The marker is **`GX_FRONTIER`**, recording raw command
hex/length, observed `CPUState.pc` and SP at its first byte, completion PC, unprocessed same-write/
same-block suffix bytes and edge-stop status. The current native chunk may finish to its next
edge; suffix bytes are counted explicitly and never replayed as another command. Any later error
still makes the terminal FAIL. A PASS means one parsed stopped command and always `rendered=0`.

`ww05.request` binds a fresh token, compiled source SHA and verified package hash. Existing
request/partial/report/log files are refused and preserved. All report/log writes and publication
flush/close/rename results propagate. Only a closed report is renamed to `ww05.report`; the strict
validator rejects stale identities, HOST evidence, malformed framing, missing stop or rendering claims.
The native I/O fixture is synthetic, `platform=HOST`, and cannot certify actual guest execution.

## Private build and footprint

All heavy outputs are under `/home/fred/projects/zeldaWWHDRecomp/build-f5/`, covered by `/build*/`
before extraction/build. Only new generic source/tests/documentation enter Git. Never upload this
VPK/SELF/ELF, generated source, DOL, disc or firmware. The VPK contains only the private translated
SELF, canonical generated SFO and the verified `main.dol`, title **BWWG00005**. No player data,
save, keys or renderer assets are copied.

```sh
STACK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack
PATH="$STACK/bin:/usr/bin:/bin" TMPDIR=/home/fred/projects/zeldaWWHDRecomp/build-f5 \
  python3 -B scripts/vita/ww05/game_build.py \
  --disc '/home/fred/projects/zeldaWWHDRecomp/Legend of Zelda, The - The Wind Waker (USA).iso' --jobs 2
```

No download or tool/emulator rebuild occurs. `build-evidence.json` records the immutable source,
pins, configuration and ELF/VELF/SELF/SFO/VPK hashes and sizes. `logs/`, `vita/compile_commands.json`,
`vita/ww05.map` and `footprint.json` retain the actual compiler commands, ELF sections/PT_LOAD
mapping, compiled ABI, single-owner symbols and all compiled frame sizes. This inspection precedes
any load. The source SHA is embedded after commit; an artifact is described as executed only when
its own run record binds its exact bytes. Packaging timestamps need not be bit-reproducible; the exact
built bytes are hashed.
Each completed build preserves its five artifact files, footprint, link map, compile commands and
receipt in a fresh `artifacts-<uuid>/` directory. Rebuilding never overwrites these retained bytes;
individual run plans refer to that preserved build, not merely a mutable filename in `vita/`.

- The reused platform reserves a **96 MiB newlib heap** and **256 KiB worker stack**.
- Estimated MEM1/VM/cache backing requests inside that heap are 25,165,824 / 33,554,432 / 16,384
  bytes. Exported initial REL aliases add **1,542,820 bytes**; the complete acquired ledger is
  recorded only during execution. The host CPU/GX/parser object is another allocation inside the
  same heap, not a second independent reservation. DOL loader's transient request is 3,822,272 bytes.
- Real compiled ELF/section/PT_LOAD and `.su` frame sizes are static measurements, not allocations
  or residency. Actual game allocations/pool peak/native stack high-water remain **NOT OBSERVED**.
  The 04 snapshots are not credited as available physical RAM. GPU resources are absent.
- An abandoned O0 pilot exposed a 172,616-byte generated frame; it is not load-eligible. An O2
  build was stopped after expensive optimization of its large generated functions; its interrupted
  log is retained. The O1 pilot compiled `chunks_d_a_auction_183/chunk_0000_rel1_C04C00F4.c` in
  124.470 s with a 72-byte function frame (159,984 bytes in O0). This is one measured unit, not
  a full-stack guarantee. O1 uses the existing composite option only, not a translator/FP patch.
  Final frame/mapping evidence must be checked against the existing bounded native call depth
  and the reservation before execution.
- Compiler profiling on that same untouched auction unit measured 138.138 s wall / 128.77 s
  GCC time, including 75.69 s in incremental SSA updates. Disabling only `tree-dominator-opts`
  reduced this to 52.328 s wall / 47.01 s GCC time, 8.13 s incremental SSA and a 64-byte function
  frame. The private translated target alone gets `-fno-tree-dominator-opts`; the pinned runtime,
  generator, generated source digest and default SHARED target are unchanged. This is a normal
  compiler optimization selection, not an FP workaround or instruction substitution. Retained
  timing/stack evidence is in `o1-time-profile/` and `o1-no-dom-pilot/`.
  No 512 MiB nominal cap or new physical-platform GO is inferred.

## Repeated private execution

After a clean source-bound build, staging alone prepares a fresh private profile with protected evidence:

```sh
python3 -B scripts/vita/ww05/stage_plan.py --wayland-socket /run/user/1000/wayland-0 --timeout 60
```

The staging entry creates no guest request and invokes zero Vita3K processes. To stage, install
and run once under the product owner's revised permission:

```sh
python3 -B scripts/vita/ww05/launch.py --wayland-socket /run/user/1000/wayland-0 --timeout 60
```

Each invocation uses a new UUID/profile. Existing evidence and failed profiles are never deleted or
reused. `execution.json` records execution intent; per-phase `process.log`/`process.json`, emitted
configuration, preparation verification and the terminal `result.json` retain the actual outcome.
There is no ceremonial reservation or numbered authorization pair.

The saved `vita3k-run-<uuid>/evidence/plan.json` contains exact argv, mounts, environment, source,
package, copied runtime/assets/libraries/firmware/config and footprint identities. Exact payload commands:

1. `/stack/vita3k/Vita3K --console --backend-renderer OpenGL ww05.vpk`
2. `/stack/vita3k/Vita3K --backend-renderer OpenGL --installed-path BWWG00005`

Each phase has a **≤180 s absolute monotonic deadline** (default 60 s), starting before Popen, with owned
TERM/KILL/reap cleanup. This is a resource-safety timeout, not a process-count budget. The unchanged
shared runner waits for exit or deadline; the closed WW05 report is collected only after owned cleanup.
A forced Qt shutdown stays INCOMPLETE (124), independently retaining a verified guest frontier
PASS/FAIL. A synthetic launcher fixture is never actual game evidence.

Between phases: fully stop/reap the installer; verify exact installed SELF/SFO/DOL bytes, private
owner/single links/directory contents and all readonly identities; reject crash/config errors;
preserve emitted config, accept only canonical or its `user-id: 00` serialization and restore
canonical; only then exclusively create a fresh request. An unavailable service produces a bounded
negative report. Diagnose and test required host services before a new source-bound attempt; never
skip the failing guest instruction, fabricate hardware completion or alter FP semantics to make it pass.

Containment reuses the established `bwrap --unshare-all --die-with-parent --new-session` pattern:
readonly `/usr`, `/etc`, copied runtime/assets/libraries and private `vs0/sa0`; private home/XDG/
work directories, PID namespace, `/proc`, synthetic `/dev`, temporary `/tmp`; cleared environment,
DEVNULL stdin, unlimited RLIMIT_FSIZE, no arbitrary address-space cap, no network/GPU/session bus/
player home. Only the named Wayland socket is proposed. Guest socket paths use short `/run/ww03`
paths (≤107 bytes), independently of the long physical evidence directory.

Integration remains with the coordinator. No push, PR, Trello/backlog edit or other worker/evidence
mutation is part of this work. Execution is authorized only in the isolated WW05 profile described here.

## Verification

TDD loops observed missing static ownership behavior (six CPU sources still present), missing
input-verification entry, missing full-command checkpoint, missing strict host, missing report/plan
and missing synthetic main I/O integration; each implemented behavior was then run green.
The staging-identity loop also reproduced acceptance of a changed ELF, then rejected it before
profile preparation (`logs/stage-elf-{red,green}.log`). All five build artifact identities are checked.
Additional framing, allocation, ABI, PVR/timebase, alias/BSS exclusion, unknown-service and I/O
regressions characterize reused/current behavior rather than pretending every added test had a red.
Default export-type extraction is the pure-refactoring exception, proved byte-identical above.

At the supplied integration base, the existing armv7 discovery contains **177 tests**, not the
requested 199; all 177 passed unchanged. The other worker's unintegrated tests/deltas are not adopted.
The iterative launcher has fake-process coverage for install-before-request, exact installed DOL
identity, failed-install withholding, preserved stale requests and separating a closed frontier from
a lingering process. Its short red/green loops are in `logs/private-{launch,guest,request}-{red,green}.log`.
The parent-symlink loop reproduced creation outside an owned fake profile, then prevented it with the
existing no-follow directory-descriptor pattern. No real/shared profile was involved.

The WW05 host suite currently has **287 tests**. Source-bound logs retain exact counts/commands for these,
native runtime CTest, 71 native FP cases and the eight direct baseline files. Audit, attribution,
pin/scope/diff and clean-worktree checks accompany delivery. The pre-launch `1ba507c` checkpoint passed
35 WW05 tests, 177 unchanged tests, 53 direct baseline tests, 71 native FP cases and 3/1/5 runtime/corpus/
contract CTest cases on Linux AArch64. Re-running the suite from a deeply nested verification clone
failed two fake socket binds with `AF_UNIX path too long`; that record is retained, while the full suite
passed at the requested worktree path and unchanged native helpers passed the remaining checks.
Physical Vita, Windows CI, a rendered scene and game performance remain **NOT RUN**. Actual WW05
startup is established only by an individual real-run record, never by compilation or synthetic tests.

## First real startup evidence and correction

The clean `ad0fdd7a82e4ea536a6581a807696a61f89824b9` ARM build completed in 5,211.096 s.
Its retained private snapshot is `build-f5/ww05/artifacts-53407ecdb5304b1dabef2caac5772160/`.
ELF PT_LOAD memory totals 351,467,732 bytes; adding the distinct 96 MiB heap container and
256 KiB worker reservation gives a nominal 452,393,172 bytes, before unmeasured costs. This large
mapping is a physical-platform blocker/risk, not a balanced Vita budget. The largest generated
frame is 192 bytes; 25 such frames contribute a conservative 4,800 bytes, not native high-water.
Estimated initial requested owners plus DOL-loader transient inside the heap were 64,847,652 bytes.

Run `904f64c1ece94edc8b67daa87eb3b995` installed verified SELF/SFO/DOL bytes, then ran the
real title in its new network-isolated profile. Both Qt processes reached their 60-second deadlines,
were stopped/reaped, and remain 124/INCOMPLETE. The guest nevertheless closed its report in about
two seconds: frontier **FAIL**, zero FIFO bytes, zero guest cycles, entry/PC `80003140`, and
`STOP reason=unsupported host call address=80003140 pc=80003140`. Report SHA-256 is
`03ea53f190624b4d7504fcc0c624d364502dfe46afba0486bdb811ff205c4343`; its plan/result, raw runtime
log, request, exact argv, emitted config, preparation and follow-up review are retained under that run.
The emulator logged native module segments `81000000..95EBCB74` and `95EC0000..95F3320C`.
These are observed emulator mapping ranges, not physical residency or physical Vita viability.

The failure exposed a host protocol bug, not an FP defect: an optional interception offer was
misclassified as a mandatory service before the entry instruction ran. `logs/guest-offer-{red,green}.log`
reproduces then corrects this for cached, physical and uncached compiled-code PCs. A separate test
retains explicit rejection of unknown host selectors. Later attempts must bind their own clean source
and preserved artifact bytes; this first negative record is neither deleted nor called GX success.

The next source-bound build at `b01d9621633de08aacbc94a1f9392957c724d052` retained snapshot
`artifacts-c81f7b5f0f804542a456d026d4eb8e03/`. Run `bcd832c42f2144a999b1de9790144a8a` proved
the interception-offer correction: the guest advanced 98 cycles and stopped at `803014EC`, SP
`8040CF98`, on `unsupported SPR read address=000003F9` (L2CR), with no FIFO bytes. Its closed
report SHA-256 is `2bf3990a1ddd8e8b2a087cff4a3cbdcb6f27aaf4d3d590d883c285c0ea3b4e40`.
Both Qt processes again reached their 60-second deadlines and were stopped/reaped; frontier
FAIL and process 124/INCOMPLETE remain distinct. The new L2CR tests reproduce the missing
read/write behavior, check empty-cache memory preservation and retain refusal of other flags.

A renewed native-generator configuration found system zlib outside the copied compiler's
sysroot and failed on `zlib.h`; both failed build/check logs remain preserved. The WW05 recipe
now explicitly disables optional RPX zlib discovery, which the DOL/REL C backend does not need.
This changes no pinned source or generated bytes. Existing armv7 helpers remain untouched;
their native test caches used the same explicit private CMake option before rerunning successfully.

Run `20ed631802b84b758e35cb9ac02d1208` at `c7fe91640d52a582506d6397225e0631d14e8c6d`
advanced 109,421 guest cycles, 521 edges, then stopped at `803031D8`, SP `8040CF50`, on
`unsupported instruction fallback address=7C001FAC` (`icbi 0,r3`). FIFO bytes remained zero;
frontier FAIL, process 124/INCOMPLETE, both 30-second phases stopped/reaped. Report SHA-256 is
`1d903788c64046d196e734eebec7e0d637078b90c58bcbfb3fba54d31b18057e`. The `icbi` seam is
checked by tests for unchanged cached/physical/uncached text, non-code RAM, changed code refusing
to advance, owned-snapshot accounting, partial-allocation cleanup and ordinary shutdown. It does
not copy the desktop fallback's unconditional instruction skip.

Run `2770acb8da7a4a6ea57548544a2623fa` at `46b262ceb50ee29e5f7106ac3675c43e3aa3e729`
advanced 116,417 guest cycles and 788 edges. It passed the guarded cache invalidation, then stopped
on a halfword MI interrupt-mask write: `unsupported MMIO write address=CC00401C pc=803047A4`;
terminal PC `8030464C`, SP `8040CF58`, FIFO bytes zero, frontier FAIL. Report SHA-256 is
`bbdf49a699103ce0fb8164580df55132999389ace7a06a11df7c55a118bc73c4`. Both 30-second Qt
phases stopped/reaped, process 124/INCOMPLETE. The real allocation ledger now includes 3,364,032
snapshot bytes and 64,389,684 total requested live owners inside the 96 MiB heap. Offline initial
owners plus loader transient were conservatively estimated at 68,211,956 bytes, not a pool peak.
The next correction adds only the MI mask, checked for readback, reserved-bit refusal and width.

Run `028141b0eb3b457597ba1dcd83f03857` at `308b64440e77386f4af21bb038a3fd132766746f`
passed the MI mask and stopped on the following DSP-control halfword read during IRQ masking:
`unsupported MMIO read address=CC00500A pc=803047C0`. It retained 116,417 cycles and 788 edges,
terminal PC `8030464C`, SP `8040CF58`, zero FIFO bytes and frontier FAIL. Report SHA-256 is
`6c094171eeef81d4420e0a88f11f2000432f95e6bd87e6bbed1e335ae2fd4deb`; both 30-second phases
stopped/reaped, process 124/INCOMPLETE. The next correction exposes only the stopped DSP's IRQ
control surface, with tests for initial state, masks, acknowledgement, reset/release refusal and
width. The code-guard pointer offsets were also made explicit before forming pointers; existing
cache-guard tests remain green. There is still no game-driven complete GX command claimed here.

Run `216eb34b0d4641eca25751a8673659a5` at `41ac0b1b55b3a44f09f0af557989f5cf3260ec36`
passed stopped DSP IRQ control and stopped at the following AI-control word read during IRQ
masking: `unsupported MMIO read address=CC006C00 pc=80304810`. It retained 116,417 cycles and
788 edges, terminal PC `8030464C`, SP `8040CF58`, zero FIFO bytes and frontier FAIL. Report
SHA-256 is `71de57662a3c2633cab930659059384450540a39cc21030f513e1186cc36cf09`; both
30-second phases stopped/reaped, process 124/INCOMPLETE. The next correction wires the pinned
stopped AI control only, with tests for initial clock, masks, acknowledgement, counter-reset,
stream/rate refusal and width. No real GX success is inferred from these register operations.

## Observed SI blocker and delivery

Run `193647366f324f4195192301624effc2` at `e2d2f129f4503367bcda4782b63eaa6239d701b4`
passed the AI mask and advanced to 119,998 guest cycles, 968 edges, terminal PC `80304630`,
SP `8040CEA8`. It stopped on `SI transfer unsupported (no fabricated controller)` at MMIO
`CC006434`, instruction PC `8030C1C0` (`stw r0,0(r6)`), with zero FIFO bytes and frontier FAIL.
Report SHA-256 is `7237245796a0c96ef66daaaa0a049aa11cf4f53aa92a6bf3be00839408c9575e`.
Both 30-second Qt phases stopped/reaped, process 124/INCOMPLETE. The source-bound snapshot is
`artifacts-5a5ed6016e2145a49dc6666783d18579/`; its ELF/VELF/SELF/SFO/VPK and every earlier
negative attempt remain private and retained.

This is a concrete limitation of the pinned SI model, not a long-running guest or FP defect.
`GXRuntime/src/si.c:104-109` immediately clears TSTART and sets TCINT without a peripheral
transaction, transfer callback or elapsed-time timeout. `GXRuntime/include/gxruntime/si.h`
offers register/poll operations only. Forwarding the requested start would claim a completion
without a controller response; writing an invented controller identity or skipping the guest
transfer would invalidate the real-startup evidence. WW05 therefore refuses it before the
runtime changes the register. The final diagnostic records the attempted write's width/value
while preserving this refusal; tests check that no TCINT is latched and no width mismatch or
failed service can certify PASS. Historical eight-record reports remain valid.

Next options require a separately authorized runtime change and new accepted pin: add a SI
transfer callback and guest-time scheduling to RecompCore, with an explicit timed no-response
result for an unplugged channel, or connect a real controller backend through that contract.
Then wire the host seam and rerun from a new private profile. This task keeps both runtime pins
pristine, does not act on another repository, and does not duplicate the SI runtime in the host.
No genuine complete GX command, rendered scene or physical Vita feasibility is established.

The final source-bound build/run/check receipts are retained under `build-f5/ww05/` and named
by their own SHA/UUID, not retroactively attributed to this earlier attempt. Final offline review
must include the added diagnostic fields in `sizeof(WwHost)` and the existing text snapshot
inside the 96 MiB heap. Unknown residency, pool peak and native stack high-water remain unknown.

## Empty SI bus continuation

The final predecessor run `80767c8a43e746e390474e0d48aeb05d` at `3cb8942` retained the
actual transfer write: `CC006434`, PC `8030C1C0`, width 4, value `C0010301`. It stopped at
119,998 cycles and 968 edges with no FIFO bytes. This record supersedes no earlier evidence.

The coordinator subsequently selected the empty SI bus: all four ports have no device.
The host now reuses the pinned SI register model for TCINT, masks, lengths, channel selection
and acknowledgement, adding the missing COMERR and selected-channel NOREP bits on transfer.
The receive buffer and input registers are not populated with a controller identity or pad data.
This follows the local pinned Dolphin reference (`HW/SI/SI.cpp:146-194` and
`SI_DeviceNull.cpp:13-20`), including its synchronous absent-device completion. It is not a
cycle-accurate serial timeout model. Both upstream pins and generated sources remain untouched.

`logs/empty-si-red.log` reproduces the refused transfers for all four channels and acknowledgement.
`logs/empty-si-green.log` passes all 78 host tests on Linux AArch64, including unchanged receive
bytes, selected-channel NOREP, COMERR/TCINT, PI source publication, acknowledgement and width
refusal. These synthetic tests do not establish real startup success; a new source-bound private
build and trial must do that.

Run `9ae86b0671014a4fa3c01f32311dc948` at `81012f996fd40d4d6ea1c780c18a19a4d981fd79`
passed the SI transfer and stopped at the host's immediate IRQ refusal:
`external interrupt delivery unsupported before GX address=00010008 pc=8030C1C0`.
The report SHA-256 is `71b7c7bc8510f1bbe160a62651614ba245eacb7b53ac133ef662dcb95484c262`.
Both 60-second processes stopped/reaped with 124/INCOMPLETE; frontier FAIL remains separate.
The unchanged 119,998 cycles and 968 edges are not a measurement ceiling or evidence of a wait
loop: both this refusal and the prior SI refusal occur in the same MMIO write, before its next
block. The cycle domain accumulates a 64-bit absolute count; its 256-cycle dispatch budget is
renewed every turn and does not cap the total. No budget increase is needed for those counters.

The next host seam leaves completed SI/DI/EXI PI causes pending, then samples the PI mask and
MSR[EE] at a guest edge. It uses the pinned `ppc_take_exception` helper for vector `00000500`,
SRR0/SRR1 and exception-mode MSR, consumes only that handled external-exception marker, and
yields the current dispatch without ending the run. A prior guest fault is never replaced.
The guest must execute its real handler and acknowledge the peripheral; an unacknowledged
level reasserts after `rfi`. Interrupt sampling is block-boundary, not instruction-cycle exact.

`logs/external-irq-red.log` reproduces the refusal/missing delivery. The green host suite has
85 tests, including SI/DI delivery, PI masking, EE deferral/re-enable, acknowledgement/`rfi`,
pending-level recurrence, guest-fault preservation, cycle flushing, and synthetic main-loop
continuation for both outer and inner dispatch boundaries. Synthetic vector handling in the
I/O fixture is not proof that the real relocated OS vector is statically dispatchable.

Run `cbb8f000252045d694fed539fb0317cb` at `03b1c73a69fae7a40a3c447fc03e7660ffcae94f`
advanced to 121,420 cycles and 1,106 edges, then stopped on `dcbi 0,r3`:
`unsupported instruction fallback address=7C001BAC pc=803030C8`.
Report SHA-256 is `470e816571042a0bdef3ac79111e5ef6ff018edb6c6c033fe5b548009a36f8ad`.
Both 60-second phases stopped/reaped; process 124/INCOMPLETE and frontier FAIL remain distinct.
The extra guest progress proves that the immediate IRQ refusal was removed; this report alone
does not count actual external-vector entries or establish that a handler ran.

The host fallback now decodes DCBI (31/470) and forwards it through the pinned runtime's
supervisor-privilege gate. DCBI preserves coherent guest memory and executes a memory fence;
DCBF/DCBST keep their existing dirty observer without changing guest bytes. No data-cache lines
exist in this host. Guarded ICBI remains unchanged. `logs/data-cache-red.log` reproduces missing
DCBI decoding and privilege handling; `logs/data-cache-green.log` passes 87 host tests, including
all three data-cache operations, zero-base/indexed addressing and preserved memory bytes.

The unchanged pinned C emitter already implements DCBZ (31/1014) as eight zero-word writes to
the aligned 32-byte block (`emitter.c:1644-1646`; private generated DOL PC `803031AC`), not a
no-op. DCBZ_L uses `ppc_dcbz_l`. SYNC/ISYNC/EIEIO/TLBSYNC already emit `ppc_memory_fence()`
(`emitter.c:1891-1896`). These neighbors were inspected, not reimplemented or newly certified.
Both upstream pins and the verified composite digest remain unchanged.

Run `53d461435d1b4d3fb9a6a835e0692a98` at `684665775603d588746c80d5a4565336a8b98e22`
passed DCBI and reached 121,788 cycles and 1,134 edges before
`EXI transfer unsupported address=00000002 pc=80309E1C`.
Report SHA-256 is `bd489caeccb30f4f9c1fce46a341c07baed787aad992281ff75d0c36d570a2a4`.
Both 60-second phases stopped/reaped; frontier FAIL, process 124/INCOMPLETE.

The EXI error address is a chip-select mask, not the channel or peripheral identity. Before
responding, the host now logs the actual channel, chip-select mask, frequency code, direction,
length, DMA flag/address and immediate word to the private `ww05.log`. This retains the refusal
and pending TSTART without fabricating TCINT or data. The diagnostic red/green loop passes all
88 host tests, including a synthetic channel-0/device-1 8 MHz transaction. A real run must still
establish which device and command the game actually uses; the synthetic command is not evidence.

Run `a208442996254b739ef051af369f9b9e` at `d36694c1622c75074d8256b399b69afffcc9c71d`
retained the actual transaction: channel 0, chip-select mask 2 (device 1, IPL), frequency code 3
(8 MHz), direction 1 (write), length 4, immediate `20000100`, PC `80309E1C`. The game writes
the command for a subsequent SRAM read; this first bus transaction is not itself a data read.
The report SHA-256 is `57edc44909eaf3db9204a2101ae9c45fef9b8501da98e64e237989b8e9a0e5f7`.
It retained 121,788 cycles, 1,134 edges and the EXI refusal; both 60-second phases stopped/reaped.

The coordinator selected the existing `runtime/host/src/ipl_sram.c` implementation and its
unchanged `default` image. WW05 now adapts its immediate/DMA SRAM transfers to the pinned EXI
callback. That component owns command/cursor/data behavior; the pinned runtime still owns
TSTART, TCINT, masks and PI publication. Deselect resets the shared command. DMA is restricted
to mapped MEM1 and converted from physical to cached guest addresses. Direction, SRAM length
and address are checked before the shared component can silently truncate or return zero.
Other IPL regions (RTC, UART, fonts/ROM) remain explicit stops, not invented data.

The selected 64-byte image is the existing Dolphin template: English, stereo, setup done,
checksums `002C/FFD0`, and its placeholder flash-ID records. It is not a dump from a physical
console or evidence of inserted memory cards; channel EXT remains clear. No persistence path
is set, so game writes affect only this private run and never a player file. The shared component,
upstream pins and generated bytes are unchanged.

`logs/exi-sram-red.log` reproduces the missing command/read/write seam. The first green attempt
retained a failed DMA mapping test; `logs/exi-sram-green-mapped.log` then passes all 95 tests,
including exact shared defaults, immediate/DMA reads, guarded byte boundaries, TCINT/PI,
deselect/reselect, private writes, and refusal of unmapped DMA/out-of-range SRAM/other IPL regions.
This is host contract evidence, not a claim of real GX execution.

Run `d520d6ac10e64865afe8931a5dd67dcb` at `995406d36208420536da64c763b0865e1c7d7e52`
completed the SRAM command and the 64-byte DMA to physical `003F0260`, then reached 143,810
cycles and 1,289 edges. It stopped at PPC vector `00000C00` with initial exception bit `8`
(SYSTEM_CALL). The host subsequently ORed PROGRAM (`1`) when refusing it; reported `9` is not
two independently observed guest faults. Report SHA-256 is
`48fb6d2f0f190e1d9c7a83f40b3c47d1762bc905961c5fea48ae123b4ab68f3f`.
Both 60-second phases stopped/reaped; process 124/INCOMPLETE and frontier FAIL remain separate.

`8` is the pinned CPU's exception-mask constant, not an edge-service number. The C emitter
raises it through `ppc_system_call_exception`, saving CIA+4 in SRR0. The DOL contains only three
SC instructions (`80301504`, `80303100`, `80303130`); two belong to data-cache flush/store
paths and are ordinary OS synchronization, not panic by definition. The prior report lacks
SRR0 and cannot establish which SC actually executed. No exception has yet been skipped.

The misleading generic reason mentioning the accepted FP defect is now replaced with
`unhandled guest exception`. The private log records exception bits, SRR0/SRR1, MSR, LR,
selected GPRs, FPSCR/program cause, and 64 bytes at a RAM-backed exception vector. The SYSTEM_CALL
bit and vector are not evidence of the Vita3K FP defect; attributing earlier control flow to FP
without the saved CIA would also be unwarranted. The diagnostic checkpoint does not modify FP.
The corrected synthetic red/green fixture checks this context and passes all 96 host tests;
the earlier native-log-file fixture mistake and its failed attempts remain recorded.

Run `706bfad5b01b4074b1046c3dcaa9a8c0` at `d41a7fa48b1ec5fc1cab881fe44c2538a1ffb0d4`
identified the SC at `80303100` (DCFlushRange), with SRR0 `80303104`, SRR1 `00000002`,
exception-mode MSR `00000000`, LR `80302E48`, r3 `81000080`, r4 `00000004`, and no program
cause. The installed vector's first 28 bytes exactly match DOL text `803077D0..803077EC`.
Its seven instructions read HID0 into r9, set bit 8 in r10, write HID0, execute ISYNC/SYNC,
restore HID0 from r9, and RFI. Bit 8 here is HID0's control bit, not a syscall cause/service.
This is an unconditional cache-synchronization syscall, not a panic or an FP instruction.
Report SHA-256 is `82809264acf1c133e8a209368632a6a2eada96a70462f6604deb4123ba173910`;
frontier FAIL and both 60-second phases stopped/reaped with 124/INCOMPLETE.

The runtime already took the architectural exception and saved SRR0/SRR1 correctly. Re-taking
it would overwrite the saved MSR with exception-mode MSR. The host now consumes only the
SYSTEM_CALL marker at the low syscall vector, after verifying all 28 copied bytes against its
original DOL text snapshot, verifying that the source is still unchanged, and checking that
the entire original handler has compiled coverage. It then executes that original compiled
handler, preserving SRR0/SRR1 for its real RFI. This is a guarded relocated-code handoff, not
an immediate host RFI, skipped SC, interpreted substitute, or change to generated code/pins.
The handler has no PC-relative instructions, so running its byte-identical original preserves
its behavior; logs name both vector and compiled PC. Other vectors, changed copies/source,
missing coverage and unrelated guest faults remain explicit stops. A generic remap of arbitrary
exception handlers would not be safe without checking their relocation semantics.

`logs/copied-syscall-red.log` reproduces rejection of an intact copied handler.
`logs/copied-syscall-green.log` passes all 99 host tests, including synthetic handler dispatch
before RFI, preservation of architectural SRR0/SRR1/MSR and refusal of changed vector/source.
The fixture's compiled-handler stub is synthetic; only a new private game run can prove the
real seven-instruction handler and subsequent GX frontier execute.

Run `1ccc7aeaf2f8436cb1fc7bbfdf391ddf` at `38ad623918513f099fc1086ac3799ba44192e4b9`
installed verified bytes but produced no closed report within the 60-second guest deadline.
Both processes were stopped/reaped; `ww05.partial` and `ww05.log` were empty, and the launcher
recorded status 2, frontier UNAVAILABLE, process 124/INCOMPLETE. This is not proof that the
syscall handoff ran or that the guest progressed. The runtime log reaches opening the output
files without a reported crash. This attempt and all files remain retained.

The coordinator's original authorization allows 120–180-second deeper-startup trials. The
WW05 plan now accepts finite positive deadlines up to 180 seconds, still using the unchanged
owned TERM/KILL/reap runner and fresh isolated profiles. The red/green plan tests verify the
new accepted boundary and reject zero, negative, excessive, infinite and NaN deadlines;
all 101 host tests pass. A new bounded private attempt, not an unbounded wait, follows.

Run `76d1f3f3ae2443c29a4445129ef3e79d` at `0f926a1f6e9906be015529bd7f24086056bd600b`
also produced no closed report: both 180-second phases stopped/reaped, status 2, frontier
UNAVAILABLE, empty log/partial. Its process log confirms the DOL was opened and closed, so
empty buffered files alone cannot establish whether the native host or guest was looping.
No progress or syscall success is inferred from this attempt.

WW05 now makes its own stderr unbuffered, flushes the report header/allocation prefix before
execution, and logs initialize/load/loaded stages plus sampled actual PC/MSR/SRR0, turn count,
edges and absolute cycles. Progress logs occur after dispatch at 65,536-cycle intervals (and
65,536-turn intervals for non-charging loops); they do not limit guest execution or fabricate
time. A partial report remains non-certifying. Logging failures propagate to a negative exit,
and the shared platform fixture and other profiles remain untouched.
`logs/private-progress-{red,green}.log` verifies visible progress and refusal of stderr-buffer
configuration failure; all 103 host tests pass before the next private diagnosis trial.

Run `68197f98888b4193bb159271cc40b536` at `b220c81ea925b6caef336fbd5197d32a3d9cad30`
logged successful load, the real SRAM transaction and progress through turn 494, 1,231 edges,
131,751 cycles, PC `80309598`, MSR `00000002`. No later sampled return or exception-resume
line appeared before the 180-second guest timeout. This narrows the problem to execution after
load but does not establish the final instruction or that a syscall handoff was reached.
The partial file remained empty despite logged SceIoWrite calls; only the immediate private
stderr log is available. Frontier UNAVAILABLE and process 124/INCOMPLETE remain distinct.
Result SHA-256: `f677a8fc2faa1412deebca1d6c0e23dd3b843a3354879c75fcaf9b5a4bfb2a2d`;
log SHA-256: `9aa5fabb249b96c38c32e96ea15c341b64c1b81231ea7151ba5bc9391dbb6fc1`.

The next diagnostic checkpoint logs entry/result of the copied-handler guard and pairs native
dispatch entry/return in the observed 131,072–196,607-cycle window, only during the first 1,024
turns. This bounds diagnostic output, not guest execution. No instructions, cycle accounting,
exceptions, pins or emulator behavior change. `logs/handoff-guard-red.log` and
`logs/handoff-window-{red,green}.log` retain the reproduced missing diagnostics and all 104
passing host tests. The window fixture charges synthetic time explicitly and cannot certify
game behavior. A fresh source-bound private run is required to locate the missing return.

Run `0d838dc7bfd447799464c6179e3a4d3c` at `034ce5736feeb0e611666378ac0b24ef61809094`
reached turn 541, 143,810 cycles, SYSTEM_CALL vector `00000C00`, SRR0 `80303104`, SRR1
`00000002`. The copied-handler guard returned matched=1. Turn 542 entered the compiled
handler `803077D0` and returned at `803077D4`, exception=0, with unchanged absolute cycles.
This proves the real `mfhid0` fallback ran and advanced PC, not that the rest of the handler or
RFI ran. No next dispatch-entry line appeared. Frontier is still UNAVAILABLE and both owned
processes stopped/reaped at 180 seconds with 124/INCOMPLETE. Result SHA-256 is
`3c795f64007bb60f9deff1a62c3389bc1433223e06720ecd00b5e651fd023e47`; log SHA-256 is
`791613da69a429a67d2ad2c579939c37282a72305345bd5634ae8922e2d1e7b4`.

Native disassembly of this immutable artifact is retained in
`logs/handoff-window-main-disassembly.log`; no invalid native branch or source-level loop
has been established. The next checkpoint traces returned/checked/edge-enter/edge-return only
while PC is inside the verified handler. Its synthetic main fixture now characterizes a real
SPR-fallback callback yielding at the first instruction without charged cycles, followed by a
separate synthetic RFI dispatch. The native fixture continues correctly; it does not reproduce
the emulator hang or execute the full real handler. The red loop observes missing boundary
diagnostics, not a native zero-cycle semantic failure. All 105 host tests pass in
`logs/handler-boundary-green.log`. No guest/runtime/generator/emulator semantics changed.

Run `661b56e2c16d451caa20f68665552a67` at `edf1b0dbffa592fd5a28f4323ec026fb85eefcf0`
again executed the handler's first fallback. Its returned/checked/edge-enter/edge-return lines
all show PC `803077D4`, 143,810 cycles, MSR=0, exception=0 and HID0 `0000CA00`. The outer
edge returned; it did not block in the interrupt refresh or interception lookup. No following
dispatch entry appeared. Both phases timed out/stopped/reaped, frontier UNAVAILABLE,
process 124/INCOMPLETE. Result SHA-256 is
`ed4cdddcc3373970d01f297b22d6be940a2947449f0977647588a8cb87aa8101`; log SHA-256 is
`509b63fa6df9a8189aa8d481cf946fff14882ed570c825367ba556aadeecb5af`.

The next checkpoint adds turn-enter/turn-begun/dispatch-ready to the same narrow handler
diagnostics, with actual downcount and budget. This brackets the reused cycle-domain calls
without changing their source, budget or time accounting. `logs/handler-budget-{red,green}.log`
observes missing diagnostics then passes all 105 host tests, including the native zero-cycle
fallback continuation. The real RFI and complete GX frontier remain unproven.

Run `eaa4fe4d392140d8a0375caf0ced3123` at `4d1f53500d909298b28b11976e84da73f5230544`
logged turn-begun/dispatch-ready at `803077D4`, 143,810 cycles, downcount 0 and budget 256.
Cycle preparation returned correctly. No next dispatch-entry line or closed report appeared;
both owned phases stopped/reaped at 180 seconds, frontier UNAVAILABLE, process 124/INCOMPLETE.
Result SHA-256: `111b80013717e896b8215190f0bf778ab3bac572052323589f44c22ddbd78219`;
log SHA-256: `8c08b4f8d69da60640511ab21af3a4c2ca344ae1e382b12eca3daddd289eed0b`.

A diagnostic retry `f6aceca225014c859c9269b7cc03cd52` used the same immutable source/package
and fresh isolation. A private observer briefly stopped/detached only verified native threads
of its own launcher descendant. Direct-child and same-bwrap-isolation probes passed; the
initial probe mistakenly matched the wrapper's argv as a second child and was retained before
correcting executable selection. The WW worker was sampled twice in anonymous generated code,
with native frames through `DynarmicCPU::run`, `ThreadState::run_loop` and `thread_function`.
This establishes active native JIT execution, not a particular PPC idle loop, DEC wait, saved
ARM PC, complete handler/RFI or an emulator defect. A later extra capture found the process
already reaped and acquired nothing. No emulator/backend/source mutation occurred. The private
observer, argv/source/package identities, register/frame/map snapshots and self-check logs are
retained. This intrusive diagnostic run is not a performance measurement.
Its terminal result remains UNAVAILABLE/124/INCOMPLETE; result SHA-256 is
`2802c65dc8d6558f6ff55d75583f937649528b53e1b3b91a38ab12ce3ee05189`, and native capture SHA-256
is `877b8c6b6dd8eb3b50768bcb94c6ed3e3bc3fff6cb1ea99c781a284785005a72`.

## Decrementer continuation

The coordinator requested DEC support and testing the idle/timer hypothesis. That hypothesis
is not yet confirmed: the seven-instruction syscall handler has no DEC read or idle instruction,
`803077D4` is ORI, and its saved/exception MSR has EE clear. A latched timer cannot interrupt
that handler until EE is re-enabled. The missing timer delivery is nevertheless a real host
limitation: expiration previously set a host failure rather than taking vector `00000900`.

The chosen clock remains the existing **12 charged guest core cycles per TB/DEC tick**, not a
new frequency or host-wall-clock shortcut. The pinned reference's
`Source/Core/Core/HW/SystemTimers.h:19-42` records core 486 MHz, bus 162 MHz, TB/DEC bus/4 and
`TIMER_RATIO=12`; `GXRuntime/src/boot.c:16-22,71-72` installs those same core/bus frequencies.
`PowerPC/PowerPC.cpp:194-195` supplies the deterministic reference DEC seed `FFFFFFFF`.
These are local implementation/reference-clock evidence, not a newly measured physical clock.
No network/manual download occurred, and the reference's suggested ratio-8 speed hack is not used.

DEC now runs from that seed on the same fractional clock as TB, wrapping modulo 2^32 even
after expiration. Crossing zero to `FFFFFFFF` latches a pending exception, not reaching zero;
large elapsed intervals retain the crossing. A positive-to-negative software write also latches
it, as in the pinned `Interpreter_SystemRegisters.cpp:438-445`. A positive rewrite is not an
acknowledgement and cannot discard an existing pending event. Only exception delivery consumes
the latch. Native SPR privilege gates and actual `mfspr`/`mtspr` fallback instructions are reused.

The reused cycle-domain deadline bounds dispatch at DEC expiry. Completed guest edges flush
their already executed charges once; SPR/MMIO observations still exclude their unexecuted
instruction suffix. Credited cycles remain charged against the existing 256-cycle dispatch
quantum, so edge observations do not perpetually reset its allowance. Zero-charge yields do
not invent ticks or fast-forward a presumed idle loop. No POW/nap/wall-clock substitute is added.

External PI interrupts retain priority. Pending DEC is delivered only with MSR[EE] set and no
existing guest exception, through the pinned `ppc_take_exception` helper at vector `00000900`.
SRR0 is the actual interrupted boundary; SRR1 and exception-mode MSR follow the same OEA helper
as external interrupts. The local consumed DEC marker is `80`, distinct from the pinned CPU's
other fault masks. There is no DEC bit in PI or a separate invented MSR enable: the reference's
`PowerPC_Exceptions.cpp:137-186` uses EE, vectors and external-before-DEC priority directly.
The guest must execute its installed vector/handler and real RFI; unsupported vector dispatch
will still fail explicitly. The desktop context-writing/handler shortcut is not adopted.

`[edge-progress]` now samples actual edge PC/cycles/TB/DEC/pending/EE from inside the composite
dispatch, not just after its return. Narrow handler logs include the same timer state. This
can distinguish an active inner-dispatch poll from the last outer-turn snapshot; neither native
JIT activity nor a stale outer log is treated as proof that guest cycles stopped.

The DEC red loop reproduced the missing seed, pending latch, deadline and delivery. The first
green attempt incorrectly expected zero PI cause; the pinned PI reset cause is instead retained,
and the corrected test checks that DEC leaves it unchanged. All failed logs remain private.
`logs/decrementer-green-reference-pi.log` passes 121 tests; sampled edge-state red/green adds
coverage, and `logs/decrementer-{edge-charge,turn-credit}-red.log` exposes stale-suffix and
renewed-allowance errors in the first boundary implementation. The corrected final
`logs/decrementer-boundaries-green.log` passes all **125 tests**, including reset/free running,
fractional ticks, rollover, software-triggering writes, latch preservation, EE deferral,
external priority, prior-fault preservation, OEA state/RFI, unchanged PI state, privilege,
zero-charge behavior, edge credit, deadline/quantum and synthetic main-loop continuation.
The synthetic vector/RFI fixture is characterization, not real OS-handler evidence. A fresh
source-bound game trial is still required for the timer hypothesis and GX acceptance.

Run `2dcc1efe30f0486ea43d07321461f976` tested the clean DEC checkpoint
`0dffdab549fa3cb2ce81459fb08207b28f9f6a4e`, VPK SHA-256
`fc59a2f54979ec08072d732a2de3ea39acc53f6ffa64598ff554c82ae0c71bb4`.
The observed frontier is still UNAVAILABLE, process 124/INCOMPLETE; both private phases were
TERM/reaped at 180 seconds. There is no closed report. The last persisted handler line is
dispatch-ready at `803077D4`, cycles 143,814, TB 11,984, DEC `FFFFD12F`, pending 0, EE 0,
downcount 0, budget 256. Thus adding DEC did not remove this observed stall, and no timer
expiration is demonstrated at this boundary. Result SHA-256:
`9f2e3812c86caaadec872f9ddded4df810bb0da76f25b0189b1688265ce9abd2`;
guest-log SHA-256: `a163fb2a027f233102cd9ba5a731f6295f260df60aebca361db60ddb8d3c7426`.
At that exact SHA, 125 host tests, 177 unchanged ARMv7 tests, audit and attribution passed;
the checkpoint logs are retained. Compilation and these tests do not prove real timer-handler
or RFI execution. Physical Vita and Windows CI remain untested.

## Closed dispatch discriminant

The coordinator's independent analysis prioritizes the first dispatch from `803077D4`, its
`mthid0` fallback and distinguishing dispatch execution from the missing formatted log write.
It correctly excludes a DEC read/OS idle instruction in the copied syscall handler. Absence
of further persisted log lines alone does not establish a native hang or its exact location.

Only the WW05 host is instrumented; the generated module, dispatcher, pins and emulator remain
unchanged. The first dispatch from `803077D4` emits single-byte markers into `ww05.dispatch`:
**L** before the existing dispatch-entry log, **E** immediately before `module->dispatch`,
**R** immediately after its return, before cycle accounting or return logs. Every marker is
flushed, checked and its file closed; every error prevents publication. A preexisting journal
is refused. This independent journal keeps the strict eight-record GX report unchanged, and
closing addresses the known empty-partial-file observation without asserting that fflush was
durable in the emulator. Only L localizes failure before the call (formatted logging or opening/
writing the next marker); LE establishes entry into the dispatch interval, not a specific JIT
defect; LER proves that call returned. Any more precise claim requires edge/fallback evidence.

The host traces at most the first 16 edge callbacks inside this dispatch, before their clock/
device work, and HID0 fallback entry/return while that narrow trace is armed. The diagnostic
does not skip an instruction or change guest registers, counters, interrupts or dispatch budgets.
The synthetic yielding-handler fixture now executes the actual ORI value and `mthid0` fallback
before its synthetic edge/RFI. This is still game-free characterization, not a real handler run.
`logs/dispatch-discriminant-red.log` reproduces missing markers/edge/fallback traces and ignored
diagnostic-open failure; `logs/dispatch-discriminant-regressions.log` passes 132 host tests,
including marker open/write/flush/close failures, stale-journal refusal and L-only formatted-log
failure. A fresh isolated real trial will decide what the discriminant establishes. No DSP
response or speculative DSP register service has been added ahead of an observed blocker.

Run `f74e101761474c09a88fa45152b6c985` tested `53fced512602f590125b49c98bb5028c40877c3c`,
VPK SHA-256 `a52bf7860a352a2298daea2f7070f26191c91151eb617a5f386dfd7fd5466221`.
The closed journal contains **LE**, no R. Crucially, the guest trace records `mthid0` at
`803077D8` entering with `CA08` and returning at `803077DC`, then the edge there; the restored
`mthid0` enters at `803077E4` with `CA00` and returns at `803077E8`, then that edge. Thus both
HID0 writes and both intervening barriers finish. It is not a hang in the initial ORI/HID0
block. The last formatted outer snapshot did not identify the actual running guest PC.
The static game archive before/after the host-only rebuild is byte-identical, SHA-256
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
At this exact SHA 132 host tests, 177 ARMv7 tests, native CTest 3/1/5, audit and attribution
passed on Linux AArch64. No network/push/CI or physical Vita test occurred.
Both real phases still TERM/reaped at 180 seconds, no report, UNAVAILABLE/124/INCOMPLETE.
Result SHA-256: `9308962500775c20b338bfda3f17236dd00f9fcfecc71b3a99fa2f840b97adad`;
guest-log SHA-256: `b367614520c221b31f0f89e8773a551238d04f2be76b4c92aeb565c4f4daf49b`;
closed-journal SHA-256: `4c85226bf4430ba6d64181ba5423e872465d0b4632eddc92d7656b3485921545`.

The suggested power/invalidation explanation does not match the host or the compiled ELF:
HID0 writes store and return; the L2 path is a distinct SPR 1017 branch. The local pinned
`PPCArch.h:149-165` names DOZE/NAP/SLEEP as `00800000/00400000/00200000`, whereas `8` is ABE.
`CA08` enables none of those sleep modes. `ppc_memory_fence` disassembles as `dmb ish; bx lr`.
The RFI helper likewise has only privilege/state operations and a return, no wait loop.
Private static disassemblies are retained as `handler-barrier-static-0dffdab.log`,
`rfi-static-53fced5.log` and `compiled-handler-53fced5.log`; no binary change was made.

## Actual inner loop and exhausted-quantum return

A same-SHA native inspection retry `ec644ae77e2140fabf166acf0caf0d76` preserves the same
package and LE/handler-edge trace. Its owned observer paused/detached only the verified
unique WW worker three times. Native A32 state layout is inferred from this pinned binary's
`Jit::Run` (Impl+180 state argument), `Jit::Regs` (Impl+198 registers) and the observed x27/x28
difference `624` (Impl+7A4 halt flag). The relation and readable mappings were checked before
capturing the bounded saved state/native code. Saved ARM registers can lag live JIT registers;
they are not mislabeled as PowerPC registers. No emulator, backend or guest memory was written.

Round 0's saved ARM PC `94D4231E` symbolizes against the exact package's ELF as
`loop_80302ED4+74`. Other captures are in `mmio_read` (`9564AF5C`, `9564AF10`), with actual
`CC00500A` arguments and PPC-address values in their saved ARM operands. The generated source
for `loop_80302ED4` reads a halfword through r31, masks bit `20` and loops while it is clear.
This is a guest DSP-status poll reached beyond the syscall handler, not a native HID0 write
wait or evidence that guest execution remained at D4. The next persisted edge need not appear
because execution is inside a generated loop that has not returned to the chassis.
Result remains UNAVAILABLE/124/INCOMPLETE; result SHA-256:
`08acda113f5d6e513f616eeb382e1c3a2a3af8540aea72958c649cea9aa8acdf`.
Native capture SHA-256: `530e32c5443fb9d62b0c36afa8ce456c4584510917915e293aa5951df17f0e4c`.
The private observer/captures are retained; this intrusive trial is not a performance result.

The source/compiled poll precharges **1** for its load. MMIO observation credits that charge,
zeros downcount, and refresh rebudgets. Once dispatch_cycles reaches the cap, the initial
remaining-quantum callback still supplies a minimum allowance of **1**. The next precharged
load is again allowed and observed; its prepaid mask/branch do not restore a spent charge.
Consequently the generated budget guard keeps seeing 0 > -1 and this single generated loop
need never return. The chassis's nine-zero-successor rule cannot bound a call still executing
inside that loop. This is a host budgeting seam defect, not missing DEC or a sleep instruction.

The WW05-only fix exposes **zero remaining cycle allowance** once already credited dispatch
cycles reach the cap, after in-block observations, device-refresh rebudgeting and SPR writes.
The emitted guard then yields at its next budget boundary; no fake negative downcount,
extra cycle, counter fast-forward, guest instruction skip or DSP completion is introduced.
The next ordinary begin/prepare turn restores its positive quantum. The shared cycle-domain,
generated code, module archive and pinned/emulator sources remain untouched.

`logs/mmio-quantum-red.log` reproduces a 257th one-cycle read and a renewed positive budget
after 256 already credited cycles. The first correction still failed because device refresh
overwrote it; `logs/mmio-quantum-green.log` retains that failure. Enforcement after refresh
passes `logs/mmio-quantum-refresh-green.log` (134 tests). The final game-free fixture uses the
real chassis header around the same one-cycle poll, yields after exactly 256 credited cycles,
then resumes a fresh dispatch without crediting those cycles twice. All 135 host tests pass
in `logs/mmio-quantum-chassis-green.log`. These tests establish host return/accounting only;
a new real trial must reveal the DSP operation/error and cannot certify a synthetic frontier.

Run `2c4b1c3c7454432f8134420a1831d859` at `481faf9a6aa8c89f73f60f4aa972b98ee2533c06`
now closes a valid **FAIL** report instead of leaving the frontier unavailable. It logs the real
post-RFI edge `80303104`, then returns at `80302ED4`, cycles 144,070, edges 1,311. The first
error is `unsupported MMIO write address=CC005012 pc=80302E54`. Thus the host-quantum fix
exposes the actual earlier failure; it does not complete the DSP operation or certify GX.
The report releases all owned allocations (`live=0`). Both emulator phases still require the
owned 180-second TERM/reap: guest FAIL and process 124/INCOMPLETE are distinct.
VPK SHA-256: `61bf527aa5aa0d8f0b2e505a7f59a70512b9cc7e092cfbe61d54fc4dc53ae411`;
report SHA-256: `178125b1608bef14d0419eff9a26df7e3d6f463205e1215c0546779c58953a9e`.
The static archive remains byte-identical. At this SHA 135 host tests, 177 ARMv7 tests,
audit and attribution passed on Linux AArch64; hardware/Windows/renderer remain untested.

The coordinator also requested longer trials and lower per-pass log volume. WW05's opt-in
maximum is now **600 seconds per phase**, default still 60, with unchanged owned TERM/KILL/
reap and source/private-profile verification. This does not alter the unchanged campaign's
limits. Handler logs/handoff details are sampled at the first verified syscall resume and
every thousandth, recording the actual SRR0 and cumulative resume count. Ordinary dispatch
logs cover the first eight turns and each thousandth; the first D4 closed L/E/R diagnostic
and its bounded edge/fallback trace remain. No logging counter substitutes for guest cycles.
`logs/long-trial-sampling-red.log` reproduced refusal of 600 seconds and 1,001 unsampled resume
records. The green loop passes 137 host tests, including 1,001 game-free handoffs yielding
only resume-1/resume-1000 samples with the real saved resume address and unchanged report.

The suggested interpretation that `f74e1017` wrote until its final second was checked directly:
its guest `process.log` has 138 sceIoWrite records and its last timestamp is `12:55:44.352`,
while launch begins near `12:55:42` and process duration is 180.001 seconds. The truncated
cache log's alternating 58/78-byte writes end earlier (`12:55:44.344`). These are not evidence
of writes continuing throughout the timeout or repeated 180-second cache-handler passes.
Native DSP-loop evidence and the new closed STOP supersede that volume hypothesis.

The coordinator explicitly permits deterministic documented initialization-mail responses
without emulating the DSP core, only at the corresponding identified-ucode transitions.
Local pinned `DSPHLE/UCodes/INIT.cpp:19-22` supplies `80544348`, and `ROM.cpp:31-34` supplies
`8071FEED`. Neither response has been implemented merely to satisfy a waiting guest. The
next implementation begins with the observed AR_INFO configuration register, real DMA and
explicit support boundaries; undocumented behavior or unknown ucode remains a refusal.

The first DSP-interface increment only implements the three configuration halfwords above in
`vita/host/dsp_interface.c`, leaving all transfer/control/mailbox behavior unchanged. Source:
RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`, `Source/Core/Core/HW/DSP.cpp:147-149,
167-168,181-210`. Unsupported widths and writes to AR_MODE fail without changing the register.
Unknown MMIO writes now retain width/value as well as address/PC in the existing strict STOP
format. `logs/aram-config-red.log` records five assertion failures before implementation;
`logs/aram-config-green.log` records 142 passing host tests on Linux AArch64. The audit and
branch attribution checks pass. No DMA or ucode response is claimed from these synthetic tests.
The unchanged ARMv7 suite passes 177 tests in `logs/aram-config-armv7-corrected.log`;
the earlier `logs/aram-config-armv7.log` used the scripts directory and discovered zero tests,
so it is retained as a failed command, not credited as validation.

## Guarded DSP reset and ROM mailbox increment

The 600-second run `65611f938cf442419e2096157797baa6` at
`4785ea1bd4bec5b6533a0407ad36647c0b42dc1e` closes **FAIL** at the next actual operation:
`DSP execution/reset requires a real DSP backend address=CC00500A pc=80302E6C`.
The state remains 1,311 edges and 144,070 credited cycles; no GX bytes exist. Both owned
emulator phases timeout/reap at 600 seconds: FAIL is a guest verdict, not process completion.
The static archive remains `9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
The sampled guest log is only 5,348 bytes and its closed discriminant is `LER`.
VPK SHA-256: `dc7483e117d323e32cfe45cb068185751f5b7121cd33c42122b2671998bd6542`;
report: `1f33a8a25fade2a531602c8580bc233f347372765d7c3a26f2fc249af4c13709`;
result: `854ffe6b7383b6cfea1b31484c3d95050c26ae68c1d33475aec329f1400596c3`;
guest log: `b0cd47443b3b34ad6d9fee8d00d1df7d02a65264c78c8160341835ef1c4f1467`;
journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.
At that exact SHA 142 host tests, 177 ARMv7 tests, native runtime CTest 3/3, audit and
attribution passed on Linux AArch64. Physical Vita, Windows and rendering remain untested.

The coordinator explicitly permits a documented, fixed-function init/ROM model, while keeping
audio stopped and refusing all unidentified programs. Before granting reset, the host now
compares **every byte** of the live init image at physical `01000000` with a private 128-byte
reference. The build extracts that reference from exactly one complete DOL section at
`8039F5B0` and checks SHA-256
`5b873ee91f33b0884574b814d7f46d81e283394de91b8b2ea9073ac218dee350`.
Only the private generated `identity/ww05_dsp_init.h` contains those bytes, and each preserved
build keeps it with its hash in the receipt. No game bytes enter Git and no override is offered.
The existing disc/DOL/tree/pin checks are unchanged. Native tests use a clearly synthetic
0-to-127 reference, not the private game image, and cannot certify a real response.

The reference identity is backed by the private static records
`logs/init-ucode-static-4785ea1.json` and `logs/init-ucode-static-completed-4785ea1.json`
(completed proof SHA-256 `a5192d1d63985f062134d96a34bea7801a7f20e0536d7060a37e0df98dbf2b51`).
The actual copied program jumps to its init entry, scans ROM, clears DSP data memory, scans
coefficient memory, waits for the outgoing mailbox, emits the documented init mail and halts.
The classifier's excluded extended CLR was resolved against `DSP/DSPTables.cpp:259`; an
initial lookup that also matched fallback/extension entries is recorded as a failed diagnostic.
This is static source evidence, not a claim that the program executed on hardware.

This increment implements reset, halted CPU/mailbox latches and **only the ROM greeting**:
`8071FEED` from pinned `HW/DSPHLE/UCodes/ROM.cpp:31-34`. Reset/halt selection follows
`HW/DSPHLE/DSPHLE.cpp:195-210` and `DSP/Interpreter/DSPInterpreter.cpp:225-244`; low-word
consumption and high-word busy semantics follow `DSP/DSPHWInterface.cpp:32-72` and
`HW/DSPHLE/MailHandler.cpp:38-68`. The ROM is the fixed on-chip reset mode, never a supplied
or unknown task. No mail is visible while halted, no DSP/ARAM/AI interrupt is created, and
no guest charge, clock rate or reset delay is invented. This models the documented handshake
state transitions, not cycle-exact physical ROM latency. A pending unsupported CPU mail
prevents release rather than being ignored. Later ROM boot-task commands and init transfer
remain explicit stops; **`80544348` is not yet emitted by this implementation**. Dynamic Zelda
audio requires the coordinator's separate backend decision, and AI stays stopped.

`logs/dsp-fixed-reset-red.log` reproduces the rejected known reset and missing failure context;
`logs/dsp-reference-red.log` records the absent reference extractor. The first green attempt
also exposes the configure-only fixture's missing new private header and is retained. The
fixture now supplies synthetic reference data, without weakening production's required input.
`logs/dsp-fixed-queued-mail-red.log` reproduces an unsupported queued mail being allowed to
release; the corrected model refuses it. `logs/dsp-fixed-reset-verified-green.log` passes
155 host tests, including all 128 single-byte mutations, re-verification at release, halted
mailbox consumption, zero-charge reset and ROM-mail/no-interrupt behavior.
`logs/dsp-fixed-reset-armv7.log` passes 177 unchanged tests; audit passes. Rejected DSP writes
now retain width/value in the existing strict report without adding GX evidence fields.

## Real ARAM DMA with credit-driven completion

Run `7a5394b685b24c8f8c0bb4962ea5b60e` at `e43abdd6c0490486633dccf9db24c9f5a10a5577`
closes **FAIL** at the first actual ARAM DMA address write:
`unsupported MMIO write address=CC005020 pc=80302EB0 width=4 value=0000000001000000`.
It has 1,311 edges, 144,070 credited cycles, zero FIFO bytes and `live=0` after shutdown.
This proves the guarded reset and halted mailbox accesses were passed, not that a DMA happened.
Both emulator phases still timeout/reap at 600 seconds, so process status remains 124/INCOMPLETE.
The static game archive remains byte-identical. At this SHA 155 host tests, 177 ARMv7 tests,
audit and attribution pass on Linux AArch64. No physical Vita, Windows, render or speed claim.
VPK SHA-256: `9aa900c998766e17723e753a365938b9e4fd8468c26de3edcdf041404c3b1000`;
report: `8e2b073aa2a895bd97014b25a2a0b68bc8ba667c8959aa3492c2cae236cd691e`;
result: `0b634cdf1427420327ce80437dc7652f124ccd6733a8082e2e083bac6d0f4a26`.
The log/journal hashes equal the preceding run because neither log embeds the terminal MMIO
failure: the source/request/package-bound closed report distinguishes these trials.

Pinned `Source/Core/Core/HW/DSP.cpp:45-62` identifies `5020/5022` as AR_DMA_MMADDR_H/L,
`5024/5026` as AR_DMA_ARADDR_H/L and `5028/502A` as AR_DMA_CNT_H/L. These are **not mailboxes**.
The native word `01000000` supplies physical MEM1 address `01000000`, cached alias `81000000`.
The reference masks high halves with `03FF`, low halves with `FFE0`, and count high with
`83FF` (direction bit included); the count-low write starts the DMA (`DSP.cpp:164-210,304-312`).
ARAM is the reference's 16 MiB separate backing (`DSP.h:34-39`), not a CPU-visible fake response.

WW05 now reuses `runtime/host/src/aram_dma.c` for validated actual byte copies and its transfer
census, using the pinned `GXRuntime/src/aram.c` buffer/copy/dirty-mark primitives. No shared
source is changed. The shared adapter's immediate pending flag is **not exposed as completion**:
WW05 keeps a separate cycle deadline and exposes DSP_CONTROL.DMAState while the transfer is
busy. Only credited guest cycles advance it, at **246 core cycles per 32-byte block** from
`DSP.cpp:456-465`. Zero-credit polls neither complete it nor move TB/DEC. When the deadline
expires, DMAState clears and the real transfer's ARAM interrupt becomes visible (`DSP.cpp:99-103`).
The existing shared cycle-domain deadline prevents a dispatch from budgeting past that event.
Address registers advance by the copied length and count drains, preserving direction, as in
`DSP.cpp:467-564`. The reference bulk-copies bytes at start and delays the interrupt; WW05
does the same, without claiming per-block contention or physical bus latency.

DSP interrupt publication follows the reference's pending/mask pair (`DSP.cpp:371-395`) and
the unchanged PI/MSR-EE exception seam. W1C acknowledges a completed transfer, but an early
acknowledgement cannot suppress the interrupt of a still-busy transfer. Audio remains stopped,
and this path generates no DSP mailbox word or AI interrupt. Supported ranges are mapped
24 MiB MEM1 and the 16 MiB ARAM store. Expansion/HSP, nibble-4 mirrored layout, zero-length
DMA, new DMA before acknowledgement and register writes while busy are explicit support
boundaries, not invented data or unnoticed shortcuts. Those refusals retain actual PC/width/value.

The host acquires ARAM once before guest execution, validates the otherwise-silent allocation
through `aram_buffer()`, refuses an existing owner and releases it on terminal shutdown.
Allocation failure unwinds CPU/VM/cache owners. `ALLOC` now includes optional `aram=16777216`,
and the strict validator includes it exactly once in `owned_live` (accepting old reports with
no ARAM owner). The eight-record framing and all GX-frontier fields and acceptance checks
are unchanged. Transfer/completion diagnostics log the first eight transfers and each thousandth,
with actual addresses, length, direction, remaining completion cycles and credited completion cycle;
trace errors propagate rather than being ignored.

TDD records: `logs/aram-dma-red.log` reproduces missing backing/register/ledger/ownership
behavior; `logs/aram-dma-green.log` and `logs/aram-dma-green-build-failure.log` preserve a C
symbol collision with the existing `aram_write`, corrected by naming the adapter handler
`aram_dma_write`. `logs/aram-dma-trace-red.log` reproduces absent transfer diagnostics;
`logs/aram-chassis-red.log` reproduces missing synthetic polling continuation.
`logs/aram-dma-final-green.log` passes **171 host tests**, including both copy directions,
32/64-byte deadlines, W1C-before-completion, zero-credit polls, deadline-bounded budgets,
memory/owner/error guards and the real main loop's synthetic poll followed by its HOST-only
FIFO fixture (246 DMA cycles plus 12 fixture cycles, not real GX evidence).
`logs/aram-dma-armv7.log` passes **177 unchanged tests**; audit passes. A source-bound real
trial must establish whether the game's two startup DMA transfers execute and expose the next
init operation. No ucode init completion is claimed by these synthetic tests.

## Verified DSP init transfer and documented completion mail

Run `6ddbfdaf995a45e790b8f0133d68786f` at `22a9c6aa63c54278946265e017ac1b3c2eaeb749`
proves both real 32-byte MEM1-to-ARAM startup copies, from cached `81000000` to ARAM offset
zero, at PCs `80302EC8` and `80302F10`. Both have a 246-credited-cycle deadline and completion
traces (absolute cycles 144,101 and 170,680). The closed guest report has 10,471 edges,
170,694 cycles and **FAIL** at `DSP initialization transfer not implemented address=CC00500A
pc=80302F34 width=2 value=0000000000000004`. GX is still `complete=0`, bytes=0; all owners
are released (`live=0`). Both owned emulator phases timeout at 600 seconds, TERM/reap with
`waited=true`; process 124/INCOMPLETE is distinct from that guest FAIL. Archive SHA-256 remains
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
VPK: `ee3b63c1d440dd154e7eb64c3404adcf2329c1bf728edee4979252a51c8a3ef3`;
report: `ecabcd01829a01d98c266846fbafe73d0aae5ed2d651bdb41a420411bc892602`;
result: `a155fb50a7602e948ea208c5dbda0a3fa709b8813763faba19240593ac92b751`;
log: `c040d04818467b9db35d34ddeb6c10a0c5e740b3702b2a83ade3255c9de14d86`;
the three-byte `LER` journal keeps hash
`169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.
Exact-checkpoint 171 host tests, 177 ARMv7 tests and attribution pass; precommit audit and
native runtime CTest 3/3 also passed on Linux AArch64.

The pinned interpreter's `DSP/Interpreter/DSPInterpreter.cpp:245-265` models INIT falling
as a **real 4 KiB MEM1-to-DSP instruction-memory copy** from `81000000`, selecting PC zero.
Its source comments acknowledge uncertainty in the physical mechanism. WW05 follows that
reference copy, not an invented ARAM transfer or a fabricated mailbox acknowledgement. The
entire source range must exist, and its first 128 bytes must exactly match the private
hash-checked init reference. The loaded 4 KiB image has a separate host-owned store; restoring
the CPU scratch afterwards cannot change it. Reset/release and execution re-verify the loaded
128-byte program. A changed loaded image cannot fall back to a still-matching MEM1 image.
The extra instruction-image and 8 KiB DSP data stores are in `sizeof(WwHost)`, so the existing
ledger counts each once and no new silent allocation is introduced.

This is an explicitly bounded **fixed-function model of that program**, not a DSP interpreter.
Private static proof identifies a discarded instruction-ROM scan, a zero fill of all 4,096 DSP
data words, a discarded coefficient scan, an empty outgoing-mailbox poll, constant high/low
mail stores, then HALT. No ROM or coefficient data are synthesized: their scan results do not
reach the fixed response. The data store is really cleared, bulk-applied before the first
outgoing store; per-instruction intermediate DSP memory state is not exposed or claimed.
`DSPHLE/UCodes/INIT.cpp:19-22` documents **`80544348`**; the identified program corroborates
the stores. Following the independent review, the host exposes the **complete word at the
first mail transition**, matching `DSPHLE/MailHandler.cpp:38-68`, not an intermediate LLE
store latch. Reading low clears busy without changing the residual bits or reissuing the mail
at the former second-store deadline. The next instruction asserts HALT
(`DSP/Interpreter/DSPIntBranch.cpp:127-132`), but the already-written mailbox remains readable.
No mailbox interrupt, AI stream, audio completion or controller data is generated.

Execution is advanced solely by real credited guest cycles while HALT is clear. The private
proof `logs/init-timing-sourced-22a9c6a.json`, SHA-256
`94b8ab7470a684e36898fb44c2630d3a3ab3d0eef52a9926d3ea0a6bc1922e22`, counts **16,404 DSP
steps** including HALT for the verified code and empty outgoing mailbox. Pinned
`DSP/Interpreter/DSPInterpreter.cpp:68-85,88-106` charges one step per instruction, and
`HW/DSPLLE/DSPLLE.cpp:249-272` supplies six core cycles per DSP step. Therefore the fixed
first-mail and HALT transitions occur at 98,412 and 98,424 running core cycles. The former
98,418 intermediate-store deadline no longer mutates the CPU-visible mailbox.
This adopts the reference ratio and static accounting, **not a new frequency or a physical
timing claim**. Pausing preserves fractional progress. Dispatch deadlines bound the complete
mail and HALT, separately from ARAM and DEC deadlines.

DSP_INIT_CODE is a different transition: the reference sets it at INIT falling and clears it
after **130 existing TB ticks** (`DSPInterpreter.cpp:261-281`). The reference describes this as
an inconsistent Wii hardware measurement. WW05 preserves that limitation and the existing
TB fractional phase; the interval elapses even while DSP execution is halted. Readback writes
of the flag cannot create a new interval. It is not reused as the mail latency. Zero-credit
polls neither expire it nor execute the program. Unknown init code, a short source range,
busy ARAM, occupied outgoing mailbox, simultaneous reset/init, completed-init re-release and
queued/running undocumented CPU-mail protocols are explicit stops. Dynamic Zelda audio is
still outside the coordinator authorization.

TDD: `logs/dsp-init-red.log` and `logs/dsp-init-red-fixture-build.log` preserve a test-fixture
constant typo, fixed before `logs/dsp-init-red-behavior.log` reproduces six missing transfer
behaviors. `logs/dsp-init-trace-red.log` reproduces absent transfer/mail/HALT evidence;
`logs/dsp-init-chassis-red.log` reproduces absent integration continuation.
`logs/dsp-init-final-green.log` passes **189 host tests**, including 128 single-byte mutation
cases, complete copy/source guards, loaded-image corruption at release/reset/during execution,
fractional TB expiry, halted/paused execution, store deadlines, a readable halted mailbox and
post-init reset after MEM1 scratch restoration. The real main-loop integration uses only
synthetic bytes and waits through repeated credited dispatches until its documented init mail,
then writes the HOST-only FIFO fixture: 98,418 execution cycles plus 12 fixture cycles.
That synthetic PASS is not real game GX evidence. The unchanged **177 ARMv7 tests**, audit
and native runtime CTest **3/3** pass. Transfer/mail/HALT trace failures propagate explicitly.
A source-bound isolated game trial is next; no physical Vita, Windows CI, rendering or
performance was tested, and no pins, generated game code or shared runtime source changed.
Tracker publication/open-PR checks remain unavailable under the offline/no-network constraint;
this Markdown record remains the local evidence for VITA-WW-05.

## Restart recovery: real init mail reached, then the SDK BAT setup

The server restart cancelled supervisor `sh_12b1d95ea001R5J027kuS3bmbg` during guest phase of
run `c683ad214d254385851731b785d0d080`, source `023736f128ab14fae47e0798a7b61f946978c08f`.
Build status was zero, 122.223 seconds; the archive is still byte-identical. Installation has
a retained 600-second timeout/TERM/reap receipt with `waited=true`, and preparation was VERIFIED.
The guest wrote a source/request/package-bound, eight-record closed **FAIL** report before the
restart. It has 10,874 edges, 271,011 credited cycles, zero FIFO bytes and `live=0`.
STOP is `unsupported SPR write address=00000218 pc=80305C4C`.
There is **no guest process receipt and no original result.json**: its exit, duration and wait
cannot be reconstructed. A fresh `/proc` inspection found no live run-owned sandbox or Vita3K
process. This is an interrupted INCOMPLETE process attempt, not an invented 124 or successful reap.

Recovery rechecked clean source/pins, readonly build/runtime/library/firmware inputs, the
installed private SELF/SFO/DOL bytes and the strict report token/package identity. The new
exclusive `evidence/restart-recovery.json` records that missing receipt rather than rewriting
the absent terminal process evidence. Recovery SHA-256:
`cbbfad0e983f356e28c1b2af01f5f47f723469da9da582c2ed37b5e8ed1f35cf`.
VPK: `346e886b7c992d1c6800d9fbcfa7a84e2f60205f3a8478424ebd0203d6ac0d2b`;
report: `d6a1c20cb14b86dd7a639537aa1450b4e8e1e27c39385223f68f73434356e085`;
log: `4165da4493a25f346e913cd499eb86e05ce6740f4ca897717e9e53f41a38ac24`;
the three-byte LER journal hash remains unchanged.

The retained log proves both 32-byte ARAM copies, the real 4 KiB DSP init copy at `80302F34`,
and **`[dsp-init-mail] value=80544348 cycles=270659`**. The CPU then consumes the mail and
asserts HALT/reset in its real startup sequence. There is no DSP init-HALT trace, so this trial
does not prove execution of the following DSP HALT instruction; it proves the fixed init mail
was reached and the guest continued. Audio remains stopped/silent. Exact-source checks remain
189 host tests, 177 ARMv7 tests, audit and attribution on Linux AArch64, with precommit native
runtime CTest 3/3. Nothing was pushed and the cancelled profile is preserved, never reused.

SPR `0x218` is **DBAT0U (536)**, not DSP/MMIO. Pinned `PowerPC/Gekko.h:852-867` maps IBAT0..3
and DBAT0..3 to SPRs 528..543. The actual failing instruction writes zero from r7, invalidating
DBAT0U before setting its lower and upper halves. Static inspection of the source-bound private
startup shows lower `00000002`, upper `800001FF`, then BAT2 lower `01000002`, upper `810000FF`,
in both data and instruction banks. The function enables MSR IR/DR by its real RFI at `80305CA0`.
With BL measured in 128 KiB blocks, these are **16 MiB at 80000000 -> physical 00000000** and
**8 MiB at 81000000 -> physical 01000000**. Pinned `Gekko.h:710-735` gives BEPI/BL/VS/VP and
BRPN/WIMG/PP; `PowerPC/MMUBAT.cpp:15-78` gives the actual base and block translation.

WW05 retains all 16 BAT halves in `bat_contract.c`, beginning with the pinned GameCube boot
setup (`Boot/Boot_BS2Emu.cpp:116-138`): IBAT0/DBAT0 `80001FFF/00000002` and DBAT1
`C0001FFF/0000002A`, other pairs disabled. It accepts only this boot view, the observed split
MEM1 view and zero/disabled preparation writes. A lower change requires the upper to have been
invalidated; an enabled upper requires the exact supported physical base/attributes. No changed
BAT is accepted while that bank's IR/DR is active. The pinned `ppc_mfspr`/`ppc_mtspr` privilege
check is unchanged; the original instruction fallback executes the actual mtdbat opcode and
advances PC normally, without an instruction skip or fake charge. Readback returns the retained
values. Active upper commits trace SPR, upper/lower, real range size and CIA; errors propagate.

This is a **fixed-view contract, not a new MMU**: the allowed tuples produce exactly the existing
24 MiB MEM1 and uncached mirror relation from pinned `GXRuntime/include/core/cpu.h:205-254`.
It does not add mappings, extra memory, segment/page translation, cache accuracy or protection
faults. Every edge with IR/DR active requires a complete supported view, including the retained
uncached data mapping, after crediting the completed block's actual cycles. An unsupported
base, length, physical page, attribute, active remap or incomplete view fails explicitly rather
than being accepted as a meaningless SPR store. Registers are host-owned and counted by the
existing `sizeof(WwHost)` allocation census. Pins/shared runtime/generated code remain untouched.

TDD records: `logs/bat-contract-red.log` plus `logs/bat-contract-red-fixture-build.log` retain a
fixture constant typo, fixed before `logs/bat-contract-red-behavior.log` reproduces all 13
missing contract behaviors. `logs/bat-contract-trace-red.log` reproduces absent commit traces.
`logs/bat-contract-edge-credit-red.log` exposes a pre-flush edge refusal; the check was moved
after the existing credit flush, so failed activation retains all executed cycles.
`logs/bat-contract-edge-green.log` passes **203 host tests**, including original instruction
fallback, boot and SDK readback, actual RFI/unchanged memory views, privilege checks, rejected
unknown/partial/active tuples and the failing edge's cycle credit. **177 ARMv7 tests**, audit,
attribution and native runtime CTest **3/3** pass on Linux AArch64; physical Vita and Windows CI
remain untested. A fresh 600-second-per-phase source-bound trial is next.

Coordinator integration note: VITA-WW-13's disc seam is present locally at
`35f7c8a05e69648a5e57241ef8ed08b9093fb337`. When the guest approaches its first actual file open,
rebase this lane onto that integration and use its pinned dvd.c force-include seam, private ISO
path `ux0:data/bluewake/GZLE01.iso` and isolated `scripts/vita/dvd/launch.py`. Do not reinvent
disc/FST reads. No rebase is needed for the current DSP/BAT wall; preserve existing source-bound
trial identities before changing the branch base.

## Real SDK BAT continuation and bounded MI startup configuration

Run `05ca92e8a373477f9120449ae00faaa1`, source `3bd525458b4610bf0065bb5e0c9ae6efcee828c3`,
records all four expected upper commits at their actual PCs: DBAT0 `80305C54`, IBAT0
`80305C64`, DBAT2 `80305C74`, IBAT2 `80305C84`. Values are `800001FF/00000002` and
`810000FF/01000002`. The guest then executes its real RFI and continues to MI startup.
The closed guest report remains **FAIL**, 10,887 edges, 271,033 cycles, `GX_FRONTIER complete=0`
with no FIFO bytes, and `live=0`. New actual STOP:
`unsupported MMIO write address=CC004020 pc=80305DA4 width=2 value=0000000000000000`.
Both owned emulator phases reach their 600-second deadlines, exit by TERM with `waited=true`;
process 124/INCOMPLETE is separate from that guest FAIL. Build status zero, 121.615 seconds;
the static game archive remains `9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source 203 host tests, 177 ARMv7 tests, audit and attribution pass on Linux AArch64;
precommit native runtime CTest 3/3 passed. VPK SHA-256:
`77fa1321d6402bbec48ff67d7f9f6d32a3a5646cefc1b7d39eeec483c4c730ed`;
report: `70332f33a3706951ed21653780c2ee956fdd1d5d8bc5ee8efd9c18266c2a7382`;
result: `ff193c14f8d3935aba00e3b148dea16d7e6842b7995e89e6f7cc1fee93f29511`;
log: `f6c7276f89fee80ad9b170588e6c18dd468592b41506721d0983e21928e55d79`;
the LER journal hash remains unchanged.

Pinned `HW/MemoryInterface.cpp:27-32` distinguishes MI_IRQMASK (`401C`), MI_IRQFLAG (`401E`)
and **MI_UNKNOWN1 (`4020`)**. The failing write is not an interrupt acknowledgement.
`MemoryInterface.cpp:60-64,87-97` gives zero reset and direct halfword storage for UNKNOWN1 and
MI_PROT_TYPE (`4010`). The actual SDK startup writes zero to UNKNOWN1, then `00FF` to PROT_TYPE
at `80305DB0`. The latter gives all four two-bit regions read/write permission: pinned
`MemoryInterface.h:51-60` defines the four fields, and
`GXRuntime/graphics/aurora/include/dolphin/os/OSMemory.h:15-18` defines READ=1, WRITE=2, RDWR=3.

The existing MI register seam now retains and reads these two halfwords. It accepts **only zero
at UNKNOWN1 and 00FF at PROT_TYPE**, preserving the reset image and the SDK's unrestricted
startup view without claiming the undocumented UNKNOWN1 hardware function or pretending to
enforce protected memory. Nonzero UNKNOWN1, restrictive/other protection values and unsupported
widths stop with the actual address, PC, width and value; their stores cannot mutate the retained
state. No synthetic MI/PI interrupt, cycle credit, memory copy or GX command is introduced.
The existing MI mask behavior is unchanged. The small host-owned register fields remain counted
once by `sizeof(WwHost)` in the existing allocation census.

`logs/mi-init-red.log` reproduces the three missing reset/zero/unrestricted behaviors before
implementation. `logs/mi-init-green.log` passes **210 host tests**, adding seven focused reset,
configuration, no-event/no-time, support-boundary and access-width cases. The unchanged **177
ARMv7 tests**, audit, attribution and native runtime CTest **3/3** pass on Linux AArch64. Physical
Vita, Windows CI, rendering and FPS remain untested. The next source-bound isolated trial must
show whether actual MI startup continues; zero FIFO bytes are still the latest real evidence.

Pending rendering contract, verified read-only against VITA-WW-12 on local `vita-integration`:
emit `[frame-timing] retrace=N us=T` on every real VI retrace and `[frame-present] frame=N us=T`
only on an actually presented GX display copy. T is host wall-clock microseconds, not guest
cycles/TB, with strictly advancing per-stream indices and times. Use the same checked,
unbuffered `stderr` channel established by `fixture_log_open("ww05")` and `setvbuf(...,_IONBF,...)`.
Propagate logging/flush failures. Do not emit markers for the FIFO frontier, emulator presents,
or unsubmitted display copies; there is no VI clock or GX presentation path in this host yet,
so no marker or FPS claim is added in this increment.

Pending REL lifecycle contract, verified read-only at integration `4312a15`: emit
`[rel-load] module=ID name=NAME bytes=B us=T` only after successful materialization and
`[rel-unload] module=ID us=T` exactly once on a live-to-dead slot transition. ID is the actual
big-endian REL header module id, NAME the requested module name without spaces, B the raw
materialized image size (not rounded capacity); T uses the same monotonic host wall-clock base
and checked unbuffered `ww05` channel as the frame markers. No duplicate live loads, inactive
unloads or non-advancing timestamps are acceptable. The cited `host_materialize_rel` and
`host_rel_slot_is_live` paths currently exist in `runtime/host/src/main.c`, not `vita/host/main.c`.
Wire the contract when this lane actually reaches runtime REL loading; do not count the
startup static-alias preparation as load events, and do not add fake REL activity now.

## Real MI continuation, then the PI cold-reset-code read

Run `7fea2e51df6e4488927d25548323da4a`, source `44ececc8f491568535b025123f96f9419bca4782`,
passes the actual MI initialization stores and continues to the SDK reset-code read.
The strictly request/source/package-bound closed guest report is **FAIL**: 11,055 edges,
274,382 credited cycles, no FIFO bytes, `GX_FRONTIER complete=0` and `live=0`. Actual STOP:
`unsupported MMIO read address=CC003024 pc=803069E8`. The private generated instruction is
`lwz r0,36(r3)` with r3=`CC003000`, a word read, followed by the SDK reset-code extraction.
Both phases timeout after 600 seconds, TERM/exit -15 and `waited=true`; the emulator
124/INCOMPLETE disposition does not replace the guest FAIL. Build status zero, 121.790 seconds;
the static archive is still `9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source checks pass **210 host tests, 177 ARMv7 tests**, audit and attribution on Linux
AArch64, with precommit native runtime CTest **3/3**. VPK SHA-256:
`216ca40a985fa88f3e8787e740c30399a8b3fe7c201bccbfddd8cbe9e5367e35`;
report: `b9c04e2e46720643b5c668a73181196b3695ee4893078d020325ef23bbd3b8ec`;
result: `7511fbad113f11570dc90cac230a1bb98843e8669bc62e05c122077e5b3d712e`;
log: `f6c7276f89fee80ad9b170588e6c18dd468592b41506721d0983e21928e55d79`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

Pinned `HW/ProcessorInterface.h:52-60` identifies **PI_RESET_CODE at 0024**; the revision
register is **002C**, not 0024. The initial revision-register interpretation was corrected
before any test or implementation. `ProcessorInterface.cpp:47-57` initializes reset code
to zero for cold reset; `119-134` returns the stored code and shows why writes are not inert:
they can reset the DVD drive. WW05 only boots cold and provides no drive-reset backend, so
this increment supports just the observed **32-bit read returning the sourced cold-reset zero**.
All reset writes remain explicit failures with address/PC/width/value, and unsupported read
widths remain failures. It adds no mutable reset state, Flipper revision, fake reset switch,
drive activity, PI interrupt, clock credit or instruction skip. Reads use the existing
observation/quantum path, so executed cycles are credited once and cannot renew a spent turn.

`logs/pi-cold-reset-red.log` reproduces the three missing supported-read behaviors. The green
suite `logs/pi-cold-reset-green.log` passes **215 host tests**, adding stable cold-reset state,
the actual generated memory-read expression, rejected reset writes, parameterized unsupported
read widths and credit/quantum preservation. **177 ARMv7 tests**, audit, attribution and native
runtime CTest **3/3** pass on Linux AArch64. This read has not yet been tested in the real guest;
the next isolated source-bound trial is required. No physical Vita, Windows CI, rendered frame
or FPS is claimed. FPS/REL markers remain deferred, and the integrated DVD seam remains the
required dependency at the first real file open.

## Real PI continuation: first external vector is outside compiled dispatch

Run `107557bedf9241489d12e7396c3f77b2`, source `5c55b79ef5acc199a942ff3e541297400da6eae5`,
passes the reset-code word read and continues through the guest's memory-init loop. The strict
closed report remains **FAIL**: **36,124 edges, 6,789,375 credited cycles**, no FIFO bytes,
`GX_FRONTIER complete=0`, `live=0`, and actual STOP:
`unsupported host call address=00000500 pc=00000500`. Both owned phases time out after 600 s,
exit -15 by TERM and `waited=true`; process 124/INCOMPLETE remains separate from the guest FAIL.
Build status zero, 120.731 seconds, static archive SHA-256 still
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **215 host tests, 177 ARMv7 tests**, audit and attribution pass on Linux AArch64;
precommit native runtime CTest **3/3** passed. VPK SHA-256:
`fcf7afb03e32ace8cd946a013c76a75754f0f47daa2c1dde9bd99b0d430d86f9`;
report: `f086b1d3e08a30131abb9d0633eba3c4fe99a3d4d72dddd2bf4602669325ff6a`;
result: `72622662b8481a1cf83cd0c45c47781ad4995609b6d17b5036f319ed9b3d87e6`;
log: `86d7ddc3236a185c814dde809a8068911c2cf3eb4668a88b49d58f8766a330aa`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

`00000500` is the external-interrupt vector, outside this module's compiled DOL ranges. The
existing EE/PI gate takes the pinned exception, saves SRR0/SRR1 and clears the consumed pending
CPU bit before dispatching the vector. The compiled dispatcher therefore reaches the unimplemented
host-call path, which sets the host failure bit. The older outer-loop diagnostic condition skipped
all state/vector logging when a host service had already failed, so this trial does **not** identify
the delivered PI cause or prove the contents/lineage of the copied vector. Do not infer them from
the zero-filled initial image or substitute a handler without verifying the actual guest memory.

The next increment is **diagnostic only**. The existing exception diagnostic now also runs after
a service-set failure, retains the first STOP address/PC/reason, includes the actual PI cause/mask,
and reads at most **256 bounded bytes** directly from guest RAM at the failed PC. It prints the
saved SRR pair and existing CPU state on the same unbuffered `ww05` channel; every write remains
checked. No CPU registers, interrupt sources, memory, instruction result, timing or vector
dispatch are changed. In particular, do not import the desktop's context-synthesis/vector-alias
shortcut: the real vector instructions must be proved and executed, not skipped.

`logs/external-vector-diagnostic-red.log` reproduces the missing state/bytes and the fact that the
previous suppressed path never exercised injected logging failures. The synthetic main-loop
fixture now follows the real edge/unsupported-host-call/error path with a game-free 256-byte
pattern; it proves the original STOP survives, actual saved state and exact pattern are logged,
the same 24 executed cycles remain, and state/vector write failures cannot publish a closed
verdict. `logs/external-vector-diagnostic-green.log` passes **218 host tests**, alongside **177
ARMv7 tests**, audit, attribution and native runtime CTest **3/3** on Linux AArch64. The real vector
diagnostics still require a fresh source-bound isolated run. Physical Vita, Windows CI, rendering,
FPS and REL lifecycle remain untested; no marker or completion is fabricated.

## Captured external-vector lineage and guarded supplemental translation

Run `215e84669a1d4951967ad29857ea55fd`, source `b8a7329b819c4cb954b8084dd018806062dcdcc8`,
reproduces the same actual STOP at `00000500`: **36,124 edges, 6,789,375 cycles**, closed guest
**FAIL**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0`. The saved state is now retained:
SRR0=`80304628`, SRR1=`00008032`, MSR=`00000000`, PI cause=`00010008`, mask=`000000F8`.
The maskable source is **SI**, not DSP, EXI or an invented VI retrace. Both owned phases reach
their 600-second deadlines, TERM/exit -15 and `waited=true`; process **124/INCOMPLETE** remains
distinct from that closed guest failure. Build status zero, 122.826 seconds; static archive
SHA-256 remains `9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source checks pass **218 host tests, 177 ARMv7 tests**, audit and attribution on Linux
AArch64; precommit native runtime CTest **3/3** passed. VPK SHA-256:
`d174abbfa87972016735fa381629007e5994f74e4d1d1d8d83b8ca8fad9de414`;
report: `acc6225ccd99964f06b75f03848ba444cf4a6857c1a9b66e52f60989f3c77662`;
result: `ef463ab8ef8b777b56e7c4334cc0664fbbef1d735650a63134cad0b62577b92c`;
log: `fb06581b0b05b21e40b0632eea1c8179677c4bdd41316e0f514f1a7552ad41c4`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The observed **152-byte** vector matches the private DOL template at `80301D8C` exactly,
except word offset **104**: the SDK changes `li r3,0` to the observed external-exception
number **4**. The remaining captured bytes through 256 are zero. Template SHA-256:
`9fd8348984aa801eafe743d52f99e803c6bf5bd5cfc620102f9fa832a1dd95a0`; observed vector:
`959cdcd88e3425871362441b5b3a335accea0263466a3316d38be5d09a5a6f22`.
Private `logs/external-vector-lineage-b8a7329.json` records the complete comparison without
publishing the bytes, SHA-256 `c7d23d6dab64679ffdb31d20f2a7109d3490d7a724b33bae589fd5397509e1b0`.
The request/source/package-bound report was validated again before staging the captured
152 bytes as private ignored `private/external-vector.bin`. No vector bytes or translated
game source are committed. Unknown input, different template, ambiguous/truncated DOL mappings
and any difference beyond that **already observed** SDK patch are refused, not repaired.

The implementation reuses pinned DolRecomp's decoder and **Gekko C emitter** to generate a
supplemental source object in the private identity directory. It does not modify the original
composite tree, its digest, static archive, pins, ABI, FP semantics or shared profiles. The
existing identified DOL-range extraction is shared with DSP init rather than duplicated.
`game_build.py` now requires the retained private vector input (or `--external-vector PATH`),
records its/template/source hashes and snapshots both the reference and generated source with
each artifact. The generated vector executes at its physical PCs `00000500..00000594`,
including the actual conditional branch and both possible RFIs; it is not a template alias or
a desktop-style context-synthesis shortcut. Its generated budget macro uses actual remaining
cycle credit, without resurrecting a zero quantum. Zero-cost SPR fallbacks retain the pinned
emitter's existing accounting and yield to the next real instruction; no invented cycles are added.

Dispatch rechecks all 152 guest bytes and physical supervisor state before every generated
entry/resumption, rejects unaligned/changed/incomplete/translated/pending-exception entries
before executing, and leaves existing module-owned ranges with their original dispatcher.
Entry and return records carry the actual PCs and saved SRR/MSR state on the checked unbuffered
channel; a logging failure propagates distinctly and cannot publish a closed guest verdict.
Only the vector's observed **SPRG0 (272)** is retained, with the pinned privilege checks, not
an unobserved SPR bank. With data translation disabled, bounded physical MEM1 accesses reuse
the existing big-endian memory adapter on the actual RAM backing; full or partial linked-alias
overlap is rejected. Stores retain dirty/reservation handling. References at the unchanged
RecompCore pin: `Gekko.h:847`, `Interpreter/Interpreter_SystemRegisters.cpp:233-323`,
`GXRuntime/src/core/cpu_interpreter_integer.c:3-35,41-90`,
`PowerPC/MMU.cpp:273-293`, and `PowerPC/MMU_Tables.cpp:70-121,204,352-372`.

Retained red checks include `external-vector-dispatch-red.log`, `external-vector-sprg-red.log`,
`external-vector-physical-red.log`, `external-vector-main-red.log`,
`external-vector-generation-red.log` and `external-vector-return-red.log`; all intermediate
failed checks are retained as well. `logs/external-vector-reviewed-host.log` passes **251 host
tests** using game-free authored PPC fixtures, not the real vector. Coverage includes actual
stores and RFI, every one-byte mutation, one-cycle deadlines, zero quantum, physical-endian
access/alias refusal, privilege checks, private-input lineage/generator failures and the real
main-loop seam with checked entry/return logging. **177 ARMv7 tests**, audit, attribution and
native runtime CTest **3/3** pass on Linux AArch64. The private observed vector has been emitted
without executing it on Linux; its Vita compilation and real-game continuation still require
the next clean source-bound build/trial. No physical Vita, Windows CI, rendering, FPS, runtime
REL event or GX acceptance is claimed. Network/remote PR inspection remains unavailable under
the offline mandate; cached remote branches were checked, and nothing was pushed.

## Real vector/RFI continuation reaches the drive inquiry, not a disc read

Run `e1f433bb14d4475aaf279bfbb0430f4f`, source `d6b439b11468292a89e2b6b096a01ae8a776a8ad`,
records **four** exact-byte-guarded vector entries. Each yields at the pinned SPR fallbacks
`00000500 -> 00000504 -> 00000510`, then executes the guest's conditional branch and real
RFI to **`80304E24`**, with SRR0=`80304E24`, SRR1/MSR=`00000030` and 35 charged emitted
cycles (0 + 2 + 33). No synthesized context or vector alias is used. The guest continues
through its actual interrupt dispatch/return and reaches **36,486 edges, 6,794,728 cycles**.
The strictly validated closed report remains **FAIL**, `GX_FRONTIER complete=0`, zero FIFO
bytes and `live=0`. Actual STOP:
`DVD command requires private disc service (not packaged) address=12000000 pc=8030EF44`.
Both phases timeout at 600 s, TERM/exit -15 and `waited=true`; process **124/INCOMPLETE** is
separate from that guest FAIL. Build status zero, 118.734 seconds, static archive still
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **251 host tests, 177 ARMv7 tests**, audit, attribution and native runtime CTest
**3/3** pass on Linux AArch64. VPK SHA-256:
`6fd93c67b52530c976e57af0eacdff4470891463e3852fba5a5add0f7b806740`;
report: `43cc1fc18226c0c98ca647f195ed63df477ba19d2cdf8ab53ecf3484f0ebc02b`;
result: `5b58d64f76a268c7b2402281c5cd6f18b8f9f39a067e31777527d03e4b799f67`;
log: `f09671cb2d5191ae37f982d11e7e8b5ec06981944abae3412096f5da7457fdfc`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

Despite the old STOP wording, **0x12 is DVDLowInquiry, not ReadDiscID or a file read**.
Pinned `HW/DVD/DVDInterface.h:40-44` distinguishes Inquiry (`12`) from Read (`A8`);
`DVDInterface.cpp:495-504` supplies three fixed response words: `00000002`, `20060526`,
`41000000`. That reference explicitly identifies them as observed Wii drive information,
used by its GC and Wii command paths. They are the pinned device model's values, not a claim
about the player's physical drive or bytes from the private ISO. The existing shared desktop
callback already emits the same three words. The private SDK sequence at `8030EEF4` loads
32, sets a read-DMA control of 3, and starts the query at `8030EF44`.

This increment extracts those existing three stores into shared `runtime/host/src/di_inquiry.h`
and reuses them from WW05 and the desktop callback. The desktop's surrounding validation,
32-byte initialization and completion behavior are unchanged. WW05 supports only the observed
exact command and **32-byte read DMA into actual bounded MEM1**, refusing unsupported modes,
lengths, low-byte variants, overflow/out-of-RAM buffers and linked aliases before changing
guest memory. It writes only the sourced **12 bytes** and preserves the reserved 20 bytes and
neighbors, rather than manufacturing a 32-byte payload. Completion uses the existing pinned
`GXRuntime/src/di.c:81-113` mechanism, including DMA length/address, TCINT, mask/EE gating and
write-one-to-clear. No guest latency, frequency, drive/media state, disc ID, interrupt handler
or game data is invented; no image is opened and the cover remains in its existing state.
The checked inquiry record names the actual DMA destination and issuing PC. Unsupported
commands still fail explicitly; actual disc/FST reads still require the `35f7c8a` integration
and its proven I/O seam, with the mandatory rebase before the first actual image open.

`logs/di-inquiry-red.log` reproduces the missing response and completion behavior through the
real MMIO callback. `logs/di-inquiry-green.log` passes **254 host tests**, including exact
sourced words, preserved padding/neighbors, parameterized rejected DMA shapes, 40 real fixture
instruction credits without fabricated time, masked external delivery and interrupt clearing.
**177 ARMv7 tests**, audit, attribution and native runtime CTest **3/3** pass on Linux AArch64.
The new query response has not yet been exercised by the real guest. The shared desktop helper
is covered by the same compiled store logic, but desktop gameplay and Windows CI were not run.
Physical Vita, GX acceptance, rendering, FPS and runtime REL lifecycle remain untested.

## Real inquiry completion reaches the lazy-FP unavailable vector

Run `4e98fd0395524ed8be8bcfe3b974f4fe`, source `367172d2d942bd17c136339122e25ff504d9b086`,
completes the observed drive inquiry into **`803F01A0`** and executes the next external
vector/RFI. It reaches **36,583 edges, 6,795,844 reported cycles**, then stops at the real
lazy-FP exception: `unhandled guest exception address=00000020 pc=00000800`.
The saved faulting PC is **`802AE070`**, an actual `lfs`; SRR1=`00008032`, MSR=`00000000`,
FPSCR=`00000004`. This is **FPU unavailable**, not a floating-point numerical mismatch.
The closed, request/source/package-bound report is **FAIL**, `GX_FRONTIER complete=0`,
zero FIFO bytes, `live=0`, `rendered=0`. Its final exception includes the host's subsequent
PROGRAM failure bit; the captured original guest exception is exactly `00000020`.
Both owned phases timeout after 600 seconds, TERM/exit -15 and `waited=true`; process
**124/INCOMPLETE** remains separate from the guest failure. Build status zero, 119.081 s;
static archive remains `9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **254 host tests, 177 ARMv7 tests**, audit, attribution and native runtime CTest
**3/3** pass on Linux AArch64. VPK SHA-256:
`466d75e390b06542fd713334bb97b9aca28723ab3f2a3f10633b49811a97ad6f`;
report: `c14d3f323187019bef9dca88b6d9c722dc82c8cb0bec70c5192b438a0f18639c`;
result: `492122d3a042ba85c136999a2bbabde3822517456a1adcb599028f58cbcb1792`;
log: `7b203e74baffeac32fe6137061c9b6308a87b9b31262ec6eaa9547e734b9a08b`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The captured **152-byte** FP vector has the same verified DOL template lineage as the
external vector, except the already installed exception-number word at offset 104 is
`li r3,7`. Its SHA-256 is
`a7537905d3a5cdeffa646f393bea89c8bcd48df2340ef1bd2a32c9da04904ae5`.
Private `logs/fp-vector-lineage-367172d.json`, SHA-256
`10b339e0f94d4acd89bcd30af9341a67271fdd8810efbddd681ef084f7213550`, retains the comparison.
No game vector bytes are committed. The existing identity/lineage extractor, pinned Gekko
emitter, physical-memory adapter and guarded dispatch are reused for **`00000800..00000894`**.
Only the two observed vectors are authorized; the new private `--fp-vector` input is required,
hashed and snapshotted with its generated source. The handoff accepts only the exact FP
exception and physical supervisor state after checking every byte, and consumes that delivered
exception without changing PC, SRR0/SRR1, MSR, FPSCR, FPRs or paired-single state. The guest's
own stores, branch and RFI execute next. Checked handoff/entry/return logging failures remain
explicit. No eager FPU enable, lazy-FP disable, desktop context shortcut or numeric patch is used.
References at the unchanged RecompCore pin: `GXRuntime/src/core/cpu.c:396-406`,
`GXRuntime/src/core/cpu_exception.c:9-36` and the existing pinned emitter/SDK template lineage.

The actual faulting generated block prepays an instruction suffix of **16 cycles** that
the FP exception never executes. WW05 now refunds only that outstanding suffix for the exact
FP-unavailable exception before ending dispatch. It keeps the attempted instruction's credit,
leaves other exception paths unchanged and clears the refunded suffix to prevent a second
refund. A suffix larger than outstanding prepayment fails loudly. This changes accounting,
not FP operations, emitter bytes or the module. The prior report's cycle count remains its
recorded value, not a retroactively corrected measurement. The build receipt records lazy FP
enabled and this suffix-refund policy separately from `fp_patch: null`.

Retained red checks: `fp-vector-input-red.log`, `fp-vector-dispatch-red.log`,
`fp-vector-credit-red.log`; intermediate fixture/build failures are retained too.
`logs/fp-vector-credit-reviewed-host.log` passes **275 host tests**, including game-free
generated FP vector stores/RFI, all byte mutations, unsupported states, one-cycle/zero budgets,
exact-exception handoff, unchanged numeric state, checked logging and prepaid/precise/invalid/
other-exception credit cases. The main-loop fixture credits **41**, not 57 cycles after the
16-cycle refund. **177 ARMv7 tests**, audit, attribution and native runtime CTest **3/3** pass
on Linux AArch64. The private real vector was emitted, not executed on Linux. Real lazy-FP
handler execution still needs a clean Vita build and isolated trial; no GX acceptance,
rendering, FPS, REL event, physical Vita or Windows CI is claimed. No image has been opened;
the mandatory DVD integration rebase remains before the first actual disc-service access.

## Real lazy-FP handler continues to an ordinary compiled register-restore epilogue

Run `020d193834d449e2bc78cf4dc8a56817`, source `cdebb145c6004e22c1537580fa7305f96db7f321`,
executes the exact guarded FP vector through `00000800 -> 00000804 -> 00000810` and its
own RFI to **`80303EB8`**. The real SDK handler subsequently restores the faulting
SRR0=`802AE070` with SRR1/MSR=`0000A032`: the **guest**, not the host, enables FP and
continues. It reaches **36,707 edges, 6,803,154 credited cycles** and stops at the broad
desktop-interception guard: `desktop interception unsupported; guest not skipped
address=80328F84 pc=80328F84`. The closed source/request/package-bound report remains **FAIL**,
`GX_FRONTIER complete=0`, zero FIFO bytes, `live=0`, `rendered=0`. Both owned phases timeout
after 600 s, TERM/exit -15, `waited=true`; process **124/INCOMPLETE** remains separate.
Build status zero, 124.473 seconds, static archive unchanged:
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **275 host tests, 177 ARMv7 tests**, audit, attribution and native runtime CTest
**3/3** pass on Linux AArch64. VPK SHA-256:
`e4b39e9a650342fcc6b960ee7028284d062249a59f33fbe5423fd2e5a7573a1c`;
report: `a7cc236c9edd02ceb3c8137762015e8fa16c7865746b5a7ce45338e31f7b547f`;
result: `7817e6785d485ba0b2fdf3a43e77d6270a8f0dc10cc8f8f6b9646f3cb9a4ce9c`;
log: `09d3a7f64a702cdc20b227086dc56044659fa72caa8a6117fbaea7cbdc98c250`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The loaded private DOL and unchanged translated chunk identify **`80328F84..80328F98`**
as `_restgpr_27`: five `lwz` instructions restore r27-r31 from the frame addressed by r11,
then `blr` returns through the real LR. It is already compiled in the static module.
The shared interception table includes this PC for a **desktop collision observation**,
not because this epilogue needs a host implementation. Shared `edge_intercepts.c:87-90`
explicitly does not require a host observation boundary here; `main.c:1699-1718` documents
the conditional GroundCross observer, which applies only to LR=`80246A04`. This run's LR
is **`8030141C`**, an unrelated startup return. No collision state is manufactured.

WW05 now allows only this **module-owned** register-restore boundary to continue through
the original dispatcher. The edge service does not change PC, LR, registers or MSR, and
does not restore the frame itself. Unowned epilogue entries and all other desktop-service
keys still fail explicitly; device events and pending interrupts retain their existing
priority. No blanket removal of the interception guard, emitted-code edit, native shortcut,
new timing constant, FP change or static-module/pin change is made.

`logs/register-restore-edge-red.log` reproduces the unwanted stop before the change.
`logs/register-restore-edge-green.log` passes **279 host tests**, including preserved
edge state, refusal of the unowned epilogue, refusal of a module-owned unsupported DSP
service, and a game-free main-loop module fixture that performs five RAM loads and returns
through LR only after the edge permits dispatch. Its register sentinels detect eager host
restoration/skipping; its authored fixture credits 20 cycles, not real-game acceptance.
**177 ARMv7 tests**, audit, attribution and native runtime CTest **3/3** pass on Linux AArch64.
The real epilogue continuation still requires a clean Vita build/trial. No GX acceptance,
rendering, FPS, runtime REL event, physical Vita or Windows CI is claimed. No disc image
has been opened by the guest; the existing DVD seam and mandatory rebase remain deferred
until the first actual disc-service access.

## Real compiled epilogue continuation reaches OSLoadContext

Run `3e3aa3e2c8db4bada470660f5ad05c98`, source `3cefb0b4fbfa234966b251d63b641dd87d377649`,
passes the previously refused register-restore boundary and continues through genuine startup
execution to **62,603 edges, 7,119,775 cycles**. The next STOP is
`desktop interception unsupported; guest not skipped address=80303A50 pc=80303A50`.
R3=`803A2960`, LR=`80307FB4`, MSR=`00000032`: the SDK has arrived at **OSLoadContext**
with an actual RAM context. The validated closed report remains **FAIL**, zero FIFO bytes,
`GX_FRONTIER complete=0`, `live=0`, `rendered=0`. Both owned phases timeout at 600 s,
TERM/exit -15, `waited=true`; process **124/INCOMPLETE** is distinct from that guest verdict.
Build status zero, 124.741 s; static archive unchanged:
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **279 host tests, 177 ARMv7 tests**, audit, attribution and native runtime CTest
**3/3** pass on Linux AArch64. VPK SHA-256:
`8c386a650af462fbdd7c11dc893365f6e92bf1a2cbef413fd289f3c8a9bd4d2a`;
report: `bc3e65dafa7c5b7d53e05afc853122262995b5a485ae4c0114a026ccd31e5174`;
result: `4840378b6dba6aa85343f19d7fe9ee9e0d9729fed1ffe14a42066d3e337d4c03`;
log: `96b7037dd5a17038f1043822e4480e296e7ae7f228770ac476ba16325738d221`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The unchanged generated `80303A50..80303B24` body performs the SDK's real context loads,
LMW, GQR/CR/LR/CTR/XER restoration, MSR update, SRR0/SRR1 loads and **RFI**. The shared
`edge_intercepts.h:50-55` makes its entry a desktop host boundary, but its CPU instructions
are already in the module and supported by the pin. WW05 extends the existing narrow
module-owned CPU-boundary allowance to exactly this entry. It does **not** import desktop
context synthesis, skip the routine, force a return, manufacture context data, change FP
semantics or enable an unobserved device. Before dispatch the host leaves PC, saved SRRs,
MSR, GPRs, GQRs and numeric state alone. The already compiled guest routine is responsible
for every context change; any unsupported instruction reached later still fails explicitly.
Unowned context entries and all remaining service keys are still refused. Interrupt/device
priority, clock/cycle accounting, static archive, composite digest, pins and ABI are unchanged.

`logs/context-load-edge-red.log` reproduces the missing boundary permission.
`logs/context-load-edge-green.log` passes **282 host tests**, including unchanged CPU state
at the permitted edge, unowned-entry refusal and a game-free module/main-loop fixture that
loads saved SRRs/registers from RAM and uses the pinned real RFI after entering its dispatcher.
Its saved-state sentinels detect eager context synthesis; its authored 19-cycle fixture is
not proof that the complete SDK routine has run. The register-restore fixture remains green.
**177 ARMv7 tests**, audit, attribution and native runtime CTest **3/3** pass on Linux AArch64.
Real OSLoadContext continuation still requires the next clean Vita build/trial. No physical
Vita, Windows CI, GX acceptance, rendering, FPS or runtime REL lifecycle is claimed; guest
disc/FST access still requires the existing DVD integration and mandatory rebase first.

## Real OSLoadContext continuation reaches the SDK's ARAM expansion probe

Run `94cb1d7bbb974f2bbae3e04eb4bbba44`, source `15ed5d0b85223f31211646767648be4709b357e5`,
passes OSLoadContext and reaches **63,005 edges, 7,123,941 cycles**. The second real lazy-FP
exception at `80254E60` executes the same verified FP vector and guest handler. The first
host failure is now `ARAM DMA expansion/HSP address not implemented address=CC00502A
pc=803176AC width=2 value=0000000000000020`. This is the SDK's real halfword DMA-count
trigger, not a DSP mailbox or an audio-ucode execution request. The final CPU PC has moved
on to `80301504`; the first STOP correctly retains the actual issuing PC `803176AC`.
The validated closed report remains **FAIL**, zero FIFO bytes, `GX_FRONTIER complete=0`,
`live=0`, `rendered=0`. **Process disposition differs from prior trials:** the install
phase still times out at 600 s, TERM/exit -15 and `waited=true`; the guest phase exits
normally **0 after 4.379 s**, `waited=true`. The launch result is **1/COMPLETE** with a
closed guest FAIL, not a guest PASS and not a 600-second guest timeout.
Build status zero, 125.099 s; static archive unchanged:
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **282 host tests, 177 ARMv7 tests**, audit, attribution and native runtime CTest
**3/3** pass on Linux AArch64. VPK SHA-256:
`c600d3b9765cbb9c8b05ac9dc8c855a23626a29c27a52dc27b4c3720fffff7af`;
report: `9611db95e2f2ac50185226ac0e94ce8436376f445b8c88111025c3abe3532cac`;
result: `a6ca6679e1b666816dccb508a4b8df38447f8351dca243c373006e775f4005ad`;
log: `6cf0adcdc973b5ee791c1e840720abc6f18ed68d5071335c759e8b98b2813e7c`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The current STOP records the low count halfword but does **not** capture the complete
main/ARAM addresses or high count/direction bits. Those missing values are not guessed.
Before retaining the existing expansion refusal, WW05 now writes a checked
`[aram-expansion-request]` record containing actual main address, ARAM address, prospective
masked count, AR_INFO, DSP control and issuing PC. The record handles both combined-word
and halfword triggers and precedes any memory copy or count-register mutation. A trace
failure fails loudly. Normal flat-ARAM transfers and their clock/completion behavior are
unchanged; no expansion DMA is accepted by this diagnostic increment.

At the unchanged RecompCore pin, `HW/DSP.cpp:479-523,536-583` routes an out-of-ARAM DMA
to HSP in GameCube mode, and `Config/MainSettings.cpp:239-240` selects **no HSP device**
by default. Crucially, `HW/HSP/HSP_DeviceNull.cpp:9-15` leaves both read destinations and
writes **unchanged**: it does not supply zeros and does not wrap expansion addresses into
the real 16 MiB ARAM allocation. The actual request must be captured before choosing any
bounded support for that documented absent-device path. Unknown devices, ucodes and dynamic
Zelda audio remain refused; no bytes, memories, DSP mails or payloads are manufactured.

`logs/aram-expansion-diagnostic-red.log` reproduces the missing request evidence and trace
failure propagation. `logs/aram-expansion-diagnostic-reviewed-host.log` passes **284 host
tests**, including word and read-direction halfword request records, unchanged MEM1/ARAM
on refusal and a checked-log failure through the real main loop. **177 ARMv7 tests**, audit,
attribution and native runtime CTest **3/3** pass on Linux AArch64. Capturing the actual
expansion request still requires the next clean source-bound Vita build/trial. No physical
Vita, Windows CI, GX acceptance, rendering, FPS, runtime REL event or guest disc access is
claimed. The DVD seam and mandatory integration rebase remain before the first actual read.

## Captured 32-byte expansion read and bounded absent-HSP completion

Run `9f1b9ffc1ae14ed7b32675aee2d42d54`, source `32795cba4d673c9641a4d5ce7d0cfddc3cf3057e`,
retains the actual request at `803176AC`: main physical **`0040CD20`**, ARAM **`01000000`**,
count **`80000020`** (read direction, **32 bytes**), AR_INFO=`0063`, DSP control=`0844`
(HALT, ARAM interrupt mask enabled). The first expansion probe is exactly at the end of the
real 16 MiB ARAM, not a permitted wrap into it. The closed report reproduces **63,005 edges,
7,123,941 cycles**, **FAIL**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0` and
`rendered=0`. This time both owned phases timeout at 600 s, TERM/exit -15 and `waited=true`;
process **124/INCOMPLETE** differs from the prior normal guest exit despite the same closed
guest STOP. Build status zero, 125.112 s; static archive unchanged:
`9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.
Exact-source **284 host tests, 177 ARMv7 tests**, audit, attribution and native runtime CTest
**3/3** pass on Linux AArch64. VPK SHA-256:
`9f01414c3a33078e530169376aeedf787403167817b4a481cf278b2341679a3b`;
report: `5b418173e4b955d7c38cbfdff941eaf42fe2226be073bdaa73fd274e2bbf0f90`;
result: `579c768c5af7d862fbfe39d95905bc04e8fd69f3387592847837014e19115bef`;
log: `7157333b139480febaadc8b30bcc2bb7f5f883f1fc977b7670df2445ffb3a320`;
closed LER journal: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

WW05 now accepts only that observed **32-byte read from `01000000` with AR_INFO=`0063`
and DSP halted**, into a complete real MEM1 range. This selects the documented **absent
HSP** device behavior at the unchanged pin (`HW/DSP.cpp:506-521`,
`Config/MainSettings.cpp:239-240`, `HW/HSP/HSP_DeviceNull.cpp:9-15`). It leaves MEM1 and
ARAM bytes **unchanged**, without zero fill, address wrapping, a fictional expansion
allocation or a payload copied from another address. Unobserved lengths, addresses, writes,
other layouts, running DSP, invalid MEM1 and unsupported requests remain explicit failures.
The checked `[aram-hsp-absent]` record labels this buffer-preserving path separately from
real flat-ARAM memory copies. No DSP mail, ucode execution or audio playback is introduced.

The shared ARAM register helper provides an **explicit absent-HSP entry point** that reuses
the same validation, transfer receipt and interrupt-pending bookkeeping without performing a
memory copy. Its existing entry point and desktop behavior are unchanged, including their
existing address-wrap contract. WW05 reuses its own established post-transfer address/count
updates, 246 credited cycles for this one 32-byte block, busy latch, masked completion and
acknowledgement. No new latency or frequency is invented; a preserved buffer is the reference's
absent-device response, not a successful transfer from real ARAM.

`logs/absent-hsp-read-red.log` reproduces the refused observed shape.
`logs/absent-hsp-read-reviewed-host.log` passes **287 host tests**: nonzero MEM1 and ARAM
sentinels remain untouched, word and halfword behavior remain bounded, invalid/unobserved
requests fail, and busy/completion/acknowledgement and checked logging use existing paths.
`logs/absent-hsp-shared-contract.log` compiles/runs the existing shared ARAM contract on
Linux AArch64, including real copies and the unchanged legacy wrap. **177 ARMv7 tests**,
audit, attribution and native runtime CTest **3/3** pass. Real absent-HSP probe continuation
still needs the next clean Vita build/trial; no physical Vita, Windows CI, GX acceptance,
rendering, FPS, runtime REL event or guest disc access is claimed.

## Independent review B1: atomic documented init mail

The requested B1 correction writes all of `80544348` at the first visible init-mail
transition. `DSPHLE/UCodes/INIT.cpp:19-22` sources the value and
`DSPHLE/MailHandler.cpp:38-68` sources complete-mail visibility and low-read consumption.
There is no transient `0054` high word before delivery and no later store that can restore
busy after an early low read. Verified-image authorization, credited execution, HALT,
130-TB-tick INIT_CODE expiry, no interrupt and stopped audio are unchanged.

`logs/review-b1-red.log` reproduces five failures at the first-mail assertion. The regression
also consumes low at that first transition and checks that busy stays clear through the old
six-cycle window and HALT. The existing main-loop poll now observes mail six credited cycles
earlier; this is the requested visibility correction, not skipped guest instructions.

Linux AArch64 checks: **288 host tests**, **177 ARMv7 campaign tests**, repository audit
and native runtime CTest **3/3** pass. The ARMv7 campaign initially failed configuration
after invoking a different host compiler, then failed on a host-only optional zlib header;
both failures are retained. Its generated build was retained under
`build-ww08-io-tests-review-b1-config-failure`; the fresh game-free generator uses the
existing no-zlib configuration, with no source, pin or floating-point change. Physical Vita
and Windows CI are not tested.

## Independent review B2: sampled, checked EXI records

EXI now counts transfers per host and uses the same sampling helper as ARAM: the first
**eight**, then each **1,000th**, with the actual count, transaction fields and guest PC.
An exhausted counter fails rather than wrapping. Each emitted `fprintf` is checked; failure
propagates as `EXI transfer trace write failed` at the actual EXI control register and PC,
before SRAM command/payload mutation or successful EXI completion. TSTART remains asserted
and no new TCINT is fabricated.

`logs/review-b2-red.log` reproduces the unsampled loop, ignored first/late/payload trace
failures and the erroneous successful main-loop report. The green suite passes **291 host
tests** on Linux AArch64. It verifies the exact nine records over 1,001 transfers, checks
each error before device mutation, and publishes a closed guest FAIL rather than accepting
the fixture FIFO after an EXI trace error. B1's early-mail-consumption regression also passes.

The review's loop-bound reservation is documented, not hidden: the current external bound
is `launch.py --timeout 600` **per emulator phase**, with owned TERM/KILL/reap. The guest
fixture has no internal turn/cycle or host-time limit. Sampling bounds the log rate, not
the total bytes of an indefinitely running standalone boot. A physical Vita launch has no
such external supervisor and still needs a local STOP bound before unattended use; none is
claimed here. No physical Vita or Windows CI result is claimed.

`BlueWakeRelData` is unchanged. Compile-time checks against `module_exports.h` pass with
the native AArch64 and ARMv7 Vita compilers: `bytes` offset 16, natural pointer alignment,
stride `16 + sizeof(pointer)`. Evidence: `logs/independent-review-rel-alignment.log`.

## Real absent-HSP read continuation and observed 32-byte write

Run `17f352955873432b9edc9af63eb566e9` is bound to clean source
`4755b6d5127204cd07f0949617787b186adb0c4c`. Its real guest logs show the accepted read at
`803176AC`, transfer **3**, main=`8040CD20`, ARAM=`01000000`, **32 bytes**, direction 1,
and completion at **7,124,177 credited cycles**. The next actual request is main physical
**`0040CDE0`**, ARAM=`01000000`, count **`00000020`** (write, 32 bytes), AR_INFO=`0063`,
control=`0844`, PC **`80317758`**. It is still the SDK expansion probe, not an audio protocol.
The closed report has **63,013 edges, 7,124,240 cycles**, `GX_FRONTIER complete=0`, zero
FIFO bytes, `live=0`, `rendered=0`, and the explicit expansion/HSP STOP at that write.

Guest **FAIL** is distinct from process **124/INCOMPLETE**: both owned emulator phases
timeout after 600 seconds, exit -15 by TERM and have `waited=true`. Build status zero,
124.815 s. Request/source/package bindings, closed report and LER journal were independently
rechecked in `logs/absent-hsp-evidence-4755b6d.json`. SHA-256:

- VPK: `895251ade3317f23b52c749445c322f5de7081b283e13b88b95b77405bc4cfe5`;
- report: `88bdf5a96c2b974f9c1fd76e77621e198e692014d37380ee31a4ed2aedabd524`;
- result: `d0ae5c2c655d553a85db5b900af7436954808e7c1c17012fce01e5d49492aa9f`;
- log: `30d183944cb3103de84e24309871b849e46225c8f840cf1948a2e89e0ac9220d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`;
- unchanged static archive: `9d1c59238cea4b83798f1c16b0549950d41ec0011a89e95591d5a307718aea08`.

B1 commit `54e8d22` passes exact clean-SHA **288 host / 177 ARMv7** checks; B2 commit
`e0200b0` passes **291 / 177**. Both also pass audit, attribution and native runtime CTest
**3/3**. Their private checkpoint receipts retain the full SHA and commands.

The next bounded increment accepts that observed **32-byte write** as well as the already
verified read. The unchanged pin routes expansion writes through `HW/DSP.cpp:566-581`;
the default absent device's `HW/HSP/HSP_DeviceNull.cpp:13-15` deliberately does nothing.
The existing explicit shared absent-HSP helper already supports this direction and preserves
both buffers. No new shared/desktop API, fake expansion memory, wrapping, zero fill, DSP
mail or audio is added. Host selection remains restricted to ARAM=`01000000`, exactly
32 bytes, AR_INFO=`0063`, HALT and complete real MEM1. Other addresses, lengths, layouts,
running DSP and invalid ranges remain refused.

`logs/absent-hsp-write-red.log` reproduces the refused write and incorrect continuation;
`logs/absent-hsp-write-host-green.log` passes **295 host tests** on Linux AArch64. Halfword
and word write triggers preserve nonzero source/ARAM sentinels, advance the real bookkeeping,
stay busy for 246 credited cycles, and use existing completion/mask/acknowledgement. The
checked record reports direction 0 and actual PC. A write-trace failure stops before an
unrecorded transfer. Authentic continuation still needs the next clean build/trial.
Physical Vita, Windows CI, rendering, FPS, runtime REL events and guest disc access remain
unverified; DVD integration `35f7c8a` still precedes the first actual guest disc/file access.

## Real HSP write continuation and disabled CP initialization

Run `ecd55871471c41c6a472be520472f9d9`, source
`12e7c1886dbb867e2d2ca3e736423ccd55236dbf`, completes the actual absent-HSP write:
transfer **4**, main=`8040CDE0`, ARAM=`01000000`, direction 0, 32 bytes, PC=`80317758`,
completion at **7,124,468 credited cycles**. The guest then performs and completes another
documented absent-device read, transfer **5**, main=`8040CD60`, PC=`80317818`, at
**7,124,824 cycles**. The next actual STOP is a halfword write of **zero** to
**`CC000002`**, PC **`80321308`**. The report has **64,235 edges, 7,139,922 cycles**, zero
FIFO bytes, `GX_FRONTIER complete=0`, `live=0`, `rendered=0` and guest **FAIL**.

Build status zero, 123.094 s, static archive unchanged. Both owned emulator phases timeout
at 60 s, TERM/exit -15, `waited=true`: process **124/INCOMPLETE** is not the closed guest
verdict. Source/request/package bindings, the report and closed LER journal are rechecked in
`logs/absent-hsp-write-evidence-12e7c18.json`. SHA-256:

- VPK: `0c575b58f56617d1c5c160951fdf29c6b36fbb81111e786bf90f4d2c77258962`;
- report: `166d77b72316386920beff3fed3b9f5ace1bb87ade82881e654a81f993fb6b5f`;
- result: `69ee0f55008d79d035dd7c2357d5685d07c376e01a9e2baffde0009dfecf5209`;
- log: `a1ead70d7460d6a707edcf1dccf0d5630fbbc169c5181de549593f6863726e9d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies `CC000002` as CP CTRL (`VideoCommon/CommandProcessor.h:56-61`).
Its reset value is zero (`CommandProcessor.cpp:107-117`); the halfword control write retains
six enable bits (`201-208`, `CommandProcessor.h:122-133`), and zero leaves GP reads, link,
breakpoint and interrupt enables disabled (`CommandProcessor.cpp:574-586`). WW05 has no
queued GPU execution before the frontier and cannot enable those services. No reusable
CP MMIO service exists in the pinned C runtime or shared host helpers.

The bounded increment therefore accepts **only the observed zero halfword control store**
through the normal memory/MMIO hook. It leaves the already-disabled control state unchanged;
it does not skip the guest instruction, change the PC, invent status or FIFO data, launch a
GPU worker, or synthesize an interrupt. Nonzero control, other widths, reads and other CP
registers remain explicit failures. In-block cycle observation and spent-quantum closure are
unchanged. The strict first-command parser and opcode/bytes/PC/completion-PC report remain
the acceptance gate, not this register store.

`logs/cp-disabled-init-red.log` reproduces three rejected zero-store scenarios and the
failed main-loop continuation. `logs/cp-disabled-init-host-green.log` passes **302 host
tests** on Linux AArch64: normal halfword memory routing, repeated zero stores, no FIFO or
interrupt manufacture, rejected enables/widths with exact context, credited cycles without
quantum renewal, and real main-loop continuation into a clearly synthetic FIFO fixture.
Authentic CP continuation is still awaiting a clean source-bound build/trial. No physical
Vita, Windows CI, rendering, FPS, runtime REL event or actual guest disc access is claimed.

## Real disabled-CP continuation and observed PI FIFO base

Run `90b129654f794d51acdb4ffb96e68aed`, source
`ce151f89eab50df1c45a8b8d695ff5688c8c51b5`, passes the disabled CP store and reaches a
word store of **`00429F60` to `CC00300C`**, PC **`80320EF8`**. Its closed guest report is
**FAIL**, **64,235 edges, 7,139,922 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`,
`live=0`, `rendered=0`. Build status zero, 123.896 s, unchanged static archive. Both owned
phases timeout after 60 s, TERM/exit -15, `waited=true`; process **124/INCOMPLETE** is
separate from the guest verdict. `logs/cp-disabled-init-evidence-ce151f8.json` rechecks
source/request/package/report bindings and the closed LER journal. SHA-256:

- VPK: `ba6514b0109e31353c02255bca372df6451f0eb8e0f2a7a63c77a9d57836c84a`;
- report: `ba9621438f4dbe8fb510963e7a31249a237bfa4f2ef82860143d42e046e3f43b`;
- result: `ab43d43bdb7105894a26dba345a09acc9476976e12b286ac4a1708144649bf69`;
- log: `a1ead70d7460d6a707edcf1dccf0d5630fbbc169c5181de549593f6863726e9d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies this register as **PI_FIFO_BASE** (`HW/ProcessorInterface.h:54`),
resets it to zero (`ProcessorInterface.cpp:47-54`) and directly retains a word masked with
**`FFFFFFE0`** (`84-85`). It has no transfer or interrupt side effect. No equivalent scalar
FIFO-register service exists in the pinned C runtime or shared host helpers. WW05 adds only
this host-owned register, ordinary word write/read routing, and that sourced mask. The
observed `00429F60` is retained unchanged. This is not a GPU FIFO allocation, a guest-memory
copy, a parser feed, a reset or an invented completion. Other PI FIFO registers and widths
still fail explicitly; unobserved read widths are not claimed even though the wider pin has
halfword-read adapters. Module/CPU ABI, pins, static archive, strict GX framing and cycle
observation remain unchanged.

`logs/pi-fifo-base-red.log` reproduces reset/store/mask/credit and main-loop refusal.
The first full green attempt retained one obsolete fixture failure: its "unknown write"
used the now-supported PI FIFO base. That fixture now probes the still-unimplemented
reserved PI offset `CC003008`, preserving fail-fast coverage rather than disabling it.
`logs/pi-fifo-base-host-final-green.log` passes **309 host tests** on Linux AArch64, including
the actual word memory hook, the alignment mask, invalid-width state preservation, no
manufactured FIFO/IRQ/time, spent-quantum closure and synthetic main-loop continuation.
Clean-source Vita compilation and authentic continuation are next; physical Vita, Windows
CI, rendering, FPS, runtime REL events and actual guest disc access remain unverified.

## Real PI FIFO base continuation and observed end register

Run `4feb9aeb93a8448fa93ed26e10d75fec`, source
`863230bb213d2e5698dda3c0f9f62abe685fb2ac`, passes the base store and reaches the next
word store: **`CC003010 = 004C9F5C`**, PC **`80320F08`**. Its closed guest **FAIL** has
**64,235 edges, 7,139,922 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0`,
`rendered=0`. Build status zero, 123.273 s, static archive unchanged. Both owned phases
timeout after 60 s, TERM/exit -15 and `waited=true`; process **124/INCOMPLETE** remains
distinct. `logs/pi-fifo-base-evidence-863230b.json` rechecks the bindings and LER journal.
SHA-256:

- VPK: `be1b76133ec61cf1a8d3ca0b3ebaef8d19160b46475e9fa657dc44eeaf7fc443`;
- report: `eb0b221865b4afafe25057c9b56eb7a445fbe626b7dc9f56ea4adf158ca338e9`;
- result: `b8aff81e6b611c6b3e354139400db35655284e3aff39d9f650c4b1b616d72b3f`;
- log: `a1ead70d7460d6a707edcf1dccf0d5630fbbc169c5181de549593f6863726e9d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies **PI_FIFO_END** at offset `10` (`HW/ProcessorInterface.h:55`), reset
zero (`ProcessorInterface.cpp:47-54`), with the same direct word read/write mask
**`FFFFFFE0`** (`87-88`). The actual `004C9F5C` therefore reads back as **`004C9F40`**;
retaining its low `1C` bits would be wrong. WW05 extends the existing scalar register
selector to base/end and shares one mask and width gate, without duplicate device logic.
The two words have independent host-owned stores. No FIFO write pointer, reset, GPU work,
memory copy, parser feed or interrupt is implemented by this configuration store.

`logs/pi-fifo-end-red.log` reproduces end-register refusal while the base cases pass.
`logs/pi-fifo-end-host-green.log` passes **310 host tests** on Linux AArch64, including
parameterized base/end reset, actual word memory hooks, masking, invalid-width state
preservation, independent retained words, credited-cycle/spent-quantum behavior and clearly
synthetic main-loop continuation. Strict GX acceptance and the module/CPU ABI are unchanged.
Authentic continuation awaits the next clean build/trial; physical Vita, Windows CI,
rendering, FPS, runtime REL events and actual guest disc access remain unverified.

## Real PI FIFO end continuation and observed write pointer

Run `a831a2e73d5d4c48a64ba952fbb12b97`, source
`ed7d0e27a2bbfcffa1fd1b44d3625bd64095b65c`, passes PI FIFO end and reaches the word
store **`CC003014 = 00429F60`**, PC **`80320F1C`**. The closed guest **FAIL** retains
**64,235 edges, 7,139,922 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0`,
`rendered=0`. Build status zero, 124.869 s, static archive unchanged. Both owned phases
timeout at 60 s, TERM/exit -15 and `waited=true`; process **124/INCOMPLETE** is separate.
Bindings and LER are rechecked in `logs/pi-fifo-end-evidence-ed7d0e2.json`. SHA-256:

- VPK: `aa8feef0263a10e5965a5dffa42b408de72510b614657ed040045eb755735557`;
- report: `562da076da24a924862279ccd46b24faa267e9e6b93a3fb89be99492ba1e5865`;
- result: `3af53c7f19fb83bbf14757a63c0d90331ad6adf30c71d83be5bb8ec0e99f219b`;
- log: `a1ead70d7460d6a707edcf1dccf0d5630fbbc169c5181de549593f6863726e9d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies **PI_FIFO_WPTR** at offset `14` (`HW/ProcessorInterface.h:56`), reset
zero (`ProcessorInterface.cpp:47-54`), direct word read/write with **`FFFFFFE0`** (`90-91`).
WW05 extends the existing scalar selector by one independently retained word; width, mask,
cycle observation and failure routing remain shared with base/end. The observed value is
already aligned and stays `00429F60`. This configures a register only: it does not manufacture
gather-pipe output, guest-memory writes, pointer advancement, overflow, GPU work or interrupts.
FIFO resets, CP enables and other unimplemented operations still fail.

`logs/pi-fifo-wptr-red.log` reproduces write-pointer refusal with base/end still passing.
`logs/pi-fifo-wptr-host-green.log` passes **310 host tests** on Linux AArch64; existing
explicit parameterizations now cover base/end/write-pointer reset, stores, mask, invalid
widths, retained-state isolation and credited-cycle/spent-quantum behavior. The synthetic
main-loop fixture covers each configuration register without claiming real GX acceptance.
Authentic continuation awaits the next clean build/trial. No physical Vita, Windows CI,
rendering, FPS, runtime REL event or actual guest disc access is claimed.

## Delta review: bounded HSP traces and known GX/integration limits

Run `da07158f060f4e79ac74d5596b78a180`, source
`0f69a6cb140bd526303c7cddcca7045864c7335e`, passes all three PI FIFO configuration
stores and reaches **`CC000020 = 9F60`**, halfword PC **`80320F80`**. The closed guest
**FAIL** has **64,248 edges, 7,140,042 cycles**, zero FIFO bytes,
`GX_FRONTIER complete=0`, `live=0`, `rendered=0`. Build status zero in 123.752 s; archive
unchanged. Both owned phases timeout at 60 s, TERM/exit -15, `waited=true`; process
**124/INCOMPLETE** is distinct. Bindings and LER are independently rechecked in
`logs/pi-fifo-wptr-evidence-0f69a6c.json`. SHA-256:

- VPK: `b3007d29050f2d7ae7a70ffdf5cb2d9068f2424346e6207d18e11b4587ba264d`;
- report: `86e1b838479ad27bf4c066fbe20ba7ddb9dff3fc18e0c293866f9548542feffd`;
- result: `f6c2bfa5307759992f092dfca66cf57e6784efb86714d77b6600388f435f2824`;
- log: `e53c5af186f23140c23810e0dd46dfbad9baa2e8b8124233c165a3eafcb1c81d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The delta review confirms B1/B2 and flags an unbounded accepted-HSP trace. The single
`ww_host_trace_transfer` predicate now lives in `trace_sampling.h`, shared by host ARAM/EXI
and DSP HSP diagnostics. Accepted expansion-request and absent-device records use the same
next-transfer ordinal: the first eight, then each thousandth. Refused expansion shapes
remain visible regardless of sampling. Checked trace failures still stop before transfer
commit/completion. WW05 rejects an exhausted transfer counter before increment, logging or
DMA, rather than wrapping the sample ordinal; the desktop ARAM API is unchanged.

`logs/hsp-trace-sampling-red.log` reproduces 1001 absent-device lines instead of nine and
an attempted unsampled trace write. `logs/hsp-trace-counter-red.log` reproduces counter
wrap. `logs/hsp-trace-sampling-host-final-green.log` passes **315 host tests** on Linux
AArch64, including 1001 accepted device-helper transfers with alternating directions,
exact request/absent/DMA/completion sample counts, preserved memory, retained refused
requests, first/thousandth checked-write failure, skipped ninth trace and counter exhaustion.
These are game-free device tests, not authentic boot/GX acceptance.

Known limit to resolve with the GX seam: PI FIFO WPTR is currently retained configuration
only. The pin's `HW/GPFifo.cpp:88-104` commits each real 32-byte gather block to guest FIFO
memory, wraps at the end or advances WPTR by 32; a guest polling WW05 WPTR will not yet see
that progress. Correct this with actual gather-block production and its backing-memory
write, not on scalar MMIO reads, parser completion or invented consumption. No runtime FIFO
progress is claimed. Strict opcode/bytes/PC/completion-PC validation remains the GX gate.

Final integration debt, explicitly unverified:

- Before genuine guest disc/file access, rebase onto DVD integration
  `35f7c8a05e69648a5e57241ef8ed08b9093fb337`; wire its existing DVD seam into boot-host DI
  routing and the host CMake aggregator rather than add a second disc implementation.
- Verify shared `aram_dma.c` in the Windows build/host checks; Linux native tests are not
  Windows CI or hardware evidence.
- Confirm exact test counts on the final source SHA/build, independently of interim counts.

No physical Vita, Windows CI, rendering, FPS, runtime REL event or actual guest disc access
is claimed. The next observed boot unit remains CP FIFO base-low configuration.

## Observed CP FIFO base-low configuration

The actual `da07158f060f4e79ac74d5596b78a180` blocker above is **CP_FIFO_BASE_LO**:
`VideoCommon/CommandProcessor.h:64` identifies offset `20`; `CommandProcessor.cpp:35-48`
resets CPBase and GP enables to zero. `128-143,159-163` directly map the low halfword and
apply **`FFE0`**, independently of CP control. The observed `9F60` remains `9F60`; a
low write of `9F7F` reads back as `9F60`. No reusable CP MMIO implementation exists in the
pinned C runtime/shared host, so WW05 extends the existing scalar FIFO register selector
with exactly this observed address and halfword width. PI stores still require words.
One shared masked-write path retains configuration only; it does not start GP reads/link,
copy guest FIFO bytes, move WPTR, feed the GX parser or raise an interrupt.

The register tests reuse the PI configuration fixture and explicit width parameterizations,
including zero reset, normal guest halfword memory hook, sourced mask, invalid read/write
widths with unchanged retained state, independent PI/CP values and real credited cycles
without spent-quantum renewal. `logs/cp-fifo-base-low-red.log` reproduces the low-register
refusal while PI cases pass. `logs/cp-fifo-base-low-host-final-green.log` passes **315 host
tests** on Linux AArch64, with extended parameters rather than an inflated method count.
The main-loop continuation remains a clearly synthetic FIFO fixture, not game acceptance.

Other CP FIFO registers, CP enables and FIFO reset remain refused. The documented WPTR
progress limitation is still open and belongs to the authentic gather/FIFO path. Strict
opcode/bytes/PC/completion-PC GX validation, pinned module/CPU ABI and static archive are
unchanged. The next clean source-bound Vita build/trial will check authentic continuation;
physical Vita, Windows CI, rendering, FPS, runtime REL and guest disc access remain unverified.

## Real CP base-low continuation and observed end-low register

Run `9f6d2ee01ffb43b7adbd252f7d0ee3b1`, source
`2b2f4faed52eb60acc28ab239b1aa8bdddd0dae4`, passes CP FIFO base-low and reaches
**`CC000024 = 9F5C`**, halfword PC **`80320F8C`**. The closed guest **FAIL** has
**64,248 edges, 7,140,042 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0`,
`rendered=0`. Build status zero in 123.234 s; archive unchanged. Both owned phases timeout
at 60 s, TERM/exit -15, `waited=true`: process **124/INCOMPLETE** remains separate.
`logs/cp-fifo-base-low-evidence-2b2f4fa.json` independently rechecks source/request/package/
report bindings and the closed LER journal. SHA-256:

- VPK: `2e77759df617769a0c6d3b9ab54a1f496183f063e2bb58b71d181d1fae2ddf46`;
- report: `db443092c03b3ca60c6588b21cae0827b9fdb55496eaa21e9e1afecaaa4c01b1`;
- result: `5a05bf7666de424c5675d451a7754735a699dabd2e26050e37bea5afc694140a`;
- log: `e53c5af186f23140c23810e0dd46dfbad9baa2e8b8124233c165a3eafcb1c81d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies **CP_FIFO_END_LO** at offset `24`
(`VideoCommon/CommandProcessor.h:66`), resets CPEnd to zero
(`CommandProcessor.cpp:35-48`) and directly maps its low halfword with **`FFE0`**
(`130,144,159-163`). The observed `9F5C` must read back as **`9F40`**. WW05 adds only
this observed halfword address to the existing scalar register selector, with independent
CP end storage and the existing shared mask/width/cycle path. No GP activity, FIFO copy,
pointer advancement, parser input, reset or interrupt is created.

`logs/cp-fifo-end-low-red.log` reproduces end-low refusal while the preceding PI/base-low
parameters still pass. `logs/cp-fifo-end-low-host-green.log` passes **315 host tests** on
Linux AArch64; explicit parameterizations now include CP end-low reset, actual guest
halfword memory hook, mask, invalid-width retained-state preservation, independent CP/PI
values, credited cycles and exhausted-quantum closure. Synthetic main-loop continuation
does not count as genuine GX acceptance. The shared HSP trace fix also compiles in the
clean `2b2f4fa` Vita build; the guest reaches the same authentic CP path without regressions.

Unobserved CP registers/enables and FIFO reset still fail. The known static-WPTR limit
still requires the real gather/FIFO memory path. Module/CPU ABI, `BlueWakeRelData` alignment,
pins, static archive and strict GX opcode/bytes/PC/completion-PC validation are unchanged.
Authentic end-low continuation awaits the next clean source-bound build/trial. Physical
Vita, Windows CI, rendering, FPS, runtime REL and actual guest disc/file access remain unverified.

## Real CP end-low continuation and empty distance initialization

Run `7052682e0f274e9393c5b40219f4d801`, source
`827a97ba898da3a549f990f8424a6735d4f1382e`, passes CP FIFO end-low and reaches
**`CC000030 = 0000`**, halfword PC **`80320F98`**. Its closed guest **FAIL** has
**64,248 edges, 7,140,042 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0`,
`rendered=0`. Build status zero in 123.323 s; archive unchanged. Both owned phases timeout
at 60 s, TERM/exit -15, `waited=true`; process **124/INCOMPLETE** is distinct.
`logs/cp-fifo-end-low-evidence-827a97b.json` independently rechecks bindings and LER.
SHA-256:

- VPK: `e69ee90817b9a751367be9e606978d8a09d05531a3573abe4cdeda0051d98aff`;
- report: `95dd6d8df8e0a6bda70cf5b023e74c26f1f99525a5a44f3908dfc0f2a8122027`;
- result: `f9a9fe97b05d850c123d52859ff36185c0eecae0e22f186b6bafa971e1c1b862`;
- log: `e53c5af186f23140c23810e0dd46dfbad9baa2e8b8124233c165a3eafcb1c81d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies **CP_FIFO_RW_DISTANCE_LO** at offset `30`
(`VideoCommon/CommandProcessor.h:72`). Distance and GP enables reset to zero
(`CommandProcessor.cpp:35-48`). The low write directly updates the low halfword masked by
`FFE0` (`255-257`); threaded low reads derive real pointer distance (`232-248`), while the
single-threaded low read uses the retained distance (`250-254`). Distance-high writes also
synchronize/run the GPU (`290-296`). This is not the same complete contract as inert base/end
configuration: real distance/progress requires the gather/consumer path.

WW05 therefore accepts **only the observed zero halfword low-distance initialization,
before any FIFO byte**. With all CP enables already disabled/refused, this leaves the sourced
empty reset state unchanged. It executes through the normal memory/MMIO hook, with credited
cycle observation and refresh intact. It neither skips the instruction nor sets PC, creates
queued bytes or discards parser input. Nonzero values, other widths, any low-distance read,
distance-high access and zero writes after observed FIFO input still fail explicitly.
No pretend distance readback or GPU consumption is introduced.

`logs/cp-fifo-distance-low-red.log` reproduces zero-init refusal while control cases pass.
`logs/cp-fifo-distance-low-host-green.log` passes **318 host tests** on Linux AArch64.
The disabled-control fixture is reused and parameterized for low-distance stores, rejected
values/widths, repeat zero stores and real credited cycles without exhausted-quantum renewal.
Separate tests retain pending synthetic FIFO bytes on a refused zero write, refuse distance
reads and prove unsupported distance cannot reach the main-loop FIFO fixture. Synthetic PASS
is not authentic GX acceptance. The next clean build/trial checks real continuation.

Static WPTR, DVD/CMake integration, Windows ARAM verification and final test-count confirmation
remain tracked above. Strict GX validation, pins, ABI, `BlueWakeRelData` and static archive
are unchanged. Physical Vita, Windows CI, rendering, FPS, runtime REL and actual guest
disc/file access remain unverified.

## Real empty-distance continuation and observed CP write-pointer low

Run `e35110cd6cc44b7890d2f463fd12963c`, source
`f1166126230311193ee64d2ba0f3bb2be8bc183f`, passes the empty low-distance initialization
and reaches **`CC000034 = 9F60`**, halfword PC **`80320FA4`**. The closed guest **FAIL**
has **64,248 edges, 7,140,042 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`,
`live=0`, `rendered=0`. Build status zero in 123.931 s; archive unchanged. Both owned
phases timeout at 60 s, TERM/exit -15, `waited=true`; process **124/INCOMPLETE** remains
separate. Bindings and LER are rechecked in
`logs/cp-fifo-distance-low-evidence-f116612.json`. SHA-256:

- VPK: `e576234387719e342ce86fc056f51b728bcaf5a183ec306295f7e69d659f7335`;
- report: `5336ed42089ead2f31fc7685e076c94403d83bc05d8654dc9dd3369bf18f2995`;
- result: `002e8fca73df49bc53140bb0c12cb96d00e1bdbef4924848376f1beaeffc15c6`;
- log: `e53c5af186f23140c23810e0dd46dfbad9baa2e8b8124233c165a3eafcb1c81d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies **CP_FIFO_WRITE_POINTER_LO** at offset `34`
(`VideoCommon/CommandProcessor.h:74`), resets CPWritePointer to zero
(`CommandProcessor.cpp:35-48`) and directly maps its low halfword with **`FFE0`**
(`130,152,159-163`). WW05 adds only that observed halfword to the existing scalar FIFO
register selector. Its independent host-owned configuration retains `9F60` unchanged;
there is no CP enable, FIFO copy, parser feed, pointer advancement or interrupt side effect.
The known progress limit now explicitly covers this CP write pointer too: authentic linked
gather bursts advance it in the pin (`CommandProcessor.cpp:339-386`). This is not implemented
by scalar reads or configuration stores and must accompany the genuine gather/FIFO seam.

The tests reuse existing parameterizations and one shared data-only register descriptor in
`probe_support.h`, replacing duplicated address/value/width selections in host and main-loop
fixtures. This test-only table is not runtime dispatch, does not skip guest code and is never
packaged as game data. `logs/cp-fifo-wptr-low-red.log` reproduces write-pointer-low refusal
while earlier configuration cases still pass. `logs/cp-fifo-wptr-low-host-green.log` passes
**318 host tests** on Linux AArch64, with extended register cases for reset, actual guest
halfword hooks, masks, invalid-width retained-state preservation, independent values and
credited-cycle/spent-quantum behavior. Synthetic main-loop PASS is not authentic GX evidence.

Unobserved CP accesses/enables and reset remain refused. Strict GX validation, pins, ABI,
`BlueWakeRelData` alignment and static archive are unchanged. Authentic continuation awaits
the clean source-bound build/trial. DVD/CMake and Windows ARAM verification remain tracked;
physical Vita, Windows CI, rendering, FPS, runtime REL and actual guest disc/file access
remain unverified.

## Real CP write-pointer-low continuation and observed read-pointer low

Run `4ae5704882bb4a519ca717c42c141e7f`, source
`1fc10014e7649f9f00f275cf9b414b99b0737fa4`, passes CP write-pointer low and reaches
**`CC000038 = 9F60`**, halfword PC **`80320FB0`**. Its closed guest **FAIL** has
**64,248 edges, 7,140,042 cycles**, zero FIFO bytes, `GX_FRONTIER complete=0`, `live=0`,
`rendered=0`. Build status zero in 124.883 s; archive unchanged. Both owned phases timeout
at 60 s, TERM/exit -15, `waited=true`; process **124/INCOMPLETE** remains distinct.
`logs/cp-fifo-wptr-low-evidence-1fc1001.json` independently rechecks bindings and LER.
SHA-256:

- VPK: `0db448e3a851afcd76d0e025fc42326efdd69564b56ad76c666afeb237b63388`;
- report: `923a3644fa544213b97f6a912f8e4597b0d09ccf77762e7db6f750a3f8141d31`;
- result: `525edfaa0645e04f3ed36710b4a6326168b0e5fefb45aee3635fd2c3eeb409df`;
- log: `e53c5af186f23140c23810e0dd46dfbad9baa2e8b8124233c165a3eafcb1c81d`;
- LER: `169da8b7a0e0d3067e984a0701130bdf9916ce1c09ccf887d9004ec9f413b079`.

The pin identifies **CP_FIFO_READ_POINTER_LO** at offset `38`
(`VideoCommon/CommandProcessor.h:76`), resets CPReadPointer to zero
(`CommandProcessor.cpp:35-48`) and writes its low halfword with **`FFE0`** (`298-302`).
Without a GPU thread the low read directly uses CPReadPointer; with a GPU thread it uses
SafeCPReadPointer. WW05 has no GPU worker and cannot enable GP reads/link, so its bounded
configuration route uses the sourced single-threaded form, not a fake safe-pointer snapshot
or asynchronous completion. The observed `9F60` stays `9F60` in independent CP read-pointer
storage, selected through the existing scalar width/mask/cycle path.

This does not consume FIFO data, advance either pointer, reset gather state, feed the GX
parser or raise an interrupt. Genuine consumption and safe-pointer synchronization remain
part of the later FIFO backend; they must not be inferred from this stored configuration.
Unobserved high-pointer accesses/enables and FIFO resets remain explicit failures.

`logs/cp-fifo-rptr-low-red.log` reproduces read-pointer-low refusal while preceding register
parameters pass. `logs/cp-fifo-rptr-low-host-green.log` passes **318 host tests** on Linux
AArch64. The shared test descriptor/parameterizations now cover this reset/store/mask,
normal guest halfword memory hook, invalid-width retained-state preservation, independent
CP/PI values and credited-cycle/spent-quantum behavior. Main-loop fixture PASS is synthetic,
not real game/GX evidence. Clean source-bound compilation and authentic continuation are next.

Strict GX validation, pins, ABI, `BlueWakeRelData` and static archive are unchanged. The
documented dynamic-pointer, DVD/CMake, Windows ARAM and final-test-count debts remain open.
Physical Vita, Windows CI, rendering, FPS, runtime REL and actual guest disc/file access
remain unverified.
