# First-fault watcher support

`watchguard.py` is support for a **separately reviewed, fresh private driver**,
not permission to launch a trial. It does not fix a native crash or prove FPS.
Do not modify or reuse sealed trial drivers. A missing ABI capsule is inconclusive.

- `FirstFaultWatchguard.sample()` reads one bounded chunk per source and dispatches
  an authenticated stop at first recognition, before another line callback or source.
  Stop disposition is pending, signal-sent, terminal-confirmed or blocked.
- `WW56Ownership` reuses WW56 process/invocation helpers. Its adapters must supply
  fresh manager properties, positive bounded cgroup membership proof and delivery
  to the original PID only. Bind guests from independently verified immutable
  receipts before sampling. Never discover/adopt a new PID during cleanup.
- A terminal supervisor with a live or unverifiable guest blocks. This support
  deliberately does not invent an emergency guest-termination path.
- A stopping invocation is observation-only: the original invocation, PID/start,
  cgroup and launch nonce must remain owned. `verify()` waits on deactivation;
  `confirm_cleanup()` uses bounded terminal observation (default 10 seconds,
  yielding 100 ms, with an independent poll-count cap). Neither sends a signal
  from a stopping state. Missing terminal proof expires blocked; permission or
  identity changes fail immediately. Native fatal status and stop intent remain
  separate, even after verified cleanup.
- Exceptions retain bounded real frames and causes, observed proof and stop outcome.
  Successful later cleanup cannot clear a safety failure. `guardian_ok()` checks
  safety, native result, cleanup and publication separately, not driver exit alone.
- `IncrementalLog.read()` is unchanged. The explicit `read_bounded()` API reads
  at most 64 KiB and returns at most 256 lines, retaining at most 2 KiB of an
  unterminated line and one chunk of unprocessed data. Do not mix the two APIs.
  `--bounded-watchguard` explicitly adopts it in `run.py`; final drains yield after
  bounded work and report partial extents, rather than claiming a full drain.
- `summarize_sources()` runs only after confirmed stop/cleanup. It streams at most
  eight sources, retains first-16/tail-16 nonoverlapping match examples per source,
  and budgets JSON to 256 KiB, including escaping. Counts are per source, not unique
  events. Hashes identify the read extent; interrupted scans are partial. Oversized
  lines have exact clipping counts but classification is explicitly prefix-only.
  Keep original logs by path/hash; never delete old evidence to reduce output size.

Synthetic checks (no real signals, services or private log data):

```sh
python3 -B -m unittest discover -s scripts/vita/ww56 -p 'test_*.py'
python3 -B scripts/vita/ww56/watchguard_fixture.py
```

The fixture also accepts `--legacy-source PATH` (AST-isolated synthetic terminal
and first-fault drain replays; expected exit 1 on the old helpers), `--template-source PATH` (actual template
log methods with synthetic files/adapters), and `--guardian-source PATH` (actual
guardian rejecting driver-zero safety/native failure). Legacy traceback output is
a new synthetic reconstruction, **not** a recovered historical crash stack.

`--postflight-template-source PATH` runs the actual template collector against
trailing-fault, nonfault-success and previously recognized live-fault scenarios.
Any postflight match must reject the final verdict without changing native status
or delivering another signal after cleanup.

Before any trial, obtain fresh independent review of the entire tracked diff and
the actual private trial/guardian files, their hashes, launcher SHA and adapters.
Failed ownership or cleanup blocks both sealing and subsequent launches.
