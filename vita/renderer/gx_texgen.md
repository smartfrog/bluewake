# VITA-WW-18: texture matrices

Continuation after ZTexture commit `3a530ae`. The read-only pin is RecompCore
`ddd031b`. Sources: `GXRuntime/graphics/gxcore/src/gxcore.cpp:1660–1709` resolves
XF MatrixIndexA/B to three rows per texgen and folds non-normalized static post
matrices. `gxcore_shader.cpp:1008–1094` defines raw position/normal/UV source
selection, AB11 versus ABC1, per-vertex TEXMTXIDX, ST/STQ and Color0/1 RG.

Source-backed implementation choice: calculate generated coordinates per vertex
in the existing CPU translation, using the plan's already-resolved matrices.
Upload eight STQ triples in a separate persistent vitaGL vertex buffer. Keep Q
through interpolation and divide in the fragment only for projected texgens.
This avoids interpreting packed integer matrix selectors as float attributes or
requiring dynamic uniform-array indexing on Vita. It adds CPU work; it is not a
performance claim and stays in the experimental renderer.

Supported here:

- Static XF matrix selection and per-vertex low/high TEXMTXIDX words.
- ST and STQ, including the pinned Q-zero coordinate clamp.
- Raw position and normal sources, cross-source UV0–4, packed UV5–7, and RG from
  the selected color channel. Position-source coordinates use object space even
  when PNMTXIDX has already transformed the output position.
- Non-normalized static post transforms already folded into DrawPlan constants.
- Bounds errors refuse translation explicitly; no out-of-range matrix access.

Not implied: regular color/binormal sources, post normalization, or
per-vertex post matrices not represented separately by this pinned DrawPlan.
Emboss and raster lighting have separate delivered coverage; Color texgens
consume translated vertex colors. BP SU-scale/texture-dimension mismatches
remain separate work.

Post normalization is now explicitly refused, not silently passed through.
The frontend's draw snapshot retains `post_tex_mask` and `post_tex_normalize`
(`retail_gx_frontend.cpp:298–337`), but `DrawPlan` loses them and the pin only
folds a post matrix when normalization is disabled (`gxcore.cpp:1694–1709`).
The shared `gx_texgen_sink.h` adapter retains the snapshot's support reason
until that draw's plan callback. It forwards a new Draw packet before replacing
the reason: the core can flush the previous draw during that call. No final XF
state is substituted for an earlier draw, and the pin remains unchanged.

Both the bridge and streaming renderer skip a normalized post-matrix plan with
`post-normalized texture matrix not supported` and count `unsupported_texgen`.
No shader or backend draw is submitted for that plan. `bridge.py` and
`extract.py` share the existing coverage verdict calculation: such a FIFO is
`verdict=ran-with-coverage-gaps` (exit 1), never plain `verdict=ran`.
Unnormalized folded matrices and disabled dual transforms remain legal.

The review regression tests failed first in both C++ consumers and the Python
verdict entry points. They now cover a refused FIFO, legal/refused/legal deferred
draw ordering, disabled dual transforms and zero backend submissions. Actual
authored FIFO replay through both Python entry points is recorded privately in
`build-f6/ww18/review-fixes/post-fifo-verdicts.json`: folded post matrix `ran`,
normalized post matrix `ran-with-coverage-gaps`. All four Linux host suites pass
normally and with ASan/UBSan; 108 Python tests pass. No new GPU or physical-Vita
run was used for this refusal fix.

Tests were red for missing generated coordinate output and wrong Cg varyings.
Four static/per-vertex × ST/STQ cases now pass through the renderer's real FIFO
boundary, checking exact S/T/Q and mixed matrix rows. Host tests additionally
cover low/high selector words at texgens 0/4/7, source selection, high UV packing,
matrix bounds, Q-zero clamping and color RG. A separate raw-XF fixture proves
that folded static post transforms are consumed, not discarded. All four suites
pass normally and with ASan/UBSan.

Synthetic bridge replay uses:

```sh
build-f6/ww18/gx-host/gx_bridge_tests --write-matrix-fixture build-f6/ww18/matrix-fixture
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/bridge.py build-f6/ww18/matrix-fixture/clear.fifo --mem1 build-f6/ww18/matrix-fixture/clear.mem1
```

Result: 555 bytes, 99 events, one plan/program, no skips/refusals, `verdict=ran`.
The now-supported per-vertex texture-matrix activity counter is no longer
misreported as a Vita feature gap. This is synthetic evidence, not a game frame.

Watched Vita3K run `480749c759304bc9bb1e7baef229b1be`: 57 backend submissions,
zero compile/link/GL failures. Cases 53–56 cover static/per-vertex ST/STQ; case
57 combines eight independently interpolated coordinates so the compiler cannot
discard the earlier samples. The install stopped after staging (0.529 s), and
the run stopped on its completed report (11.91 s). CPU/log evidence is retained
under `build-f6/ww18`. No physical Vita or pixel-parity claim.
