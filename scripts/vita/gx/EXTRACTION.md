# VITA-WW-18: private draw-journal replay

`extract.py` copies a contiguous `[gx-command]` journal from index 1 into
`stream.fifo`, records source/FIFO/report/MEM1 SHA-256 hashes in `capture.json`,
and optionally runs the existing pinned frontend/DrawPlan/Vita translation
bridge. It never synthesizes commands or accessible captured bytes. Outputs belong under
`build-f6/ww18`, stay private and must not be committed or uploaded.

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/extract.py \
  --latest /home/fred/projects/zeldaWWHDRecomp/build-f5/ww05 \
  --min-draws 2 --output build-f6/ww18/real-draw-sequence --replay
```

The selector counts draws in the journal bytes, not the renderer's totals.
The standard adjacent manifest is **`ww05.mem1.json`**, referring to a binary
**`ww05.mem1.bin`**. Extraction automatically loads it when no explicit memory
argument is supplied. For a manifest elsewhere:

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/extract.py RUN/ww05.log \
  --snapshot RUN/ww05.mem1.json --output build-f6/ww18/replay-RUN --replay
```

Direct FIFO replay accepts the same manifest:

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py FIXTURE/stream.fifo \
  --snapshot FIXTURE/memory.json
```

For a **visible GPU image** rather than translation counters alone, add
`--image-output build-f6/ww18/images/NEW` to extraction. The PNG, raw RGBA PNG,
local HTML viewer and SHA/source manifest are retained privately. See
`vita/renderer/gx_visible_image.md` for capture boundaries, the surface-sync
setting, explicit presentation-alpha handling and live-host integration limits.
No image is synthesized when memory is missing.

The legacy explicit form still accepts a raw, contiguous memory image starting
at physical address zero, without a manifest:

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/extract.py RUN/ww05.log \
  --mem1 RUN/mem1.bin --output build-f6/ww18/replay-RUN --replay
```

MEM1 is required for indexed vertices/XF, nonempty display lists, TLUT loads or
draws after texture-image binding. This is a conservative check, not a proof
that a single snapshot contains every resource version in a long stream.
The owner must supply memory matching the draw interval; dynamically modified
resources need time-matched snapshots/write records, not a boot-time dump.
Neither contemporaneity nor MEM2 resources can be inferred from the journal.
The tool refuses to replay memory-dependent commands against the bridge's
default zeroed RAM. Full draw framing remains the pinned frontend's job.

## MEM1 snapshot v1: contract for lane 16

Capture MEM1 from the same run **before guest memory is freed**. Start with a
full 24 MiB dump when possible: initialization texture bindings need resources
even if the later clear quad has only inline vertex data. Store bytes unchanged,
including the guest's big-endian fields; do not endian-convert the RAM dump.

Full-image manifest template (replace both hash placeholders):

```json
{
  "format": "gx-mem1-snapshot-v1",
  "binary": "ww05.mem1.bin",
  "binary_sha256": "<64 lowercase hex digits: SHA-256 of the binary file>",
  "fifo_sha256": "<64 lowercase hex digits: SHA-256 of concatenated journal command bytes>",
  "regions": [
    {"address": "0x00000000", "offset": 0, "size": 25165824}
  ]
}
```

For captured regions instead of full RAM, concatenate their **actual captured
bytes** into one binary. Each entry supplies its physical MEM1 byte address,
file byte offset and byte size. Example for two synthetic 64-byte textures:

```json
"regions": [
  {"address": "0x00001000", "offset": 0, "size": 64},
  {"address": "0x00001800", "offset": 64, "size": 64}
]
```

- Physical address is a JSON integer or `0x` hex string; offset/size are JSON
  integers, not strings, floats or booleans. Size must be positive.
- Every region lies in `[0, 25165824)`, and its payload span lies within the
  binary file. Physical and payload spans must not overlap. Input order may
  differ; adjacent physical regions are merged for resolver availability.
- Cached `80000000`/uncached `C0000000` aliases are **not manifest addresses**;
  normalize them to physical addresses in the producer. The resolver still
  accepts guest pointer aliases during replay.
- The binary must be an adjacent filename, not a directory/absolute/traversal
  path. Version, required fields, both hashes and duplicate JSON keys are
  validated before creating a fixture. The binary is at most 24 MiB; at most
  65,536 regions are accepted. Unknown producer annotations may be retained but
  do not change the mapping.
- `fifo_sha256` binds the snapshot to the **logged command prefix**, not the
  entire log text, renderer byte total or unlogged stream. The producer remains
  responsible for temporal correspondence/resource versions; the digest is not
  proof that a late snapshot contains every earlier texture version.

The post-run harness can compute the two hashes using the existing parser:

```python
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, "scripts/vita/gx")
import capture

journal = Path("RUN/ww05.log")
binary = journal.with_suffix(".mem1.bin")
data = binary.read_bytes()
if len(data) != 24 << 20:
    raise ValueError("full MEM1 dump must be exactly 24 MiB")
fifo = b"".join(capture.journal_commands(journal.read_text()))
manifest = dict(format="gx-mem1-snapshot-v1", binary=binary.name,
                binary_sha256=hashlib.sha256(data).hexdigest(),
                fifo_sha256=hashlib.sha256(fifo).hexdigest(),
                regions=[dict(address=0, offset=0, size=len(data))])
with journal.with_suffix(".mem1.json").open("x") as output:
    json.dump(manifest, output, indent=2)
```

### Staged fixture and sparse safety

Extraction retains the original binary (`memory.bin`), original manifest bytes
(`memory.source.json`), reusable manifest (`memory.json`) and provenance hashes
in `capture.json`. For the legacy CLI's address-based buffer it also stages
`mem1.bin` and the coverage transport `mem1.ranges`.

Sparse `mem1.bin` has padding for storage only. **Padding is inaccessible guest
memory**, not a zero-filled reconstruction. The host bridge's guarded callback
rejects every request outside captured regions and clips `available` to the
end of the captured contiguous range. It never exposes a hole to subsequent
texture/array/DL decoding. Missing reads produce an address/size diagnostic and
a rejected replay, even if the unguarded backing buffer would have enough bytes.
Legacy raw `--mem1` remains unguarded and is for real contiguous captures only.

The Python-to-host-bridge transport uses child-only `GX_BRIDGE_MEM1_RANGES` and
an explicit version acknowledgement (`memory coverage=GX_MEM1_RANGES_V1 ...`).
An old bridge that ignores the guard is refused; rebuild using
`WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/build.py`. The renderer on Vita
does not load fixture manifests or inherit this host-only transport.

Report `decoded/logged/pending` is kept separately from captured draw count.
`all-reported-commands` covers decoded commands, not unlogged pending bytes;
a partial tail is named explicitly. Nonzero feature gaps, skipped plans,
refused programs/copies or lost draw programs return failure, retaining the
bridge text in `replay.txt`. `verdict=ran` proves translation, not GPU pixels.
Existing fixture directories are never overwritten.

## Checked on Linux

- 44 Python tests pass, including snapshot format/hash/bounds validation,
  sparse coverage, fixture tampering and stale-bridge refusal, plus frame validation after a memory-dependent
  command, sequence gaps, metadata, pending tails, memory refusal, silent draw
  loss and nonzero feature gaps.
- The synthetic two-clear-quad journal round trip produced 509 bytes, two
  bridge plans/programs, no skips/refusals and `verdict=ran`.
- Snapshot proof: the 521-byte indirect fixture passes with full 24 MiB RAM and
  with only its two 64-byte texture regions. Omitting the second region rejects
  the BP handler with `snapshot memory missing address=0x00001800 size=64`.
  Native tests check alias resolution, adjacent-region merging and clipped
  `available`; four host suites also pass with ASan/UBSan. These are synthetic
  memory-loader proofs, not a real game-frame replay.
- Real run `10c1b74cc7b647aab66bf18882e53a39` reports 1,171 decoded commands
  and one renderer draw, but only logs 64 commands/423 bytes and **zero captured
  draws**. Its extracted setup prefix replays successfully with zero plans.
- The current read-only build-f5 scan finds no journal with two captured draws:
  `verdict=no-captured-draw-sequence`. A full journal from initialization through
  draws plus matching guest resources is still needed from the host/harness
  lane. No host or WW05 harness changes were made here.

Source-backed assumption: journal order/index/hex bytes are the capture contract
in `capture.py`; renderer totals are not FIFO evidence. When that evidence
arrives, run the selector first, before further synthetic feature work.
