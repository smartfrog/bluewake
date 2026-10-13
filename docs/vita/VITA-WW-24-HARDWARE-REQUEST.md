# VITA-WW-24 — opt-in render-node access

2026-10-13. **Mechanism and synthetic tests only; hardware rendering and the
visible/playable 30 FPS goal remain unverified.** No Vita3K process, game data,
GPU smoke, compilation, installation or network operation was used in this lot.

## Contract

WW05 and FP reuse `scripts/vita/armv7/fp_launch.py`. Its software default keeps
the exact sandbox command/environment, OpenGL arguments, private `/dev`, dummy
audio, display authorization and configuration bytes. No device is discovered
or selected automatically.

Explicit opt-in: add `--render-node /dev/dri/renderD128` to the existing FP,
WW05 stage-only or WW05 execution command. This is an example path, not a
default. FP execution still requires its existing independent-review gate.
The ARM08/ARM09 saved CPU comparisons remain software-only.

The selected path must be absolute, directly under `/dev/dri`, without any
symlink component, and named `renderD<minor>`. It must exist as a character
device with DRM major 226 and render minor 128..255 matching the name. Missing
paths, directories, regular files, card/input/audio devices, aliases and device
number mismatches fail before staging inputs or launching a process.

The sole command/environment changes are:

```text
--dir /dev/dri --dev-bind /dev/dri/renderD128 /dev/dri/renderD128
```

and removal of `--setenv LIBGL_ALWAYS_SOFTWARE 1`. `--clearenv` remains, so a
caller's software-force variable cannot leak into this mode. `/dev/dri` itself
is never bound; no card node, input device or audio device is exposed. The
network namespace remains unshared with `--unshare-all`; no network permission
is added. Libraries, firmware, Wayland socket and writable paths are unchanged.

## Auditable request, not an observed GPU

Root and phase plans contain `renderer`:

- Default: `{"mode": "software", "render_node": null}`.
- Opt-in: `mode=hardware-requested`, plus the node's absolute path, major/minor,
  filesystem device and inode. Existing input gates recheck this identity before
  execution and between phases; replacement or removal is a failure.

FP result receipts and WW05 execution/result receipts carry the same requested
identity. Legacy plans without this field retain software behavior. Nothing in
this metadata asserts a selected GPU, a successful frame or a speed gain: Mesa
could still fall back to software. A future watched run must show the actual GPU
in the process log and independently establish visible gameplay and frame rate.

## Verification

Tests were written and run red before the mechanism, then again before the
identity recheck/receipt additions. The exact-default characterization was
already green on the starting revision; opt-in and provenance were absent.

- `python3 -B -m unittest discover -s vita/feasibility/armv7 -p test_hw_renderer.py -v`:
  22 contract tests, including exact default bytes, exact opt-in delta with/without
  libraries/Wayland, invalid device/symlink rejection, stage-only commands and
  saved identity/receipt checks. Device metadata is mocked; no GPU is opened.
- Guarded launcher/profile/deadline suites: `test_hw_renderer`, `test_fp_launch`,
  `test_fp_review`, `test_fp_profile`, `test_fp_campaign`, `test_fp_deadline`,
  `test_mem_launch`, `test_mem_deadline`, plus ARM08/ARM09 stage-only checks:
  **112 passed** on Linux AArch64.
- Guarded WW05 suites: `test_plan`, `test_launch` in `vita/host`: **72 passed**.

The local check driver is `/tmp/opencode/ww24-checks.py`; it polls MemAvailable
every 50 ms and terminates/reaps below 2.5 GiB. The deep worktree makes the
existing review-test socket exceed AF_UNIX's path limit, so the driver redirects
only that test fixture's temporary directory to `/tmp/opencode`. Tests run with
their own test directories on the import path. No existing test was edited.
The final guarded runs stayed above 6,595,624,960 bytes MemAvailable; no RAM stop
occurred. The shell fixture processes are synthetic, not Vita3K or game runs.

An additional attempt at the full ARM08/ARM09 executor suites was blocked by
their required, absent `ref/vita-ww-01/RecompCore` checkout. No extra mock bypassed
that source gate; no dependency was fetched or checkout installed. Windows CI and GPU/game
validation were not run. Independent review is still required before integration.
