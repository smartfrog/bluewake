# WW63: exact ship lifecycle admission and translated prolog

## Outcome and boundary

The host can admit module 168's exact opt-in lifecycle contract and let the
translated dynamic-link caller execute its prolog, owner-qualified constructor
walk and return through `80240EE8`. This is a bounded public synthetic runtime
slice, not a claim that the real ship actor activates or that WW62 now boots.

`WW05_ENABLE_SHIP_PROLOG` remains **OFF** by default. Enabling it requires a
non-null compiled exact contract and full raw DOL identity verification. Other
actors' lifecycle entries, uncertified ship lifecycle addresses, ship epilog and
unresolved execution, explicit ship unlink and native actor unlink remain closed.
There are no host prolog, constructor or lifecycle success replacements.

No emulator launch, integration, push, private game build, actual ship C execution
or hardware gameplay occurred. Independent review is next.

Initial worktree was clean at
`accef5abe217a5868b4791717f8e965ac2c22b39`. Local branch:
`ww63-ship-lifecycle`. Remote branches were checked; `gh pr list` could not run
because `gh` is not installed.

## Exact rejection path at the initial HEAD

WW62 observed `d_a_ship`, object `80AE0918`, at PC `80240744`, and the lifecycle
STOP from `vita/host/rel_link.c:171-174`.

1. `ww_host_dvd_edge` reads the guest object's requested name, then calls
   `ww_host_materialize_rel` and `ww_host_link_rel`.
2. Materialization reads the raw REL and calls `validate_rel`, which invokes
   `ww_host_validate_rel_link` for non-module-1 images **before scratch allocation
   or publication**.
3. The early gate rejects any non-module-1 image with a nonzero prolog, epilog or
   unresolved section byte or offset. Ship's section-1 prolog offset is zero,
   but its section byte is nonzero; its epilog and unresolved offsets also matter.
4. WW37's lifecycle V2 metadata describes these entries; metadata alone is not
   permission to execute them. WW39's owned constructor boundary already validates
   the live caller, immutable raw image, table bytes, targets and terminator.
   Neither provides the missing non-module-1 lifecycle admission/return contract.

Ship's approved linked lifecycle addresses are `C1B100F4`, `C1B10120` and
`C1B1014C`. Admission requires exact full raw REL identity, those exact addresses
inside owned translated executable sections, complete section/data/BSS exports,
canonical relocation replay equality and matching compiled executable chunk
hashes. Replay reuses the existing validator, with types 1/4/6/10 only for the
ship contract. Existing non-ship certification, branch displacement and WW43
tagging checks are unchanged.

Link publishes only the three lifecycle pointer words in the owned raw header
and refreshes the existing scratch-integrity hash. It still returns to the guest
caller. That caller loads header `+0x34`, sets CTR and executes `bctrl`; the host
does not invoke or skip the prolog. Entry requires LR `80240EE8` and r31 naming
the live owner. Return requires the same stack, r31, LR, inherited r2/r13 and a
completed owner-qualified constructor walk. No r2/r13 substitution is used.

## WW44 import map and adaptations

All 16 regular archive members were checked for safe relative paths and extracted
only into `/tmp/opencode/ww63/archive`. The preserved source archive contains no
private game bytes. No generated source, raw game image or archive was committed.

| WW44 piece | WW63 treatment |
| --- | --- |
| `vita/host/ship_contract.c` | Reused exact DOL/REL matching, lifecycle publication and boundary checks. Added inherited-register return checks, required constructor completion, and a narrowly qualified caller-resume state. |
| `vita/host/rel_link.c` deltas | Reused ship-only admission and canonical certification, lifecycle publication and constructor-completion trace. Added explicit executable lifecycle ownership and constructor-completion state. Existing relocation semantics remain intact. |
| `vita/host/rel_service.c/.h` deltas | Exported the existing raw hash helper and retained ship unlink rejection. **Did not copy** the archive's old live-owner/collection logic: WW60's resident profile-global lifetime fix is preserved. |
| `vita/host/boot_host.c/.h` deltas | Reused ship boundary calls and qualified compiled caller return; tightened return qualification rather than broadly permitting `80240EE8`. |
| `vita/host/main.c`, `CMakeLists.txt` | Reused default-off opt-in and fail-closed initialization. Kept current wall-time progress logging and all newer host configuration. Added the public fixture probe target. |
| `cmake/composite/module_exports.h`, `module_export.c` | Reused the typed exact contract and optional getter, returning null when a contract was not generated. Lifecycle V2 exports remain unchanged. |
| `ship_prolog_probe.c`, `test_ship_prolog.py` | Reused the translated public vertical fixture and six base tests; expanded to 22 methods with parameterized rejection/context/alias checks. |
| `ship_fixture_emit.c`, generator CMake | Reused the pinned emitter adapter, adding rejection of empty inputs and truncated record headers. Producer source and pin were not changed. |
| `scripts/ship_contract.py` | Imported only canonical-sidecar parsing and contract emission, exercised with synthetic input. Omitted the unverified receipt consumer and its unused helpers/constants. |
| `scripts/generate_composite.py` | **Not imported.** Full controlled receipt/batch/composite binding is the next bounded change, not an unverified consumer path silently added here. |
| Current `test_host.py`, `io_probe.c` | Generate the public ship fixture through the shared test setup; build serially after checking for Vita3K. The synthetic main's optional contract getter returns null so the opt-in initialization can be checked fail-closed. |

The synthetic fixture has three small executable sections, rather than the real
single section at raw offset 244, linked `C1B100F4`, size 55,804. It uses the
approved lifecycle PCs, constructor table `C1B1DAF0`, terminator/destructor-table
location `C1B1DAF8`, walker `80241178`, constructor `C1B1D414` and leaf
`C1B1016C`. The emitted leaf increments synthetic BSS twice. This proves the
dispatch/control-flow and preserved context, **not** the real FP initialization,
destructor-registration effects, actor behavior or full private composite.

## TDD and verification

Linux AArch64, GCC, isolated build `/tmp/opencode/ww63/host`, read-only pinned
RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`. All builds were `-j 1`;
live `/proc/*/comm` checks found no Vita3K before compilation.

- Initial red: **71 methods, 3 failures**, including the positive translated
  fixture stopped at `80240744` with the observed lifecycle rejection. The other
  two expected ship-specific errors were also masked by that gate.
  `red-build.log`, `red-tests.log`.
- First green: **71/71**, `first-green-build.log`, `first-green-tests.log`.
- Context red: after correcting the negative entry fixture's injection point,
  **19 ship methods, 4 failing cases**: r2, r13, skipped constructor completion
  and an unqualified caller resume. All now pass. The earlier uncorrected run
  also had two fixture-injection failures; they were not implementation evidence.
  `context-red-tests-corrected.log`, `context-green-tests.log`.
- Executable ownership red: lifecycle-in-data rejected by the general data gate
  instead of the explicit lifecycle ownership gate; the focused ownership error
  check is now green. `executable-red-tests.log`.
- Emitter red: **2/2 negative tests failed**, because empty and truncated-header
  inputs were incorrectly accepted by the archived adapter. Both now pass.
  `emitter-red-tests.log`.
- Final ship + existing REL suite: **87/87** (22 ship + 65 REL methods),
  `final-rel-tests.log`.
- Broader host Python regressions: **872/872**, `host-regressions-final.log`.
  The first broad run had eight missing worktree dependency-path setup errors;
  an external runner redirected only dependency paths to the pinned checkout
  and reran successfully. No dependency source was edited.
- Shared public fixture generation and serial host build passed through the
  normal test setup: `public-setup-final.log`, `host/build.log`.
- Synthetic main compiled with the opt-in ON and rejected a missing contract
  **before guest execution**; OFF was restored and the complete serial host
  build rerun: `main-opt-in.log`.
- CTest **51/51**, `ctest-final.log`; `bash scripts/audit_repo.sh` and
  `git diff --check` passed.

Logs and isolated runners are under `/tmp/opencode/ww63`. Generated code has the
pinned emitter's existing inline/noinline attribute warning. Windows CI, real
Vita ARM game compilation, actual ship generation/export binding and hardware
gameplay were not tested.

## Next frontier

The ordinary composite generator still emits **no ship contract**, so switching
ON the real host option alone intentionally stops at missing-contract
initialization. This commit does not change the WW62 launcher/artifact or remove
its default-off STOP. Next, independently review and test WW44's controlled
`--ship-generation-result` consumer against the actual reviewed generation
receipt, its complete batch/address map, canonical executable hashes and exported
composite identity. Review full raw DOL embedding's memory/build cost before a
private game build. Epilog execution, unresolved handling and native unlink are
separate later contracts, not part of that export wiring or this mission's proof.
