# VITA-WW-18: synchronized later-draw GPU replay

Follow-up: the later CI window is now captured and replayed. See
[gx_boot_101_replay.md](gx_boot_101_replay.md) for the exact frame-101 match and
the transparent progressive-scan notice. The missing-window statements below
describe the earlier 85-frame run, not the new delivery.

## Outcome

The supplied later draws reproduce the Nintendo logo. **Draw 10767 is a
captured black fade overlay, not a missing texture or scene draw.** It reduces
the logo's RGB by exactly **17/255** at frame 11. The resulting raw and display
PNGs are **byte-identical to the original live frame 11**.

The separate 85-frame run does **not** return to black after the logo: every
actual PNG from frames **11–85 is nonblack**, and the opened frame 85 is still
the red Nintendo logo. That run did not reach its requested later CI window and
has **zero CI-window RAM snapshots**. A post-logo black scene cannot be diagnosed
from an event that is not present in these new inputs.

No new speculative renderer fix is required for this fade. The replay lane did
need the already-verified matrix transpose from Fred's `5f3e179`; it was reused
as the renderer-only prerequisite **`f981c1e`**, with its original numerical
regression first failing on the old transform and then passing. Host sources,
pins and capture-lane runs remain untouched.

## Original inputs and exact bindings

Delivery `b276f38` identifies the closed first-window run
**`b403a0647ca6495cbe8edcd415c0b76f`**, source **`39ee655`**, under
`/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/`. Its journal has **11004
commands / 74190 bytes**, with the full earlier BP/CP/XF state. Fresh private
extractions retain that state, but select only each manifest's exact prefix.

| Snapshot | Last command | FIFO bytes / draws | FIFO SHA-256 | MEM1 SHA-256 |
| --- | ---: | --- | --- | --- |
| 1 | 10716 | 72094 / 11 | `f8869c428817908fe3a6773fd351f8f070ac0ee5f48a6065f39fb8a76d3274e6` | `4a0503bfa9fd53ac9b868522df742ef95ac5d38b0d992b5ec79c3dc92aea355a` |
| 2 | 10767 | 72806 / 12 | `9c7b233a00bbd8dc49a8fbac367f9f4b4ada68fcab1e97dc8230654207e7bb9e` | `beb257c9dfa5ffa39022c4cdd3f1e161640d7d4dc80f259dcbf17f680da661ea` |

Both guarded native preflights are `ran`, with no skips/refusals/gaps. Both
watched GPU runs are `image-captured`, with all 11/12 draws submitted and no
missing guarded memory. The two RAM versions remain distinct. The actual
**376×104 IA8 logo texture** is identical in both, SHA-256
`2a93d975f1f2865dfeb28181acb7e7d2aca90f966a5c477f10c2706725874e8a`.
No resources from the other 85-frame run were borrowed.

Private results are under:

```text
/tmp/opencode/ww18/build-f6/ww18/late-b403/
  fixture-snapshot{1,2}/
  image-snapshot{1,2}/
  inspection-snapshot{1,2}/plans.jsonl
  logo-resource-proof.json
  pixel-proof.json
```

Each image directory retains the raw PNG, display PNG, viewer, preflight,
renderer report, code/source hashes and watched process evidence. The replay
checkpoint is clean `f981c1e`. Its transform implementation/header match the
capture source byte-for-byte.

## Command-level mechanism

The first ten draws are earlier initialization/fade quads. The requested
9976–11004 interval contains exactly these two later draws:

| Command | Captured geometry | Shader/resource state | Effect |
| --- | --- | --- | --- |
| 10716 | Four-vertex textured quad, 83 bytes | One enabled color channel, valid IA8 logo, ST coordinates in [0,1], two TEV stages, red register 220 | Visible red Nintendo logo |
| 10767 | Four-vertex GX full-screen quad, 67 bytes | RGBA `(0,0,0,238)`, no texture/texgen, one TEV stage forwarding raster color/alpha | Black fade overlay |

Both have RGB/alpha writes enabled, depth testing/writes disabled and culling
disabled. Channel 0 is enabled, so the reviewed disabled-channel mask is not
zeroing a needed color. No normalized-post-matrix refusal occurs, and both
captured transforms produce finite in-view clip coordinates. The texture
decodes successfully with **26309 nonblack source texels**; IA8 needs no TLUT.

For 10767 the generated fragment really outputs the captured raster RGBA. The
blend is **FUNC_ADD / SRC_ALPHA / ONE_MINUS_SRC_ALPHA** (GL 32774/770/771).
Therefore:

```text
RGB_after = 0 × (238/255) + RGB_before × (1 − 238/255)
          = RGB_before × (17/255)
```

The comparison uses the two genuine GPU outputs, not a recolored PNG:

- Before the overlay: logo red reaches **220**, display `distinct_rgba=221`,
  PNG SHA `767feb508ed7ae3c566ae7a51da47aeb3fccdc2a3c72fedb378bd583b9d3c9fb`.
- After the overlay: red reaches **15**, display `distinct_rgba=16`,
  PNG SHA `0411aab11be85ba3df03fd4cf62c1640f010774c10378cef6800c60308d9cda6`.
- Every one of the **1566720 RGB channel bytes** exactly matches the rounded
  byte equation `(before × 17 + 127) // 255`: **maximum error zero**.
- The second replay's **raw and display PNG files both match the original live
  frame 11 byte-for-byte**, including the raw alpha, not just a summary hash.

These selected prefixes end at a draw, before GX presentation 11. The GPU
evidence explicitly says **`prefix-preview-swap-not-gx-present`**: ten genuine
GX presentations plus a labelled preview of the pending frame. No guest copy
command is synthesized. Pixel equality to live frame 11 establishes this
specific frame's output parity, not hardware or whole-game parity.

## What the 12:44 run actually captured

Run **`a31f77e552114075806cf6ec06f6c536`**, source **`b276f38`**, has **87529
logged commands / 590240 FIFO bytes / 114 draw records**. Its 85 actual image
pairs end at command **86586**, with **113 cumulative submissions**. There is
no closed guest report; its preserved disposition is **600-second timeout,
status 124 / INCOMPLETE**.

Independent parsing of all 85 actual display PNGs proves:

- Frames 1–10 are uniform black.
- **All frames 11–85 contain nonzero RGB**, not merely nonzero alpha.
- Frame 85 has **26422 nonblack RGB pixels**, SHA
  `a73716ed3eadafce2e6ea7662160b9e852fa43a55fb7b2612b0e7da9c1d96d30`;
  it was opened and shows the logo.
- Frame 38 still submits the logo plus a black overlay with alpha **9/255**.
  From frame 39 the logged new draw is still an **83-byte textured logo quad**,
  not a black clear. The fade overlay has stopped, rather than erased the logo.
- The requested MEM1 window is **101854–102936**. It was never reached, so the
  run's verified memory capture count is **zero**. The validated logo snapshots
  belong to the separate closed run above and cannot certify this later run's
  resources.

## Next handoff and verification

The logo/fade interval is now replayed and explained. To diagnose an actual
transition to a black scene, lane 16 must deliver the **first frame after 11
whose RGB really becomes zero**, its preceding nonblack frame, the exact
state-complete FIFO through that transition and matching draw-time resources.
Alternatively finish the requested **101854–102936** window to inspect the first
CI draws; do not substitute the 85-frame timeout or another run's RAM for it.

Four Linux host suites pass normally and with **ASan/UBSan**; 111 GX Python
tests pass after reusing the matrix fix. Builds use two jobs. The two Vita3K GPU
runs watch CPU/logs and stop on their
actual completed report: **27.344 s / 22.025 s** guest phases, rather than waiting
for their 120-second ceilings. No physical Vita, Windows CI, game scene,
steady-state FPS or sound result is claimed. Game bytes and rendered images
remain private and ignored, with no network or push.
