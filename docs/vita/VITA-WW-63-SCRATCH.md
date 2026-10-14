# WW63 follow-up: bounded ship staging after WW66

## Diagnosis before implementation

The worktree was clean on `ww63-ship-lifecycle` at
`dff2de0c8c6135087c08ce8080628895deb3739d`. That tip was already integrated;
this follow-up stays on the same branch for the coordinator to re-merge.
Remote branches were checked. `gh pr list` was unavailable because `gh` is absent.

WW66's actual report identifies candidate
`dd8dcd86e21a7b1587b089c41cf4faf195249c40` and the scratch-allocation STOP at
`80240744`. Its journal has four REL markers: the first module-1 request, its
16,500-byte materialization at `81822E80`, its link, then the ship request at
line 139669. There is no ship materialization or prolog entry.

The raw ship is **74,960 bytes**. `ww_host_materialize_rel` rounds this up to
32-byte alignment: **74,976 bytes**, including **16 bytes of alias padding**.
`bluewake_rel_scratch_first_fit` does not add further padding to that already
aligned capacity. BSS/data exports, immutable section metadata and temporary
native read/occupancy allocations do not increase the contiguous staging request.

The offline census uses the actual generated `rel_modules.inc` and
`module_tables.inc` in `build-f5/ww54/dd8dcd8/composite`, with the resident
module-1 staging capacity of 16,512 bytes. Module-table hashes are
`c0676099d90ebf3a28a01baf2135acd179e1dace07da23987a1e6668e0efc7bb`
and `2e7d7ca8c711cdcd57a571979763e63f0a5940e527d71c450c218c22e07d4cfd`.

| Reservation inside `81820000–81F80000` | Bytes |
| --- | ---: |
| Window size (7.375 MiB) | 7,733,248 |
| Canonical sections: 635 intersecting ranges | 1,073,557 |
| Code ranges: 116 intersecting ranges | 957,812 |
| Live module-1 staging | 16,512 |
| Combined occupied union | 1,090,069 |
| Free total across 329 gaps | **6,643,179** |
| Largest raw gap | **65,316** |
| Largest usable 32-byte-aligned gap | **65,300** |
| Required staging capacity | **74,976** |

Code reservations overlap canonical executable sections; adding both category
totals would double-count them. Top gaps, with an exclusive end:

| Start | End | Raw bytes | Aligned usable bytes |
| --- | --- | ---: | ---: |
| `81A801D0` | `81A900F4` | 65,316 | 65,300 |
| `81AF0240` | `81B000F4` | 65,204 | 65,204 |
| `81D20390` | `81D300F4` | 64,868 | 64,852 |
| `81BA03A0` | `81BB00F4` | 64,852 | 64,852 |
| `81C303B8` | `81C400DC` | 64,804 | 64,796 |

The failure is fragmentation, not a shortage of total address space.
`/tmp/opencode/ww63/occupancy-r5.json` records the complete merged topology and
the calculation. No game instruction or data bytes were used in that census.

## Chosen adaptation and rejected alternatives

Chosen direction: **a separate, bounded ship-only fallback alias aperture**,
`82000000–82020000` (128 KiB). The legacy window is unchanged and remains the
first choice. Only validated module 168 with an explicitly enabled ship contract
may use the fallback after legacy first-fit fails. Both attempts reserve the
same live images, folded code ranges and folded canonical sections. Actual alias
overlap checks still cover both cached and tagged views. Raw executable-address
remapping recognizes the fallback only for its certified live ship owner.

This is an address aperture, **not extra physical GameCube RAM** and not a
128-KiB backing allocation. The existing alias API allocates only the requested
74,976-byte capacity. The region is outside physical MEM1
(`80000000–81800000`), the configured VM (`7E000000–80000000`), locked cache
(`E0000000–E0004000`) and MEM2's namespace (`90000000`, absent on this host).
All 415 canonical modules and all code ranges end at or below masked
`81F80954`; the fallback is outside that catalog. Its tagged mirror is
`C2000000–C2020000`. The pinned alias resolver checks exact aliases and then
the folded view, so both views use the same owned storage without shadowing
ordinary MEM1. Synthetic tests check this pointer identity and aperture bounds.

- **No-copy candidate rejected:** `r3 = 80AE0918` is a controller object, not
  a raw REL buffer. The journal shows its module/publication word is zero;
  `disc_service.c` reads its name from `+0x1C` and later publishes at `+0x10`.
  The provider reads a separate native image from the ISO. No verified, owned
  guest raw-buffer pointer is supplied here. Borrowing the controller would
  overwrite its structure. The fallback test preserves its non-publication words.
- **Free module-1 staging rejected:** removing its reservation still leaves a
  largest aligned gap of **65,300 bytes**, so it cannot solve this request.
  `ww_host_link_profile_list` still reads its raw header and section table on
  relink; owner-qualified constructor integrity also uses retained raw storage.
  WW60's resident profile-global authority does not prove those reads obsolete.
- **No blind enlargement or reservation removal:** no canonical reservation is
  discarded, no guest heap range is borrowed, and other modules cannot allocate
  in the new aperture. A full fallback or an unowned alias collision still stops
  before publication. Module-1 residency, lifecycle, relocation and unlink gates
  are unchanged.

## TDD and verification

Public synthetic ship input was padded with zero bytes to the **real requested
raw size**, 74,960, without adding game bytes. Public tests model the canonical
slot fragmentation; an external replay passes the **actual complete merged
occupancy topology** to the same synthetic materialization probe. This exercises
the real host allocator with the real request size, not private game code.

- Red: **34 ship methods, 3 failures** (fallback success, tagged-prefix
  reservation and alias-collision reachability), `red-tests-r5.log`.
- Exact real-topology red: STOP at `80240744`,
  `REL scratch has no bounded unoccupied range`, `real-shape-red-r5.log`.
- Green: **99/99 ship+REL** (34 ship + 65 REL), `green-tests-r5.log`.
  Exact real-topology replay stages at **`82000000`**, bytes 74,960,
  capacity 74,976, `real-shape-green-r5.log`.
- Negative coverage: both apertures exhausted, folded tagged reservations,
  unowned tagged aliases, preserved controller words, unchanged legacy/module-1
  and resident-profile tests. The existing genuinely exhausted module-1 test
  still stops rather than using the ship aperture.
- Final committed-revision rechecks: **99/99 ship+REL**, **884/884 host Python**,
  **51/51 CTest**, serial host build, audit, attribution and diff checks.
  `/tmp/opencode/ww63/*-r5.log` holds raw outputs; `identity-r5.json` records
  the tested revision. Real-topology replay is also repeated at that revision.

Linux AArch64, GCC, pinned RecompCore
`ddd031b2a7f45fcbda37a49daac9b308b8d95da1`. All builds are `-j 1` after checking
that Vita3K is not running. No emulator, integration, private actor compilation,
Windows CI or hardware gameplay was performed. The copied synthetic caller/prolog
path is a synthetic regression check, not a real actor-activation claim.

The coordinator owns integration and any next real trial. This slice changes
only raw-image staging; it does not predict or bypass the next gameplay gate.
