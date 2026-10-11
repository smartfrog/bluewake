# VITA-WW-06 — acceptance instrumentation: event markers, scenario runner, input seam design (preparation)

2026-10-11 · [ticket](https://trello.com/c/ulqtpJbm) · preparation, delivered on branch
`vita-ww-06-acceptance` at integration head `0c8019f`. **No game code is changed, no emulator
run happens, no playable result is claimed.** The ticket's product acceptance needs the game
playable first; this delivery makes its measurement ready the moment it is: the acceptance
event-marker contract, the five-minute Outset scenario checker, and the design of the SI pad
seam that gives the game real input instead of the empty bus.

**What is proven**: the marker parser and the scenario checker are tested end-to-end on
synthetic streams (41 tests); the runner reads a real WW05 run log and records the honest
not-measured baseline; the standard-pad transfer encoding is pinned as a checked reference
(`gc_pad.py`) against the desktop host's game-validated model and the pinned Dolphin sources.
**What is not**: no acceptance event has ever been emitted, no movement, camera, audio,
transition or save has been observed, no Vita3K process has run, no physical-Vita result exists.

## Preflight: what the ticket's acceptance decomposes into

The ticket asks for a reproducible five-minute Outset session: Link movement and camera usable,
audio audible and controlled, one defined transition, a disposable save written and reloaded,
with boot/scene/transition instrumentation and no silent loss. Each item maps to a signal the
stack already has, or to the exact seam that must grow one:

| Acceptance item | Signal in the stack today | Marker (this contract) | What the marker proves / cannot |
| --- | --- | --- | --- |
| Link movement | SI pad transfers (05: empty bus; desktop: `pad_wire.c` + live pad) | `[input-frame]` + `[input-consumed]` | input served and read by the game's controller code; Link's displacement is a visual check (hardware path) |
| Camera change | substick in the same transfers; desktop's stick-camera reads the same pad | `[input-frame]` (sub axes) + `[input-consumed]` | camera input delivered and consumed; the view turning is a visual check |
| Audio active | AI DMA register model + DSP HLE/capture (`nonzero_samples`, `peak_sample`); 05 host has no audio backend yet | `[audio-active]` / `[audio-silent]` | the guest's stream ran at a nonzero rate; audibility is a hardware check |
| Transition | REL lifecycle (ticket 14's `[rel-load]`/`[rel-unload]`), the game's overlap/scene-change machinery (`0x803F6160` non-null while a change is in progress, desktop gates on it) | `[scene-load]` | a load boundary crossed to a different scene; visual fidelity is a hardware check |
| Save then reload | `card_runtime.c` write calls/bytes are the honest in-run save signal; the card file is checked after the run | `[save-write]` / `[save-read]` / `[save-error]` | the guest wrote its own buffer and a later session read it; the card contents stay private, sizes only |
| Reproducible session | source SHA + config + ordered sessions | baseline document | the same protocol ran again; wall time in Vita3K is not physical time |

Boot/scene/transition peaks and pool staging (the ticket's instrumentation clause) are already
carried by the WW05 report's `ALLOC`/`UNKNOWN` lines and the boot lane's counters; they are
outside this harness, and a 06 run should attach its reports to the session evidence.

The input seam is the one that does not exist yet: the 05 host's empty bus is honest but gives
the game no pad at all, and 06 needs movement and camera. The design is below.

## The marker contract (for the host lane)

Eight markers, searched anywhere in a line so interleaved logger output cannot hide a stamp
(the frame and REL harnesses read their markers the same way):

    [input-frame] retrace=N channel=C buttons=0xHHHH stick=X,Y sub=X,Y us=T
    [input-consumed] retrace=N hold=0xHHHH trig=0xHHHH axes=0xHHHHHHHH us=T
    [audio-active] us=T rate=HZ
    [audio-silent] us=T
    [scene-load] name=NAME us=T
    [save-write] bytes=B us=T
    [save-read] bytes=B us=T
    [save-error] op=read|write reason=REASON us=T

- `[input-frame]` — one pad-data transfer the SI seam served on channel C, decoded: the
  16-bit GC button mask and the signed stick/substick bytes the game was given. Emit one per
  completed pad-data transfer on each served channel; channel 0 is player one, the acceptance
  channel. `retrace` is the host's VI retrace (or its frame counter while no VI clock runs)
  and must not go backwards; `us` is the host's monotonic wall clock in microseconds.
- `[input-consumed]` — the game's own pad record changed (GZLE01 `0x803A4E20..0x803A4E27`).
  `hold` and `trig` are the two 16-bit words the desktop input-chain probe already reads there
  (`runtime/host/src/main.c`); `axes` is the raw 32-bit word at `0x803A4E24` (the stick bytes,
  printed raw on purpose until the record's inner byte order is pinned from the decomp). Emit
  on change: its presence inside a deflection span is guest-side evidence that the game's
  controller code read the state.
- `[audio-active]` — the AI stream is running and producing output at `rate` Hz (nonzero).
  `[audio-silent]` — the stream stopped or its output went silent. A trailing silence is a
  missing-feature exposure, not something the harness may hide.
- `[scene-load]` — a scene transition completed; `name` is the destination scene without
  spaces. Emit at completion, not at the request, so the marker is a load boundary crossing.
- `[save-write]` — the guest's card write completed, `B` bytes (`card_runtime`'s write calls
  and bytes are the natural point). `[save-read]` — the guest opened the saved file in a later
  session, `B` bytes. `[save-error]` — a card I/O failure with the operation and reason; a
  failure must surface, never a fake save.

The harness refuses a malformed marker, a wall time that does not advance across timed events,
a `retrace` that goes backwards, a channel outside 0..3, a stick outside the signed byte, an
empty or malformed scene name, a nonpositive audio rate, a zero-byte save (a zero-byte write
is not a save), and an unknown save-error operation. A broken stream is not a measurement.

## The scenario runner

`scripts/vita/ww06/run.py` reads one or more session logs in order (the save session first, the
reload session after it), evaluates the five requirements and writes `baseline.json` plus
`summary.txt` under `--out` (the ignored `build-f6/` tree by default):

```sh
python3 scripts/vita/ww06/run.py --out build-f6/ww06/<label> --label <label> \
  --run <save-session.log> --run <reload-session.log> --scene outset [--source <sha>]
```

- **movement** — a sustained channel-0 left-stick deflection (at least 30 consecutive frames
  at or beyond the 30/128 deadzone) with at least one `[input-consumed]` change inside that
  span. It proves input served and consumed; Link's displacement itself is a visual check.
- **camera** — the same for the substick, which drives the camera.
- **audio** — at least one `[audio-active]`, and the last audio event is not a `[audio-silent]`
  that never returned.
- **transition** — at least two distinct `[scene-load]` names: a load that is not the start.
- **save/reload** — an earlier session wrote a save (`bytes > 0`) and a later session read it
  back; a `[save-error]` anywhere fails loudly. One session alone is not measured: the reload
  needs its own session.

Each requirement answers observed, failed or not measured. A family whose markers never appear
is not measured; a family that appears but does not show the behaviour has failed. The overall
verdict is `acceptance met` only when all five are observed, `acceptance not met` when any has
failed, and `not measured` otherwise. Exit 0 means the evidence was read and the baseline
written (a not-measured or not-met verdict still exits 0 — the honest result); exit 1 means a
stream was unusable.

## The input seam: a standard pad behind the empty bus

### What the game asks for (real evidence)

The real WW05 run `80767c8a` stopped at the game's first SI write: `CC006434`, PC `8030C1C0`,
width 4, value `C0010301`. Decoded with the pinned Dolphin register layout
(`ref/vita-ww-01/RecompCore/Source/Core/Core/HW/SI/SI.h`, `USIComCSR`):

- TSTART = 1, channel = 0, OUTLNGTH = 1, INLNGTH = 3, TCINT acknowledged, TCINTMSK = 1.

That is the SDK's controller **status probe**: one output byte (command `0x00`), three input
bytes expected. The standard controller's answer is `0x09 0x00 0x00` (device type 0x09, status
0). The empty bus answers COMERR plus the selected channel's NOREP instead (the 05 host's
continuation, `vita/host/boot_host.c` on branch `vita-ww-05-host`), and the game then runs with
no pad. A connected pad is what turns the empty bus into 06's input.

The follow-up reads are **direct reads** (command `0x40`, eight input bytes). The game's SDK
`PADRead` assembles the eight bytes into its own record; the desktop host's input-chain probe
shows where the game keeps it: `0x803A4E20` (hold), `0x803A4E22` (trigger), `0x803A4E24`
(the stick word). `[input-consumed]` reads exactly that record.

### The transfer encoding (pinned in `scripts/vita/ww06/gc_pad.py`)

The checked reference mirrors the desktop host's game-validated model
(`runtime/host/src/pad_wire.c`, `host_si_complete_pad_transfer` in `runtime/host/src/main.c`)
and the pinned Dolphin `SI_DeviceGCController.cpp` (mode 0):

| Transfer | Command | Response (big-endian bytes) |
| --- | --- | --- |
| status probe | `0x00`, INL 3 | `09 00 00` |
| direct read | `0x40`, INL 8 | buttons hi, buttons lo, stick X, stick Y, substick X, substick Y, trigger L, trigger R |
| origin / recalibrate | `0x41` / `0x42`, INL 8 | `09 00 80 80 80 80 00 00` (neutral origins) |
| write (rumble/mode) | via STATUS WR | no response; the mode is recorded |
| unknown command | any | refused — never answered with invented data |
| absent channel | any | no response; COMERR + NOREP(channel), receive buffer untouched |

Sticks are signed bytes; on the wire they are `+128` centred (neutral `0x80`). The button mask
is the 16-bit GC mask: A `0x0100`, B `0x0200`, X `0x0400`, Y `0x0800`, START `0x1000`, L
`0x0040`, R `0x0020`, Z `0x0010`, d-pad `0x0001..0x0008`. The response words are the two
32-bit values the SI channel registers carry (`0x04 + channel*0x0C` and `+4`); the same bytes
go to the shared SI IO buffer at `0x80`. Reading either input word consumes the channel's RDST
status, matching hardware (`GXRuntime/src/si.c`).

Two honest differences from the pinned Dolphin model are recorded rather than smoothed over:
Dolphin's `SOrigin` is ten bytes and starts with a `u16` button word, while the desktop's
game-validated model answers the eight bytes above; and the desktop model does not set
Dolphin's internal `PAD_USE_ORIGIN` (0x0080) flag. Both were found by serving this game, not
invented. A third: the desktop's digital-R click implies full analog travel before encoding
(measured on the pause menu); that rule belongs to the input source, not the device, and
`gc_pad.py` deliberately does not apply it.

### Where the device model plugs in

The pinned runtime completes a TSTART synchronously with no peripheral transaction
(`GXRuntime/src/si.c:104-109`: TSTART clears, TCINT latches). The 05 host intercepts that
write and adds COMERR/NOREP for the empty bus. A real pad model needs one of two shapes,
both through the same contract:

1. **A runtime transfer callback** (the 05 document's next option): `si.c` gains a device
   callback invoked at TSTART, with the transfer length and channel already known, and the
   callback returns the response bytes (or a no-response result). This is a RecompCore change:
   separately authorized, upstreamed by pull request, pinned, exported as a patch — never a
   local fork of the runtime.
2. **A host-side completion** (what the desktop host does today): the host intercepts the
   TSTART write after the pinned model ran, serves the command from the SI IO buffer, writes
   the response to the channel registers and the IO buffer, sets RDST and the channel status
   byte, and clears the error nibble. No runtime change, but the transfer completion then
   lives in the host seam instead of the device.

Either way the device model is the same state machine: absent → COMERR/NOREP; connected
standard pad → the table above; the pad state comes from a source. The sources, honestly:

- **live controls** (hardware path): the Vita's own input read once per poll, exactly as the
  desktop's live pad path works. A pad IS hardware; its protocol answers are documented, and
  a human's state is a human's state.
- **scripted schedule** (measurement path): the desktop's proven `pad_event_schedule.h` pattern
  — button pulses and axis values at retraces, deterministic and reproducible. This is a
  measurement instrument and must be labelled synthetic; it cannot be presented as a session
  a person played.
- **replay** of a recorded schedule is the same mechanism with a file behind it; not needed
  until a recorded acceptance run exists.

What is fabricated and therefore refused: skipping the guest transfer (writing response
registers without the game's SI write), inventing a controller identity when no pad model
exists, answering unknown commands with plausible bytes, or claiming movement from a served
transfer without the `[input-consumed]` record change.

## What Vita3K can and cannot prove

- **Can**: event ordering, counts and spans; the game's own pad record changing; card write and
  read signals; scene-load names; AI stream state — all guest-side log evidence, bound to a
  source SHA and a run.
- **Cannot**: pixels. VITA-WW-02's delivery documents the readback blocker: Vita3K's present
  cannot be read back, so rendered output is a hardware-path check. Audibility, "usable"
  movement and camera, and visual fidelity are hardware checks too. Wall time in Vita3K is not
  physical-Vita time (VITA-WW-12), so the five-minute session is a protocol, not a wall-clock
  gate.
- Until a physical Vita run exists (VitaShell, per the ticket), any result is an emulator
  prototype, not a physical-Vita certification.

## Evidence recorded today

`run.py` was run read-only over the real empty-SI run log
(`build-f5/ww05/vita3k-run-9ae86b06.../ww05.log`; never written to) and over synthetic fixture
logs generated by the tests' own builders. Stored in the ignored `build-f6/ww06/`:

- `real-ww05-empty/` — one real session, zero acceptance events: all five requirements not
  measured, verdict `not measured`. This is the only honest verdict today.
- `synthetic-five-minute/` — the fixture save + reload sessions: all five requirements
  observed, verdict `acceptance met`. It proves the runner, not the game.

## Scope and checks

- Authored: `scripts/vita/ww06/**`, `vita/feasibility/ww06/**`, this document. No pin, product
  default, desktop file, runtime file, host file or game file is modified; no network, no push,
  no Vita3K process.
- Tests: `python3 -m unittest discover -s vita/feasibility/ww06` — 41 tests, all green at the
  delivery SHA.
- Outputs (ignored): `/home/fred/projects/zeldaWWHDRecomp/build-f6/`.
