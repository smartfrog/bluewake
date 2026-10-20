# Textured draw inventory (diagnostic, default OFF)

Configure `vita/host` with `-DWW_RENDERER_DRAW_CENSUS=ON`. The normal renderer
has no census fields, counters or additional GL checks. No decoder is changed.
The canonical private builder accepts `--draw-census`; its default is OFF.
Omitting the flag explicitly restores OFF even when reusing an ON build cache.

One `[draw-census]` line follows each successful present, through present 1024.
Every submitted textured draw is counted, independent of format, dimensions,
depth state, UV range or vertex count. Each sampled texmap contributes an entry
with the source guest texture address (not the reused GL texture object), slot,
dimensions, GX format, four-vertex flag, fullscreen flag and depth-test/write
flags. `draws` has this grammar:

`address@slot,widthxheight,format,four_vertices,fullscreen,depth_test,depth_write*count`

Entries are separated by `|`; `-` means none. Identical entries are aggregated
within a frame, in first-seen order. At most eight distinct entries are retained.
`rest` counts omitted draw/texmap occurrences, not distinct omitted identities.
Already-listed entries continue accumulating even after the list fills.
Any `rest>0` means the inventory cannot establish absence of an unlisted size.
Multi-texture draws have one entry per sampled slot, but increment `tx` once.
Non-four-vertex draws are included too, so they cannot hide a textured overlay.

`tq`/`uq` count textured/untextured four-vertex, six-index triangle-list draws;
`other` counts all remaining draws. `tx` counts textured draws of any geometry.
`fullscreen` requires four transformed NDC corners at (+/-1,+/-1), tolerance
0.02. Texture dimensions are source-image dimensions, not screen-space bounds.

`invalid` counts draws missing any required successful bind/upload/sampler;
`no_valid` counts draws with none of their required textures valid;
`decode_failed` counts required slots whose translated texture is invalid.
`bind_valid`/`bind_skipped` count all successful/skipped bind helper calls,
including harmless skips on untextured draws.
`skip` counts required slots silently skipped by the existing bind helper.
`bindfail`/`uploadfail` count GL failures at those boundaries. `refused` counts
plans refused by the frontend, translation or shader generation, not submitted
draws. These fields limit any negative-submission conclusion.

All counts and entries reset at present. Shutdown emits `partial=1` only for
pending unpresented work, using the last completed present number. A killed
guest need not execute shutdown. Partial lines are not presented-frame evidence.

There is no logo heuristic, alpha scan/cache or per-texel diagnostic. Per-draw
work and storage are bounded by eight texture slots and eight retained entries;
formatting allocates only at present, never while counting draws.

Run the pure inventory/formatting tests and actual backend fake-GL scenarios:

```sh
CC=/absolute/path/to/native-gcc python3 -B -m unittest discover -s vita/host -p test_draw_census.py
```

The 376x104 replay in `gx_logo_replay.md` is Nintendo's boot logo, not the title.
An inventory is submission evidence, not a semantic logo classifier or pixel
verdict; a framebuffer/depth-of-field composite is not a logo because it is IA8.
