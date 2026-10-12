# VITA-WW-18: MEM1 snapshot loader

Baseline `7497874`. Producer contract: `scripts/vita/gx/EXTRACTION.md`.
No capture-lane/host changes; source pins remain read-only.

The fixture accepts either an existing raw image or a versioned full/sparse
MEM1 snapshot manifest. The latter binds a binary and its physical regions to
the exact captured FIFO using SHA-256. `ww05.mem1.json` is picked up automatically
beside the selected journal; `--snapshot` also works explicitly on both extractor
and bridge CLI. Mismatched captures, corrupted payloads, invalid/overlapping
extents, duplicate keys and unsafe file paths refuse before fixture creation.

Source-backed safety decision: pinned `DolGuestResolvedRange::available` can
extend beyond the originally requested size; later texture/array decoders rely
on that span. Guarding the initial request alone is insufficient. The new
host-only wrapper both checks the requested range and clips `available` to the
captured contiguous extent. Cached/uncached pointer normalization reuses pinned
`dol_gx_recomp_guest_to_physical`, not a guessed address mask.

The legacy CLI needs address-indexed backing memory. A sparse backing therefore
has inaccessible padding, with a separately validated coverage map. A missing
read fails the replay with address/size context rather than obtaining zeroes.
Only the child bridge sees the coverage environment variable. Its explicit
version acknowledgement prevents an old executable from silently ignoring
coverage. No manifest/parser transport is added to the live Vita renderer.

## Verification on Linux

- Test-first: new snapshot tests failed on the missing loader/fixture argument;
  the bridge behavior test showed an unguarded hole being accepted. After the
  loader/guard implementation, all **44 Python tests** and four native suites
  pass normally and under **ASan/UBSan**, without diagnostics.
- Native cases prove physical/cached/uncached pointers map to the same captured
  bytes, resolver `available` stops at a hole, cross-boundary requests refuse,
  adjacent regions merge and malformed coverage is rejected.
- Private proof directory:
  `build-f6/ww18/mem1-proof-8c90da58ee3c49e1894b6ef419338b27`.
  The same synthetic 521-byte indirect FIFO passes with full RAM and with only
  **128 captured bytes**: one usable plan/program, two guarded texture regions,
  `verdict=ran`. Direct bridge replay of the staged reusable manifest also passes.
- Removing the second texture range gives
  `snapshot memory missing address=0x00001800 size=64`, `verdict=rejected`.
  This proves the zero padding is not silently accessible.

The real `5a4fb3...` prefix still awaits its same-run snapshot from lane 16.
No real resource bytes were synthesized or committed. A matching digest binds
files, not capture timing: changing resources across a long FIFO still requires
time-matched snapshots or write records. These checks prove the loader and
translation boundary, not GPU pixels or physical Vita behavior.
