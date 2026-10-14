# WW87: verify the SDK image at the ship prolog entry

## Diagnosis first

Started clean on `vita-integration` at
`4afd4d788ad1bdc560609926fadd5cd5ad9809ea`. Worktree:
`/home/fred/projects/zeldaWWHDRecomp-ww87`, branch `ww87-ship-entry`.
Remote branches were checked; `gh pr list` could not run because `gh` is absent.
Nothing was pushed. Private receipts: `/tmp/opencode/ww87/`.

**The publication-time whole-image hash rejects the SDK-mutated image.**
There is no erroneous canonical-PC requirement in the original entry guard:
`vita/host/boot_host.c:303-306` already resolves staged `81F900F4` to linked
`C1B100F4` before calling `ww_host_ship_boundary`. At the base revision,
`ship_contract.c:68,101` selects canonical `81B100F4`; that selection succeeded
in WW86 before the later linked-PC rewrite.

Every predicate in the base entry refusal at `ship_contract.c:104-107` is:

| Predicate | Expected | WW86 evidence |
| --- | --- | --- |
| Contract | Enabled exact generated contract | Enabled |
| Live image | Module 168, linked; controller `+10` equals staged base (`rel_service.c:48-61`) | Owner `80AE0918`, base `81F90000`; owned alias resolution succeeded |
| Caller LR | `80240EE8` | Exactly `80240EE8`: passes |
| Caller owner | r31 equals live image owner `80AE0918` | r31 was not captured; decoded loader keeps its controller in r31 |
| Whole image | FNV-1a of all 74,960 bytes equals pristine publication's `raw_hash` (`rel_service.c:18-24`) | SDK changed table, lifecycle and relocation bytes: equality fails |

The failing hash predicate is **`ship_contract.c:106`**. Publication at base
`ship_contract.c:38-40` records the pristine image; the SDK's stores are decoded
in WW79 `sdk-link.txt` and WW85 `sdk-fixup.disasm`. The base guard checks neither
CPU PC nor r3. The loader's indirect call is `80240EE4`; its arriving context
is PC `81F900F4`, LR `80240EE8`, r3 `81F90000`, not PC `80240EE4`.

WW86 captured neither r31 nor the live whole-image hash. Those exact observed
values cannot be recovered from its receipts, and an additional owner failure
cannot be excluded. The private real-fixture model isolates the hash failure
with correct owner/LR: pristine FNV **`095AC17893B66ABC`**, modeled complete
fixed-mode SDK FNV **`74A4F91BF90EB477`**. These are offline fixture values,
**not captured WW86 live hashes**; the model supplies previous module
`81822E80`, BSS `807FF620` and zero name-table base.

The discarded buffer at `rel_link.c:261-307` is not that SDK image. It certifies
canonical producer bytes using linked `C1B...` addresses and pristine headers.
The SDK uses staged self references, the separately allocated BSS, staged
branch PCs, and absolute header/import/lifecycle fields. Reusing the canonical
buffer's hash would reject the legitimate SDK call again.

## One entry-state transition, fail closed

The existing relocation validator now has an optional staged-image context;
all existing callers pass `NULL`, preserving canonical validation. The ship
entry helper reconstructs the complete expected SDK image from the already
certified pristine contract, immutable section metadata and owned controller:

- Publication preserves pristine bytes/hash and snapshots the SDK list tail
  and name-table base; no SDK mutation is trusted as a new baseline.
- Self references use staged section starts or controller `+14` BSS. BSS must
  satisfy the SDK alignment and its full extent must fit retail main RAM.
  Only self and DOL imports are admitted by this staged reconstruction.
- SDK list/header/table/import/lifecycle writebacks are modeled in full.
  Both complete OSLink and OSLinkFixed import-size forms are checked.
- LR and r31 owner checks are unchanged. New checks require r3 to equal the
  live staged module base and CPU PC to resolve to the approved prolog entry.
  The staged alias and the subsequent linked-PC boundary both satisfy this.
- Only an exact expected-image hash match at initial entry transitions
  `raw_hash` to the **independently computed expected hash**, after successful
  entry tracing. Reconstruction requires the original pristine checkpoint;
  it cannot refresh an already transitioned checkpoint or continue a tampered
  active call. Guest bytes and immutable section snapshots are never rewritten.

The continuation, constructor, return and interrupted-return guard bodies are
unchanged and still require exact current integrity. An SDK-mutated image
without verified prolog entry still fails the constructor gate. Strict pristine
link/relink identity, default-off ship opt-in, dependency pins, generic raw-hash
helper and host dispatch are unchanged. The production diff is confined to
`vita/host/rel_link.c`, `ship_contract.c` and their two declaration headers.
This models the observed separate-main-RAM BSS path, not an inline-BSS contract.

## Tests first

Tests-only revisions:
`dc527f12fffcf0aba15f9f424239584150854609` and
`2bdb0af65774e8832f107ce3ee572b45935c06b6` (scenario-suffix correction).
Production was byte-identical to the base at both revisions. Clean corrected
synthetic red and clean real-fixture red each have exactly **two expected
failures, zero setup errors**: legitimate SDK entry refused, mismatched CPU PC
incorrectly admitted. Receipts retain earlier fixture-setup/suffix failures;
they are not counted as behavioral red evidence.

```text
python3 -B /tmp/opencode/ww87/run_tests.py test_ship_prolog.ShipPrologTest.test_publication_sdk_image_enters_with_the_real_loader_context test_ship_prolog.ShipPrologTest.test_publication_sdk_entry_refuses_wrong_caller_context test_ship_prolog.ShipPrologTest.test_publication_sdk_entry_refuses_tampering_anywhere_in_the_image test_ship_prolog.ShipPrologTest.test_publication_sdk_entry_refuses_partial_sdk_writebacks test_ship_prolog.ShipPrologTest.test_pristine_prolog_entry_refuses_a_different_pc
python3 -B /tmp/opencode/ww87/real_fixture.py red
```

Green tests model SDK payload and header stores independently of production
using normal host materialization/link/publication. The positive probe uses
PC `81F900F4`, LR `80240EE8`, r3 `81F90000`, r31 `80AE0918`, exercises the real
host-call alias rewrite to `C1B100F4`, and checks canonical-boundary readmission
with only one entry marker. The synthetic fixture's prolog file offset is now
`F4` too. Existing direct return probes supply their complete entry context.

Negative coverage includes wrong PC/LR/owner/module base; changed header,
lifecycle, code, data, relocation stream, BSS entry and import size; partial SDK
writebacks; post-entry tampering without a hash refresh; and constructor entry
without verified prolog entry. Existing lifecycle/interrupt tests remain green.
The private real fixture uses exact raw DOL/REL and generated canonical/chunk
metadata from `build-f5/ww75/composite`; it executes no translated game or SDK
instructions. Raw REL SHA-256 remains
`945a6493a7f9d353657b07048b26e63c8fd53ca147df9d1b5378c44f0ef2280e`.

## Checks and limits

All checks below passed at clean implementation SHA
**`dfd4d070a5d3bc250b9dc595e5552edab89daf9a`**, on Linux AArch64 workstation
`fuzhou`, kernel `7.1.13-402.asahi.fc44.aarch64+16k`.

| Check | Command | Result |
| --- | --- | --- |
| Focused host | `python3 -B /tmp/opencode/ww87/run_tests.py test_ship_prolog test_rel_section_guard test_rel_materialize test_host.StartupHostTest.test_instruction_cache_invalidation_accepts_unchanged_compiled_text test_host.StartupHostTest.test_instruction_cache_invalidation_accepts_data test_host.StartupHostTest.test_instruction_cache_invalidation_refuses_changed_compiled_code` | 157 methods pass |
| Full host | `python3 -B /tmp/opencode/ww87/run_tests.py full` | 956 methods pass, no skips |
| Host build | Original checkout's `build-vita-tools/local-stack/bin/cmake --build build-f5/ww87/host -j 1` | Pass |
| CTest | Original checkout's `build-vita-tools/local-stack/bin/ctest --test-dir build-f5/ww87/host --output-on-failure -j1` | 51/51 pass |
| Real fixture | `python3 -B /tmp/opencode/ww87/real_fixture.py` | 29 methods / 42 native scenarios pass |
| ARM | `python3 -B /tmp/opencode/ww87/arm_compile.py` | Both changed C units compile for Cortex-A9/NEON hard-float |
| Audit | `bash scripts/audit_repo.sh` | Pass |
| Attribution | `python3 scripts/check_attribution.py 4afd4d788ad1bdc560609926fadd5cd5ad9809ea..HEAD` | Pass |
| Whitespace / preservation | `git diff --check 4afd4d788ad1bdc560609926fadd5cd5ad9809ea..HEAD`; `python3 -B /tmp/opencode/ww87/preservation.py` | Pass |

`/tmp/opencode/ww87/final_checks.py <HEAD>` records exact clean SHAs, expanded
commands, exits and log hashes in `checks.json`. The first combined wrapper's
120-second ceiling interrupted the full suite without a result; the complete
serial rerun passed (full host: 115.816 seconds). All builds use `-j1`; large
private outputs stay under ignored `build-f5/ww87`. Dependencies remain clean:
RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`, DolRecomp
`b8b534591cba8ca7cd43943a655ee6e2591cf5de`.

No emulator run, full VPK build, Windows CI, physical Vita, prolog gameplay or
actual SDK execution was performed. The offline model does not prove the next
live stop or recover missing WW86 registers. Independent bounded review,
integration and a separately authorized trial remain coordinator gates.
