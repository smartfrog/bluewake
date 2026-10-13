# VITA-WW-18: first captured real draw prefix

## Matching-memory image delivered

The newer same-run capture `33aa66c91b9d49a1b78cbabdeb1f86c3` delivered actual
24 MiB MEM1 snapshots. Its snapshot 2 and the exact 2,048-command/14,464-byte
journal replay through the guarded bridge with **two usable plans/programs,
zero gaps/refusals and `verdict=ran`**.

The actual vitaGL replay PNG was opened:
`build-f6/ww18/images/real-33aa-snapshot2/capture.png`, with `image.json`, raw
RGBA PNG and a local viewer beside it. SHA-256:
`c871acff3fc84ccec64ed29b05f2b1549e05b5067e2e5adf193b60d5490fdae5`.
It is black uniformly, consistent with the two recorded opaque-black 640×480
initialization quads, not evidence of gameplay. The final capture is a labelled
preview of the logged prefix, not its missing next GX presentation.
Full provenance, GPU run and limits: [gx_visible_image.md](gx_visible_image.md).
All images/RAM/resources remain private and ignored; no source pin changed.

## Earlier blocked capture (historical)

The real stream took priority as soon as it appeared; emboss implementation was
paused to extract and inspect it. Renderer branch at `0c425d0`.

Source run: `build-f5/ww05/vita3k-run-5a4fb3b0ba4c46efa69ab545e225c8dd`.
The journal is contiguous from index 1, now with a 2,048-command limit.

| Evidence | Value |
| --- | --- |
| Captured commands / FIFO bytes | 2,048 / 14,464 |
| Captured draws | 2 quads, commands 990 and 1962 |
| Each draw's size / vertices / stride | 67 bytes / 4 / 16 bytes |
| Vertex data | direct position + RGBA8 color, no vertex arrays |
| Report decoded commands / renderer draws | 4,249 / 4 |
| Report pending bytes | 5 |
| Extent | logged-command-prefix-only, not the full 28,385 renderer bytes |
| FIFO SHA-256 | 4f9b7592e088442ab9156410b9e5b1517e5a22e18f10d6623d2e67d6a1e13149 |

`extract.py --latest ... --min-draws 1 --replay` correctly selected this run,
wrote an exact private FIFO/provenance fixture and stopped with
**`verdict=memory-required`**. It did not run against the bridge's zero-filled
default RAM. The run contains no adjacent MEM1 dump.

To investigate that dependency without inventing memory, the exact stream was
also fed to the bridge with an explicitly empty/unmapped address space. It
refused at FIFO offset 1,904, command 294: BP texture-image binding register 94,
guest image address `0x0039D860`. This is before either draw and demonstrates
why inline vertex bytes alone are insufficient for this frontend replay.
No commands were stripped, no state/resources were reconstructed and no
translated module/disc assets were copied into the fixture.

Next input from the capture lane: raw MEM1 starting at physical address zero,
matching this run/draw interval, or time-matched guest resource records. Then:

```sh
WW02_BUILD=build-f6/ww18 python3 scripts/vita/gx/extract.py RUN/ww05.log \
  --mem1 RUN/mem1.bin --output build-f6/ww18/real-replay-with-memory --replay
```

The exact format and memory requirements are in
`scripts/vita/gx/EXTRACTION.md`. The report stops at pinned trace capacity before
renderer drain; its four rendered clears are the capture lane's evidence, not
proof that this branch replayed them. Real draw replay coverage is still pending
matching memory. All captured bytes and diagnostic artifacts remain private
under `build-f6/ww18`; no host/harness edits or network/push.
