# VITA-WW-05 — private Wind Waker startup host

**Build/preparation delivery only. Real guest execution is gated and NOT RUN.**
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
  Composite O2 and its existing three O1 fallbacks, Cortex-A9/NEON/hard-float,
  `-ffp-contract=off`, `-fno-fast-math`, stack-usage files and a link map. Desktop defaults are untouched.
- New build/inspection/staging/report entries are in `scripts/vita/ww05/`. Nothing in
  `scripts/vita/armv7/` or the feasibility fixtures is modified. Existing pin checking, platform
  worker/log/report publication, private profile, firmware copying and bwrap construction are reused.
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
Known PI/VI/PE/DI/EXI/SI register operations use the pinned C models with width/range guards.
An unimplemented register, SPR, cache operation, instruction, host call, desktop interception,
interrupt delivery, DVD command or EXI/SI transfer stops with reason/address/PC. There is no
fabricated DSP/controller/DVD completion, arbitrary zero-valued service or instruction advance.
Audio/DSP, CP hardware service and later rendering are not implemented. These are material risks
of stopping **before** GX, which only the separately authorized run can resolve.

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
any proposed load. The source SHA is embedded after commit; final-SHA artifacts are not described
as executed. Packaging timestamps need not be bit-reproducible; the exact built bytes are hashed.

- The reused platform reserves a **96 MiB newlib heap** and **256 KiB worker stack**.
- Estimated MEM1/VM/cache backing requests inside that heap are 25,165,824 / 33,554,432 / 16,384
  bytes. Exported initial REL aliases add **1,542,820 bytes**; the complete acquired ledger is
  recorded only during execution. The host CPU/GX/parser object is another allocation inside the
  same heap, not a second independent reservation. DOL loader's transient request is 3,822,272 bytes.
- Real compiled ELF/section/PT_LOAD and `.su` frame sizes are static measurements, not allocations
  or residency. Actual game allocations/pool peak/native stack high-water remain **NOT OBSERVED**.
  The 04 snapshots are not credited as available physical RAM. GPU resources are absent.
- An abandoned O0 pilot exposed a 172,616-byte generated frame; it is not load-eligible. O2 uses
  the existing composite recipe, not a translator/FP patch. Its final frame/mapping evidence must
  be reviewed against the existing bounded native call depth and the reservation before execution.
  No 512 MiB nominal cap or new physical-platform GO is inferred.

## Authorization proposal — stage only, then STOP

After the clean delivery build, staging prepares a new private profile with protected evidence:

```sh
python3 -B scripts/vita/ww05/stage_plan.py --wayland-socket /run/user/1000/wayland-0 --timeout 60
```

This entry has **no execution option or launcher entry point**. It creates no guest request and
invokes zero Vita3K processes. A permanent exclusive `build-f5/ww05/campaign-reservation.json`
binds the source/run/plan hash and forbids another preparation/retry. The proposed authorization is
`VITA-WW-05:<source-sha>:<run-uuid>:startup-gx:1,2`, plus the coordinator's independently reviewed
root-plan SHA-256. Neither is inferred from approval of another ticket.

The saved `vita3k-run-<uuid>/evidence/plan.json` contains exact argv, mounts, environment, source,
package, copied runtime/assets/libraries/firmware/config and footprint identities. Exact payload commands:

1. `/stack/vita3k/Vita3K --console --backend-renderer OpenGL ww05.vpk`
2. `/stack/vita3k/Vita3K --backend-renderer OpenGL --installed-path BWWG00005`

**Proposed budget: exactly one install plus one guest, each ≤60 s absolute monotonic deadline**,
starting before Popen, with owned TERM/KILL/reap cleanup. This is a ceiling, not a retry allowance;
the larger real module justifies more installation/startup allowance than the tiny diagnostics.
A valid closed report gets ≤250 ms natural-exit grace inside the guest deadline, then only the
owned group is stopped. Status 124/125 stays INCOMPLETE, separately retaining reported frontier
PASS/FAIL. No help, version, probe, trace, second guest or extra GUI process is proposed.

Between phases: fully stop/reap the installer; verify exact installed SELF/SFO/DOL bytes, private
owner/single links/directory contents and all readonly identities; reject crash/config errors;
preserve emitted config, accept only canonical or its `user-id: 00` serialization and restore
canonical; only then exclusively create a fresh request. An unavailable service produces a bounded
negative report, not permission to add a new service or rerun.

Containment reuses the established `bwrap --unshare-all --die-with-parent --new-session` pattern:
readonly `/usr`, `/etc`, copied runtime/assets/libraries and private `vs0/sa0`; private home/XDG/
work directories, PID namespace, `/proc`, synthetic `/dev`, temporary `/tmp`; cleared environment,
DEVNULL stdin, unlimited RLIMIT_FSIZE, no arbitrary address-space cap, no network/GPU/session bus/
player home. Only the named Wayland socket is proposed. Guest socket paths use short `/run/ww03`
paths (≤107 bytes), independently of the long physical evidence directory.

Integration, review and real execution authorization remain with the coordinator. No push, PR,
Trello/backlog edit, other worker/evidence-directory mutation or actual target run is part of this delivery.

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
The WW05 host suite currently has **35 tests**. Final-SHA logs retain exact counts/commands for these,
native runtime CTest, 71 native FP cases and the eight direct baseline files. Audit, attribution,
pin/scope/diff and clean-worktree checks accompany delivery. Actual Vita3K, physical Vita, Windows CI,
rendered scene and game performance are **NOT RUN**; compilation and synthetic tests are not game boot.
