# WW81: validate the owned section table in its expected relocation form

## Ground truth and scope

The worktree started clean on `ww81-relocation-aware-guard` at integration
commit `e614b1ee20ab267795c266eaa6311bfdf8e740b2`. Remote branches were checked;
`gh pr list` could not run because `gh` is absent. Nothing was pushed.

WW80's private `/tmp/opencode/ww80/EVIDENCE.md` records the exact table mutation
between ship publication and the SDK's ICBI at `803031D8`. Journal
`build-f5/ww56-ww80/data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`
lines 139673–139679 contain these seven differences, with unchanged count and
every size. WW79's disassembly identifies the SDK stores at `80305200`
(table pointer), `80305258` (entry addresses), and `80305270` (BSS address).

| Field | Pristine snapshot | Exactly relocated at base `81F90000` |
| --- | --- | --- |
| Header table pointer | `0000004C` | `81F9004C` |
| Entry 1 flags | `000000F5` | `81F900F5` |
| Entry 2 flags | `0000DAF0` | `81F9DAF0` |
| Entry 3 flags | `0000DAF8` | `81F9DAF8` |
| Entry 4 flags | `0000DB00` | `81F9DB00` |
| Entry 5 flags | `0000DEE0` | `81F9DEE0` |
| Entry 6 BSS flags | `00000000` | Observed `807FF620`; bounded allowance below |

The real fixture has 21 sections: entry 6 is BSS, entries 7–20 are empty.
WW78 established that dispatch uses only the immutable host snapshot, not
the current table words. No dispatch mapping, raw identity, relocation
certification, ownership or lifecycle contract is changed here.

## Correction and four call-site groups

`ww_host_rel_sections_match` is unchanged: strict pristine equality remains
at both link-time callers. The new `ww_host_rel_sections_match_post_link`
accepts either the complete pristine table or the complete expected relocated
table. It does not accept a mixture of pristine and relocated nonzero entries.

| Caller | Base revision location | Validation after this change |
| --- | --- | --- |
| Link/publication | `rel_link.c:317,579` | Unchanged strict pristine equality |
| ICBI/branch resolver | `rel_link.c:553` | Pristine or exactly relocated |
| Code-ownership query | `rel_service.c:83` | Pristine or exactly relocated |
| Constructor entry/continuation | `rel_link.c:454,487` | Pristine or exactly relocated; separate whole-image hash still required |

For relocation, the header pointer must equal `guest + snapshot_section_table`.
Each nonzero snapshot offset must equal
`(guest + (snapshot_flags & ~1u)) | (snapshot_flags & 1u)`.
Widened sums reject overflow; executable bits, all sizes and count remain exact.
Reads use the bounded snapshot table offset, never the current absolute pointer.
Missing metadata/storage, malformed snapshot bounds and unlinked relocated
images are refused. The existing strict helper remains the pristine fast path.

For a zero snapshot offset, the relocated value must be zero or word-aligned
in **`[80000000,81800000)`**, the retail 24 MiB main RAM bound, expressed with
`GC_RAM_BASE + GC_MAIN_RAM_SIZE`. WW80's `807FF620` is a separate game allocation,
not within the ship staging window. This is an address allowance, not proof
of allocator ownership or of an entire BSS extent. It applies to zero-offset
entries only and does not admit tagged aliases, extended MEM1, the ship window,
misalignment or any alternative address for a nonzero-offset section.

All existing refusal texts remain unchanged. The WW79 capture and its bounded
eight-entry loop are intact: genuine resolver mismatches still journal before
the original refusal; the expected relocated path emits no mismatch capture.
The guard never refreshes the snapshot, raw hash or guest memory.

## Tests and evidence

Tests-first revisions are `9a70082ba7273d02a3cf809d497d169e364909e5` and
`6c1bb9c371b513999b11e10fd4d14bea0d5f00c4`. The second corrects the test-only
BSS lookup for the real fixture's trailing empty sections. Its clean red
runs have exactly **20 expected acceptance failures** in both synthetic
(13 methods, including the profile link-time refusal) and real (12 methods)
suites, with no setup errors. Production code was unchanged at both revisions.

The same probe assertions run against the public synthetic contract and a
private native harness with the exact raw DOL/REL, generated ship contract,
section catalog and compiled chunk hashes. Raw REL SHA-256 is
`945a6493a7f9d353657b07048b26e63c8fd53ca147df9d1b5378c44f0ef2280e`.
Normal materialization/link/publication stages it at `81F90000`; the probe
asserts all seven WW80 values verbatim before exercising the post-link paths.
No translated game instructions or SDK linker are executed by this harness.

Coverage includes pristine and relocated forms at resolver/ICBI/ownership and
both constructor guard calls, strict ship/profile link-time rejection, zero
BSS and aligned endpoints, outside/tagged/ship-window/misaligned BSS values,
size/count/offset/exec-bit/table-pointer tampering and mixed forms. Probe checks
also preserve the image metadata, immutable sections and current guest bytes.
All pre-existing test expectations remain unchanged.

Constructor tests explicitly isolate the independent whole-image hash with a
**test-only** hash checkpoint. Separate tests, on both fixtures, retain the
publication hash and prove relocation still fails that independent gate.
`ww_host_rel_raw_unchanged` and the ship prolog's raw integrity check remain
unchanged. A later real run can therefore stop at that gate: acceptance of
the section table is not constructor/prolog completion evidence.

Candidate checks on Linux AArch64 workstation `fuzhou` passed: 134 focused
methods, 936 full host methods, 51 CTests and 12 real-fixture methods. Three
existing DOL ICBI methods are also included in the final focused recheck.
Checks and the exact clean implementation SHA
are recorded in `/tmp/opencode/ww81/REPORT.md` and `checks.json`: focused and
full host Python suites, host build, CTest, Cortex-A9/NEON hard-float compilation
of both changed C translation units, exact real-fixture probe, repository
audit, attribution and diff checks. All builds are serial; heavy outputs stay
under this worktree's ignored `build-f5/ww81`. Pinned dependencies and existing
private fixture inputs are borrowed read-only. Receipts contain no game bytes.

No emulator trial, Windows CI, full VPK build or physical Vita gameplay was
run. Independent review, integration and the separately dispatched real trial
remain with the coordinator. The existing experimental ship default stays off.
