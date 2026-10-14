# WW64: ship contract in ordinary composite generation

## Outcome and boundary

`scripts/generate_composite.py` now emits WW63's exact ship contract from the
ordinary module-168 REL and full raw DOL inputs. It requires translated module
168 and its raw REL, rejects layout/lifecycle/table mismatches, replays only the
admitted relocation semantics, and writes a deterministic generation receipt.
No patched producer, canonical sidecar or foreign generation-result input is used.

The worktree started clean at `880a0bc302aed554aaa508b8743a9f110a780f6a`, on a
new local branch `ww64-ship-composite-wiring`. Remote branches were checked
locally; the no-network restriction prevented querying open GitHub pull requests.

This is generation and public synthetic verification, not a candidate build,
integration, emulator launch or hardware/gameplay result. Independent review and
the next mission's private build remain outstanding. `WW05_ENABLE_SHIP_PROLOG`
stays OFF by default; epilog, unresolved execution and unlink gates are unchanged.

## What the host consumes

- `cmake/composite/module_exports.h` already defines `BlueWakeShipContractV1`:
  version, full raw DOL size/bytes, full raw REL size/bytes, canonical file-backed
  section catalog/count and a 64-character lowercase hexadecimal build identity.
- `cmake/composite/module_export.c` includes `rel_data.inc`. Its existing getter
  returns `&s_ship_contract` when `MODULE_SHIP_CONTRACT_PRESENT` is defined;
  otherwise it returns null. Both declarations were integrated by WW63, so no
  further ABI/header/glue edits are needed.
- With the opt-in ON, `vita/host/main.c` calls the getter and
  `ww_host_enable_ship_contract` during initialization. `ship_contract.c` checks
  the complete schema and byte-identical full-length raw DOL before admission.
- `rel_link.c` subsequently requires exact raw REL identity, the three approved
  lifecycle PCs, complete section/data/BSS exports, canonical relocation replay
  equality and canonical executable chunk hashes. Canonical sections omit BSS;
  its ordinary null-byte data export remains unchanged.

The generator verifies executable section 1 at raw offset 244, linked
`C1B100F4`, size 55,804; lifecycle `C1B100F4` / `C1B10120` / `C1B1014C`; and
eight-byte constructor/destructor sections at `C1B1DAF0` / `C1B1DAF8`, including
owned executable targets and zero terminators. It requires complete contiguous
translated chunk coverage. Missing module 168 is now an error even for DOL-only
inputs, not permission to emit a null contract. All 415 real approved modules,
including module 168, were present in the normal WW05 inputs used by WW54.

## Receipt and necessary hash correction

`generation_receipt.json` records raw sizes/hashes, executable layout, lifecycle
and table addresses, canonical section layouts/hashes/count, ship chunk ranges,
contract text hash, build identity and the existing four ABI-output hashes.
The build identity hashes the sorted, compact JSON contract fields before adding
the identity and contract text hash. It is a ship-input identity, **not** the full
composite tree digest. The existing `scripts/ios/composite_manifest.py` covers
every generated file, including this receipt, without a self-referential hash.
A failed rerun removes the prior success receipt before processing inputs.

**Necessary deviation from the expected file diff:** normal generation previously
hashed raw ship executable bytes. WW63's admitted path compares hashes against
relocated canonical bytes. Therefore four ship hashes in `module_tables.inc`
must change as well as appending the contract to `rel_data.inc`. The other 744
chunk hashes, all ranges, all translated sources and their header remain identical.

## TDD and checks

Linux AArch64, Python 3.14, native C compiler. No network or installs. Native
verification compiler commands were single invocations; no parallel build ran.

- Baseline `python3 -B tests/test_composite.py`: **26/26**.
- Test-first contract/receipt/layout/lifecycle/missing-input cycle:
  **26/34 pass, 8 failing methods**, then **34/34**. Negative parameterized
  methods stop at their first failing variant in the initial red run.
- Follow-up stale-success-receipt test: **35/36 pass, 1 failure**, then **36/36**.
- Ambiguous raw REL selection test: **37/38 pass, 1 failure** when the generator
  silently selected the first matching file, then **38/38** after rejecting it.
- Final generator suite: **38/38**, including additional branch tagging,
  relocation rejection, owned constructor target/terminator checks, additive ABI
  and unchanged translated include-set assertions, and a compiled/executed
  synthetic contract/getter probe using the existing export header and getter.
  Only the probe's CPU/descriptor dependencies are synthetic declarations.
- Repeated ordinary real-input regeneration: **755/755 files byte-identical**,
  with the same complete tree digest and unchanged approved input hashes.
- An external, single-compiler verification adapter compiled the clean pinned
  ordinary DolRecomp REL loader, `b8b534591cba8ca7cd43943a655ee6e2591cf5de`.
  Its replay of **2,075 relocations** was byte-identical to the new generator for
  **all five** real file-backed ship sections. This did not translate, compile or
  execute game instructions, and its output is not an input to generation.
- Repository audit and whitespace checks passed. The existing WW63 sidecar parser
  and contract emitter remain unchanged; no host/runtime source or dependency pin
  changed. No Windows CI, full host regression build or hardware verification ran.

Raw logs, the external verification adapter, command records and private outputs
are under `/tmp/opencode/ww64/`; no game-derived bytes are tracked.

## Real ordinary regeneration

Read-only inputs were the same WW05 translated DOL/REL directories, extracted
REL directory and `main.dol` recorded by WW54. Output:
`/tmp/opencode/ww64/composite/`. Before/after hashes of **1,998 input files**
matched. The ordinary generator reported **748 chunks, 415 REL modules, 417 code
ranges**. `regeneration-evidence.json` records the exact command and file diff.

- Previous tree: `c7e5b9c61e3fc9aba951e640f8ac05103dc13c55876af9ce41ce5b59e714347c`
  (754 files).
- New tree: `61c9ebc7812fcaceb7bbcb00424572dc4150d5685fffe668438450bbb939eaf5`
  (755 files).
- Changed: `rel_data.inc`, with its old contents preserved byte-for-byte as a
  prefix; `module_tables.inc`, with only ship hashes at indices 667–670 changed.
- Added: `generation_receipt.json`. Removed: none.
- **All 748 translated C sources and `generated.h` are byte-identical** to WW54.
  Chunk objects remain source-compatible for the next mission's reuse audit;
  this mission did not build or certify a new link graph.

The contract adds **3,955,264 bytes** of embedded raw/canonical arrays, including
the 3,822,272-byte raw DOL. Its emitted C text is 12,184,843 bytes. These are
static byte counts, not compiler peak memory, linked footprint or Vita memory-fit
measurements. The next build must account for this cost and truthfully bind the
new digest/receipt; legacy launcher/digest admission was not changed here.
