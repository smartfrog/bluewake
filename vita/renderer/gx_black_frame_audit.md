# VITA-WW-18: black live-frame input audit

## Outcome: scene replay is blocked by the capture extent

Run `a3eb11fc6882448c93a4688ca5bc46a9`, source
`b8e20483880120e7a56519f7cbfefe23e111f6ac`, does not contain a complete scene
journal or scene-time RAM snapshots. **213 is cumulative submitted draws at
frame 128, not draws per frame.** The preserved frames contain one to three
new submissions each. A scene-scale workload is not established by these
counters.

The RGB blackness is verified, but **its late-draw rendering mechanism remains
unresolved**. There is no justified renderer fix from these inputs. Replaying
the two available black initialization quads again would not diagnose the
requested later draws, so no such GPU replay was substituted for this task.

## Checked evidence

Read-only input root:

```text
/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/vita3k-run-a3eb11fc6882448c93a4688ca5bc46a9/
```

The independent private audit is
`/tmp/opencode/ww18/build-f6/ww18/black-a3eb-audit/audit.json`. It records all
128 raw/display PNG hashes and pixel counts, metadata, cumulative/per-frame
draw counts, validated snapshot hashes and the actual failed replay requests.
No upstream run file was edited; journal/report hashes were rechecked unchanged.

- Closed report: **133,260 decoded / 2,048 logged commands**, 216 cumulative
  draws, 129 presentations. Report SHA-256:
  `583ab08797085a27f5cf1d425f10d13d026f12f6862132f929475e95ab046198`.
- Only **128 PNG pairs** are preserved. Presentation 129 hits the live-capture
  bound and has no PNG; launcher result remains status 2 / INCOMPLETE. Do not
  promote the run to a successful scene capture.
- Every raw and display PNG has **RGB exactly zero at every pixel**, mean RGB
  zero. Display PNGs have one RGBA value `(0,0,0,255)` at 960×544 and SHA-256
  `c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5`.
  Raw images have two or three RGBA values because alpha changes; forcing
  display alpha did not remove or recolor any nonzero RGB.
- Frame 128 has cumulative `draws=213`; frame 127 has `draws=210`:
  **three new submissions**, not 213. `vita/host/frame_capture.cpp:83–100`
  writes the lifetime `WwRendererStats` counters directly into each record.
- All preserved frame records have zero skipped plans, refused programs and
  refused copies. This proves recorded submission acceptance, not pixel parity.

| Frames | New draws per frame | Last command at final frame |
| --- | ---: | ---: |
| 1–10 | 1 | 9,975 |
| 11–38 | 2 | 38,787 |
| 39–99 | 1 | 100,824 |
| 100 | 2 | 101,853 |
| 101–128 | 3 | 132,177 |

The histogram is 71 frames with one draw, 29 with two, 28 with three: 213
submissions across 128 captured frames. The final closed-report total of 216
includes the subsequent presentation that has no retained image.

## Why the supplied replay inputs cannot reach the later draws

The 8.4 MB text log is mostly execution diagnostics, not a complete FIFO dump.
`capture.journal_commands()` validates exactly 2,048 whole command records.
Their binary FIFO is only **14,464 bytes**, SHA-256:

```text
4f9b7592e088442ab9156410b9e5b1517e5a22e18f10d6623d2e67d6a1e13149
```

Its only draws are commands **990 and 1962**, two four-vertex direct
position/RGBA8 quads, each 67 bytes, with `(0,0,0,255)` at all vertices.
There are no later draw payloads, effective BP/XF state changes, array reads,
texture resources or display-list bytes recoverable from the counters.
The log contains no alternative gather-pipe byte stream, and the run inventory
contains no complete raw FIFO asset.

Both 24 MiB MEM1 files are validated against their original snapshot manifests.
Snapshot 2 is taken before command 1962 at **24,342,927 cycles**, with RAM SHA:

```text
8cc8414e03e8f8775ef493ac5db0bb7bc95fd7f84351e4f5f966e17773ee884b
```

Frame 11 is at **150,764,580 cycles / command 11004**, and frame 101 is at
**879,863,624 cycles / command 102936**. Their resources were not snapshotted.
The manifests bind the logged initialization FIFO, not either late interval.

The source explains this limit:

- `vita/host/gx_stream.c:24–42` stops journal output at `stream.log_limit`.
- `gx_stream.c:49–53` only requests MEM1 capture for draws inside that limit.
- `vita/host/mem1_capture.c:8–22` consumes the bounded snapshot count at those
  first eligible draws. Here both snapshots are exhausted during initialization.

Actual requests through commands **11004** and **102936**, using `extract.py
--through-command ... --snapshot ... --replay`, both exit 1 with the existing
whole-command extent refusal. No fixture or emulator process is created. The
requests and exact errors are retained in the private audit. No FIFO prefix,
producer timestamp or digest was rebound to pretend the later bytes exist.

## The two review fixes and the late CI counter

The run's `gx_lighting.h`, `gx_texgen.h`, `gx_texgen_sink.h`, `gx_vita.cpp` and
`gx_cg.cpp` are byte-identical to the corrected lane at `882818c`; their full
hashes are recorded in the audit. Its renderer is wired through `TexGenSink`.

- The unsupported normalized post-matrix form is explicitly refused by this
  guard. Zero skipped plans and no `unsupported_texgen` diagnostic rule out
  the old **silent acceptance** defect in the recorded run.
- The 0/1/2 color-channel mask matches the pin; the four existing Linux host
  suites were rerun successfully, including numerical mask/Color1 regressions.
  Whether the *actual later guest state* intentionally chooses a zero color
  channel cannot be determined without its missing FIFO. Do not claim that a
  legitimate zero mask, blend/alpha state, texgen, projection/depth/culling or
  texture/TLUT values have been excluded.
- `disable-surface-sync: false` is present in the emitted guest config. The
  known surface-writeback switch is already enabled, and raw RGB is black too.
- The sole late diagnostic is `tlut_texture`, starting at frame 101 with value
  2 and reaching 56 at frame 128. The pin's
  `GXRuntime/graphics/gxcore/src/gxcore.cpp:1044–1047` increments it for **every
  valid CI-format texture**, irrespective of palette resolution. It cannot by
  itself prove missing palette bytes or explain black pixels.

## Exact handoff to lane 16

1. Capture a byte-exact state-complete FIFO through **frame 11 / command 11004**
   first; the first extra-draw interval is **commands 9976–11004**. Preserve the
   earlier BP/CP/XF state too, or provide an explicit equivalent state snapshot.
   Do not label the extra draw a scene until its geometry/state is inspected.
2. Take matching RAM/resource snapshots **before the actual later draws**, not
   the first two initialization draws. Preserve versions when arrays or textures
   change between draws. Bind each exact FIFO extent to its original digest and
   record command/frame timing separately.
3. Capture the first CI transition through **frame 101 / command 102936**, whose
   interval is **101854–102936**. Include the actual indexed arrays, textures,
   TLUTs and display-list bytes used by those draws. The current MEM1-only format
   does not capture external aliases/MEM2; explicitly supply required resources
   or report their absence instead of substituting old RAM or zero padding.
4. Publish cumulative counters as cumulative, and add per-frame deltas if wanted.
   Keep the 128-image bound separate from the 129 presentations counter.

With those inputs the existing guarded loader and GPU replay can inspect the
first real later draw and isolate blend/write masks, colors, texgen, resources
and visibility. Until then, **no scene replay and no causal renderer fix are
claimed**. No host source, pinned source or capture-lane run was changed here.
