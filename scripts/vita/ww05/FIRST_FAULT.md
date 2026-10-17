# First-fault gate for WW05 runs

`first_fault.py` is the always-on safety net used by `launch.py` for both the
install and guest phases. It never launches an emulator, builds a VPK or edits a
run.

## Contract

- `decide(Observation) -> Stop | None`, with precedence: guest `[exception]`,
  then host `Invalid write` / `Invalid read`, then the per-log size ceiling.
- The ceiling is 64 MiB per watched log and is inclusive: a log exactly at the
  ceiling stops the run.
- `bounded_sample(lines, fault_index)` returns head 5,000, fault window 300
  (150 lines either side, clamped at the file edges) and tail 300.
- `FirstFaultWatch` incrementally tails the guest log
  `data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`, the phase PTY log
  `phases/<phase>/evidence/process.log` and the host log
  `cache/Vita3K/vita3k.log`. Only new whole lines are scanned.
- On a stop the runner sends SIGTERM to the owned process group, then writes
  `phases/<phase>/evidence/first-fault-sample/`: per-log `head.txt`,
  `fault.txt`, `tail.txt`, plus `flood-stats.json`, `SHA256SUMS` and
  `verification.json`. `deletion` is recorded as `none`; the gate never deletes
  a log or a sample.
- The gate runs on every observer poll and has no off switch. When no signal is
  present and every watched log is below the ceiling, the previous activity or
  installer observer runs unchanged.

## E1 harness status

`scripts/vita/e1/launch.py` is not in this tree. Its `run_phase` still stops only
on `PC is 0x0`, `|E| [MemoryWrite]: Invalid write` and `Unhandled SIGSEGV`; it
does not stop on `Invalid read`, does not watch the guest `ww05.log`, and has no
per-log ceiling. E1 is not wired to this gate. When the e1 harness is next
changed, it can import `FirstFaultWatch` from `scripts/vita/ww05` and use the
same observer wrapper; runs that still use e1 retain the gap.

## Synthetic checks

No emulator, VPK, service or private log is involved:

```sh
python3 -B -m unittest discover -s scripts/vita/ww05 -p 'test_*first_fault*.py'
```
