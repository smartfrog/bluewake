# VITA-WW-18: visible framebuffer output

## Delivery

`scripts/vita/gx/image.py` renders an extracted private FIFO/MEM1 fixture in a
fresh watched Vita3K profile, using the actual `WwRenderer`/vitaGL backend.
It writes a visible PNG, raw RGBA PNG, renderer report and SHA/source manifest
into a **new private directory**. No screenshots, RAM dumps or game resources
are committed. All evidence below stays under ignored `build-f6/ww18`.

First visible **synthetic** result:

- `build-f6/ww18/images/synthetic-checker-visible/capture.png`
- Viewer: `build-f6/ww18/images/synthetic-checker-visible/index.html`
- Manifest: `build-f6/ww18/images/synthetic-checker-visible/image.json`
- PNG SHA-256: `97ed8bae1cf7694508b8d2ccb621e16f85a40cd8df4b4dce7712c97179320da8`.

The image was opened and inspected: four colored quadrants at 960×544. Every
quadrant contains exactly **130,560 pixels** with one of the authored RGB values
`244,64,64`, `64,220,96`, `64,128,244`, `248,224,64`. This is actual GPU readback
of an authored GX fixture, **not CPU rasterization or an expected-image file**.
It proves the image pipeline; it does not certify game or physical-Vita parity.

## Real replay command

With lane 16's matching snapshot:

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/build.py
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/extract.py RUN/ww05.log \
  --snapshot RUN/ww05.mem1.json \
  --output build-f6/ww18/real-image-fixture-NEW \
  --image-output build-f6/ww18/images/real-NEW
```

Legacy contiguous captured RAM can use `--mem1 RUN/ww05.mem1.bin` instead.
An already extracted fixture can be rendered directly:

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/image.py FIXTURE \
  --output build-f6/ww18/images/replay-NEW
```

The source pin must be pristine; the FIFO/MEM1/coverage hashes must match before
staging or launching. Missing required memory returns `memory-required` and
does not create a fake PNG. Frontend rejection is kept. Skips, unsupported
programs/copies and nonzero gaps do not become an unqualified image success.
The VPK contains only the replay app; FIFO/RAM are staged separately in that
run's private `ux0:data/ww02`, never embedded in a public artifact.

At the last check, build-f5's new `logs/ww16-mem1-format-contract` files explicitly
say `synthetic_test_only: true`; they are **not** the missing real run snapshot.
No real image is claimed yet, and those dumps are not borrowed for real FIFO.
Numbered per-draw manifests must match the selected command prefix exactly;
their producer command/timing data must not be mistaken for a final-run dump.

## Capture boundary and the live-run seam

The additive backend `readback` reads the **most recently presented front
buffer**, following WW02's `glFinish` → `glReadBuffer(GL_FRONT)` → `glReadPixels`
pattern. It restores `GL_BACK` afterwards. Unsupported backends explicitly
refuse; no pixels are inferred from draw counters.

`ww_renderer_set_frame_capture(renderer, callback, user)` is **off by default**.
It runs after the backend's successful swap, **before held next-frame commands
resume**. The callback receives tightly packed RGBA8, bottom row first, and may
call `gx_capture::write_png`. Readback/PNG errors stop the renderer and preserve
the cause. Native tests prove the callback sees only the completed frame and
that a failed writer prevents next-frame drawing.

This is the available integration seam for lane 16's live host. The host must
opt in and link `gx_frame_capture.cpp` plus the existing `png_capture.c`. No
`vita/host/**` or WW05 harness source was changed here, so **live game-run PNG
capture is not yet enabled or verified**. Post-swap reading after the renderer
has already resumed the next frame would be the wrong integration point.

Replay captures each GX-presented frame, retaining the last captured image.
If the journal ends after a draw without a GX display copy, it performs an
explicit **preview-only swap** and labels the capture
`prefix-preview-swap-not-gx-present`. It never inserts a GX command or claims
that this prefix is a completed game frame. There is no promised viewport,
scissor or EFB feedback fidelity beyond the current renderer.

## Why a PNG was previously invisible

The local Vita3K source defaults `disable-surface-sync` to true
(`config/config.h:150`), and `renderer/src/scene.cpp:124–154` skips surface
writeback when that flag is set. The first control run produced transparent
zeroes despite successful submissions. The replay image launcher now sets
**`disable-surface-sync: false` in its private profile only**, and re-applies it
after the installer rewrites the config. Other launcher profiles stay unchanged.
The CPU/log watcher aborts silent low-CPU hangs after about 30 seconds; the
timeout remains a ceiling, not the waiting plan.

With surface sync enabled, real RGB bytes reach readback. A GX RGB-only target
can still leave framebuffer alpha zero: alpha is not screen transparency.
Vita3K's own screenshot path forces display alpha to 255
(`interface.cpp:565–568`). The replay therefore preserves **both**:

- `capture-rgba.png`: original readback bytes, flipped to top-row-first only.
- `capture.png`: presentation view with **alpha forced to 255**. Every RGB byte
  is checked against the raw PNG; the manifest declares the transformation.

This is not repair of shader colors or replacement rendering. A raw transparent
capture without a declared presentation conversion is reported blocked, not
silently delivered as a visible image. Uniform images are reported as uniform,
not assumed to show gameplay. The synthetic source explicitly sets TEV's
identity swap table; earlier grayscale fixture output is retained as evidence,
not overwritten or recolored in postprocessing.

## Reuse and verification

- Reuses `vita/feasibility/gx/png_capture.c` and its independent Python PNG
  parser/tests; no new PNG encoder or image dependency.
- Native 3×2 bottom-first input decodes as the exact top-first golden bytes;
  presentation conversion preserves every RGB byte and changes only alpha.
- Renderer tests cover capture ordering, default-off, invalid readback, reader
  failure and writer failure propagation. Existing renderer paths remain tested.
- Bridge no longer miscounts `GXCopyDisp` as a refused texture destination.
  Texture copy refusals still count. A targeted red/green regression proves it.
- Cross-build succeeds against the existing pinned frontend/vitaGL. The image
  replay needs RW placement at `81200000`: the linked text exceeds the old
  one-MiB fixture margin; the old placement was rejected for section overlap.
- The private manifest records FIFO/source/RAM/ranges SHA, pin, code revision
  and dirty state, renderer/encoder source hashes, VPK hash, watch evidence,
  capture point, exact PNG hashes and pixel statistics. The HTML viewer uses
  local files only.

Reproduce host encoder checks without a GPU:

```sh
cmake -G Ninja -S vita/renderer -B build-f6/ww18/image-host \
  -DRECOMPCORE_DIR="$PWD/ref/vita-ww-01/RecompCore"
cmake --build build-f6/ww18/image-host --target gx_frame_capture_tests
build-f6/ww18/image-host/gx_frame_capture_tests
```
