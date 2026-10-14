# WW83: admit ICBI throughout the owned module image

## Ground truth and scope

Started clean at `c259e637586531595859b5f2541e6857597607f3` on
`vita-integration`; isolated branch `ww83-icbi-full-image`, worktree
`/var/tmp/bluewake-ww83`. Remote branches were checked. `gh pr list` could not
run because `gh` is absent. Nothing was pushed.

WW82's private `/tmp/opencode/ww82/EVIDENCE.md` records the next stop at
`81F9DAF4`, PC `803031D8`: four bytes past the ship executable interval
`[81F900F4,81F9DAF0)`, inside section 2's constructor data. ICBI admission was
using the executable address resolver even though cache invalidation does not
require an executable target.

This change admits a target anywhere in **`[slot->guest, slot->guest + slot->bytes)`**
only after the existing linked/publication checks and WW81 relocation-aware
section guard pass. The image length, not scratch capacity or a cache-line-rounded
length, is the upper bound.

## Change and retained tripwires

`rel_link.c` extracts the existing image lookup and guard into `linked_image_at`.
`ww_host_rel_address` still maps only executable sections, using the immutable
host snapshot. The new `ww_host_rel_icbi_allowed` queries the same guarded image
lookup without imposing executable-section membership.

`boot_host.c` uses that query only for the REL alternative in ICBI. The DOL
code guard, canonicalization, out-of-image refusal text, exception context and
memory fence are unchanged. Scratch-window limits, the ship certification
requirement, linked flag, guest owner publication and widened image-end check
remain in the shared lookup. WW81 validation and WW79 mismatch capture/error
remain unchanged and run before admission. Unlink/collection does not leave an
acceptance cache.

Production diff: three files, 25 insertions and 11 deletions, mostly extracting
the existing lookup. No runtime pin, new option, dispatch expansion, guest-memory
repair, snapshot refresh or lifecycle bypass. The experimental ship default
stays off.

## Tests-first evidence

Tests-only commit **`410d74e2bc89b9037a4acf752b052e1c7727ef1c`**:

```text
python3 -B /tmp/opencode/ww83/run_tests.py icbi
python3 -B /tmp/opencode/ww83/real_fixture.py
```

Both clean red runs have exactly five expected acceptance failures and no setup
errors: pristine/relocated non-executable data, the first image byte, the last
image byte and its tagged alias. The public run has 19 methods including the
three unchanged DOL ICBI methods; the real run has 16 methods / 18 native
scenarios. Production code was unchanged at that commit.

The shared `rel_icbi_probe.h` reuses the existing ship materialization/link
fixture and asserts the real section-2 target is exactly `81F9DAF4`. The real
image is `[81F90000,81FA24D0)`: last byte `81FA24CF` is admitted; exclusive end
`81FA24D0` and the following byte are refused with the existing text. Coverage
also retains executable/unaligned/tagged admission, unknown-target context,
unpublished/unlinked/collected refusal and refusal when the relocation-aware
guard fails. The unlink-state probe retains the existing test-only state
transition; actual ship unlink remains gated by its unsupported epilog contract.
Data ICBI does not make the same target executable in the address resolver.

The exact real raw REL SHA-256 is
`945a6493a7f9d353657b07048b26e63c8fd53ca147df9d1b5378c44f0ef2280e`.
Its private harness uses the existing raw DOL/REL, generated ship contract,
section catalog and compiled chunk hashes, through normal host publication.
No translated game instructions or SDK linker are executed.

## Checks and limits

Initial candidate checks on Linux AArch64 workstation `fuzhou` pass: 19 focused
ICBI methods, 943 full host methods without skips, 51 CTests and 16 real-fixture
methods. Both changed C translation units compile for Cortex-A9/NEON hard-float;
repository audit passes. Clean committed checks, exact SHA, commands, exits and
log hashes are recorded by `/tmp/opencode/ww83/final_checks.py` in `checks.json`
and summarized in `REPORT.md`. Builds are serial (`-j1`), with heavy outputs only
under this worktree's ignored `build-f5/ww83`; existing fixture inputs and pinned
dependencies are borrowed read-only. Receipts contain no game bytes.

**Next independent gate remains fail-closed:** `ww_host_ship_publish` checkpoints
the whole-image hash after publishing lifecycle addresses, before the SDK's
section-table rewrite. `ww_host_ship_boundary` requires that hash at canonical
prolog entry `81B100F4` and on continuations while `ship_prolog_owner` is set.
With a rewritten table, prolog entry can refuse with
`ship prolog entry requires its exact translated dynamic-link caller`, before
the `entered` stamp. The constructor boundary at `80241178` (or an active walk's
continuation) also requires the unchanged whole-image hash in addition to the
WW81 guard, refusing with `REL constructor raw image changed after validation`.
These gates are not changed; passing ICBI is not constructor/prolog completion.

No emulator trial, full VPK build, Windows CI or physical Vita gameplay was run.
Independent review, integration and the separately dispatched trial remain
with the coordinator. Worktree retained for that handoff.
