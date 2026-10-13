# VITA-WW-18: what the first 101 frames display

## Result

**The visible output is still the red Nintendo boot logo, with fade-in and
the beginning of fade-out. No title screen or 3D scene is visible.** The first
new non-logo draw, **command 102648**, prepares the message **“Screen display
has been set to progressive scan mode.”** Its captured raster alpha is zero,
so it contributes no visible pixels at frame 101.

All three synchronized prefixes were replayed through the actual vitaGL/Cg
backend. The third replay's **raw and display PNGs are byte-identical to the
live frame 101**. The new run's frame 11 also matches the previously certified
logo replay, raw and display. No renderer fix or invented scene input is needed
to explain these captured outputs. The title screen has not been reached in
this evidence; its later timing or successful arrival is not established.

## Authentic input and exact replay extents

The original run is **`3f5aaa45e50f4c35b8785969f3b5bf5c`**, source
**`5f0ed9ec292246c16e1d0b941bf1d5c219d80444`**, under
`/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05/`. Its closed report ends
successfully with **`scene MEM1 draw window capture complete`**: **102936
logged/decoded commands, 693346 FIFO bytes, 132 draws, 101 presentations**,
zero skipped plans/program refusals and three complete **24 MiB** snapshots.
The VPK SHA-256 is
`f6236689d14cab7e39986c2dc8d740f6a3a9c246d9c6d4c46e918f26b4c38b0d`.

Each fresh extraction uses its original manifest and exact whole-command
prefix, including all earlier BP/CP/XF state. All three snapshots are before
their selected draw, with **100 presentations already completed**.

| Snapshot | Selected command / cumulative draw | FIFO bytes | FIFO SHA-256 | MEM1 SHA-256 |
| --- | --- | ---: | --- | --- |
| 1 | 102594 / 130 | 690767 | `7a70ca6f7c6bcdd30d6381115160fd68b48fe36ba198971b1cd073a9957800d8` | `177ee1e56955c32df727760600fa1184e6f4179922d61bb068af659e6656be9e` |
| 2 | 102648 / 131 | 691250 | `c661436c938f760c9cdd93e9562cc2dfa378655726fdb2e9844d6a81f4a2681b` | `fe2ce415e5dc6e407c64870c52b26e5eec5a1994a8b40129da245bdf04ffd475` |
| 3 | 102699 / 132 | 691962 | `e95068620f90e78fd087fa997449a843876ffc3a71d012abe206cd917a71989a` | `edad2d486a521d112be8b4649b6c2374ca838de159118c3a2619f7fecfa1977e` |

The replay checkout is clean **`0bcbf4d`**, already containing the verified GX
matrix transpose. Its GX translation, Cg, lighting, TexGen, snapshot resolver
and frame-capture implementations match the run source byte-for-byte. The
integration's additional GPU-finish API is not invoked by this image replay;
there was no host, pin or capture-run modification.

## What the three draws actually do

| Command | Selected state/resources | Visible effect |
| --- | --- | --- |
| **102594** | Direct four-vertex quad, **376×104 IA8** Nintendo texture, two TEV stages, red register **220**, ordinary alpha blending | Red Nintendo logo, maximum RGB `(220,0,0)` |
| **102648** | Direct four-vertex quad, **336×88 CI8** progressive-scan notice, **256-entry RGB5A3 TLUT**, vertex RGBA **`(255,255,255,0)`**, two TEV stages | **No pixel change**: texture alpha is multiplied by raster alpha zero |
| **102699** | Direct full-screen quad, no texgen, one raster-forwarding TEV stage, RGBA **`(0,0,0,17)`**, `FUNC_ADD / SRC_ALPHA / ONE_MINUS_SRC_ALPHA` | Darkens the logo by **238/255**, to maximum red **205** |

All selected draws have RGB/alpha writes enabled, depth testing/writes disabled
and culling disabled. Their translated clip coordinates are finite and visible.
The notice's second TEV stage uses `(zero, previous, raster, zero)` for both
color and alpha. Its color is valid; its output alpha is identically zero.
This is the captured guest state, not a missing TLUT, disabled color-channel
mask or rejected Cg program.

The decoded notice resource was opened and reads the quoted message in white
and yellow. This is **resource inspection, not a framebuffer image**: its alpha
was not changed to manufacture a visible in-game notice. The two actual GPU
outputs before/after command 102648 have **identical raw and display PNG files**.

The logo, CI8 bytes and palette are identical across all three genuine RAM
versions; the logo also matches the earlier frame-11 resource:

| Resource | MEM1 physical address | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Nintendo IA8 | `0x01482da0` | 78208 | `2a93d975f1f2865dfeb28181acb7e7d2aca90f966a5c477f10c2706725874e8a` |
| Notice CI8 | `0x014a1800` | 29568 | `c20c920e8f8a2b28b847e0bc2544680b06f5a99069113004c33457c48d989c8b` |
| Notice RGB5A3 TLUT | `0x014a8b80` | 512 | `2324657e48e96b30eb776ca2e609d079f677facbf273c328b61dc6f3f624fdf6` |

No RAM from the earlier logo or 85-frame run was staged into these replays.

## Pixel proof and continuity

| GPU checkpoint | Display PNG SHA-256 | Distinct RGBA | Verdict |
| --- | --- | ---: | --- |
| After 102594 | `4b4db59766e81bc6836e832389a467a168eb69e7f33410de9d5f3717730b9f77` | 220 | `image-captured` |
| After 102648 | `4b4db59766e81bc6836e832389a467a168eb69e7f33410de9d5f3717730b9f77` | 220 | `image-captured-with-coverage-gaps` |
| After 102699 | `8499c309ba7fafb187e99d069415e919a337fff717fbdf1861c2deba199dd7cc` | 205 | `image-captured-with-coverage-gaps` |

- The third replay equals the **original live frame 101 byte-for-byte**, both
  PNG variants, including raw alpha. It has **26422 nonblack RGB pixels**.
- Every one of **1566720 RGB channel bytes** after the black overlay is exactly
  `(before × 238 + 127) // 255`: **maximum error zero**.
- Frame 11 equals the earlier synchronized logo replay's raw/display PNGs;
  its display SHA remains
  `0411aab11be85ba3df03fd4cf62c1640f010774c10378cef6800c60308d9cda6`.
- **All 170 raw/display files** for frames 1–85 match the previous 85-frame
  trial byte-for-byte. The longer run continues that output, not a different
  guessed boot path.
- All 101 live PNG pairs were independently decoded and their RGB equality
  between raw/display validated. Green and blue are zero throughout; every
  nonblack pixel lies within the logo's unchanged mask. There is no visible
  notice, title scene or unexplained black return in these images.

## The full 101-frame visual sequence

| Live frames | Actual captured content |
| --- | --- |
| **1–10** | Uniform black initialization; the first nine quads disable blending |
| **11–38** | Nintendo logo fades in: overlay alpha **238→9**, maximum red **15→212** |
| **39–99** | Nintendo logo, no black overlay, maximum red **220** |
| **100** | Logo starts fading out: overlay alpha **8**, maximum red **213** |
| **101** | Logo plus transparent progressive-scan notice, overlay alpha **17**, maximum red **205** |

The actual FIFO's 132 draws classify as **91 identical Nintendo logo quads,
31 black blend overlays, 9 opaque black initialization quads and 1 transparent
progressive-scan notice quad**. Every logo draw has the same geometry,
transforms, TEV and selected texture dimensions. No 3D geometry appears here.

## Limits, verification and private evidence

The CI checkpoints retain `tlut_texture=1/2` and nonzero coverage status.
Pinned `gxcore.cpp:1044–1047` counts valid CI-format bindings, independently
of whether the TLUT resolves. In these selected draws the palette does resolve,
CI8 decoding succeeds and the output matches live pixels. **The gap was not
cleared or hidden**, and this is not a claim of general CI/game parity.

Each manifest ends at its draw, before presentation 101. Reports explicitly
retain **100 GX presentations plus `prefix-preview-swap-not-gx-present`**;
no display-copy command was invented. The exact live-frame match establishes
the selected pending frame's result. Earlier RAM versions, external aliases
and later boot state are not certified by these three draw-time snapshots.

The three Linux Vita3K GPU runs used watched CPU/log monitoring, surface sync
and private profiles. They stopped on their completed reports after **46.854,
49.326 and 48.280 seconds**, not on timeout. A private wrapper retained the
existing replay path and set a bounded **600-second ceiling**, recorded in
`launch-arguments-snapshot*.json`. The two coverage-gap runs correctly exit 1,
with all **131/132 draws submitted** and zero skipped/refused work.

Four Linux host suites pass normally and under **ASan/UBSan**. All **111 GX
Python tests**, the repository audit and whitespace checks pass. Builds use at
most two jobs; no production implementation changed for this investigation.

Private evidence stays under **`build-f6/ww18/scene-3f5aaa/`**: original-bound
`fixture-snapshot*`, GPU `image-snapshot*`, guarded plan/resource inspections,
`live-101-audit.json`, `pixel-proof.json` and `resource-proof.json`. The actual
frame-101 replay was added to **`build-f6/ww18/images/INDEX.md`** with immutable
provenance and its coverage-gap verdict. Game data and images remain ignored;
only this textual result is committed. No network, push, physical-Vita,
Windows CI, gameplay, audio or FPS result is claimed.
