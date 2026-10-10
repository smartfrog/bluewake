# VITA-WW-08 — independent scalar VFP / pristine runtime comparison

2026-10-10 · [ticket](https://trello.com/c/N9iec8am) · **negative diagnostic: STOP_CONTROL / REVOIR**

The reviewed pristine baseline pair **1/2** ran once and produced a valid negative report. Independent
scalar arithmetic failed despite correct FPSCR mode write/readback. This is not a runtime repair,
CPU GO, game boot, FPS result or physical Vita result. Actual 08 Vita3K process usage is **2/4, frozen**;
processes **3/4 are forbidden**, not spare attempts for configuration comparisons. No retry or runtime
patch follows from this result. The exhausted 03/04 campaigns must not be rerun. The remaining
05/02/06 product work stays blocked; any further experiment needs a new approved scope and budget.

## Inputs and scope

- Verified integration base: `cef3b1fe9727302934e8034a8140e8afe6e208ab`.
- RecompCore: `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`, pristine before/after.
- DolRecomp: `b8b534591cba8ca7cd43943a655ee6e2591cf5de`, pristine before/after.
- Private references are local `--no-hardlinks` clones in ignored `ref/vita-ww-01/`.
- Authored scope: `vita/feasibility/armv7/`, `scripts/vita/armv7/`, this document only.
  No product pin, desktop default, dependency source, backlog or coordination record changes.
- Branch: `vita-ww-08-fp-control`; human authorship Fred. No push/publication/history rewrite.
  `gh` is unavailable; remote branches show only `origin/main` and its HEAD alias. The coordinator
  supplied the accepted independent preflight and verified integration head.

03's actual synthetic guest result remains **56 PASS / 15 FAIL**, all 71 path checks 1: eleven
directed-rounding failures (two add, six frsp, three call/boundary), four double-NI failures.
Single NI and quantization were positive controls. 04 acquired/touched real backings but failed
the first SDK USER access guard; CDRAM/PHYCONT were not attempted. Its inconsistent free counter,
unknown physical/native high-water/game budget and opaque emulator source/Boost mapping are not
evidence that FP alone explains game viability. No 05/02/06 or playable-game claim follows here.

## Independent ARM oracle and instructions

`arm_vectors.py` contains **32 literal integer-bit control cases**, independently specified for ARM,
not copied from observed runtime/emulator output. It imports no runtime vectors. The unchanged
`fp_vectors.py` remains the separate Gekko oracle for **all 71 original runtime cases**, names,
order, RN/NI modes, expected result bits, quantized outputs and direct/indirect/host-boundary paths.

Authoritative read-only public references (downloaded documentation, not tools, kept ignored):

1. [Cortex-A9 FPU TRM, ARM DDI 0408F](https://documentation-service.arm.com/static/5e8e128288295d1e18d34d1c),
   table 2-5, pages 2-10/2-11: FPSCR RMode bits 23:22 are 00 nearest, 01 +infinity,
   10 -infinity, 11 zero; FZ is bit 24. Scalar VFP obeys these, unlike Advanced SIMD.
2. [ARM ARM, DDI 0406C.d](https://documentation-service.arm.com/static/5f8daeb7f86e16515cdb8c4e),
   A2.7.5 (A2-67): double/single subnormal inputs and pre-rounding subnormal outputs flush;
   VFPv3 preserves the input/result sign of zero. This normative architecture section is used,
   not the older TRM glossary's generic “positive zeros” wording. A8.8.310 (A8-877/878) specifies
   scalar double/single conversions; the four IEEE binary rounding rules define literal ties.
3. Copied SDK `arm-vita-eabi/include/sys/fenv.h:59–63` independently agrees on ARM rounding
   encodings. RecompCore private header FPSCR masks are **Gekko**, not ARM FPSCR encodings.

| Control family | Count | Literal derivation |
| --- | ---: | --- |
| ±addition, four RN | 8 | ±1 plus same-sign 2^-54, a quarter binary64 ulp. +infinity increments positive magnitude; -infinity increments negative magnitude; others retain ±1. |
| ±binary32 conversion, even/odd ties, four RN | 16 | ±(1+2^-24), ±(1+3·2^-24). Nearest chooses even low bit; directed modes choose the adjacent single value. Widening to binary64 is exact. |
| ±double subnormal input, FZ off/on | 4 | Minimum subnormal ×2 gives signed bit pattern 2, or signed zero with FZ. |
| ±double subnormal output, FZ off/on | 4 | Minimum normal ×1/2 gives signed `0008000000000000`, or signed zero with FZ. |

ARM FZ is **not full Gekko NI certification**. Exceptions/status accuracy, all mode cross-products,
NaNs, paired arithmetic and the full native game boundary remain outside this bounded diagnostic.

`arm_vfp.c` uses volatile assembly and compiler memory barriers: `VMRS`/`VMSR`, scalar `VADD.F64`,
`VMUL.F64`, `VCVT.F32.F64` then `VCVT.F64.F32`. Operand/result bits move through integer register
pairs and D/S registers using `VMOV`, not C floating arithmetic or constant-foldable expressions.
All overwritten D registers, including the S-register aliases, are clobbered. No NEON substitution
or soft-float replacement CPU is used. HOST measurements are explicitly **SKIP**, never ARM PASS.

Binary inspection proves these instructions/transfers and the actual runtime lowering: `ni_add`
and `ni_mul` contain scalar binary64 instructions; `force_single` and `ppc_frsp` contain scalar
conversions; the pristine ARMv7 `ppc_fpscr_control_updated` contains **no FPSCR mode write**.
Source-only context is `cpu_interpreter_float.c:75–115,349–357,383–445`; it is not emulator attribution.
ELF attributes show ARMv7/Cortex-A9 hard-float VFP-register ABI. Build logs retain exact objdump,
readelf, nm commands, imports, full disassembly, map, `.su`, generated sources and artifact hashes.
All 43 target compile records preserve `-O2 -ffp-contract=off -fno-fast-math -fstack-usage`
and `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard`.

## Environment isolation and report

The fixture saves initial FPSCR at entry. Each raw control records operands, expected/actual bits,
requested FPSCR, immediate write readback, post-arithmetic FPSCR and exact restoration readback.
Only RN/FZ bits change; all other initial bits are preserved. Arithmetic correctness matters,
not just a successful register write/readback.

Before **every** runtime case, the observer explicitly restores and reads back the initial FPSCR,
then restores/checks it again immediately before the real `ppc_fpscr_control_updated`. It only reads
after that runtime setter and after the emitted operation: it never prepares the guest's requested
mode. The original real `CPUState`, allocation, direct-call helpers, dispatch and result/path checks
are shared with 03 through `fp_runtime.c`. Each runtime case records before/updated/operated/restored
FPSCR. Checked host fenv restoration remains intact. Final/early-error cleanup writes and verifies
the original FPSCR again; no cached “last mode” assumption is used.

Distinct `ARM08`, `method=scalar-vfp-v1`, `phase=guest`, platform/source/token/package identity,
ENV, 32 CONTROL, 71 RUNTIME, RESTORE and END records are mandatory (**107 ordered lines**).
`arm_report.py` independently checks literal cases, preserved requested bits, restoration, paths,
verdicts and counts. Valid raw control FAIL is `STOP_CONTROL`, not mislabeled runtime failure.
Correct controls plus runtime FAIL is `REVIEW_RUNTIME_OBSERVATIONS`, **not automatic patch permission**.
HOST cannot certify a VITA guest. Wrong/stale identity, truncation, duplicate/order changes and
dishonest verdicts are rejected, preserving raw failed observations rather than correcting them.

Small existing seams are reused: real 03 runtime runner; shared 04 request/absence checks;
checked flush/close/rename publication; exact SFO/SELF validation; protected paired launcher.
Request symlinks/non-regular files and preexisting partial/completed output are rejected without
deletion. Exclusive `arm.partial` becomes `arm.report` only after successful write/flush/close.
I/O errors return nonzero and do not publish. Host-only injected APIs are unit tests, never target
measurements; report-I/O failure tests verify their final environment restoration too.

## Protected baseline pair — executed once after review

`arm_launch.py` defaults to staging only: **zero processes, no guest request**. Exact synthetic title
**BWWA00008** has just SELF and SFO. Both phase plans/evidence are host-protected, read-only in the
guest namespace. `--execute-reviewed` cannot stage and immediately execute a new 08 pair.
The separate `arm_execute.py` requires the exact saved plan and its coordinator-authorized
`ARM08:<source>:<run>:baseline:1,2` string **and** independently supplied `--expected-plan-sha256`.
It reads the bounded, no-follow, privately owned root plan once, hashes those exact bytes against
the coordinator's external hash before reservation or subprocess creation, and parses the same bytes.
The expected hash is never derived from the parsed plan or an embedded self-hash.
It rechecks clean checkpoint/pins, commands, deadlines,
protected plans, source/staged package, runtime/assets/libraries and canonical configuration.
An exclusive baseline reservation prevents a second baseline authorization/retry. The original file
`/tmp/opencode/ww08/build-ww08-baseline-reservation.json` remains intact. The coordinator excluded
that exact generated filename through local Git metadata; no source ignore rule or guard was changed.
The conditional 3/4 pair is not implemented by this entry point and is forbidden after this result.

1. Installer **1**, `Vita3K --console --backend-renderer OpenGL arm.vpk`, fresh private profile,
   absolute deadline **at most 30 s**. Installer status and preparation readiness remain separate.
2. Owned stop/TERM-KILL/wait/reap, exact installed SELF/SFO byte/type/owner/link/directory checks,
   raw emitted configuration evidence and canonical restoration. Restore accepted canonical bytes
   even after rejected installer disposition. Only VERIFIED preparation creates a fresh request.
3. Guest **2**, **non-console** `Vita3K --backend-renderer OpenGL --installed-path BWWA00008`,
   same profile, its own absolute deadline **at most 30 s**. No positional autoboot or retry.
4. Closed report is no-follow, regular, current-owner/single-link, bounded to 65,536 bytes. The same
   bytes are validated/hashed. At most 250 ms natural-exit grace stays inside the deadline. Forced
   stop **125** or deadline **124** stays **INCOMPLETE**, even with valid reported PASS/FAIL.
   Natural exit 0 can give COMPLETE semantic FAIL; report and process semantics remain separate.
   Guest stop is followed by input verification and canonical configuration restoration.

Containment is unchanged: copied ELF/adjacent public libraries/assets; installed public vs0/sa0
read-only; `bwrap --unshare-all`, DEVNULL stdin, unlimited RLIMIT_FSIZE, private home/XDG/work,
only the explicitly named minimal Wayland socket. No network, GPU device, session bus, real home,
player data, global kill, permission/clock change, console transfer, help/version/GUI/trace probe.
Compact worktree/output names avoid the Unix-socket path limit. `/run/ww03` is the reused sandbox
mount name, not diagnostic identity. Opaque copied runtime hash remains
`cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30`; canonical config remains
`c11fa8feaa4240186f652672643ee1c8d4cbdecbc09b62ac841a936272b67f25`.
Runtime/source/Boost and installed firmware version/PUP provenance remain UNVERIFIED.

## Historical implementation and review verification

Linux AArch64, copied tool stack only:
`/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack`.
Initial red loops observed missing control API, missing typed report/launch mode, rejected-installer
configuration not restored, missing report-I/O injection and missing fake cleanup audit. Focused
green loops then passed. No full baseline rerun preceded the first red loop.

The reviewed code checkpoint is `fd66ff3d38f4835f71fd198e69b1f7b8bd129b07`. These checks ran at that
immutable checkpoint before its separately authorized target pair:

- **155/155** fixture tests: previous 151 plus four focused reviewed-plan binding regressions.
- Real HOST FP **71/71** in both 03 and 08; 08 independent ARM **32 SKIP**.
- Existing eight host drivers **53/53**; CTest corpus 1, contracts 5, runtime 3, memory 1,
  new fake-control guard 4. HOST real memory PASS, SDK SKIP.
- All four older VPK builds plus new 08 ELF/SELF/VPK; source bindings, hard-float flags,
  scalar instruction inspection, pristine pins, scope/diff, repository audit and attribution.
- At that preexecution checkpoint, no real 08 emulator process had run. Windows CI/hardware,
  physical Vita and gameplay remain untested after the diagnostic pair too.

The original `ab8e3b2f5c1b37927b29616fcc280363ef1c6e99` evidence remains in
`build-ww08/{arm-evidence.json,logs,proof}`, `build-ww01/evidence.json`, `build-ww03/fp-evidence.json`
and `build-ww04/mem-evidence.json`. The reviewed fd66 checkpoint evidence is separately preserved
under `/home/fred/projects/zeldaWWHDRecomp/build-r1/build-proof/` and its `build-{01,03,04,08}` trees.
The manifests retain actual commands, counts, source bindings, instruction and artifact hashes.
Preexecution statements such as **NONEXECUTED** describe that historical checkpoint, not the later
authorized run. Intermediate dirty-source packages and the ab8 stage remain ineligible.

Reproduction: prepend `$STACK/bin` to PATH, set CC to its native GCC and VITASDK to `$STACK/vitasdk`;
run `arm_build.py --vita --out "$PWD/build-ww08" --cc "$CC" --cmake "$STACK/bin/cmake"
--ninja "$STACK/bin/ninja" --vitasdk "$VITASDK" --jobs 2`. The final proof records complete argv.
Staging is not runtime success. The named fd66 baseline below exhausted the permitted baseline;
this reproduction command is a build command, not permission to create or execute another campaign.

## Authorized baseline result

The coordinator independently supplied both the exact authorization string and the reviewed root-plan
SHA-256 `9e7a87dae41b907c4ecd120029cc55334467d4de6194d725f6bd416f486c6d6f`.
The existing saved stage was used without rebuilding, replacing its UUID or making a second request.

| Binding | Observed value |
| --- | --- |
| Executed source | `fd66ff3d38f4835f71fd198e69b1f7b8bd129b07` |
| Run | `6c6f3c20224d478c85bbe5bd2bc983f0` |
| VPK SHA-256 | `84f4f51e2a50997d9897efa44e05ca23ae1ae85dd313e311f0fade1a32f6c51b` |
| Fresh guest token | `1edb49bccb294e518a0de9c1869d493d` |
| Method / platform | `scalar-vfp-v1` / `VITA` guest in Vita3K, **not physical Vita** |
| Report SHA-256 | `18eef2875e2674e4787c24f7e85141b0630fb82ffc6a846f90f538b9f9cd977e` |

Installer 1 reached its configured 30-second deadline: status **124**, raw exit **-15**, waited,
duration **30.001257481 s** including stop/wait overhead. Preparation was independently **VERIFIED**,
not process PASS. Installed `eboot.bin` (146,551 bytes, SHA-256
`196692627cb4e8d3214d20cc02bdcbfac7c5e77069481152f37e5c4fdbbe0aa2`) and `sce_sys/param.sfo`
(912 bytes, SHA-256 `52b86eedd2314a646fc0ae0db0736021bf34557c0d765778b717b4756a34115a`)
matched the VPK exactly, with the required type/owner/link/directory checks. Only then was the fresh
guest request created in the same private profile.

Guest 2 produced a closed **20,305-byte / 107-line** report with the required identity and order.
`arm_report.analyze` validated those same bounded bytes: **status 1, STOP_CONTROL**. After the
natural-exit grace, the runner stopped/reaped the process: status **125**, raw exit **-15**, duration
**0.866093347 s**. Protected result: **INCOMPLETE**, semantic **UNAVAILABLE**, reported semantic
**FAIL**. Neither preparation readiness nor a complete negative report implies natural process exit.

### Independent scalar controls

**20 PASS / 12 FAIL / 0 SKIP**. All **32/32** requested FPSCR mode words read back correctly, all
**32/32** retained the requested RN/FZ bits after their operation, and all **32/32** restored the
initial `00000000` word. These register observations do not prove arithmetic correctness: two RN
addition, six RN conversion and four FZ double cases returned the wrong literal result bits.

| Failed control | Expected bits | Actual bits |
| --- | --- | --- |
| `vadd-positive-rn2-fz0` | `3ff0000000000001` | `3ff0000000000000` |
| `vadd-negative-rn3-fz0` | `bff0000000000001` | `bff0000000000000` |
| `vcvt-positive-even-rn2-fz0` | `3ff0000020000000` | `3ff0000000000000` |
| `vcvt-positive-odd-rn1-fz0` | `3ff0000020000000` | `3ff0000040000000` |
| `vcvt-positive-odd-rn3-fz0` | `3ff0000020000000` | `3ff0000040000000` |
| `vcvt-negative-even-rn3-fz0` | `bff0000020000000` | `bff0000000000000` |
| `vcvt-negative-odd-rn1-fz0` | `bff0000020000000` | `bff0000040000000` |
| `vcvt-negative-odd-rn2-fz0` | `bff0000020000000` | `bff0000040000000` |
| `vmul-input-positive-rn0-fz1` | `0000000000000000` | `0000000000000002` |
| `vmul-output-positive-rn0-fz1` | `0000000000000000` | `0008000000000000` |
| `vmul-input-negative-rn0-fz1` | `8000000000000000` | `8000000000000002` |
| `vmul-output-negative-rn0-fz1` | `8000000000000000` | `8008000000000000` |

RN1/RN2/RN3 requests were respectively `00c00000`/`00400000`/`00800000`; FZ-on requests were
`01000000`. Requested/readback values agree in every record; post-operation words preserve these
mode bits while some arithmetic records also set status bit `00000010`. Full observations are
retained, not replaced by the table's arithmetic verdicts. Earlier HOST controls remain **32 SKIP**;
they are not these actual emulated ARM measurements.

### Pristine runtime comparison

**56 PASS / 15 FAIL**, all **71** original cases in order with **71/71 path checks 1**. The failures
are the original eleven directed-rounding cases (two add, six frsp, three direct/indirect/host-boundary)
and four double-NI cases. All 32 quantization cases passed; this is not full NI certification.

Every runtime `before` and `updated` FPSCR was `00000000`. All **51 nonzero requested host modes**
were absent after the pristine setter, and RN/FZ bits remained zero after the operations; other
post-operation status bits varied. All **71/71** runtime restorations and the final restoration
passed with `00000000`. The ARM runtime mode-write gap is confirmed, but independent arithmetic
also failed when the raw control word was correct. The whole failure cannot be attributed solely
to that runtime gap, nor repaired blindly by inserting a mode write.

### Cleanup, configuration and retained evidence

Both owned leaders (113389/113464) were waited/reaped; their groups were absent, and no identified
process retaining the private profile binding was observed. Historical namespace inode IDs were
**UNRECORDED**: there is no retrospective inode-based namespace certificate. A read-only `/proc`
mountinfo check returned EINVAL for an unrelated pre-existing zombie (4661, group 4605/session 3033);
that error and its context remain recorded. Runtime/API negatives also remain raw: missing `os0`
preloads, Vulkan/PulseAudio warnings, absent-output ENOENT and existing-directory EEXIST. Do not
claim zero errors globally or convert these records into a clean natural exit.

Both emitted configuration snapshots retain `user-id: 00`, SHA-256
`321eb130dcf8d38a639ab67b16f6eb9eccf52a80e0c0c3f97fb21ada47266eb9`. The only byte difference from
the approved configuration is that user ID serialization. The reviewed runner restored canonical
`user-id: ""` after preparation and guest cleanup; the final 4,284 bytes were independently verified
against `c11fa8feaa4240186f652672643ee1c8d4cbdecbc09b62ac841a936272b67f25`. This is verified
restoration, not a claim that the emulator never rewrote configuration.

The private run is `/home/fred/projects/zeldaWWHDRecomp/build-r1/build-08/vita3k-run-6c6f3c20224d478c85bbe5bd2bc983f0`.
Postcampaign evidence is `/home/fred/projects/zeldaWWHDRecomp/build-r1/build-08/post-baseline-6c6f3c20224d478c85bbe5bd2bc983f0/`:
`observation.json`, `cleanup-supplement.json` and `manifest-final.json`. The final manifest SHA-256 is
`49e576b47f08f90674b200feaf535fb11f884251b2ad73cbc80f23fbb23f2c5c`, with **2,558 typed entries /
1,962 regular files / 114 internal cache links**. It includes the current report and protected result.
Old ab8, fd66, stage and postcampaign proofs keep their original source bindings. The old stage
manifest is historical: the guest wrote cache/data; the whole private profile is not unchanged.

## Decision after the baseline — STOP_CONTROL

| Observations | Next disposition |
| --- | --- |
| Raw arithmetic wrong despite verified instructions/readback | STOP attribution/options review; no runtime patch. |
| Raw correct; runtime wrong; requested controls not applied | Local targeted ARMv7 mode seam may become eligible after result/review authorization. |
| Runtime controls right but arithmetic wrong | Attribution remains open; no automatic FPSCR patch. |
| No reproduction | No patch. |

The first row applies. Result review finalized a **negative diagnostic / REVOIR**, not a repaired
runtime or product GO. No experimental checkout, emitter/HLE change, configuration A/B, physical
execution or confirmation is authorized. Processes **3/4 remain forbidden**. The product owner
must select any new objective, scope and execution budget; permanent adoption would also require
the normal project review. Useful negative evidence does not establish native playability or a
balanced physical game/renderer/memory budget.

Source-only options, not an execution plan: the public template has `cpu-opt: true`; candidate
`cpu/dynarmic_cpu.cpp:335–350` routes `cpu-opt: false` to `no_optimizations` and removes fastmem
globally, rather than selecting a dedicated strict-FP flag. `all_safe_optimizations` excludes
`Unsafe_IgnoreStandardFPCRValue` (ASIMD). Candidate `Arm64SetFpscr` in `emit_arm64_a32.cpp:668–688`
updates state without `MSR FPCR`, while `a32_address_space.cpp:252–256` installs FPCR at JIT entry.
That contrast is a compatible hypothesis, not exclusive attribution or verified mapping to the
opaque `cea2b91f...` executable. A separately approved physical scalar fixture could discriminate
the platform behavior; a cheaper configuration comparison would have low confidence and broader
optimization effects. Neither is approved here, and no illustrative patch is a ready implementation.

## Review rework 1/2 — blocker 1

`ab8e3b2f5c1b37927b29616fcc280363ef1c6e99` and its staged baseline are **not eligible** for execution.
Its VPK, plans, proof manifest and recorded quota failures are preserved unchanged.
The authorization string alone permitted a caller to change the display socket and coherently
rebuild both phase commands/plans and the root plan. The fake-only red regression reproduced
acceptance through the complete fake pair. With the required external root-plan byte hash, the
same coherent mutation and old authorization/hash fail before any reservation or subprocess.
The unchanged reviewed plan reaches the fake pair; invalid hashes and omission of the CLI hash
fail closed. All prior source/pin, argv, phase-plan, ownership, package/input and configuration
guards remain intact. Native FP code, 32 ARM oracles and 71 runtime cases are unchanged.

The reviewed human checkpoint was validated from an isolated, detached verification worktree at
`/home/fred/projects/zeldaWWHDRecomp/build-r1`, avoiding the known tmpfs UID quota. The sole writing
branch/session stays at `/tmp/opencode/ww08`; no primary worktree is moved and no old proof is rewritten.
Its copied-tool builds and then **stage-only** pair live in that verification worktree's ignored
`build-{01,03,04,08}` trees; exact commands, hashes and regressions are in `build-proof/checkpoint.json`
and its proof manifest. The new root-plan SHA-256 and intended command containing it are recorded
there as preexecution history: a locally computed digest is **not authorization**. Usage was **0/4**
at that checkpoint. The coordinator later independently verified the plan hash and authorized only
the exact pair recorded above; actual usage is now **2/4, frozen**, with no native-playability result.

## Documentation-only finalization

The final authored delta is this document only. Every other tracked blob, including native code,
scripts/tests and dependency pins, must match the reviewed fd66 tree exactly. Target results remain
bound to **fd66 and VPK `84f4f51e...`**, not to the later documentation commit or its rebuilt package.

Final verification uses the separate detached worktree `/home/fred/projects/zeldaWWHDRecomp/build-f8`
and ignored `build-proof/` evidence, keeping the primary writing branch and all earlier proofs intact.
Required local checks at its immutable documentation SHA are 155 fixture tests, 53 baseline tests,
HOST runtime 71/71 with ARM controls 32 SKIP, HOST memory PASS/SDK SKIP, CTest 1+5+3+1+4, ARM08
cross-build, repository audit, attribution, diff/scope and pristine-pin checks. Exact commands,
results, tree-equivalence proof and artifact hashes belong to that final checkpoint evidence.
No artificial TDD red test is added for prose; accuracy is checked against the retained report.

The four older target package/instruction records are explicitly reused by unchanged-code/pin
equivalence, not claimed as fresh cross-builds. A new final-SHA ARM08 ELF/SELF/VPK is **NONEXECUTED**.
Finalization creates no fresh target stage/profile/request, reservation attempt or process. It does
not integrate into `main`, close a ticket or enlarge the frozen target budget.
