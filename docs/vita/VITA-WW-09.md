# VITA-WW-09 — comparison of Vita3K with cpu-opt: false

2026-10-10 · [ticket](https://trello.com/c/utl27kIR) · **executed once: negative comparison, identical to baseline08**

One bounded configuration comparison against the negative VITA-WW-08 baseline. The single intended
difference is `cpu-opt: false`; the fixture is unchanged (32 independent scalar-VFP oracles + 71 pristine
runtime cases, method `scalar-vfp-v1`, title **BWWA00008**). The hypothesis is weak and
corrective-uncertain: the candidate `cpu-opt: false` path routes Dynarmic to `no_optimizations` and turns
fast-memory paths off — a **global** behavioral comparison, not strict-FP attribution, and not a runtime
repair. No emulator rebuild and no runtime patch are involved. The authorized pair of real processes ran
once on 2026-10-10, after the coordinator's exact `ARM09` authorization and independently reviewed plan
SHA-256; the closed result is recorded below.

## Inputs and scope

- Verified integration base: `778f51e22130bcbf7ca68a8382d49fe6d7144500` (branch
  `vita-ww-09-cpu-opt-off`, one delivery commit).
- RecompCore: `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`, DolRecomp:
  `b8b534591cba8ca7cd43943a655ee6e2591cf5de`, pristine before/after.
- Private references are local `--no-hardlinks` clones of the pinned sources in ignored `ref/vita-ww-01/`.
- Authored scope: `vita/feasibility/armv7/`, `scripts/vita/armv7/`, this document only.
- Copied tool stack only: `build-vita-tools/local-stack` (no download, no emulator rebuild).
- `git diff --name-status fd66ff3d38f4835f71fd198e69b1f7b8bd129b07 778f51e...` is exactly one entry:
  `M docs/vita/VITA-WW-08.md`. All native code, oracles (`arm_vectors.py`, `fp_vectors.py`), instructions
  and report validators are byte-identical to the reviewed 08 checkpoint; the only expected identity
  differences in 09 are the embedded source SHA and package hashes.

## The named cpu-opt-off profile

The public template `fp_config.yml` is unchanged. The 09 profile is derived from it at runtime and is
closed: baseline `c11fa8feaa4240186f652672643ee1c8d4cbdecbc09b62ac841a936272b67f25` (4,284 bytes,
154 unique keys) with **exactly one line replaced**, `cpu-opt: true` → `cpu-opt: false`, giving canonical
alternate `b9cc0c63d791d0add83db669a6d340b46b839be7ee23c31285e270993364a079` (4,285 bytes, still
154 unique keys; `false` is one byte longer than `true`).

- `alt_config()` re-derives and verifies the mutation: exactly one `cpu-opt` line, 154 unique keys,
  reversal of the single change reproduces the baseline byte-for-byte, and the derived hash must match the
  recorded alternate constant.
- `checked_alt_config()` rejects the baseline fallback, a second mutation, a serialized `user-id: 00`
  variant, an oversized file, and any other content; the size bound is 4,285, not 4,284.
- The plan's `private_config` identity for 09 records the baseline hash, the alternate hash and the single
  change; no caller ever supplies a template, hash or variant name.
- `stage_firmware`, `config_identity`, `emitted_config` and `restore_config` keep their 03/04/08
  behavior by default (`alt=False`); only the 09 paths pass `alt=True`.

## Execution contract

- `arm09_launch.py` stages only. `--execute-reviewed` is refused for 09: execution must go through the
  exact saved plan. Staging writes into `<ROOT>/build-ww09` (the ignored output directory) and then
  claims the **permanent exclusive reservation** `<ROOT>/build-ww09/campaign-reservation.json`
  (created `O_EXCL`, one owner/link; records authorization, source, run UUID, `1,2` and
  `retry: FORBIDDEN`). The reservation path does not depend on the run UUID or on `--out`, and no
  invocation ever clears or overwrites it.
- Once reserved, a second staging is refused, and a second UUID is refused before any `Popen`:
  `arm09_execute.py` re-validates the placeholder-free chain — external SHA-256 of the root-plan bytes
  (supplied independently, never derived from the plan), exact authorization
  `ARM09:<source-sha>:<run-uuid>:cpu-opt-off:1,2`, clean checkpoint equal to `source`, pins, both phase
  commands/entries/deadlines, source/staged package, runtime/assets/libraries/firmware, exact alternate
  configuration identity, reservation concordance (source, UUID, `1,2`, `FORBIDDEN`) — and refuses an
  already-started campaign (phase `process.json` present) before `Popen`.
- Campaign semantics for 09: emitted configuration is accepted only as exact canonical ALT or ALT with
  `user-id: 00` and is saved as evidence; canonical ALT is restored after each phase. A rejected emission
  still gets the reliable canonical ALT restore (only after the owned process is verified stopped) and
  never becomes `PREP VERIFIED`. The guest request is created fresh only after preparation is `VERIFIED`
  (exact installed SELF/SFO bytes, type/owner/link/directory checks).
- Budget: at most **one target VPK and two real processes** (`1,2`: installer then guest), each with an
  absolute deadline of at most 30 s; statuses **124/125 stay INCOMPLETE** even with a valid reported
  PASS/FAIL, and `reported_semantic` remains separate. No retry, help/version, probe or trace.

## Intended exact commands (post-commit, build-f9)

```sh
STACK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack
# build the 09 VPK at the delivery SHA (same arm_build.py, control fixture):
arm_build.py --vita --out "$PWD/build-ww09" --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" \
  --cmake "$STACK/bin/cmake" --ninja "$STACK/bin/ninja" --vitasdk "$STACK/vitasdk" --jobs 2
# stage only; zero processes, no guest request; then claim the reservation:
arm09_launch.py --runtime "$STACK/vita3k/Vita3K" \
  --runtime-sha256 cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30 \
  --runtime-source-revision 'observed-banner:bbd5c36;local-RPATH-relocation;mapping:UNVERIFIED' \
  --libraries "$STACK/vita3k-env" --firmware "$STACK/firmware" \
  --wayland-socket /run/user/1000/wayland-0 \
  --build-evidence "$PWD/build-ww09/arm-evidence.json" --out "$PWD/build-ww09" \
  --timeout 30 --two-phase
# later, only with the coordinator's exact authorization and the reviewed plan SHA-256:
arm09_execute.py --plan "$PWD/build-ww09/vita3k-run-<uuid>/evidence/plan.json" \
  --authorize-pair 'ARM09:<source-sha>:<uuid>:cpu-opt-off:1,2' --expected-plan-sha256 <plan-sha256>
```

Phase 1 (`arm.vpk` staged inside the sandbox): `/stack/vita3k/Vita3K --console --backend-renderer
OpenGL arm.vpk`. Phase 2 (same owned profile, non-console):
`/stack/vita3k/Vita3K --backend-renderer OpenGL --installed-path BWWA00008`. Containment is unchanged
from 08: `bwrap --unshare-all --die-with-parent`, read-only copied runtime/library firmware trees,
private home/XDG/work, DEVNULL stdin, unlimited inherited RLIMIT_FSIZE, only the explicitly named
minimal Wayland socket, no network/GPU device/session bus/console.

## Verification of this prepared checkpoint

- **177/177** fixture tests (155 previous + 22 new), run at the delivery SHA in both the writing worktree
  and the clean `/home` verification worktree `build-f9` with the copied toolchain.
- New targeted fakes: unknown plan variant (including the 08 variant) rejected; a second staged mutation
  and the baseline substitution rejected; stale plan/source/external-hash rejected; second UUID and retry
  refused before any `Popen`; reservation exclusivity/permanence and never-cleared behavior; ALT restored
  after failed installer, rejected installer emission and rejected guest emission; stale report identity
  preserved as INCOMPLETE/UNAVAILABLE; staging bound to the fixed output directory.
- TDD loops were executed locally (red observed first). Cross-inspection: native/oracle code identical to
  the reviewed `fd66` checkpoint (single docs-file diff), build flags and generated oracles are the
  unchanged 08 generator outputs, and the new VPK differs only in the embedded source SHA and package
  hashes.

## Executed result — identical to baseline08, no attribution

Executed exactly once from clean delivery source `2d8eaec6e4dac2c433beb08c6250e544eb8c5d23` with the
prepared plan (sha256 `0d8b272ff4bd3c19d22042f996f1f7c685cef245a9b75100de4cf13a5a9838e6`), full
authorization `ARM09:2d8eaec…:46090f7c…:cpu-opt-off:1,2`, in the reserved run
`build-ww09/vita3k-run-46090f7c0fd04f41b9da120a328b6028` of the `/home` verification worktree
(the ROOT that owns the permanent reservation `build-ww09/campaign-reservation.json`).

Installer process 1 (pid 136812) reached its 30 s deadline: status **124**, raw exit **-15**, waited,
duration **30.001 s**. Preparation was independently **VERIFIED**, not process PASS: installed
`eboot.bin` (146 551 bytes, sha `d461d135…`) and `sce_sys/param.sfo` (912 bytes, sha `52b86eed…`,
byte-identical to baseline08), both phases' emitted configuration was the alternate bytes serialized with
`user-id: 00` (`246274b1…`), and only then was the fresh guest request created
(token `fc329e90b00c474c8a25e81dca0b1a5e`). Guest process 2 (pid 136836) produced a closed
**20 305-byte / 107-line** report (sha `7e845f6187f8f5a3636f3e17633962f86f5a4ffbd8b34ec441ec58f95f510173`)
and was stopped after the natural-exit grace: status **125**, raw exit **-15**, duration **0.867 s**.
`arm_report.analyze` on those exact bytes: **status 1 / STOP_CONTROL** (controls FAIL, runtime FAIL,
restore PASS); protected result **INCOMPLETE**, semantic **UNAVAILABLE**, `reported_semantic` FAIL.

Compared case by case with baseline08 (`18eef287…`, fd66/`84f4f51e…`):

| Observation | baseline08 (cpu-opt: true) | VITA-WW-09 (cpu-opt: false) |
| --- | --- | --- |
| Controls | 20 PASS / 12 FAIL | **20 PASS / 12 FAIL — same cases, same bits** |
| Runtime | 56 PASS / 15 FAIL, 71/71 path 1 | **56 PASS / 15 FAIL — same cases, same bits** |
| FPSCR readback / restore | 32/32 / PASS | **32/32 / PASS — identical words** |
| Report body (lines 2–107) | — | **byte-identical; only the identity header differs** |

The 12 control failures are the same directed-RN/FZ literals (`vadd-positive-rn2-fz0`,
`vadd-negative-rn3-fz0`, six `vcvt` even/odd RN1/RN2/RN3, four `vmul` FZ-on input/output pairs) with the
same expected/actual bit pairs. The 15 runtime failures are the same two add, six `frsp`, three
direct/indirect/host-boundary and four double-NI cases. Every runtime `before`/`updated`/`restored` word
stayed `00000000`, `operated` status bits varied identically in both (71/71). Emitted configuration in
both phases was the alternate schema with `user-id: 00`; the final canonical file was restored exactly
(`b9cc0c63…`, 4 285 bytes). Both leaders were waited/reaped and their groups are absent.

**Decision.** Controls remain unchanged-wrong even on the globally different `cpu-opt: false` path, so by
the accepted interpretation rule this is **STOP / options: no runtime attribution and no runtime patch**.
This bounded diagnostic does not support the hypothesis that the global `cpu-opt: false`
(`no_optimizations` + fast-memory off) path changes these observations, and it does not isolate the 08
control failures to strict-FP handling. The permanent reservation `a023aaab…` and the plan
`0d8b272f…` stayed unchanged; 03/04/08 evidence and the 08 reservation
(`8bdc0fa3…`, `/tmp/opencode/ww08`) are untouched (mtimes pre-date this campaign). No process from this
campaign remains, and nothing here authorizes another execution.

## Boundaries

This was a global configuration comparison and it changed no observed bit. No CPU GO, game boot, FPS,
physical-Vita or high-water/pool result is claimed, and a negative diagnostic is acceptable. Any repair
of the ARM mode/rounding behavior requires a separate objective, scope and budget with its own review.
