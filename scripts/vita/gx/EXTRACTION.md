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

## Checked on Linux

- 21 Python tests pass, including frame validation after a memory-dependent
  command, sequence gaps, metadata, pending tails, memory refusal, silent draw
  loss and nonzero feature gaps.
- The synthetic two-clear-quad journal round trip produced 509 bytes, two
  bridge plans/programs, no skips/refusals and `verdict=ran`.
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
