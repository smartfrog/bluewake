# Live draw census (diagnostic, default OFF)

Configure `vita/host` with `-DWW_RENDERER_DRAW_CENSUS=ON`. This is a
compile-time switch; no environment variable or fixture probe marker is used.
The normal renderer has no census fields, counters or additional GL checks.

One `[draw-census]` stderr line follows each successful present, for the first
1024 presents only (N=1). All counters describe that frame, not the whole run.
Shutdown emits a `partial=1` line only for outstanding, unpresented work.
A killed guest need not execute shutdown; completed frame lines remain usable.
There are no draw/bind log calls, retained draw lists, pixel scans or allocations
in the census. Classification examines at most four vertices, eight texture
slots and sixteen TEV stages, independent of mesh/texture size.

- `tq` / `uq` / `other`: submitted four-vertex, six-index triangle-list draws
  with / without sampled textures, or all other submitted draws. Incremented
  at the actual `glDrawElements` call, not at plan creation.
- `full`: four clip-space corners at NDC (+/-1, +/-1), tolerance 0.02.
- `no_valid` / `incomplete`: a textured draw has no / not all required slots
  successfully bound, uploaded and assigned its sampler for this draw.
- `decode_failed`: required texture slots still invalid after translation,
  including absent resources, missing TLUT and decode refusal.
- `bind_valid` / `bind_skipped`: actual bind helper outcomes. The existing
  helper also skips texture 0 for untextured draws; `required_skipped` excludes
  those harmless calls and counts only slots the shader samples.
- `bind_failed` / `upload_failed`: GL errors isolated immediately after
  bind/sampler operations or `glTexImage2D`. A pre-texture error is checked
  separately, so it cannot be misattributed to an upload. Errors still stop the
  renderer, with the pending counters available at shutdown.
- `logo`: submitted quad with a source texture of GX IA8 (3) or CI8 (9),
  depth testing AND writing off, and all four generated ST coordinates within
  [0,1] for a stage sampling that texture. Projective coordinates divide by
  positive, finite Q. Detection does not require decode success.
- `logo_valid` / `logo_skipped`: logo-like draws whose candidate texture slots
  all passed the actual bind/upload/sampler checks / had any silent skip.
- `refused`: plans rejected by the frontend sink, translation or Cg generation;
  these are not submissions. Nonzero values limit a negative-submission verdict.

“Logo-like” is a shape/resource predicate, not game-object identity. In
particular, `gx_logo_replay.md` documents a Nintendo boot logo, not a captured
Wind Waker title logo. Successful upload proves our GL submission path only;
it cannot certify the emulator's internal texture-cache contents or pixels.

Tests run without game inputs through `WW05_TESTS_ONLY=ON`:

```sh
CC=/absolute/path/to/native-gcc python3 -B -m unittest discover -s vita/host -p test_draw_census.py
```

The focused suite runs the pure classification/counting cases plus the actual
Vita backend against the existing fake GL, including invalid textures,
nonzero/multiple slots, GL errors, per-frame reset and the output bound.
