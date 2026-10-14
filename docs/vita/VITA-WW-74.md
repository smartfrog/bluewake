# WW74: ship staging below the module-region ceiling

## Diagnosis and scope

The worktree started clean on `ww74-ship-aperture` at integration commit
`e6b00290365718d2eace8183c40f282f0ae1267e`. Remote branches were checked;
`gh pr list` could not run because `gh` is absent. Nothing was pushed.

WW73 identifies the retail `DynamicModuleControl::do_link` assertion in DOL
`80240BB4–80240F54`, reported as `DynamicLink.cpp:615`, message `Halt`:
`(u32)mModule + mModule->fixSize < 0x82000000`.
Its read-only receipts are `/tmp/opencode/ww73/REPORT.md`, `assert-site.txt`,
`backchain-map.txt` and `ship-rel-header.txt`. WW72's journal at
`build-f5/ww56-ww72/data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`
publishes module 168 at `82000000` on lines 139670/139672 and hits the assertion
at line 177915. The real raw REL's big-endian word at `+0x48` is `E3B8`, so
the asserted sum was `8200E3B8`. Module 1 at `81822E80` passes the same bound.

WW66's fragmentation diagnosis still stands: the ship needs 74,976 aligned
bytes, but the largest aligned legacy gap is 65,300 bytes. The catalog's masked
end is `81F80954`. The fallback is necessary; removing reservations is not a fix.

## Correction

The ship-only fallback is now **`[81F90000, 82000000)`**, 458,752 bytes, entirely
above the catalog end and below the game ceiling. Its folded tagged view is
`[C1F90000, C2000000)`. This supersedes **only the aperture choice** in
[VITA-WW-63-SCRATCH.md](VITA-WW-63-SCRATCH.md).

Legacy first-fit stays first and unchanged for other actors. Only module 168
with its enabled exact ship contract can fall back. Both attempts retain live
image, folded code and canonical section reservations and alias collision gates.
After successful fallback selection, a widened sum of the candidate base and
`be32(raw + 0x48)` must be strictly below `WW_REL_MODULE_REGION_LIMIT`
(`82000000`). Equality, crossing and 32-bit overflow are refused with
`ship REL staging fix area reaches the module-region ceiling`, before alias
creation, image ownership or guest publication. Existing error cleanup frees
the raw image and section metadata. No gameplay or timing bypass was added.

## Tests and assumptions

Tests-first commit: `4bd5615d8b4239b0fb6af820dc23d36448b48f1f`.
Its clean committed red run has 105 ship/REL methods and 10 expected failures;
both new non-ship legacy tests pass. The corrected focused suite passes 105/105.
Coverage includes free fallback, a live staging prefix, folded tagged ranges,
full windows, unowned aliases, controller preservation and no partial publication.

The real ship's `fixSize` is smaller than its capacity, so occupancy alone cannot
make its fix area cross a window that still fits the image. Boundary tests use
matching synthetic contracts and occupy the prefix to force base `81FE0000`:
`fixSize=1FFFF` passes; `20000`, `20001` and `FFFFFFFF` fail closed. The public
fixture carries the real `E3B8` word without game bytes; a separate read-only
real-fixture parity check records its hash and compares that word with staging.

Linux AArch64 candidate checks pass: 105/105 focused ship/REL methods, 907/907
full host Python methods, 51/51 CTest, serial host build, Cortex-A9/NEON
hard-float compile of `rel_service.c`, real-fixture parity, repository audit,
attribution and diff checks. Exact committed-revision rechecks are recorded
under `/tmp/opencode/ww74/`; `REPORT.md` records commands,
results and the final implementation SHA. Heavy outputs are in this worktree's
`build-f5/ww74`; dependencies are borrowed read-only from the pinned checkout.
The first full-suite attempt was stopped to isolate additional hard-coded paths;
it rebuilt the shared `build-f5/host-tests/checkpoint` binary, not tracked source.

No emulator, real ship trial, Windows CI or Vita hardware gameplay was run.
Independent review, integration and the separately dispatched real trial remain
with the coordinator; there are no blocking implementation questions.
