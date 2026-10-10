# VITA-WW-08 — independent scalar VFP / pristine runtime comparison

2026-10-10 · [ticket](https://trello.com/c/N9iec8am) · **stage 1 only, target NOT RUN**

The bounded diagnostic is implemented and host/cross-build checked. This is not a runtime repair,
CPU GO, game boot or physical Vita result. Actual 08 Vita3K process usage is **0/4**. Material-risk
review and exact coordinator authorization must precede the named pristine baseline pair **1/2**.
Any experimental correction and confirmation pair **3/4** remain conditional within this ticket.
The exhausted 03/04 campaigns must not be rerun.

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

## Protected baseline pair — not executed

`arm_launch.py` defaults to staging only: **zero processes, no guest request**. Exact synthetic title
**BWWA00008** has just SELF and SFO. Both phase plans/evidence are host-protected, read-only in the
guest namespace. `--execute-reviewed` cannot stage and immediately execute a new 08 pair.
The separate `arm_execute.py` requires the exact saved plan and its coordinator-authorized
`ARM08:<source>:<run>:baseline:1,2` string. It rechecks clean checkpoint/pins, commands, deadlines,
protected plans, source/staged package, runtime/assets/libraries and canonical configuration.
An exclusive ignored baseline reservation prevents a second baseline authorization/retry.
The conditional 3/4 pair is not implemented or authorized by this stage-1 entry point.

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

## Checkpoint verification

Linux AArch64, copied tool stack only:
`/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack`.
Initial red loops observed missing control API, missing typed report/launch mode, rejected-installer
configuration not restored, missing report-I/O injection and missing fake cleanup audit. Focused
green loops then passed. No full baseline rerun preceded the first red loop.

Checks are repeated after the checkpoint commit (`git log -1 --format=%H -- docs/vita/VITA-WW-08.md`):

- **151/151** fixture tests: unchanged 125 plus 26 focused control/report/I/O/paired-launch tests.
- Real HOST FP **71/71** in both 03 and 08; 08 independent ARM **32 SKIP**.
- Existing eight host drivers **53/53**; CTest corpus 1, contracts 5, runtime 3, memory 1,
  new fake-control guard 4. HOST real memory PASS, SDK SKIP.
- All four older VPK builds plus new 08 ELF/SELF/VPK; source bindings, hard-float flags,
  scalar instruction inspection, pristine pins, scope/diff, repository audit and attribution.
- No Windows CI or hardware, physical Vita, real 08 emulator process or gameplay was run.

Ignored evidence is in `build-ww08/{arm-evidence.json,logs,proof}`, `build-ww01/evidence.json`,
`build-ww03/fp-evidence.json`, `build-ww04/mem-evidence.json`. The proof manifest records actual
commands, counts, immutable checkpoint SHA, documentation provenance hashes, selected instructions,
ELF/SELF/VPK hashes and the protected exact pair plans. The final source-binding rebuild is
**NONEXECUTED**; intermediate dirty-source packages are not eligible as target evidence.

Reproduction: prepend `$STACK/bin` to PATH, set CC to its native GCC and VITASDK to `$STACK/vitasdk`;
run `arm_build.py --vita --out "$PWD/build-ww08" --cc "$CC" --cmake "$STACK/bin/cmake"
--ninja "$STACK/bin/ninja" --vitasdk "$VITASDK" --jobs 2`. The final proof records complete argv.
Staging is harmless and is not runtime success. Only the coordinator may authorize execution
of the exact recorded baseline pair after material-risk review.

## Decision after the baseline — do not act yet

| Observations | Next disposition |
| --- | --- |
| Raw arithmetic wrong despite verified instructions/readback | STOP attribution/options review; no runtime patch. |
| Raw correct; runtime wrong; requested controls not applied | Local targeted ARMv7 mode seam may become eligible after result/review authorization. |
| Runtime controls right but arithmetic wrong | Attribution remains open; no automatic FPSCR patch. |
| No reproduction | No patch. |

Any later experimental dependency checkout stays ignored, with an explicit scoped delta; pristine
pins stay untouched. A fixed confirmation needs separate review/exact authorization, consumes only
processes **3/4**, at most 30 seconds each, with no retry. Permanent upstream adoption must follow
project policy; no external pull request/action is authorized. Useful bounded results still do not
establish native playability or a balanced physical game/renderer/memory budget.
