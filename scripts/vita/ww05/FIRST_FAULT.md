# First-fault gate for WW05 runs

`first_fault.py` is the always-on safety net used by `launch.py` for both the
install and guest phases. It never launches an emulator, builds a VPK or edits a
run.

## Contract

- `decide(Observation) -> Stop | None`, with precedence: guest `[exception]`,
  then host `|E| [MemoryWrite]: Invalid write` / `|E| [MemoryRead]: Invalid read`,
  then the per-log size ceiling.
- The ceiling is 64 MiB per watched log and is inclusive: a log exactly at the
  ceiling stops the run. Size is checked after each bounded read chunk, so a
  log already at the ceiling is not read to the end first.
- `bounded_sample(lines, fault_index)` returns head 5,000, fault window 300
  (150 lines either side, clamped at the file edges) and tail 300.
- `FirstFaultWatch` incrementally tails the guest log
  `data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`, the host file
  `phases/<phase>/evidence/process.log` (a regular file written by `run_owned`,
  not a PTY) and the host log `cache/Vita3K/vita3k.log`. Only new whole lines
  are scanned.
- On a stop the runner sends SIGTERM to the owned process group, waits a short
  bounded grace, and sends SIGKILL if the group is still alive. It snapshots
  each watched log's size at stop and reads samples only up to that byte cap.
  Samples go to `phases/<phase>/evidence/first-fault-sample/`: per-log
  `head.txt`, `fault.txt`, `tail.txt`, plus `flood-stats.json`, `SHA256SUMS` and
  `verification.json`. `verification.json` re-reads the written sample files,
  counts their lines and compares their hashes with `SHA256SUMS`; it does not
  hash itself. `deletion` is recorded as `none`; the gate never deletes a log
  or a sample.
- The gate runs on every observer poll and has no off switch. When there is no
  signal and every watched log is below the ceiling, the previous activity or
  installer observer runs unchanged. A watcher error is not a pass-through:
  it stops the run rather than falling back to the old observer.

## E1 harness status

`scripts/vita/e1/launch.py` is not in this tree, so its state was not verified
here. The design note reports that its `run_phase` stops only on `PC is 0x0`,
`|E| [MemoryWrite]: Invalid write` and `Unhandled SIGSEGV`, does not stop on
`Invalid read`, does not watch the guest `ww05.log`, and has no per-log ceiling.
Treat that as out-of-tree information. E1 is not wired to this gate. When the e1
harness is next changed, it can import `FirstFaultWatch` from
`scripts/vita/ww05` and use the same observer wrapper; runs that still use the
out-of-tree e1 harness retain the gap.

## Residual risks

- **Aggregate disk:** 64 MiB is per watched log, so three logs can reach roughly
  192 MiB before the stop, plus growth during the stop/sample window.
- **Overshoot:** the ceiling is observed on a poll, so a fast writer can pass
  64 MiB by up to one poll's growth before the process is signalled.
- **Ceiling precedence:** a log already at the ceiling is inspected only through
  the first bounded read chunk; a fault later in that same log reports
  `log-size-ceiling` instead of the guest or host signal.
- **Partial evidence:** if the sample directory cannot be created, a sample file
  cannot be written, or verification fails, the run stops with a watcher error;
  any samples already written remain, but the set may be incomplete.
- **SIGTERM window:** SIGTERM is sent before sampling, but the process may append
  until it honours the signal or the bounded grace expires. Samples are capped
  at the captured size, so the extra bytes are not read, but the source logs can
  still grow during that window.
- **Anchor limit:** same-inode rewrite detection compares the last 4 KiB before
  the recorded offset. A rewrite that preserves that tail can still hide a
  generation change; identity changes and size shrinkage are always refused.

## Synthetic checks

No emulator, VPK, service or private log is involved:

```sh
python3 -B -m unittest discover -s scripts/vita/ww05 -p 'test_*first_fault*.py'
```
