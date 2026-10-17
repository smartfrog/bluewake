# Two DuskGXM transfer levers (POC)

Date: 2026-10-18. Source: `build-f5/dusk-reference-analysis-5f3a9c21/REPORT.md`
(static analysis of DuskGXM 0.8.3; no artifact, emulator or console was run).
Both levers are off by default and have **not** been measured on hardware. They
are proof of concept changes on `poc/dusk-transfer`, not defaults for `main`.

## 1. Official clock ceiling at boot

DuskGXM requests the official Vita ceiling when it starts, only upward
(`REPORT.md` section 4, rank 1). BlueWake now has the same call, compiled only
when explicitly configured:

```sh
cmake ... -DWW05_ENABLE_MAX_CLOCKS=ON
```

Exact calls (`vita/host/clocks/vita_clocks.c`): `scePowerSetArmClockFrequency(444)`,
`scePowerSetBusClockFrequency(222)`, `scePowerSetGpuClockFrequency(222)`,
`scePowerSetGpuXbarClockFrequency(166)`.

- Only axes whose current frequency is **below** the target are touched, so the
  code can never lower a clock. An axis already at or above the target is a
  no-op.
- Each axis is applied independently; one failed request does not skip the
  others. Failures are logged (`[vita-clocks] ... raised=... failed=...`) and
  never stop the boot.
- With the option off there is no call and no log line, so a default build is
  byte-for-byte unaffected at the API level.

**Vita3K may not honor ScePower clock requests.** An emulator run therefore
proves nothing about clock gains; this lever is only meaningful on a Vita.

What a hardware run must measure: the `[vita-clocks]` line (current and target
frequencies, raised/failed masks), then the same fixed save or scene with
`WW05_ENABLE_MAX_CLOCKS=OFF` and `ON`, comparing game-thread progress per wall
time (the `[progress] turns=... cycles=... us=...` lines) or frame timing on a
hardware session. Record battery drain and device temperature on both runs. A
clock gain alone is not worth a default if heat, battery or stability regress.

## 2. Bounded disc read-ahead

DuskGXM prefetches 128 KiB through positional `sceIoPread`
(`REPORT.md` section 4, rank 3). The existing seam
(`vita/host/dvd/dvd_io.h`) now has one optional window per stream:

```sh
cmake ... -DWW05_ENABLE_DVD_READ_AHEAD=ON
```

- Off by default (`bw_dvd_io_set_read_ahead(0)`); a stream then performs the
  same direct chunked `sceIoPread`/`pread` calls as before and allocates no
  staging buffer.
- On, each stream allocates at most `BW_DVD_READ_AHEAD_MAX_BYTES` (128 KiB),
  refills with one backend read of at most that size, and serves ordinary
  `fread` bytes from the window. A seek discards the window. Reads past EOF and
  short backend reads return exactly what the direct path would return.
- Requests above the cap are clamped to 128 KiB and logged, so no configuration
  can grow the window without bound.

This is a load-time lever, not a steady-state framerate lever. A hardware run
must measure disc load time and streaming hitches with the option OFF and ON on
the same image and scene, and confirm the `[dvd-readahead] bytes=131072` line is
present only in the ON build. Vita3K filesystem timing is harness timing and
must not be reported as a Vita result.

## Test evidence (host)

- `vita/host/dvd/dvd_io_test all` (includes `readahead-bounds`,
  `readahead-short-read-eof`, `readahead-short-read`, `readahead-seek`,
  `readahead-default-off`).
- `vita/host/clocks/vita_clocks_test all` (target constants, upward-only,
  no-op at target, never lowers, mixed axes, isolated failure, host no-op).
