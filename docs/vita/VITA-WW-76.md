# WW76: instruction-cache maintenance of certified linked REL code

## Diagnosis and scope

The worktree started clean on `ww76-linked-rel-icbi` at integration commit
`0b76345b771f4eae2c04fc0fac348abf6f6414fc`. Remote branches were checked;
`gh pr list` could not run because `gh` is absent. Nothing was pushed.

WW75's private receipts are `/tmp/opencode/ww75/EVIDENCE.md` and
`trial-evidence.json`. Its journal's first new stop targets `81F900F4` at
ICBI instruction `803031D8`, before the ship prolog:
`instruction cache invalidation requires unmodified mapped DOL code`.
The old module-region ceiling assertion is absent, but completion of the
translated link/fix/BSS path and the prolog remain unproved.

## Correction

The ICBI handler retains its existing effective-address canonicalization and
tries the existing DOL guard first. Only when that guard refuses does it ask
`ww_host_rel_address` whether the target resolves to owned translated REL code.
That existing resolver requires linked state, unchanged guest publication,
the validated section table, and membership in an executable section. In the
ship fallback window it also requires the existing exact ship contract.
The link machinery already certified complete translated coverage and
raw-to-canonical relocation/chunk identity before publishing those sections.
No additional range, allow-list, trust flag or certification path was added.

The coherent static-composite host has no instruction-cache blocks or mutable
compiled code to evict. Accepted REL targets execute the same memory fence as
the DOL path, without dispatching a prolog, changing bytes or interpreting
ICBI as a branch. Range membership uses the target byte, not a rounded-down
cache-block address: starts and unaligned interior/last bytes are accepted;
bytes outside the half-open executable range are not newly admitted.

The DOL guard, original refusal message, cache operations, linker, lifecycle
gates and experimental defaults are unchanged. In particular the old DOL
guard also accepts mapped RAM data with no changed text in its cache block;
that existing behavior is preserved, not tightened by this correction.

## Tests and evidence

Tests-first commit: `436a87b109374034ebcdda6095b211c5aea4a376`.
Its clean red run has 51 methods and exactly four expected acceptance failures;
existing DOL text/physical/uncached, data and modified-text cases pass.
The public fixture covers start, unaligned interior, last byte, tagged staging
alias, adjacent boundaries, cleared publication, unlinked state, collection,
missing ship contract, and the exact unknown-target refusal text/context.
Unlink/collection tests arrange the completed state directly: ship unlink
itself remains intentionally unsupported and its existing refusal test passes.

A separate private native probe reuses the same ICBI assertions with WW75's
exact generated contract, raw DOL/REL, section catalog and compiled chunk
hashes, through normal materialization/link/publication. Raw REL SHA-256 is
`945a6493a7f9d353657b07048b26e63c8fd53ca147df9d1b5378c44f0ef2280e`.
It stages at `81F90000`; executable bounds are **`[81F900F4,81F9DAF0)`**,
not the allocation or `fixSize` range. Its red run fails the same four cases;
the corrected probe passes **12/12**. Game bytes and heavy outputs stay in
this worktree's ignored `build-f5/ww76`; receipts contain no game bytes.

Linux AArch64 candidate checks pass: **118/118** focused ship/REL/DOL methods,
**917/917** full host Python methods, **51/51** CTest, serial host build,
Cortex-A9/NEON hard-float compile of `boot_host.c`, the exact real-fixture
probe, repository audit, attribution and diff checks. Clean committed-revision
rechecks, commands, exact SHAs and reviewer file:line references are recorded
under `/tmp/opencode/ww76/REPORT.md` and `checks.json`. All builds are serial;
pinned dependencies and WW75 inputs are borrowed read-only.

No emulator, real boot trial, Windows CI or physical Vita gameplay was run.
Native fixture acceptance is not evidence of a completed retail linker or
prolog. Independent review, integration and the separately dispatched trial
remain with the coordinator; there are no blocking implementation questions.
