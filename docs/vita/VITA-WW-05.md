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
The runtime offers ordinary guest PCs to the optional host-call hook before trying compiled code.
An aligned address in the loaded descriptor's code ranges (including the dispatcher's physical/
uncached aliases) is declined without changing PC or exceptions, so the original guest code runs.
An address outside those declared ranges, including an unsupported native-query selector, still
fails explicitly. This classification does not implement or skip a guest service.
Known PI/VI/PE/DI/EXI/SI register operations use the pinned C models with width/range guards.
An unimplemented register, SPR, cache operation, instruction, host call, desktop interception,
interrupt delivery, DVD command or EXI/SI transfer stops with reason/address/PC. There is no
fabricated DSP/controller/DVD completion, arbitrary zero-valued service or instruction advance.
Audio/DSP, CP hardware service and later rendering are not implemented. These are material risks
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

Each phase has a **≤60 s absolute monotonic deadline**, starting before Popen, with owned
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

The WW05 host suite currently has **51 tests**. Source-bound logs retain exact counts/commands for these,
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
