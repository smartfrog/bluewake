# WW42: ship producer source patch, pending independent review

Source-only preparation, not an integrated producer, private build receipt,
admission contract, or lifecycle certificate. WW41's research handoff at
`b2c5063` was read, not integrated. Old `5709d12` build evidence remains distinct.
No patched producer use with private game inputs is authorized by this delivery.

## Apply scope and identity

- BlueWake started clean at `7914025aa41a1d78bf6e693455f59b2cdf921243` in
  `ww42-ship-producer-hook`.
- Exact external base: `b8b534591cba8ca7cd43943a655ee6e2591cf5de`.
  `ref/vita-ww-01/DolRecomp` in the original workspace was read-only and clean
  at that revision before and after testing. No pin or foreign checkout changed.
- Artifact: `patches/dolrecomp/0020-c-emit-ship-canonical-code-input.patch`.
  SHA-256: `6c745b6515128696eeb7cb32a96000e671b3980c099059388af304547b864446`.
- Disposable source: `/tmp/opencode/ww42-producer-copy`, extracted with
  `git archive` of the exact base. Archive SHA-256:
  `571fea1d64db0c6694feb8206a18853b3d12e1c0d0d69e14ef732d954eb45820`.
  The source diff was reversed, the pristine index checked with
  `git diff --exit-code`, then the final artifact passed `git apply --check`
  and was applied there. This was not applied to the pinned checkout.

The patch changes only upstream `.gitignore`, `src/app/pipeline.c`, and
`src/backend/codegen.c`. Keep DolRecomp's GPLv3 license and existing notices;
the synthetic probe/test sources carry compatible GPL notices.

## Hook

`DOLRECOMP_SHIP_CODE_INPUT` opts into C-backend REL **directory** emission and
names a new file ending in `.ship-code-input`. Both repositories ignore that
suffix. Unset means unchanged behavior; empty/unsafe paths, non-C backends and
single-REL exports fail. The batch must contain exactly one module 168.

After the existing sorted batch load/map/`rel_apply_relocations`, ship's split
implementation builds its usual `LoadedCodeSection` array. It validates and
copies that array's metadata and bytes immediately before passing the **same
array** to `emit_code_sections_split`. There is no relocation replay or new
hash/certificate schema. Other modules are not captured.

The snapshot remains in memory until every module emits successfully. Only
then is the destination exclusively created, written and closed; an existing
destination is never overwritten. Reported write/close failures remove the new
sidecar, with cleanup errors reported explicitly. Header, manifest and chunk
stream errors now reject the opt-in batch even when close itself succeeds.
Failed batches may leave partial C outputs, never a successful export/receipt.
Publication is synchronous, not crash-atomic; consumers must wait for process
success and reject incomplete/stale files rather than treating existence as proof.

The binary export is eight bytes `SHIP168\0`, big-endian u32 section count,
then, per executable section in emission order: big-endian u32 index, raw file
offset, linked address, byte size, followed by exactly that many canonical
bytes. It contains private game bytes when used with a real disc: never commit,
upload, attach, or include it in a public artifact.

## Bounded checks on Linux aarch64

GCC 16.2.1, Python 3.14.8, kernel `7.1.13-402.asahi.fc44.aarch64+16k`.
Only small native C fixture/probe compilation, serial execution and synthetic
public bytes were used. The probe links the actual pipeline, relocation frontend
and C emitter; linker wrappers observe the actual emitter's `ppc_decode` inputs
and inject failures, without replacing the emitter or relocation operation.

```sh
python3 -B patches/dolrecomp/tests/test_ship_code_input.py /tmp/opencode/ww42-producer-copy
python3 -B tests/test_composite.py
```

- Producer probe: **25/25 passed**. Single/multiple executable sections match
  actual emitter-decoded words/addresses after canonical tagged relocation;
  raw pre-relocation bytes differ. Opt-in and default C outputs are identical.
  Publication is checked after the later module's chunk/header/manifest close.
  Missing/wrong/duplicate ship, missing/invalid section, unsupported invocation,
  relocation failure, ship/later emission failure, sidecar open/write/close
  failure and existing-file rejection are covered. `/dev/full` covers write
  errors whose stream close succeeds.
- Test source identity (runner, probe, compiled C sources):
  `449d4e973e1cbb568ed47b1a25e5e385602eac0ec3664f340d038170b8367a1a`.
  Probe executable SHA-256:
  `4a3ffe401944d110d69228c6adaef3b6702e77d57e8f8bf0e5910230761e84cd`.
- Initial unpatched 13-case run: seven failures and one missing-sidecar error,
  as expected (console evidence). A subsequent focused `/dev/full` chunk test
  failed before the chunk stream-error fix; retained log:
  `/tmp/opencode/ww42-chunk-red.log`.
- Final producer log: `/tmp/opencode/ww42-green.log`, SHA-256
  `7c5be7bbdf44ad5f7a5a868b3eb7ba1273d6b6c0b94bf19aaaaa2233540c9d17`.
- Existing `tests/test_rel.c`: six scenarios passed, compiled directly with
  `src/frontend/container/rel.c` in the disposable copy. Expected external-import
  and alignment rejections printed errors. Log: `/tmp/opencode/ww42-rel.log`.
- Composite regression: **26/26 passed**, `/tmp/opencode/ww42-composite.log`.
- BlueWake `scripts/audit_repo.sh` and staged `git diff --check`: passed.
  Upstream source diff whitespace check and artifact apply check: passed.

No network, installation, upstream action, game translation, native game build,
LLVM build, full producer CLI build, emulator, Windows CI or hardware test ran.
The bounded producer hook is verified; full controlled invocation is unrun by
scope, not represented as a dependency failure. Open PRs were not queried because
network access was forbidden; locally known remote branches were checked.

## Review and remaining gates

Independent review must precede integration and any private patched-producer use.
Then the build owner needs separate explicit authorization for a fresh complete
batch invocation, with no stale output reuse, binding full raw DOL/ship REL
SHA-256 and byte lengths (including imports), sorted batch identities/address
map, exact base + patch + executable identity/options,
sidecar canonical sections, ship C outputs and composite tree identity. No
successful receipt may survive an invocation/input/output failure.

This does not discharge runtime tagged branch validation, real ship prolog
`C1B100F4`, epilog `C1B10120`, unresolved `C1B1014C`, or native unlink/destruction.
Module 168 remains blocked by those admission gates; synthetic emission identity
is not proof of native lifecycle execution or permission for a full game build.
