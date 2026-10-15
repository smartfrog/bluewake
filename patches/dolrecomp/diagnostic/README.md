# Reviewed actor capture diagnostic

This is private acquisition tooling, not a dependency update. Leave the pinned
checkout and installed tools untouched.
For a copy inside another worktree, set `GIT_CEILING_DIRECTORIES` to the copy's
parent when applying patches, so Git does not silently resolve the outer repository.

In an owned source copy of DolRecomp `b8b534591cba8ca7cd43943a655ee6e2591cf5de`:

1. Apply `../0020-c-emit-ship-canonical-code-input.patch` (SHA-256
   `6c745b6515128696eeb7cb32a96000e671b3980c099059388af304547b864446`).
2. Apply `actor-batch.patch`, then copy `actor_batch_capture.h` to `src/app/`.
3. Build the C/Gekko producer serially. Set `DOLRECOMP_ACTOR_BATCH_DIR` to a
   fresh absolute private directory outside the generated-output tree. Emit the
   **entire unchanged REL directory**, with `--rel-base 0xC0400000 -j 1`.

The compiled `opening-d` delta is **81 (toripost) only**. The review ceiling is eight new
modules; it is not a runtime selector. Every reviewed ID must occur exactly
once before emission. Captures publish exclusively after all emission succeeds;
`complete.json` publishes last. A failed or incomplete batch is not consumable.
Legacy and batch capture controls cannot be combined. No capture is made for an
unselected module.

The generator's immutable `--actor-rel-batch opening-d` selection is
`{9,19,60,81,84,168,400}` and requires explicit existing provenance/title/ikada inputs.
The earlier `opening-a`, `opening-b` and `opening-c` selections remain reproducible
with their original capture groups. Toripost has 45 bytes of BSS aligned to 8
and 8/8-byte ctor/dtor tables; its SDK policy is `OWNER_PUBLISHED`. Bridge retains
`OWNER_PUBLISHED` with 4/4-byte tables; grass retains `OWNER_PUBLISHED` with its
independent 4/8-byte tables. Wood retains
`CERTIFIED_INLINE_ZERO`. No destructor execution is authorized.
Captures, generated code, manifests containing private paths, and composites
stay private. Acquisition and registration do not authorize execution: the
default-OFF host option separately checks source-owned exact identities. This
does not expand SDK zero-BSS input forms or qualify any game initialization.
Later immutable snapshots retain each earlier capture group's original receipt
and bindings; only the new delta needs fresh acquisition. A capture group's
eight-member ceiling is not a ceiling on cumulative registered membership.

## WW149A bulk producer only

For the frozen 399-new-module acquisition, additionally apply `actor-bulk.patch`
after `actor-batch.patch` and copy `actor_bulk_entries.h` into `src/app/`. This
overlay replaces the acquisition table, sets the exact 399-entry compile-time
bound and names its completion group `bulk-ww149a`. The ordinary opening-d table
and its eight-new-entry bound remain reproducible without this overlay. The old
review cap is deliberately lifted **only for this bulk producer unit**, not for
runtime admission. No generator or host selection changes are included.

Regenerate the metadata-only table in a fresh output path with:

```sh
python3 -B patches/dolrecomp/diagnostic/generate_bulk_capture.py SPEC.json OUTPUT.h
```

The generator requires WW147A's exact frozen spec SHA-256, the full ordered
1..415 census and its explicit membership flags. The table excludes
`{1,26,46,129,188,189,214,224,276,379}` and leaves inherited
`{9,19,60,84,168,400}` untouched: exactly 399 new captures, never a selected-only
producer input directory. This partition is frozen relative to WW149A's original
base: module 81 stays in the bulk table with `REL00081`, independently of the
per-wave toripost capture's `TPOST81` magic and opening-d completion group.
Each entry has its own seen counter, dynamically
allocated capture buffer, private basename and expected executable payload size.
The buffer includes the 12-byte header and one 16-byte section record in addition
to that payload. Publication refuses any unexpected total size.

The collision-free name/ID scheme follows the frozen spec, not a truncated name
hash: for the validated unique `(name,id)` pair, basename is
`<name>-<id>.ship-code-input` and magic is eight ASCII bytes `REL%05d` using its
unique ID (for example `d_a_agbsw0,2` maps to `REL00002`). The spec binds the name
to that ID; all IDs, names, basenames and magics are checked for uniqueness.
Inherited magic bytes and capture files are not rewritten. Capture bytes must
then pass independent strict metadata/EOF and original/processed replay checks;
a mismatch is a hard failure, never repaired or normalized. This is private
producer evidence only, not a certificate or game/runtime qualification.

## WW149B bulk consumer

`--actor-rel-batch bulk` selects exactly **405 certificates**: IDs 1..415 minus
`{1,26,46,129,188,189,214,224,276,379}`. Profile 1 keeps its resident contract;
the other nine exclusions still require a separate foreign-dependency contract.
The full producer input directory is never reduced. All earlier selections,
including ship-only, pair, triple and opening-a through opening-d, keep their
original bytes and identities.

The inherited seven are `{9,19,60,81,84,168,400}`. The consumer uses **398** of
WW149A's 399 captures: module 81 keeps its opening-d `TPOST81` certificate and
pin, never a second `REL00081` certificate. Both publication directories and
their exact completion receipts remain required; the bulk completion receipt
still names all 399 acquisitions, including its independently captured 81.

`scripts/bulk_rel_catalog.json` contains immutable metadata for the 398 new rows.
Reproduce it from the pinned spec with `scripts/generate_bulk_rel_catalog.py
SPEC.json OUTPUT.json`. Every original/processed identity, original alignment,
section/BSS layout, independent ctor/dtor size, walker successor and executable
chunk is checked. Disc reading covers both archived and loose members. Large
BSS stays an unbacked null-byte export, never capture or certificate payload.
`scripts/generate_bulk_actor_policy.py GENERATION_RECEIPT.json OUTPUT.inc`
reproduces the source-owned exact certificate/SDK table.

The existing **default-OFF** `WW05_ENABLE_ACTOR_BATCH_CONTRACTS` option now admits
this one exact bulk set, not opening-d, a subset, or every registered module.
Explicit ship/title/ikada prerequisites, full-set/DOL and per-member pin checks,
duplicate refusal and all-or-nothing startup admission remain mandatory before
materialization. There are 402 actor rows: 242 `OWNER_PUBLISHED` and 160
`CERTIFIED_INLINE_ZERO`; ship/ikada/title retain their existing special forms
(title remains `TITLE_INLINE_ZERO`, not an actor inline record). The policy's
FNV identity binds the table and is distinct from both the aggregate certificate
identity and the composite tree digest. No allocator, SDK input-form, lifecycle,
destructor or runtime-support authority is broadened. Admission is not proof of
initialization, gameplay or platform qualification; a separate watched trial is
still required. With the option OFF, the existing host layout and code remain
unchanged.
