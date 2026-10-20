# Vita3K patch policy

Owner: Chris. Decided October 20, 2026.

The reference runtime for the Vita port is the **official Vita3K build**. Patching Vita3K is allowed, but it is a
**last resort** and a **diagnostic tool**, not a way to make the game pass.

## Order of remedies

Before any Vita3K patch:

1. Fix our own code — the host, the runtime, the renderer, `GX` coverage.
2. Fix the configuration — fresh profile, official options, exact component versions.
3. Reproduce the behaviour without a patch, and show from the evidence that it lives in the emulator and not in
   our code.
4. Only then patch Vita3K, to diagnose.

## Rules for a patch

- Each patch is a file under `patches/vita3k/NNNN-*.patch`, with the upstream Vita3K commit it applies to.
- Its record states the purpose and the measured effect, including the unpatched control run.
- Every run with a patched emulator records the Vita3K commit and the patch hashes in its evidence.
- A fix that only exists on a patched emulator is **not acceptance** of the game. The target remains real Vita
  hardware, and an emulator patch cannot replace hardware validation.
- `cpu-opt` stays on. A patch is justified to make the optimized path work, never to keep a workaround alive.
- Patches are local development material. They are not part of any release and are never presented as the
  supported runtime.

## Status

Patch status, and whether the ~310-present fault has been shown to be emulator-only, are tracked in task
`VITA3K-PATCH`. This file states policy only.
