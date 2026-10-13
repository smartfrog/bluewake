# VITA-WW-24 — opt-in render-node access

2026-10-13. **Mechanism and synthetic tests only; hardware rendering and the
visible/playable 30 FPS goal remain unverified.** No Vita3K process, game data,
GPU smoke, compilation, installation or network operation was used in this lot.

## Selection and software default

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

The planned device bind names the authorized canonical path:

```text
--dir /dev/dri --dev-bind /dev/dri/renderD128 /dev/dri/renderD128
```

`LIBGL_ALWAYS_SOFTWARE` is removed only in this opt-in. `--clearenv` remains,
so a caller's software-force variable cannot leak in. `/dev/dri` itself is never
bound. No card/input/audio bind or network permission is added: `--unshare-all`,
the private `/dev`, firmware, Wayland socket and writable paths remain unchanged.

## Corrected execution contract: validate after mount, before exec

The original path rechecks did not close the validation-to-bind race. Each
opt-in phase now opens the selected node through the existing no-follow directory
walker, using `O_PATH | O_NOFOLLOW | O_CLOEXEC`, and compares `fstat` with the
authorized type, filesystem device, inode and DRM major/minor. The descriptor is
passed to bwrap with `pass_fds`; its source is `/proc/self/fd/N`, its destination
remains the canonical render-node path. The parent closes it immediately after
spawn or on any validation/construction/spawn failure.

**This is preflight validation, not a stable-binder guarantee.** Local bwrap
0.12.0 calls `realpath` on the source before mounting, including fd references.
An unlinked source fails setup; recanonicalization can reintroduce a pathname
race. Its `--bind-fd` alternative also failed the unlinked-file probe and denied
device access in a harmless `/dev/zero` probe. No bwrap was installed or patched.

The accepted contract therefore permits a transient mismatched mount, but
**no Vita3K/guest or graphics-library execution on that mount**. After all mounts,
bwrap starts `/usr/bin/python3 -I -S -c` with the exact, auditable guard program
from `scripts/vita/armv7/render_node_guard.py`. It imports only `os`, `stat` and
`sys`, uses `lstat` on the canonical **sandbox** node, and requires a character
device with the authorized `st_dev`, `st_ino` and `st_rdev` major/minor. It does
not open/read/use the GPU. A mismatch exits 126; metadata/setup/exec errors stay
nonzero. There is no silent software fallback.

The guard emits its identity result before restoring the SDK's
`LD_LIBRARY_PATH`, then `os.execv` runs the exact planned application argv.
SDK library lookup is withheld while the guard runs; `-I -S` disables Python
environment/startup-site hooks. Other library/plugin paths are unchanged.

A file bind holds the mounted inode in the private namespace: later udev
recreation of the host name does not retarget it. The guard and exec are
sequential in that same namespace, with no intervening remount or guest code.
This mount-to-exec property is tested with a recreated synthetic host name.
It is not a guarantee against privileged namespace manipulation, nor a promise
that an unauthorized object was never mounted transiently.

## Auditable request, not an observed GPU

Root and phase plans contain `renderer`:

- Default: `{"mode": "software", "render_node": null}`.
- Opt-in: `mode=hardware-requested`, plus the node's absolute path, major/minor,
  filesystem device and inode. Root and phase plans also contain `renderer_guard`:
  method `mounted-render-node-stat-v1`, authorized identity, interpreter, library
  policy and the explicitly relaxed guarantee. The command includes the full
  guard source and exact application argv.

Hardware phase `process.json` receipts include `renderer_launch`: the actual
command with its per-spawn fd number, `pass_fds`, `preflight_validated=true`, and
`postmount_validation=VERIFIED`, `REFUSED` or `UNOBSERVED`. Only the guard's first
log line matching the authorized tuple establishes `VERIFIED`; setup failures
or mocked processes remain `UNOBSERVED`. Validation does not claim a successful
exec, frame or GPU. Fd numbers are local observations, not stable identifiers.

FP result receipts and WW05 execution/result receipts carry the same requested
identity. Legacy software plans retain their behavior; an old hardware command
without the mandatory guard is rejected before spawn. Nothing in
this metadata asserts a selected GPU, a successful frame or a speed gain: Mesa
could still fall back to software. A future watched run must show the actual GPU
in the process log and independently establish visible gameplay and frame rate.

## Verification

The initial implementation used test-first cycles for selection and provenance.
The corrective cycles first reproduced missing fd transfer/cleanup and the
single-phase race, then the absent mounted-node guard. The exact software-default
characterization remains green; no GPU/game verification is inferred from it.

- `python3 -B -m unittest discover -s vita/feasibility/armv7 -p test_hw_renderer.py -v`:
  23 contracts (the original 22 plus optional WW05 mount/argv ordering), including
  exact software-default bytes, opt-in mounts/guard and
  delayed SDK lookup with/without libraries/Wayland, invalid device/symlink
  rejection, stage-only commands and identity/receipt checks.
- `test_hw_renderer_launch`: 16 tests for fd preflight, substitution, no-follow,
  error/cleanup paths and FP single-phase/campaign plus WW05 install/guest plumbing.
  A real-bwrap campaign refusal proves that its synthetic installer/guest code
  does not run on a regular-file mismatch. Mocked launches never claim postmount
  validation. Deletion of A fails closed before the fake marker can execute.
- `test_hw_renderer_guard`: 9 tests, including actual bwrap guards with harmless
  `/dev/null` and `/dev/zero`, regular/symlink/missing inputs, each identity field,
  exact marker argv on a match, and a nonzero exec error after validation. These
  isolate mounted-identity validation from the independently tested DRM selection
  policy; no guard or production selection check is bypassed. A separate file-bind
  probe proves the mount remains on A after the host name is recreated.
- Guarded launcher/profile/deadline suites: `test_hw_renderer`, `test_fp_launch`,
  `test_fp_review`, `test_fp_profile`, `test_fp_campaign`, `test_fp_deadline`,
  `test_mem_launch`, `test_mem_deadline`, the new launch/guard suites, plus
  ARM08/ARM09 stage-only checks: **138 passed** on Linux AArch64 (initial delivery:
  112).
- Guarded WW05 suites: `test_plan`, `test_launch` in `vita/host`: **72 passed**.

The local check driver is `/tmp/opencode/ww24-checks.py`; it polls MemAvailable
every 50 ms and terminates/reaps below 2.5 GiB. The deep worktree makes the
existing review-test socket exceed AF_UNIX's path limit, so the driver redirects
only that test fixture's temporary directory to `/tmp/opencode`. Tests run with
their own test directories on the import path. The existing review fixture was
not edited; renderer contracts were updated for the mandatory guard.
The full guarded regressions stayed above 5,000,000,000 bytes MemAvailable; no RAM stop
occurred. The shell fixture processes are synthetic, not Vita3K or game runs.

The recanonicalization evidence is retained locally in
`/tmp/opencode/ww24-fd-unlinked-failure.log` and
`/tmp/opencode/ww24-bwrap-disassembly.txt`; the binary's `realpath` call at
`0x3a3c` replaces the source operation's pathname. These are diagnostic evidence,
not a runtime dependency or a claim that fd transport alone fixes the race.

An additional attempt at the full ARM08/ARM09 executor suites was blocked by
their required, absent `ref/vita-ww-01/RecompCore` checkout. No extra mock bypassed
that source gate; no dependency was fetched or checkout installed. Windows CI and GPU/game
validation were not run. The revised design requires independent review by the
same reviewer before any real Vita3K execution or integration.
