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
Unknown code, external DSP interrupts, running/queued CPU-mail protocols and the init transfer
still refuse explicitly. No DSP core or audio is implemented. The ARAM DMA increment below
adds real copies and credit-driven busy/interrupt state. ARAM configuration
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

The WW05 host suite currently has **171 tests**. Source-bound logs retain exact counts/commands for these,
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
