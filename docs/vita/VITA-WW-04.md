# VITA-WW-04 — bounded memory diagnostic

**Accepted bounded diagnostic delivery: REVOIR, not execution PASS or physical-platform GO.**
The authorized Vita3K pair ran at source `6973b1b7980dab3abee602fb327fb84deb410e04`: REAL PASS,
SDK FAIL, strict closed report valid, **125 / INCOMPLETE**, reported semantic **FAIL**. The actual
process budget is **2/2, exhausted**; there is no retry, emulator fix or further target run.
Review rework 1 corrects the block-info structure-size blocker. The old `24d044b` package/pair is
**not eligible**. Its proofs are preserved; the corrected checkpoint uses the incompatible, strict
`bounded-v2` report method. Finalization changes **only this document**; implementation, tests,
protocol, pins and oracle remain identical to the executed `6973b1b` revision.
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

Scenario 2 runs **sequentially after scenario 1 has released its owners**. Its planned sequence requests one named
SDK block at a time: `ww04-user` 4,096 bytes, `ww04-cdram` 262,144 bytes, and `ww04-phycont`
1,048,576 bytes. Thus at most **one** SDK block is live (below the approved maximum of two),
and its upper requested SDK maximum is 1,048,576 bytes if every preceding guard succeeds.
The actual campaign stopped after USER; its observed SDK maximum was **4,096 bytes**, not 1 MiB.
This is not an exhaustion/stress test.
UID, base-return status, base alignment, returned block-info structure size, mapped size, read/write access bits, block type,
write/read pattern and free-return status are checked. All raw return values, mapped sizes and
flags are reported. Base/info failure still frees the acquired UID; allocation failure never frees
an unowned UID. Failed free retains the live owner in the report, returns failure and stops further
allocation; it does not pretend cleanup succeeded or retry an unknown disposition.

The native adapter initializes `SceKernelMemBlockInfo.size`, preserves the **raw API result** and
**returned size** separately, and requires `sizeof(SceKernelMemBlockInfo)` before reading its mapping
fields or allowing a pattern write. The scenario checks the returned size against that SDK-derived
requirement too. A successful API result `0` with an incorrect size is a semantic failure, not a
fabricated negative API code: the acquired UID is freed normally, subsequent allocations stop, and
the closed report has terminal FAIL/nonzero. `BLOCK` records expose `info`, `info_size` and
`info_required`; host SDK SKIP retains UNKNOWN for all three. The copied VitaSDK header asserts
the target structure is **0x18 / 24 bytes** at `sysmem.h:38–46`, which the target parser requires.

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
Candidate `SceSysmem.cpp:218–227` copies a block descriptor into the returned info structure. This
is source-only context for the observed descriptor fields, not unique attribution of the opaque
copied binary's `access=0` or accounting behavior. No source/Boost provenance gap is resolved here.

The accepted architect preflight used Cloudpunk's reader/owner failure patterns and Xenogears'
phase/artifact protocol. This implementation copies no donor heaps, renderer budgets or player data.
Xenogears' `vita/MILESTONE-RETURN-PROTOCOL.md:35–62` was read for fresh-run/phase distinctions; its
execution/performance figures are not evidence for this diagnostic.

## Closed report and protected paired lifecycle

The distinct 26-line `MEM` schema binds the fresh token, compiled source SHA, package SHA-256,
`method=bounded-v2`, `phase=guest` and platform. Ordered ABI/reservation/UNKNOWN, four OWNER,
twelve SNAP, REAL, three BLOCK, SDK and terminal END records are mandatory. Literal owner sizes,
target ABI, touched bounds, types, raw-snapshot validity, live/peak totals and terminal verdicts are
independently checked. HOST reports cannot certify a VITA guest. Truncated, stale, excessive,
out-of-order or dishonest evidence is rejected.
Returned block-info size must equal the public SDK's 24-byte target requirement before a BLOCK/SDK
can claim PASS. The previous method is rejected, rather than accepted without these observations.

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

## Authorized actual campaign — accepted negative evidence

The coordinator authorized exactly the two owned processes after the targeted review passed.
This historical command ran once at **6973b1b**; it is recorded for reproducibility, **not permission
to rerun it**. No help/version, extra GUI probe, trace instrumentation, retry or third process ran.

```sh
STACK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack
PYTHONDONTWRITEBYTECODE=1 python3 -B scripts/vita/armv7/mem_launch.py --two-phase \
  --runtime "$STACK/vita3k/Vita3K" \
  --runtime-sha256 cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30 \
  --runtime-source-revision a32b6aedb619ca1358758679466bd780d9443059 \
  --libraries "$STACK/vita3k-env" --firmware "$STACK/firmware" \
  --wayland-socket /run/user/1000/wayland-0 \
  --build-evidence "$PWD/build-vita-ww-04-rework1-final/mem-evidence.json" \
  --out "$PWD/build-vita-ww-04-rework1-final" --timeout 30 --execute-reviewed
```

The new exclusive run was
`/tmp/opencode/ww04/build-vita-ww-04-rework1-final/vita3k-run-da813f5fdf5141bcbbe5761b6ed460f7`.
Its protected `phases/{install,guest}/evidence/plan.json` hold exact argv/mount/environment identities.
`evidence/observation.json` preserves the raw observations and checks; `evidence/manifest.json`
covers **2,168 typed entries / 1,643 file hashes**, excluding the manifest itself to avoid self-hash
recursion. The coordinator independently checked the manifest and strict report (validator return 1).

| Historical identity | Value |
| --- | --- |
| Source | `6973b1b7980dab3abee602fb327fb84deb410e04` |
| Executed VPK | `fc8a16e617c749d00a0de55c864689dfabd233ecf29725c003aa5fcea8767fb4` |
| Installed SELF, 103,408 bytes | `9c1ced92c04c01360c740af55861791abce37df8e5e0c5a6538d851a4e5f829c` |
| Installed SFO, 912 bytes | `b14c04cd99f5c6f7bfa901a05578ba7e0041327362873ad582798fe5f2ca6983` |
| Build evidence | `82c8a60475e4eab9d13ba193e5b72cdd57201e9dfd70d0b42c2cd12e66218aa0` |
| Guest token | `ed242ec5f36c4016a5824cd587376b46` |
| Closed report, 2,556 bytes / 26 lines | `93d8d918b6d44fd5a4ff634fe669bc281e66fdf507ee82f031304ae9d2b55ced` |
| Full typed/hash manifest | `8e0e994ad9044d32b686de06c50b2bac4b7443cb7da11808dda8bf22ffb0b2d8` |

Installer **1/2** timed out with status **124**, actual exit **-15**, PID **90740**, waited, total
duration **30.004662 seconds** including cleanup after its 30-second absolute deadline. Preparation
was separately **VERIFIED**: installed title contents, SELF/SFO bytes, type, owner and single links
matched exactly; no config parse, crash or input-identity error was accepted. Only after full owned
stop/reap and canonical config restoration was the new guest token/request created in the same profile.

Guest **2/2** published a valid, closed/flushed `MEM` report with `method=bounded-v2`, `phase=guest`,
`platform=VITA` and the identities above. Its terminal was **`END real=PASS sdk=FAIL failures=1`**.
After bounded natural-exit grace, the owned runner stopped Qt: status **125**, actual exit **-15**,
PID **90777**, waited, total duration **0.824540 seconds**, `stop_reason=validated_report`.
The collection remains **INCOMPLETE / semantic UNAVAILABLE**, preserving **reported_semantic FAIL**.
Report hashes before and after shutdown matched; no partial publication remained. This is accepted
bounded **negative diagnostic delivery (REVOIR)**, not a natural-exit success or physical-platform GO.

REAL observed the four acquired/touched owners in the method table above: maximum **58,736,704**,
pattern-written/read-verified subset **918,016**. `pattern=1`, `alias=1`, `bounds=1`, `live=0` and
two alias views passed. The 96 MiB heap container must not be added again to these inside allocations;
CPU object 3,496 is a subset of the 262,144-byte worker reservation. None is a residency or native
high-water measurement.

USER allocation returned **UID 2**; base API return **0**, info API return **0**, returned/required
structure size **24/24**, mapped **4,096**, type **0x0c20d060**, access **0**, free return **0**.
The base field is an **API status**, not pointer address zero; the actual address is **UNRECORDED**.
The base-pointer/alignment guards passed before the info query. Size validation passed, but access
did not establish SDK read/write permissions: public `sysmem.h:48–52` defines R=4 and W=2, required
bits **6**. The correct fail-fast guard refused the pattern write: **touched=0**, `pattern=0`, then
freed the acquired UID normally and stopped later allocations. `access=0` is not proof that physical
memory is inaccessible; no unsafe write or fabricated permission value was used to test that theory.

CDRAM and PHYCONT were **NOT ATTEMPTED**. Their initialized `-1` return and `0` size/type/access fields
are sentinels, not observed API failures, allocations or measurements. Their before/live/after snapshots
are explicitly **UNKNOWN**, not fabricated zeros. SDK observed requested maximum **4,096**, maximum
simultaneous blocks **1**, final **live=0**. Native high-water, largest block, physically available
memory, code/game boot and scene-transition requirements remain **UNKNOWN**.

| Raw snapshot observation (bytes) | USER | CDRAM | PHYCONT |
| --- | ---: | ---: | ---: |
| REAL before, live and after; USER before | 167,772,160 | 117,440,512 | 27,262,976 |
| USER live | 167,768,064 | 117,440,512 | 27,262,976 |
| USER after free | 167,768,088 | 117,440,512 | 27,262,976 |

Each available query returned API **0**, structure size **16**, validity **1** under the documented
API/structure checks, not a physical plausibility certification. USER decreased **4,096** on
allocation but increased only **24** on free, leaving a reported-counter debit **4,072** despite
successful free and zero owned live blocks. That is consistent with the candidate source's suspected
accounting defect, but mapping remains **UNVERIFIED**: no unique emulator causality, measured physical
leak, physical pool limit or available game-RAM budget is established. The raw values are not normalized.

Both phases emitted `user-id: 00`, SHA-256
`321eb130dcf8d38a639ab67b16f6eb9eccf52a80e0c0c3f97fb21ada47266eb9`.
Preparation recorded canonical **c11fa8…** restored **before guest**. After guest the config again
contained `00` with the emitted hash above; the **final config is not claimed canonical**. Private
user data was retained. Owned leaders/groups were absent and waited; the private PID namespace was
torn down, including detached members. Runtime/assets, read-only libraries/FW, source/staged VPK,
installed payload, pins and previous proofs remained intact. Both worktrees were clean; integration
was still `b4319477d8e5ff26be7a44be6a963f33340a4852`. No player data, code, protocol or pins were changed.

05's real-game footprint comes after 08's FP work; 02's conservative renderer prototype must not
credit doubtful emulator counters as available RAM. No emulator repair or new campaign is part of
04. Physical testing, actual game/FPS and scene transitions require separate future work.

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

The executed implementation checkpoint is **6973b1b**; its complete checks were **125 fixture tests**,
**71 host FP cases**, **53 direct baseline tests**, CTest **1+5+3+MEM1**, four cross-built VPKs, audit,
attribution and clean/pinned-source checks. Its `build-vita-ww-04-rework1-final`,
`build-vita-ww-03-ww04-rework1-final` and `build-vita-ww-01-ww04-rework1-final` evidence is preserved.

The final delivery commit changes only this document. This is the TDD **documentation-only factual
validation exception**, not a new behavior change or an artificial red/green test. Historical report,
hash/identity, raw ledger, config transition and cleanup facts above were checked against the recorded
evidence. The same required host/tests/build checks are rerun at the immutable final delivery SHA,
whose identity is recorded in new ignored `build-vita-04-doc-final`, `build-vita-03-doc-final` and
`build-vita-01-doc-final` evidence. A tracked diff containing only `docs/vita/VITA-WW-04.md` and equal
`vita/feasibility/armv7` / `scripts/vita/armv7` Git trees demonstrate implementation equivalence to
6973b1b. No executed artifact, report or manifest is overwritten or relabelled as final-SHA evidence.
The build embeds the new source SHA, so its newly hashed SELF/VPK metadata differ; **the new final-SHA
packages are NOT RUN in Vita3K**. Doc-only equivalence does not invalidate the historical observations,
but does not pretend the new package was executed. The exhausted 2/2 budget is not reset by a doc commit.

Rework 1's genuine focused red used the copied native compiler and copied SDK with
`python3 -B -m unittest discover -s vita/feasibility/armv7 -p test_mem_info.py`. The real native
adapter was compiled into a host-only fault probe: SDK success `0`, incorrect returned structure
size, otherwise valid mapping/access/type. Old code returned semantic success, allocated/freed all
three blocks and wrote **20,671 changed bytes**. On this AArch64 host the SDK structure has a native
pointer, so `sizeof` is 32 and the injected returned size was 31; this is not an ARM/Vita measurement
or an arbitrary substitute ABI. The ARM target's public SDK type remains 24 bytes.
The same test became green: one allocation/free, no writes, live zero, raw API result still `0`.
A separate publication regression verifies closed terminal FAIL/nonzero, both size observations,
no partial publication and no later allocations. Its injected host SDK report is only fault-test
evidence, never accepted as actual HOST/VITA execution evidence. Parser red/green used
`PYTHONPATH=vita/feasibility/armv7 python3 -B -m unittest
test_mem_report.MemoryReportTest.test_target_info_size_observation_is_required`; the old parser
rejected the new required observation contract, then the strict v2 parser accepted it. Additional
parser regressions reject a claimed SDK PASS with bad size and preserve raw success `0` in an honest
terminal FAIL. Existing allocation/base/info/free failure tests remain unchanged in behavior.

```sh
STACK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack
CC="$STACK/bin/aarch64-conda-linux-gnu-gcc" VITASDK="$STACK/vitasdk" \
  PATH="$STACK/bin:/usr/bin:/bin" PYTHONDONTWRITEBYTECODE=1 \
  python3 -B -m unittest discover -s vita/feasibility/armv7 -p 'test_*.py'
python3 -B scripts/vita/armv7/mem_build.py --vita --out build-vita-04-doc-final \
  --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" --cmake "$STACK/bin/cmake" \
  --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
python3 -B scripts/vita/armv7/fp_build.py --vita --out build-vita-03-doc-final \
  --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" --cmake "$STACK/bin/cmake" \
  --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
CC="$STACK/bin/aarch64-conda-linux-gnu-gcc" VITASDK="$STACK/vitasdk" \
  PATH="$STACK/bin:/usr/bin:/bin" PYTHONDONTWRITEBYTECODE=1 \
  python3 -B scripts/vita/armv7/build.py --vita --out build-vita-01-doc-final \
  --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" --cmake "$STACK/bin/cmake" \
  --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
bash scripts/audit_repo.sh
```

Also run the eight direct baseline Python files listed in 03 (53 tests), attribution for the delivery
range, diff/clean/scope checks and pristine dependency checks. Baseline includes unchanged 71 host
FP cases and CTest 1+5+3; MEM adds its real ownership CTest and focused host scenarios. The
coordinator already checked overlap before delegation; this worktree sees only `origin/main` and
its HEAD alias, and `gh` is unavailable. No push, integration, Trello closure or other-repository
mutation is performed. Physical Vita, Windows CI, full game and final-SHA VPK execution are **NOT RUN**;
the actual MEM guest observation belongs only to the historical 6973b1b campaign above.

## Manual physical-Vita protocol (not executed)

After separate approval, copy only this synthetic VPK through VitaShell manually and install title
BWWM00004. Create `ux0:data/bluewake-ww-fixture/mem.request` with a fresh token and the exact
checkpoint source/package identities after confirming there are no existing `mem.*` outputs; archive
old diagnostic outputs manually rather than deleting player data. Run once, retain the closed report
and record actual device/model, firmware, source/package hash and platform. Compare snapshots and
live cleanup within each pool; do not infer the largest block, physical stack high-water, game memory
or scene margins. A failure is evidence to review, not permission to retry stress allocations or alter
runtime/FP code. There is no automatic transfer, game asset or save access in these scripts.
