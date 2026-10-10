# VITA-WW-09 — comparison of Vita3K with cpu-opt: false

2026-10-10 · [ticket](https://trello.com/c/utl27kIR) · **prepared: plan staged, VPK built, NOT executed**

One bounded configuration comparison against the negative VITA-WW-08 baseline. The single intended
difference is `cpu-opt: false`; the fixture is unchanged (32 independent scalar-VFP oracles + 71 pristine
runtime cases, method `scalar-vfp-v1`, title **BWWA00008**). The hypothesis is weak and
corrective-uncertain: the candidate `cpu-opt: false` path routes Dynarmic to `no_optimizations` and turns
fast-memory paths off — a **global** behavioral comparison, not strict-FP attribution, and not a runtime
repair. No emulator rebuild and no runtime patch are involved. The authorized pair of real processes runs
only after the coordinator supplies the exact `ARM09` authorization and the independently reviewed plan
SHA-256; this document is not authorization and nothing here is a result.

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

## Boundaries

This is a global configuration comparison. Different 09 observations would show that the configuration
changes these diagnostics; identical observations would show this global setting alone does not reproduce
or resolve the 08 control failures. Either outcome is data for a review decision: a negative diagnostic is
acceptable, no runtime patch follows automatically, and no CPU GO, game boot, FPS, physical-Vita or
high-water/pool result is claimed. The real pair runs once, on this machine's copied Vita3K, under the
coordinator's authorization; nothing here authorizes any other execution.
