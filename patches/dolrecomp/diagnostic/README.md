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

The compiled `opening-a` delta is **84 only**. The review ceiling is eight new
modules; it is not a runtime selector. Every reviewed ID must occur exactly
once before emission. Captures publish exclusively after all emission succeeds;
`complete.json` publishes last. A failed or incomplete batch is not consumable.
Legacy and batch capture controls cannot be combined. No capture is made for an
unselected module.

The generator's immutable `--actor-rel-batch opening-a` selection is
`{60,84,168,400}` and requires explicit existing provenance/title/ikada inputs.
Captures, generated code, manifests containing private paths, and composites
stay private. Acquisition and registration do not authorize execution: the
default-OFF host option separately checks source-owned exact identities. This
does not expand SDK zero-BSS input forms or qualify any game initialization.
Later immutable snapshots retain each earlier capture group's original receipt
and bindings; only the new delta needs fresh acquisition. A capture group's
eight-member ceiling is not a ceiling on cumulative registered membership.
