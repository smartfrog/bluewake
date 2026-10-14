# WW85: publish pristine lifecycle offsets for the SDK linker

## Diagnosis before the fix

Started clean at `8c6f92e138d8bfc6122c9f5f8f42ab39f51bf6dc` on
`vita-integration`; isolated branch `vita-ww85`, worktree
`/home/fred/projects/zeldaWWHDRecomp-ww85`. Remote branches were checked;
`gh pr list` is unavailable because `gh` is absent. Nothing was pushed.
Private receipts are under `/tmp/opencode/ww85/`.

**The host is at fault.** It publishes absolute lifecycle addresses into REL
header fields that the game's SDK linker still treats as section-relative
offsets. The loader does not add a base at its indirect call.

The private DOL (`build-f5/ww05/private/main.dol`, SHA-256
`680e347ef8c77739165908cc6a375e5af714f354494daca17a1c34e53c275bec`)
was decoded using a copy of WW73's `ppc_dis.py`, adapted for the SPR field
order and indirect branches. `loader.disasm` proves this call sequence:

| PC | Instruction | Meaning |
| --- | --- | --- |
| `80240ED8` | `lwz r3,16(r31)` | Load module base from its controller's `+0x10` publication |
| `80240EDC` | `lwz r12,52(r3)` | Load **module header `+0x34`**, not a GOT slot or section entry |
| `80240EE0` | `mtctr r12` | Copy that word directly into CTR |
| `80240EE4` | `bctrl` | Call CTR; LR becomes `80240EE8` |
| `80240EE8` | `stw r3,24(r31)` | Store the prolog's result in the controller |

At the base commit, `vita/host/rel_service.c:292-310` copies the verified pristine
REL and records its hash. `rel_link.c:355-367` initializes canonical linked data
and BSS, not the scratch header. `rel_link.c:369` publishes the separate
controller's `+0x10`; lines 377-379 then call
`ship_contract.c:54-63`, which writes absolute addresses into scratch
`+0x34/+0x38/+0x3C` and hashes that mutated image.

`disc_service.c:181-190` intercepts **do_load**, not do_link. The host's linked
flag is not an SDK input and does not suppress the subsequent translated SDK
linker. WW79's private `ANALYSIS.md` documents that call ordering.

The DOL's SDK stores, independently decoded in `sdk-fixup.disasm`, are:

- `803051F8/FC/80305200`: header `+0x10` becomes module + section-table offset.
- `80305248/54/58`: each nonzero section entry gains the module base, retaining
  its executable flag. `80305270` writes the separately supplied BSS base.
- `803052B4`: read prolog section byte at header `+0x30`.
- `803052C0/C4/CC/D0`: index the already-rebased table by section * 8 and clear
  its executable bit to get the absolute section start.
- `803052C8/D4/D8`: load header `+0x34`, add that absolute section start, and
  store the sum back to `+0x34`.
- `80305300` and `80305328`: analogous epilog/unresolved writebacks.

The exact pristine ship REL SHA-256 is
`945a6493a7f9d353657b07048b26e63c8fd53ca147df9d1b5378c44f0ef2280e`.
All three lifecycle section indices are 1. The section's file offset is `F4`,
but the **prolog's section-relative header offset is zero**, not `F4`:

| Field | Pristine | Host publication at base | SDK result at base |
| --- | --- | --- | --- |
| prolog `+0x34` | `00000000` | `81F900F4` | `03F201E8` |
| epilog `+0x38` | `0000002C` | `81F90120` | `03F20214` |
| unresolved `+0x3C` | `00000058` | `81F9014C` | `03F20240` |

For the prolog, the host first writes `M + F4 + 0 = 81F900F4`; SDK store
`80305258` independently makes section 1's flags `81F900F5`. SDK `803052D4`
then adds `81F900F4 + (81F900F5 & ~1)`, producing `03F201E8` modulo 2^32.
That matches WW84's recorded target, `r3=81F90000`, `r12=03F201E8`, and
`lr=80240EE8`. Writer attribution is static disassembly plus the inspected
host path; WW84 did not separately sample live header words or writer PCs.

With pristine offsets preserved, one SDK fixup produces `81F900F4`.
The unchanged owned-executable resolver maps that staging address to linked
`C1B100F4` (canonical `81B100F4`), the standing approved prolog contract.

## Minimal repair and retained gates

The only production change is in `vita/host/ship_contract.c`: remove the three
lifecycle writes from `ww_host_ship_publish`. Its existing whole-image hash
checkpoint remains. `ww_host_ship_raw_matches` now requires the entire pristine
image, including lifecycle offsets, on both first link and repeat link; it no
longer expects host-rebased lifecycle words for a live image. This tightens the
published form rather than accepting both forms.

`rel_link.c`, `rel_service.c`, `boot_host.c`, WW81 section validation, WW83
full-image ICBI, and every prolog/constructor/return gate are byte-identical to
the base. No SDK mutation causes a hash refresh. The ship option remains off by
default; no runtime/dependency change or wider dispatch allowance was added.

## Tests-first evidence

Diagnosis and tests-only commit:
**`4102629749f10abb17d68589d6ca79ac041f267f`**. Production was unchanged.
The clean red runs were:

```text
python3 -B /tmp/opencode/ww85/run_tests.py test_ship_prolog.ShipPrologTest.test_publication_preserves_the_exact_pristine_lifecycle_offsets test_ship_prolog.ShipPrologTest.test_publication_allows_one_sdk_rebase_to_the_approved_lifecycle_targets
python3 -B /tmp/opencode/ww85/real_fixture.py red
```

Each has exactly two expected failures, zero setup errors: publication does not
preserve pristine bytes, and one SDK fixup produces a double-rebased target.
The real-fixture failure prints the exact `81F900F4 + 81F900F4 = 03F201E8`
chain, as well as the analogous epilog/unresolved sums. `red-checks.json`
records clean SHA, commands, exits and log hashes.

After the production change, all six new publication methods pass. The common
`ship_publication_probe.h` uses normal materialization/link/publication, models
the inspected SDK section-table and lifecycle writebacks, and checks all three
corrected targets. It also proves pristine repeat-link preserves initialized
BSS, changed lifecycle offsets refuse repeat-link, and SDK mutation still
fails both the prolog and constructor whole-image hash gates. It does not
refresh the snapshot/hash, dispatch a real game instruction or run the SDK.

The existing game-free lifecycle caller omitted the SDK and formerly relied
on the bad host publication to supply an absolute header target. It now computes
the target from pristine section/header offsets **in registers**, before the
same indirect call at `80240EE4` with LR `80240EE8`. Scratch epilog/unresolved
refusal probes similarly calculate their staging target from pristine offsets.
Their lifecycle assertions remain intact. This synthetic caller preserves an
unchanged image to exercise the independent entry/constructor/return rules; it
is not evidence that the retail SDK's mutations pass those rules.

The private real fixture reuses the verified raw DOL/REL, generated contract,
compiled section catalog and chunk hashes from `build-f5/ww75/composite`.
Large/private generated files remain under ignored `build-f5/ww85/`;
receipts contain no raw game bytes. Dependencies are borrowed read-only:
RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`, DolRecomp
`b8b534591cba8ca7cd43943a655ee6e2591cf5de`.

## Checks and limits

Checks on Linux AArch64 workstation `fuzhou` (kernel
`7.1.13-402.asahi.fc44.aarch64+16k`):

| Check | Command | Result |
| --- | --- | --- |
| Focused host | `python3 -B /tmp/opencode/ww85/run_tests.py test_ship_prolog test_rel_section_guard test_rel_materialize test_host.StartupHostTest.test_instruction_cache_invalidation_accepts_unchanged_compiled_text test_host.StartupHostTest.test_instruction_cache_invalidation_accepts_data test_host.StartupHostTest.test_instruction_cache_invalidation_refuses_changed_compiled_code` | 150 methods pass |
| Full host | `python3 -B /tmp/opencode/ww85/run_tests.py full` | 949 methods pass, no skips |
| CTest | `build-vita-tools/local-stack/bin/ctest --test-dir build-f5/ww85/host --output-on-failure -j1` (tool from the original checkout) | 51/51 pass |
| Real fixture | `python3 -B /tmp/opencode/ww85/real_fixture.py` | 22 methods / 26 native scenarios pass |
| ARM compile | `python3 -B /tmp/opencode/ww85/arm_compile.py` | Changed production translation unit compiles for Cortex-A9/NEON hard-float |
| Audit | `bash scripts/audit_repo.sh` | Pass |

`/tmp/opencode/ww85/final_checks.py <HEAD>` records the final clean SHA,
exact commands, exits and log hashes in `checks.json`, including serial host
build, attribution, whitespace and byte-preservation checks. All builds use
`-j1`; no emulator run, full VPK build, Windows CI, physical Vita or gameplay
test was performed. Independent review and integration remain coordinator gates.

**Next expected fail-closed boundary:** the correct staged prolog address now
resolves to canonical `81B100F4`, but the SDK has mutated the raw image. The
unchanged prolog gate can therefore refuse with
`ship prolog entry requires its exact translated dynamic-link caller`, before
an `entered` stamp. The constructor gate independently retains
`REL constructor raw image changed after validation`. Offline contract tests
demonstrate those refusals; a separately dispatched real trial must establish
the next actual stop. No prolog/constructor/return or gameplay success is claimed.
