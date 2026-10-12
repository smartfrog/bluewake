# VITA-WW-18: private draw-journal replay

`extract.py` copies a contiguous `[gx-command]` journal from index 1 into
`stream.fifo`, records source/FIFO/report/MEM1 SHA-256 hashes in `capture.json`,
and optionally runs the existing pinned frontend/DrawPlan/Vita translation
bridge. It never synthesizes commands or guest memory. Outputs belong under
`build-f6/ww18`, stay private and must not be committed or uploaded.

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/extract.py \
  --latest /home/fred/projects/zeldaWWHDRecomp/build-f5/ww05 \
  --min-draws 2 --output build-f6/ww18/real-draw-sequence --replay
```

The selector counts draws in the journal bytes, not the renderer's totals.
When a journal exists, the explicit form also accepts a contemporaneous memory
snapshot from the capture owner:

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

Report `decoded/logged/pending` is kept separately from captured draw count.
`all-reported-commands` covers decoded commands, not unlogged pending bytes;
a partial tail is named explicitly. Nonzero feature gaps, skipped plans,
refused programs/copies or lost draw programs return failure, retaining the
bridge text in `replay.txt`. `verdict=ran` proves translation, not GPU pixels.
Existing fixture directories are never overwritten.

## Exact journal contract for the capture lane

The existing `--continue-gx 64` capture setting logs only the first 64 complete
commands, not 64 draws. Raise that limit in the host/harness lane sufficiently
to retain initialization through the desired draws; this renderer lane does
not change that setting or the host. Extraction expects each complete command
in this exact shape (uppercase hex, no truncation or ellipsis):

```text
[gx-command] index=1 opcode=61 bytes=5 pc=80325DAC completion_pc=80325DB8 sp=8040CEB0 hex=610F0000FF
```

Indices must be contiguous from 1 across the captured initialization/draw
sequence; `bytes` counts the **entire** opcode + header + payload. Draw records
must include all vertex bytes, not just their three-byte primitive header.
Unknown gaps, reordered records, lowercase/truncated hex or a record starting
at the draw's index rather than index 1 are refused. Other ordinary log lines
may be interleaved. No synthetic state is prepended to a draw-only slice.

The adjacent `ww05.report` supplies `GX_STREAM ... decoded=N logged=M ...
pending=P ...` and `RENDER ... draws=D ...`. The tool verifies M against the
journal count, retains unlogged/pending extent and does not equate D to logged
draw records. Nonempty display lists may contain additional draws in memory;
direct journal draw counts do not include those nested commands. Supply raw
MEM1 starting at physical address zero for `--mem1`, matching that run/time.
There is no journal-size cap in the extractor. The capture owner must avoid
truncating long draw records and must retain matching texture/array/DL bytes.

## Checked on Linux

- 21 Python tests pass, including frame validation after a memory-dependent
  command, sequence gaps, metadata, pending tails, memory refusal, silent draw
  loss and nonzero feature gaps.
- The synthetic two-clear-quad journal round trip produced 509 bytes, two
  bridge plans/programs, no skips/refusals and `verdict=ran`.
- Real run `10c1b74cc7b647aab66bf18882e53a39` reports 1,171 decoded commands
  and one renderer draw, but only logs 64 commands/423 bytes and **zero captured
  draws**. Its extracted setup prefix replays successfully with zero plans.
- The first longer journal is now available: run
  `5a4fb3b0ba4c46efa69ab545e225c8dd`, limit 2,048, two captured draws in
  14,464 bytes. The report totals are four draws/4,249 decoded commands, so this
  remains a command-prefix capture with five pending bytes.
- Extraction succeeds and retains its provenance under
  `build-f6/ww18/real-indirect-replay`; guarded replay reports
  **`verdict=memory-required`**, not a successful real draw replay.
  No MEM1 snapshot is adjacent to the capture. Even though both captured quads
  have inline positions/colors, the frontend resolves an initialization texture
  before them: command 294, BP 94, FIFO offset 1,904, guest address 0039D860.
  A diagnostic replay against an entirely unmapped/empty address space refuses
  that BP handler before any draw. This is not permission to fabricate resources
  or delete unused-looking initialization commands.
- Supply the same run's memory snapshot/resources to replay this exact prefix.
  No host or WW05 harness changes were made here.

Source-backed assumption: journal order/index/hex bytes are the capture contract
in `capture.py`; renderer totals are not FIFO evidence. When that evidence
arrives, run the selector first, before further synthetic feature work.
