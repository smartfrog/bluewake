# VITA-WW-18: boot alpha regression contracts

Two authored fixtures and renderer tests retain the captured alpha behaviors
without committing any game FIFO, texture, palette or image:

- **`black_alpha_17_preserves_source_and_blend`**: a direct RGBA8 quad carries
  black RGB and exact **17/255 alpha** through the frontend and translation.
  The recording backend must receive `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`,
  `FUNC_ADD`, enabled writes, disabled depth/cull and a raster-forwarding Cg
  fragment that returns the computed alpha rather than forcing opacity.
- **`transparent_notice_preserves_CI_and_raster_alpha`**: an authored 8×8 CI8
  texture and 16-entry RGB5A3 palette reuse the existing synthetic texture
  zones. A direct white/zero-alpha quad must retain the valid decoded texture,
  two-stage texture→raster TEV, exact raster alpha zero and ordinary alpha
  blending. Its `tlut_texture=1` coverage flag remains visible.

The fixtures are in `gx_boot_alpha_fixture.h`; the tests extend the existing
`ww_renderer_tests.cpp` recording infrastructure with the blend equation. All
previous test bodies/calls and capture contracts remain intact. No renderer
implementation, host source, WW05 harness, pin or original capture was changed.

Test-first fixture development exposed missing explicit EFB-alpha and identity
TEV-swap state, and an unresolved fixture texture matrix. Those authored inputs
were corrected using existing fixture conventions; **no production rendering
bug is claimed**. Private mutation builds additionally force alpha **17→255**,
force transparent alpha **0→255**, or replace the destination blend factor with
`ONE`: all three are rejected by the new focused assertions. Mutants remain
under `build-f6/ww18/standby/mutations/`, never in production code.

These are **host submission/Cg contracts, not a software framebuffer or a new
GPU parity result**. The authentic byte-exact frame-11/101 proofs remain in
[gx_boot_101_replay.md](gx_boot_101_replay.md); a later renderer change must still
be replayed against those private baselines to certify its GPU pixels. Passing
host tests cannot guarantee future physical-Vita or whole-game parity.

Four Linux host suites pass normally and under **ASan/UBSan**; **128 GX Python
tests**, the repository audit and whitespace checks pass. The previous **22
named renderer-test bodies and 22 main calls** are unchanged. No fresh GPU run
was substituted for absent post-window data. Independent maintainer review is
still needed before these lane changes are integrated.

The newer lane-16 handoff `2ffaace`/`6cb922c`, Units 89–90, delivers the same
already-replayed run `3f5aaa…`: 102936 journal commands, snapshots before
102594/102648/102699. Its actual STOP is **capture-window completion**, not a
boot hang. Guarded requests through **102937 / 103001** reject before staging;
`build-f6/ww18/standby/post-ci-extent-check.json` retains that check. No pixels
after the disappearance of the logo, final black interval or title screen are
yet available. The post-window watcher and next-capture procedure are described
in [EXTRACTION.md](../../scripts/vita/gx/EXTRACTION.md#post-logo-standby).
