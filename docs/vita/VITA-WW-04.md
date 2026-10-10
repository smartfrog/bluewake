# VITA-WW-04 — bounded memory diagnostic

**Checkpoint: implementation and host/build evidence only; target observations NOT RUN.**
This is neither Wind Waker nor an FP repair. The 03 guest diagnostic remains **56 PASS / 15 FAIL**
(11 rounding, four NI), status **125 / INCOMPLETE**, reported semantic **FAIL**. Its accepted negative
delivery is not an execution PASS or a platform GO. 08 owns FP attribution/repair; game, renderer,
Outset, frame rate and scene-transition certification remain future work.

Scope is `vita/feasibility/armv7/`, `scripts/vita/armv7/` and this document. Runtime sources, product
pins and desktop defaults are unchanged. The real pinned `CPUState`, MEM1, auxiliary VM, locked cache
and shared-alias APIs are reused; there is no substitute CPU. Only small second-caller parameters in
03's package/report/paired lifecycle helpers and a shared pinned-source check were added. FP's
validator, vectors, absolute deadlines, completion policy and default commands are unchanged.

## Method and ownership

`mem_build.py` selects the opt-in CMake `mem` mode. Synthetic title **BWWM00004** has only
`eboot.bin` (SELF) and `sce_sys/param.sfo` in its VPK. The title is parsed as an exact SFO `TITLE_ID`,
not found by substring. No game data, network fetch, console transfer or toolchain installation occurs.

Scenario 1 acquires the actual backings through `cpu_init`, `dol_guest_memory_init`,
`ppc_guest_alias_add` and `ppc_guest_alias_add_shared`. Each unique allocation is counted once:

| Owner | Requested backing bytes | Explicit pattern-written/read-verified bytes | Views |
| --- | ---: | ---: | ---: |
| MEM1 (`CPUState.ram`) | 25,165,824 | 393,280 | 1 |
| Auxiliary VM | 33,554,432 | 524,352 | 1 |
| Locked cache | 16,384 | 320 | 1 |
| Owned alias | 64 | 64 | 2 |
| Sampled simultaneous acquired backing maximum | **58,736,704** | **918,016** | not a pool/residency measurement |

Volatile writes and a separate read-verification pass cover 64 bytes at each 4 KiB boundary and the
last 64-byte edge. The alias is written/read through both real guest views, including a big-endian
word at the first and last valid offsets. The shared view is non-owning. Alias, MEM1, VM and cache
out-of-range checks reject a range crossing the last byte. Alias clear frees the backing once;
auxiliary shutdown and CPU free release their own allocations. Host allocation interception verifies
each of the four allocation failures, earlier-owner cleanup and zero live allocations. This is host
failure injection, not Vita allocator measurement.

The report's `mapped` for these heap-backed owners means the successfully acquired byte-addressable
backing size, not physical committed pages. `peak` is the maximum acquired owner-ledger sample, not
an allocator/process high-water mark. A failed auxiliary initializer can allocate then internally free
a temporary backing before returning; that transient is not sampled by this ledger. `touched` counts
the diagnostic pattern subset only; runtime `calloc` zero-initialization can touch more. CPU object
initialization, library/allocator overhead and executable mappings are outside that backing total.

Scenario 2 runs **sequentially after scenario 1 has released its owners**. It allocates one named
SDK block at a time: `ww04-user` 4,096 bytes, `ww04-cdram` 262,144 bytes, and `ww04-phycont`
1,048,576 bytes. Thus at most **one** SDK block is live (below the approved maximum of two),
and the sampled requested SDK maximum is 1,048,576 bytes. This is not an exhaustion/stress test.
UID, base-return status, base alignment, mapped size, read/write access bits, block type,
write/read pattern and free-return status are checked. All raw return values, mapped sizes and
flags are reported. Base/info failure still frees the acquired UID; allocation failure never frees
an unowned UID. Failed free retains the live owner in the report, returns failure and stops further
allocation; it does not pretend cleanup succeeded or retry an unknown disposition.

Host runs report SDK blocks as **SKIP**, and unavailable pool observations as **UNKNOWN**, never
as zero-byte Vita measurements. The SDK failure-injection adapter is test-only; it is not an emulator
fallback. SDK unattempted blocks after a failure have negative sentinel return fields and unavailable
snapshots, and are FAIL, not successfully measured blocks.

## Reservations, samples and limits

- `platform.c` requests a **96 MiB newlib heap** (100,663,296 bytes). The malloc-backed MEM1/VM/cache/
  alias allocations are **inside** that container, not additional disjoint reservations. Do not add
  96 MiB and the 58,736,704-byte backing total as separate pool consumers.
- The worker requests **256 KiB** (262,144 bytes). This is a stack reservation, not usage/high-water.
  The compiled ARMv7 CPU ABI is pointer 4, object 3,496, ABI version 6, RAM pointer offset 3,428,
  external pointer offset 3,436. Its local object is a subset of the worker stack, not a heap backing.
  Host AArch64 compilation observes pointer 8 and CPU object 3,552.
- `sceKernelGetFreeMemorySize` provides before/live/after **snapshots** for user, CDRAM and physical-
  contiguous counters. Its 16-byte size field is initialized; negative API returns, changed size or
  negative counters invalidate that observation. Raw returned values are preserved even on failure.
  A validity bit says only that these API/structure checks passed; it does not certify physically
  plausible limits. Incoherent positive values/deltas are retained without correcting them.
- No true pool peak, largest contiguous allocation, physical residency or physical Vita limit is
  measured. Those quantities and game peak remain explicitly **UNKNOWN**. No native stack high-water
  API is called: semantics are unverified, and inspected candidate `sceKernelGetThreadStackFreeSize`
  is UNIMPLEMENTED. A fallback zero must not become measurement. Compiled `.su` frames in build
  evidence and 01's host callback-marker span remain separate from native stack high-water.

As a **proposed diagnostic margin**, leave at least 16 MiB of the configured heap unused by explicitly
owned fixture backings. The nominal difference here is 41,926,592 bytes, before unmeasured allocator,
library and transient costs; it is not measured allocatable headroom. No balanced 512 MiB game budget,
30 FPS claim, game boot or old/new scene-resource overlap can be inferred. Boot, a representative
scene and a transition each need their own future ownership/pool/native-stack evidence.

## SDK and candidate-source evidence

The copied SDK's `arm-vita-eabi/include/psp2/kernel/sysmem.h:30–45,59–108,174–180` defines initialized
free-size and block-info structures and the allocation/base/info/free returns; read/write flags are
in lines 48–52. Pool type constants are in `psp2common/kernel/sysmem.h:25,31,48`.

Read-only candidate inspection is **not a verified source mapping for the copied emulator**:
`/home/fred/projects/xgr-tools/vita3k-src` is at the provenance described in 03. Its
`vita3k/modules/SceSysmem/SceSysmemForDriver.cpp:73–113` supplies the initial 4 KiB / 256 KiB / 1 MiB
alignment candidates. Lines 149–151 set `mappedSize` to allocation size but `size` to the block-info
structure size; lines 264–280 appear to subtract that latter field on free, while lines 155–169 add
allocation size. This is a suspected accounting defect, **not** permission to repair the emulator or
rebalance observed counters. `SceSysmem.cpp:171–210` exposes snapshots with candidate nominal limits;
these are not physical-console measurements. `SceKernelThreadMgr/SceThreadmgr.cpp:1220–1223` returns
UNIMPLEMENTED for stack-free size. Runtime/source mapping and firmware version remain UNVERIFIED.

The accepted architect preflight used Cloudpunk's reader/owner failure patterns and Xenogears'
phase/artifact protocol. This implementation copies no donor heaps, renderer budgets or player data.
Xenogears' `vita/MILESTONE-RETURN-PROTOCOL.md:35–62` was read for fresh-run/phase distinctions; its
execution/performance figures are not evidence for this diagnostic.

## Closed report and protected paired lifecycle

The distinct 26-line `MEM` schema binds the fresh token, compiled source SHA, package SHA-256,
`method=bounded-v1`, `phase=guest` and platform. Ordered ABI/reservation/UNKNOWN, four OWNER,
twelve SNAP, REAL, three BLOCK, SDK and terminal END records are mandatory. Literal owner sizes,
target ABI, touched bounds, types, raw-snapshot validity, live/peak totals and terminal verdicts are
independently checked. HOST reports cannot certify a VITA guest. Truncated, stale, excessive,
out-of-order or dishonest evidence is rejected.

`mem_main.c` rejects preexisting completed/partial files without deleting them, exclusively creates
`mem.partial`, checks every report write, then reuses 03's sticky-error/flush/close/rename publication.
Only a closed, flushed report becomes `mem.report`. Allocation/measurement failures publish a
terminal FAIL and return nonzero; report open/write/flush/close/rename failure returns nonzero without
accepted publication. The source binding is compiled into `mem_identity.h`; a request for a different
source is rejected. The package hash is supplied after hashing the final package, avoiding self-hash
recursion, and verified by the paired install's exact SELF/SFO byte checks.

`mem_launch.py` always stages the two-phase lifecycle and defaults to **zero emulator processes**.
Staging creates neither a guest request nor process evidence. Both plans/evidence directories are
exclusive, privately owned and read-only inside the guest namespace. No guest can rewrite a plan or
collection result. Report traversal is no-follow and bounded to 65,536 bytes; the final regular file
must have the current owner and one link. Captured report bytes are parsed and hashed together.

After explicit independent review and coordinator authorization only:

1. Installer **1/2**, `Vita3K --console --backend-renderer OpenGL mem.vpk`, in a fresh private profile.
   One absolute monotonic deadline starts before Popen, at most **30 seconds**, with a separate owned
   TERM-to-KILL/reap cleanup bound. Preserve installer 124/125 or crash disposition separately from
   preparation readiness. Reject a crash, config parse error, unreaped process or mutated input.
2. Stop/reap the entire owned namespace/group before inspecting installation. Require exactly
   `ux0/app/BWWM00004/{eboot.bin,sce_sys/param.sfo}`, matching package bytes/hashes, regular file type,
   owner and single links, and exact directory contents. Preserve emitted config evidence, allow only
   the canonical config or its `user-id: 00` mutation, restore canonical bytes and retain private user data.
3. Only verified preparation may exclusively create a fresh `mem.request`. Preexisting request,
   partial or completed files are rejected and preserved. Guest **2/2**, **non-console**,
   `Vita3K --backend-renderer OpenGL --installed-path BWWM00004`, uses the **same** private profile and
   its own at-most-30-second absolute deadline. No positional auto-boot, retry/help/GUI/trace probe occurs.
4. A valid publication gets at most 250 ms of natural-exit grace within that deadline. Early owned
   forced stop stays **125 / INCOMPLETE**, late/deadline stop **124 / INCOMPLETE**, even for reported PASS.
   Retain `reported_semantic`, process exit/stop reason, closed-report hash and input integrity separately.
   Natural Qt exit is not promised. A bounded negative REVOIR diagnostic can be accepted by the
   coordinator after closed evidence and cleanup; it is not execution PASS/GO.

Containment reuses 03: `bwrap --unshare-all --die-with-parent --new-session`; read-only `/usr`, `/etc`,
runtime/assets, adjacent public libraries and installed public `vs0`/`sa0`; only the fresh profile's
home/config/cache/data/state/work/xdg-runtime are writable. `/proc`, synthetic `/dev` and temporary
`/tmp` are private. Only the explicitly authorized `/run/user/1000/wayland-0` socket is exposed; no
GPU device, session bus, player/home paths, network, global kill, sudo, ACL or clock change. Sandbox
cwd is `/stack/vita3k`; environment is cleared and rebuilt with the exact values in protected argv.
RLIMIT_FSIZE must be unlimited. `/run/ww03` is the shared helper's sandbox path, not an FP identity.
The public config template SHA-256 is
`c11fa8feaa4240186f652672643ee1c8d4cbdecbc09b62ac841a936272b67f25`; copied Vita3K ELF SHA-256 is
`cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30`.

## Verification and reproducible checkpoint

Precommit focused commands really ran: `python3 -m unittest discover -s vita/feasibility/armv7 -p
test_mem.py`, `test_mem_report.py`, `test_mem_io.py`, `test_mem_launch.py` and `test_mem_deadline.py`.
Observed red/green loops were missing real-owner implementation, invalid snapshot availability,
missing SDK snapshot accepted as success, HOST allowed as guest evidence, missing last-edge
corruption verifier, dangling partial symlink followed, and incorrect target ABI accepted. Each was
rerun green. Some added contract tests exercise already-reused behavior without an artificial red.
The IO fixture initially lacked its standalone import path; that fixture issue was corrected before
the genuine dangling-symlink behavior red. Fake paired processes test namespace/ownership and
conditional process count only; their reports are never claimed as ARM guest execution.

The code checkpoint is the commit adding this file. Final checks are rerun at that immutable SHA
into new ignored `build-vita-ww-04-final`, `build-vita-ww-03-ww04-final` and
`build-vita-ww-01-ww04-final` directories. Their JSON evidence records the exact source, artifact
sizes/hashes, compiled ELF/ABI/stack evidence and actual host reports; the handoff supplies those
identities. No earlier package is presented as the checkpoint package.

```sh
STACK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack
CC="$STACK/bin/aarch64-conda-linux-gnu-gcc" VITASDK="$STACK/vitasdk" \
  PATH="$STACK/bin:/usr/bin:/bin" PYTHONDONTWRITEBYTECODE=1 \
  python3 -B -m unittest discover -s vita/feasibility/armv7 -p 'test_*.py'
python3 -B scripts/vita/armv7/mem_build.py --vita --out build-vita-ww-04-final \
  --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" --cmake "$STACK/bin/cmake" \
  --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
python3 -B scripts/vita/armv7/fp_build.py --vita --out build-vita-ww-03-ww04-final \
  --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" --cmake "$STACK/bin/cmake" \
  --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
CC="$STACK/bin/aarch64-conda-linux-gnu-gcc" VITASDK="$STACK/vitasdk" \
  PATH="$STACK/bin:/usr/bin:/bin" PYTHONDONTWRITEBYTECODE=1 \
  python3 -B scripts/vita/armv7/build.py --vita --out build-vita-ww-01-ww04-final \
  --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" --cmake "$STACK/bin/cmake" \
  --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
bash scripts/audit_repo.sh
```

Also run the eight direct baseline Python files listed in 03 (53 tests), attribution for the delivery
range, diff/clean/scope checks and pristine dependency checks. Baseline includes unchanged 71 host
FP cases and CTest 1+5+3; MEM adds its real ownership CTest and focused host scenarios. The
coordinator already checked overlap before delegation; this worktree sees only `origin/main` and
its HEAD alias, and `gh` is unavailable. No push, integration, Trello closure or other-repository
mutation is performed. Physical Vita, Windows CI, actual MEM guest and game execution are **NOT RUN**.

## Manual physical-Vita protocol (not executed)

After separate approval, copy only this synthetic VPK through VitaShell manually and install title
BWWM00004. Create `ux0:data/bluewake-ww-fixture/mem.request` with a fresh token and the exact
checkpoint source/package identities after confirming there are no existing `mem.*` outputs; archive
old diagnostic outputs manually rather than deleting player data. Run once, retain the closed report
and record actual device/model, firmware, source/package hash and platform. Compare snapshots and
live cleanup within each pool; do not infer the largest block, physical stack high-water, game memory
or scene margins. A failure is evidence to review, not permission to retry stress allocations or alter
runtime/FP code. There is no automatic transfer, game asset or save access in these scripts.
