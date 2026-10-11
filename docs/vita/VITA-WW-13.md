# VITA-WW-13 — disc data pipeline: Vita I/O seam, packaging, measurements

2026-10-11 · [ticket](https://trello.com/c/WpqVAUf2) · implementation, delivered on branch `vita-ww-13-disc`

The pinned runtime reads the game image with host stdio (`hle_dvd.c` -> `dvd.c`: FST parse plus offset
copies into guest RAM). This delivery adds a reusable file-access seam that routes those reads to
**SceIofilemgr** on the Vita without touching any pin, documents the packaging choice for the 1.36 GiB
image, and measures the read budget (throughput, chunk bound, staging memory, failure propagation).
The seam is tested with a fake file system, then against the pinned `dvd.c` on a synthetic image, then
against the private image with an independent FST oracle.

**What is proven**: the seam compiles and links on the host and for ARMv7 with the SceIofilemgr
transport; the pinned DVD layer works byte-identically through it; the real image's FST, entry numbers,
offsets and guest copies cross-check against an independent reader; the host read budget is measured;
and the guest probe ran in Vita3K through the **vita-sceio** transport (isolated profile, firmware
staged, offscreen sandbox with the authorized Wayland socket), reporting REAL PASS with zero staging.
**What is not**: no physical-Vita result, no gameplay, no FPS, no memory-card measurement.

## Inputs and scope

- Worktree `/tmp/opencode/ww13`, branch `vita-ww-13-disc`, base
  `0598d224b3659cd71a1de72b3ac0bfbfb2b8502c`.
- Pins (read-only): RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1` (`ref/vita-ww-01/RecompCore`).
- Private image (local only, never committed or published):
  `Legend of Zelda, The - The Wind Waker (USA).iso`, **1,459,978,240 bytes**, magic `0xC2339F3D`,
  FST at 3,948,288 (57,054 bytes, 2,391 entries, 2,213 files).
- Authored scope: `vita/host/dvd/**` (seam, probe, tests), `scripts/vita/dvd/**` (packaging and
  measurement tools, isolated launcher), this document. The lane's earlier increment
  (`vita/feasibility/disc/**`, `scripts/vita/disc/**`, commit "Add the VITA-WW-13 disc probe against the
  pinned DVD layer") is a host-side probe of the same pinned layer; it stays as it is.
- Outputs (ignored): `/home/fred/projects/zeldaWWHDRecomp/build-f13/`.
- No pin, product default, dependency or desktop file is modified.

## Where the file access lives (preflight)

In the pin, `GXRuntime/src/dvd.c` is the only game-data file access:

- `load_from()` (L60–109): `fopen(path,"rb")`, 0x440-byte header, `fseeko` to the FST, `fread` of the
  FST blob, validation of magic `0xC2339F3D` and FST offset/size at 0x424/0x428.
- `dvd_read_to_guest()` (L216–292): `fseeko` + `fread` straight into guest RAM (MEM1 fast path), a
  byte fallback for non-MEM1 targets, and a trace prefix read.
- `dvd_close_image()` (L124): `fclose`.
- `GXRuntime/src/hle/hle_dvd.c` performs no I/O; it calls `dvd_path_to_entrynum`, `dvd_entry_info`,
  `dvd_read_to_guest`.
- Both files are members of the `gxruntime` **static** archive (`GXRuntime/CMakeLists.txt` L19, L42).

## The seam (no pin modification)

Option chosen: compile the pin's `dvd.c` **byte-identical** with a force-include header that redirects
only its four stdio primitives (`dvd_seam.h`):

```c
#define fopen  bw_dvd_fopen
#define fread  bw_dvd_fread
#define fseeko bw_dvd_fseeko
#define fclose bw_dvd_fclose
```

The object defines all six `dvd_*` symbols and is linked before `libgxruntime.a`, so the archive's
`dvd.c.o` is never extracted (verified in the generated link line; exactly one `dvd_open_image`
definition). The FST, path resolution and guest copy logic stay the pin's; nothing is duplicated.
Rejected alternatives: reimplementing the six functions (duplicates the FST), global `--wrap` on stdio
(affects the DOL loader and logging), and any pin patch (forbidden).

`vita/host/dvd/dvd_io.c` implements the four shims plus three transports behind a small backend
(`open`, positional `read_at`, `size`, `close`):

- **vita-sceio** (target): `sceIoOpen(path, SCE_O_RDONLY, 0)`, `sceIoPread` with a 64-bit `SceOff`,
  `sceIoLseek(END)` for size, `sceIoClose`.
- **host-posix** (tests/measurement): `open`, `pread`, `fstat`, `close`.
- **fake** (tests): in-memory image, injectable open/read failures, request accounting.

Reads are chunked at `BW_DVD_READ_CHUNK_BYTES` (1 MiB) directly into the caller's destination: the seam
allocates **no staging buffer**, so peak staging memory is zero by construction and each backend call is
bounded. Failures are loud: `[bw-dvd] open failed`, `[bw-dvd] read failed: offset=… length=…`, seek
errors, and unsupported open modes each print and return the POSIX-style failure. Stats counters
(`total_read_bytes`, `max_chunk_bytes`, `staging_peak_bytes`) expose the budget.

Reference install path: `ux0:data/bluewake/GZLE01.iso` (`BW_DVD_INSTALL_PATH`).

### off_t limit (documented)

The pin's `fseeko` call sites cast to `(off_t)`; VitaSDK newlib's `_off_t` is 32-bit `long`. Disc
offsets therefore stay below 2 GiB: the USA image (1.36 GiB) is fine. **Exact condition to lift it**:
an image larger than 2 GiB requires a separate, framed pin change (widen the seek arithmetic); the seam
cannot fix it because the cast happens before the shim is called.

## Tests (TDD, fake FS first, then real files)

All host tests live in `vita/host/dvd/` and run with system Python; the CMake project builds the seam
alone (`WW13_TESTS_ONLY=ON`), or with the pinned runtime when `RECOMPCORE_DIR` is set.

- `test_dvd_io.py` + `dvd_io_test.c`: 9 fake-backend cases — open/close, sequential and item reads,
  short read at EOF, read past EOF, injected read failure (loud), seek SET/CUR/END, invalid seeks,
  chunk cap exercised with a 5 MiB read, and the loud open/write-mode failures.
- `test_dvd_probe.py` + `dvd_probe.c synthetic`: the **pinned** `dvd.c` through the seam on a synthetic
  in-memory image (root file + subdirectory): pinned entry numbers and offsets, case-insensitive and
  `..` resolution, MEM1 and uncached guest copies, the pin's zero fill past the image end, and
  `staging=0`.
- `test_dvd_probe.py` real class (skipped when the private image is absent): the probe's independent
  FST oracle parses the image itself, then the pinned layer must agree on entry numbers, offsets and
  lengths for `framework.str`, `RELS.arc`, `res/Menu/Menu1.dat`, `res/Msg/bmgres.arc`; guest copies of
  those files and of the largest file's tail must equal direct reads at the oracle's offsets.
- `test_packaging.py`: synthetic image format (header, FST, pattern, large-file variant), install
  layout (symlink, idempotence, copy, refusal of a tracked destination).
- `test_measure.py`: `measure.py` on a generated image writes evidence with verdict, backend, sizes,
  throughput, chunk and staging.

Measured at the delivery revision: `python3 -m unittest discover -s vita/host/dvd` → **29 tests, all
green**; `scripts/audit_repo.sh` → AUDIT PASS; `scripts/check_attribution.py` → no bot/AI attribution.
The lane's earlier probe: `python3 -m unittest test_disc_probe` → 3 tests green.

## Measurements (host, private image)

`scripts/vita/dvd/measure.py --iso <private image>` reads 256 MiB of the largest file
(`end_st_epilogue.thp`, 549,725,536 bytes) through the pinned layer and the seam, then reports:

| Quantity | Value |
| --- | ---: |
| Verdict | REAL PASS (all cross-checks ok) |
| FST entries / files | 2,391 / 2,213 |
| Read for throughput | 268,435,456 bytes in 0.065 s → **3,927 MiB/s** |
| Max chunk requested | 1,048,576 bytes (the cap) |
| Staging memory | **0 bytes** |
| Total bytes through the seam | 268,668,553 (throughput + samples + FST + header) |

Guest (Vita3K, SceIofilemgr transport, game-free 64 MiB synthetic image with a `big.bin` entry):

| Quantity | Value |
| --- | ---: |
| Verdict | REAL PASS, backend **vita-sceio** |
| FST entries | 6 (oracle and pin agree; `boot.bin`, `res/asset.bin`, `res/leaf.bin` resolve) |
| Guest copies | byte-identical to direct reads (48/256/64 bytes and the largest file's 4 KiB tail) |
| Read for throughput | 67,108,864 bytes in 0.028 s → **2,270 MiB/s** |
| Max chunk requested | 1,048,576 bytes (the cap) |
| Staging memory | **0 bytes** |
| Total bytes through the seam | 67,114,529 (throughput + samples + FST + header) |

Evidence: `build-f13/ww13-seam/measure/real-host/` (host: `report.txt`, `stdout.txt`, `stderr.txt`,
`evidence.json` with image hash, pin hash, probe hash, throughput and stats) and
`build-f13/ww13-seam/vita3k/vita3k-run-guest-wayland-1-5f22078d/` (guest: `data/Vita3K/Vita3K/ux0/data/ww13/probe.report`,
`evidence/ww13-evidence.json`, install and guest process records, console log). Both throughput figures
are **host-page-cached** numbers: the host one is plain POSIX I/O, the guest one is the emulated
SceIofilemgr layer reading a host file. Neither is memory-card throughput.

## Packaging

Decision: **one private ISO file on ux0, outside the VPK**. The VPK ships the probe/game code only
(this delivery: `ww13.vpk`, 63,435 bytes, title `BWWG00013`); no disc data, saves or keys are ever
committed, attached or published.

- Install layout: `ux0:data/bluewake/GZLE01.iso` (reference path). `scripts/vita/dvd/iso_layout.py`
  validates the image magic, installs it with a **symlink by default** (no 1.36 GiB copy; `--copy`
  available), and refuses any destination that is not git-ignored, so game data cannot enter the
  repository. The same script places the image into an isolated Vita3K profile.
- Sizes: ISO 1,459,978,240 bytes (1.36 GiB); VPK 63,435 bytes; FST blob 57,054 bytes resident.
- Alternative **extraction + synthetic FST** (documented, not implemented): per-file copies under
  `ux0:data/bluewake/fst/…` plus a generated FST that maps entries to host files. It offers no size win
  for the full disc and adds a mapping layer; it becomes interesting only for partial installs or
  on-demand REL paging (VITA-WW-14). The **app0 variant** (ISO inside a personal VPK) is documented for
  personal builds only; it is never publishable.
- Wiring for the boot host (one call, fail loudly):

```c
if (!dvd_open_image(BW_DVD_INSTALL_PATH)) { /* stop: print install guidance */ }
```

and link `ww13_dvd_io` + the `ww13_dvd_pin` object before `gxruntime`.

## Vita target

- The probe cross-builds for ARMv7 with the VitaSDK (`-DWW13_VITA=ON`): `arm-vita-eabi-gcc`, hard-float
  Cortex-A9, linking `SceIofilemgr_stub`; 40 `sceIo*` call sites are present in the ELF, and the VPK is
  produced by `vita_create_vpk`.
- `scripts/vita/dvd/launch.py` runs it through the established isolation machinery
  (`scripts/vita/armv7/fp_launch.py` / `fp_profile.py`): bwrap with unshared namespaces, private
  HOME/XDG, staged public runtime (read-only), installed firmware `vs0`/`sa0`, offscreen Qt/SDL, fresh
  profile per run, never the real HOME. Two phases (install the VPK, then run the installed title); the
  guest writes its report to `ux0:data/ww13/probe.report` because Vita3K does not relay guest stdout,
  and the launcher polls that file before stopping the emulator.
- **Status: guest execution PASS.** One bounded run (≤300 s deadline, guest phase 2.0 s) in a fresh
  isolated profile: the VPK installed, the guest ran the probe on the synthetic image through the
  **vita-sceio** transport and wrote its report to `ux0:data/ww13/probe.report` (REAL PASS, all
  cross-checks ok, throughput and staging above). The sandbox is offscreen; Vita3K still requires a
  GL 4.4 context, which the offscreen platform cannot create, so the run used the coordinator-authorized
  Wayland socket (`--wayland-socket /run/user/1000/wayland-0`, VITA-WW-03.md L343/390); Vulkan
  enumeration fails in the sandbox and no headless compositor exists on this machine. Every run is
  recorded under `build-f13/ww13-seam/vita3k/`, never the real HOME.

## Reproduce

```sh
cd /tmp/opencode/ww13
python3 -m unittest discover -s vita/host/dvd          # 29 host tests
python3 scripts/vita/dvd/measure.py --iso '/path/to/private.iso' \
    --out /home/fred/projects/zeldaWWHDRecomp/build-f13/ww13-seam/measure/real-host
python3 scripts/vita/dvd/iso_layout.py --iso '/path/to/private.iso' --ux0 /path/to/ux0
# ARMv7 probe VPK (VITASDK set):
VITASDK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/vitasdk \
  cmake -S vita/host/dvd -B /home/fred/projects/zeldaWWHDRecomp/build-f13/ww13-seam/vita-probe \
  -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
  -DRECOMPCORE_DIR=/tmp/opencode/ww13/ref/vita-ww-01/RecompCore -DWW13_TESTS_ONLY=OFF -DWW13_VITA=ON
cmake --build /home/fred/projects/zeldaWWHDRecomp/build-f13/ww13-seam/vita-probe
# bounded isolated guest run (authorized Wayland socket; offscreen without it):
python3 scripts/vita/dvd/launch.py \
  --vpk /home/fred/projects/zeldaWWHDRecomp/build-f13/ww13-seam/vita-probe/ww13.vpk \
  --image /home/fred/projects/zeldaWWHDRecomp/build-f13/ww13-seam/vita3k/probe-64m.img \
  --timeout 300 --wayland-socket /run/user/1000/wayland-0 \
  --path boot.bin --path res/asset.bin --path res/leaf.bin
```

## Boundaries

No physical Vita, no gameplay, no FPS, no memory-card measurement. Both throughput figures are
host-page-cached. The seam covers the pinned DVD path only (the DOL loader and save/memory-card paths
keep their existing host I/O). The guest run used the authorized Wayland socket because Vita3K cannot
create a GL 4.4 context offscreen; it is a bounded probe run, not a game boot. Integration and ticket
handling remain with the coordinator.
