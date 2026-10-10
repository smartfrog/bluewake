# VITA-WW-03: bounded ARMv7 floating-point diagnostic

2026-10-10 · [ticket](https://trello.com/c/DcwHCjuc) · game-free, opt-in

## Deadline correction checkpoint: REVISE, exact-launch authorization pending

The AArch64 Linux diagnostic passes **71/71** independently specified result-bit cases. The same
emitted synthetic PowerPC functions and pristine RecompCore CPU build into a Vita ARMv7 VPK.
The corrected launcher passes fake-process tests, including protected host evidence, closed stdin,
bounded report collection, staged-package/library identity, network/filesystem isolation, detached
descendant cleanup and preserving semantic failure when the emulator process exits zero.

**No ARMv7 FP case has been observed.** Two prior diagnostic startups were INCOMPLETE: the original
60-second attempt timed out, and temporary tracing imposed an inherited file-size limit which killed
PulseAudio's 64 MiB memfd allocation. Subsequent isolated GUI-only runs reached Welcome (confirmed by
the user) and the main window with the copied stack. GUI startup is not FP execution.

This correction pass is code, fake-process tests, builds and a checkpoint commit only. Renewed real
Vita3K execution is **NOT RUN** and requires coordinator risk review and exact-launch authorization.
The approved later budget is at most two 30-second attempts by this original worker. Physical Vita,
Windows hardware and gameplay remain NOT RUN. This is not completion of 03 or a GO recommendation;
runtime repair follows a reproduced defect. VITA-WW-04 and the native game/renderer work remain separate.

Round 1 replaced the proposed clean-source provenance hold with an accepted opaque runtime hash.
The current copied ELF has an explicitly accepted RPATH-only relocation and a **different hash**,
recorded below. Source and Boost mapping remain UNVERIFIED. This bounded, noncertifying disposition
does not authorize a launch by itself.

There is no supported working BlueWake game host here. These results establish neither game boot,
memory/stack/pool sufficiency nor physical 30 FPS/normal speed. Confidence is high in the bounded
host results and cross-build, high in the fake-process sandbox observations, and insufficient in
actual Vita3K/ARMv7 correctness. Physical tests follow useful emulator results.

## Frozen inputs and reproduction

| Input | Revision |
| --- | --- |
| Integration base | `a6ea2e94809649c5fb035fa321ad44f9f32432da` |
| RecompCore | `ddd031b2a7f45fcbda37a49daac9b308b8d95da1` |
| DolRecomp | `b8b534591cba8ca7cd43943a655ee6e2591cf5de` |

The checkpoint revision is `git log -1 --format=%H -- docs/vita/VITA-WW-03.md`. After committing,
the complete commands below are repeated at that immutable revision. The resulting exact source
SHA, ELF/SELF/VPK SHA-256 values, sizes and host run directory are in
the new `build-vita-ww-03-deadline-c6f417fb8b79483a9165b0d6273bfde1/fp-evidence.json`;
01 evidence is in `build-vita-ww-01-deadline-c6f417fb8b79483a9165b0d6273bfde1/evidence.json`.
Do not identify a package by filename alone. Rebuilding embeds the source revision and changes
hashes; SELF/VPK packaging can also vary. The handoff includes that revision's artifact hashes.

Dependencies are private pristine clones under this worktree's ignored `ref/vita-ww-01/`, copied
with `git clone --no-hardlinks` from the integration tree's pinned references. Both campaigns check
the product lock/profile and verify dependency SHA and clean status before and after building.
No dependency, runtime pin, default platform build or game data is modified.

Checkpoint commands on AArch64 Linux (`7.1.13-402.asahi.fc44.aarch64+16k`), repeated after committing:

```sh
STACK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack
OUT="$PWD/build-vita-ww-03-deadline-c6f417fb8b79483a9165b0d6273bfde1"
BASELINE_OUT="$PWD/build-vita-ww-01-deadline-c6f417fb8b79483a9165b0d6273bfde1"
PYTHONDONTWRITEBYTECODE=1 VITASDK="$STACK/vitasdk" PATH="$STACK/bin:/usr/bin:/bin" \
  python3 -B scripts/vita/armv7/fp_build.py --vita --out "$OUT" \
  --vitasdk "$STACK/vitasdk" --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" \
  --cmake "$STACK/bin/cmake" --ninja "$STACK/bin/ninja" --jobs 2
PYTHONDONTWRITEBYTECODE=1 VITASDK="$STACK/vitasdk" PATH="$STACK/bin:/usr/bin:/bin" \
  python3 -B scripts/vita/armv7/build.py --vita --out "$BASELINE_OUT" \
  --vitasdk "$STACK/vitasdk" --cc "$STACK/bin/aarch64-conda-linux-gnu-gcc" \
  --cmake "$STACK/bin/cmake" --ninja "$STACK/bin/ninja" --jobs 2
CC="$STACK/bin/aarch64-conda-linux-gnu-gcc" VITASDK="$STACK/vitasdk" \
  PATH="$STACK/bin:/usr/bin:/bin" PYTHONDONTWRITEBYTECODE=1 \
  python3 -B -m unittest discover -s vita/feasibility/armv7 -p 'test_*.py' -v
bash scripts/audit_repo.sh
```

Also run each of `tests/test_{composite,direct_calls,inline_gpr,guest_checkpoints,frame_tail,
signing_entitlements,sample_owners,one_game_guard}.py` directly with Python. These are 53 tests;
the composite file has its own driver. The coordinator's fresh API PR check found no overlapping Vita
work; `gh` is unavailable. The remote-branch check showed only `origin/main` and its HEAD alias.
No public action was taken. Historical builds using the original tool paths remain in their original
output roots; no old package hash is reused as the new checkpoint's artifact identity.

Tool versions: native GCC 16.2.0, Vita GCC 15.2.0, CMake 4.4.3, Ninja
1.13.2.git.kitware.jobserver-pipe-1, Python 3.14.8. This pass uses only the completed copied compiler,
SDK, CMake/CTest and Ninja; it installs nothing and does not mutate the tool stack.

## Independent expected bits

`scripts/vita/armv7/fp_vectors.py` contains literal integer bit patterns, expanded only through
integer sign-bit operations and mode enumeration. It performs **no native floating-point
calculation** to derive expected values. Results use the 64-bit FPR representation; quantized
memory bytes/halfwords are zero-extended to 64 bits. AArch64 agreement localizes a future divergence
but is not the oracle. No soft-float emulator or replacement CPU is included.

Authoritative sources inspected, with downloaded copies kept only in ignored `build-vita-ww-03/reference/`:

1. IBM **Gekko RISC Microprocessor User's Manual**, version 1.2, 2000-05-25,
   [public manual](https://datasheets.chipdb.org/IBM/PowerPC/Gekko/gekko_user_manual.pdf).
   SHA-256 `08ef8428f39260ffe567bead019e085a4c0ca6e14083138292d1ec00538e0be0`.
2. **PowerPC Microprocessor 32-bit Family: The Programming Environments**,
   [manual mirror](https://open-watcom.github.io/open-watcom-1.9/devel/powerpc-programming-environments.pdf).
   SHA-256 `5ba873438738003fcd4a37e217caab8b9341b56ad0dc8940f6f3d5448c9557b4`.
   Section 3.3.5, table 3-8 (page 3-26) defines RN: 0 nearest/ties-even, 1 toward zero,
   2 toward positive infinity, 3 toward negative infinity. Section 3.3.2 (pages 3-22–23)
   defines result signs, including multiplication and signed zero.

| Case family and count | Exact derivation / source |
| --- | --- |
| `add-positive`, `add-negative`: 8 | Gekko `fadd`, page 12-59, and Programming Environments table 3-8. Add ±1 and same-sign 2^-54: magnitude is one quarter of a binary64 ulp above 1. RN2 increments positive `3ff0000000000000` to `...0001`; RN3 increments negative `bff0000000000000` to `...0001`; other modes retain ±1. |
| `frsp-{positive,negative}-{even,odd}`: 16 | Gekko `frsp`, page 12-82, plus table 3-8. Inputs ±(1+2^-24) and ±(1+3·2^-24) are exact binary32 halfway values. Positive even tie chooses `3ff0000000000000` in RN0; positive odd tie chooses `3ff0000040000000`. Adjacent single results differ by `0x20000000` in normalized binary64 encoding. Negative direction reverses RN2/RN3. Only ps0/FPR result is checked; the manual declares frsp ps1 undefined in paired mode. |
| `double-input-{positive,negative}`: 4 | Gekko §2.2.3, table 2-21, pages 2-28–31; `fmul`; sign rule §3.3.2. Multiply ±minimum binary64 subnormal by 2. NI0 gives `0000000000000002` / `8000000000000002`; NI1 gives signed zero. |
| `double-underflow-{positive,negative}`: 4 | Gekko table 2-22 and `fmul`; sign rule §3.3.2. Multiply ±2^-1022 by 1/2: NI0 gives `0008000000000000` / `8008000000000000`; NI1 gives signed zero. |
| `single-underflow-{positive,negative}`: 4 | Gekko table 2-22 and `fmuls`; sign rule §3.3.2. Single-representable operands ±2^-126 and 1/2 produce ±2^-127, represented as `3800000000000000` / `b800000000000000` when NI0, signed zero when NI1. |
| Eight `quant-*` inputs × four RN modes: 32 | Gekko §2.3.4.3.12, pages 2-55–58, especially steps 2, 5 and 6 of the quantization algorithm, and GQR table 2-16. Add signed ST_SCALE to the exponent, truncate toward zero, clamp to the integer type. This is not FPSCR-directed integer rounding. Inputs are already exactly single-representable; no double-to-single ambiguity is introduced. |
| `direct`, `indirect`, `host-boundary`: 3 | Same RN2 positive `fadd` oracle as above, each expected `3ff0000000000001`. Synthetic `bl`/`bctrl` route to the arithmetic callee through the production helpers. The boundary case rejects continuation at guest return PC `80007108`, observing the completed value and unchanged control mode. |

The eight quantized inputs are deliberately bounded:

| Input | GQR type / signed scale | Expected stored value in **every** RN mode |
| --- | --- | --- |
| +1.75 | s8 / 0 | `01` |
| -1.75 | s8 / 0 | `ff` (-1) |
| 255.5 | u8 / 0 | `ff` |
| -0.5 | u8 / 0 | `00` |
| 128 | s8 / 0 | `7f` |
| -129 | s8 / 0 | `80` |
| 8192 | s16 / +2 | `7fff` (scaled 32768 saturates) |
| 131069 | u16 / -1 | `fffe` (scaled 65534.5 truncates) |

This is not exhaustive quantization certification. NaNs, infinities, exception/status-bit accuracy,
all NI×RN combinations, paired-lane arithmetic, guest FPSCR-writing opcodes and the full dynamic
module host boundary are outside these vectors. The underflow cases test result bits, not UX/FI/FR.
The boundary probe covers the existing composite readiness/return handshake, not a new host port.

## Production paths and compiler evidence

The native `ww_fp_emit` links pinned DolRecomp's real decoder/backend and emits seven small routines
at `80007000`–`80007134`. The existing corpus splitter retains unchanged function bodies and feeds
the real `scripts/generate_composite.py` and `scripts/windows/direct_calls.py`. FP mode has 27 chunks,
two rewritten direct calls, three indirect calls, four interpreter continuations and the existing
871-address watch list. 01 still emits its original 20 chunks and uses its original entrypoints.

The executable links the same six RecompCore CPU sources and guest-memory/dirty sources as 01,
with its real CPUState. Production composite headers, depth guards and terminal-call attributes are
preserved. Existing noinline/inline attribute warnings remain visible. Native generation and Vita
cross-compilation are separate. There is no renderer dependency.

Flags remain `-O2 -ffp-contract=off -fno-fast-math -fstack-usage`, with the original warning/error
flags; Vita adds `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard`. Operands are copied as bits into
CPUState and consumed by separately compiled runtime functions. No expected arithmetic can fold
into a runtime operation. Each scenario sets FPSCR through the real `ppc_fpscr_control_updated`,
then executes emitted guest code. Cleanup calls that runtime path with mode zero and restores the
saved host environment with checked `fegetenv`/`fesetenv`. The fixture never installs a replacement
ARM FPSCR setter; host-environment restoration cannot repair the operation under test.

`build-vita-ww-03/logs/vita-fp-assembly.log` shows runtime loads from CPUState, `ni_add` using VFP
addition, `force_single` using `vcvt.f32.f64`, and `ppc_fpscr_control_updated` returning without
an ARM FPSCR write. It also shows the explicit sign-preserving NI flush in `force_single`.
Thus “all flushing is broken” would be false. RN and double NI discrepancies are candidates for
reproduction, not observed Vita3K results. `fixture.map`, `compile_commands.json`, ELF attributes,
symbols and `.su` files remain under the ignored build directories.

## Fail-closed completion protocol

Each launch has a fresh UUID run root, a request containing run token + source SHA + artifact SHA,
and no pre-existing `fp.partial`/`fp.report`. The fixture checks the source against its compiled-in
SHA and rejects malformed requests or existing output. On host the artifact is the executable;
on Vita it is the VPK. The launcher checks the source package, then hashes and checks the ZIP allowlist
of the actual staged copy **after** runtime asset staging and **before** issuing the request or command.
The request carries the package hash to avoid the impossible circular operation of embedding a
VPK's own hash inside that VPK. Fresh isolated staging and the launch manifest establish that binding.

The report has exactly one `RUN` identity, 71 ordered `CASE` lines with ID, RN/NI mode, expected and
actual bits, path validity and verdict, and one terminal `END`. Every write is checked. The existing
corrected logger retains both-stream flush/error checks; report publication reuses its checked
flush helper and additionally checks `fclose`. Only then is `fp.partial` renamed to `fp.report`.
A close failure therefore cannot leave an accepted completion file. No unchecked completion marker
is written after closing. Semantic failure publishes a complete **FAIL** report and returns nonzero.

The validator independently checks the manifest's literal cases, order/count, every verdict and
the terminal totals. Missing, stale, truncated, duplicated, mismatched or dishonest reports fail.
Collection opens each parent directory with `O_DIRECTORY|O_NOFOLLOW`, then the report with
`O_NONBLOCK|O_NOFOLLOW`. `fstat` must identify a regular file of at most 65,536 bytes; the descriptor's
read is also bounded. The same captured bytes are parsed and hashed, without reopening the path.
FIFO, symlink, oversized, missing or malformed output is INCOMPLETE. Timeout (124), crash and other
nonzero emulator exits also remain INCOMPLETE, even if a valid report exists; `reported_semantic`
preserves its diagnostic content without accepting it as PASS/FAIL. An emulator exit of zero with
a valid semantic FAIL still returns 1. Infrastructure/report/identity failure returns nonzero separately.
The runner now observes closed publications every 50 ms. A fully validated report gets at most 250 ms
of natural-exit grace within the deadline. If Qt remains alive, only the owned group is terminated and
waited, with a 2-second TERM-to-KILL cleanup bound. This is explicitly `stop_reason: validated_report`,
**status 125 / INCOMPLETE**, not an emulator PASS. Both reported PASS and FAIL bits remain available as
`reported_semantic`; actual process exit, PID, wait and duration are separate in `process.json` and
`result.json`. Report bytes must match the hash captured before shutdown. Timeout remains 124,
invalid publication 2, and spontaneous nonzero process exits remain failures. Nothing in this policy
relaxes the requirement for natural emulator exit 0 before an accepted COMPLETE semantic verdict.
One absolute monotonic deadline starts before process creation. The observer checks it before polling,
after polling but before accepting an exit, after report validation when calculating the remaining
exit grace, and after any wait/decision before accepting its status. Both monitored and unmonitored
waits use the remaining budget; report polling sleeps also recalculate it. Reaching the deadline,
including exactly at the boundary, returns **124 / INCOMPLETE**, even when a delayed observation
finds exit 0 and a valid PASS report. Only early report-driven shutdown retains 125. Cleanup still
has its separate owned TERM-to-KILL bound; post-exit report/identity collection does not extend guest
execution. An exit that cannot be established before the deadline is conservatively rejected.
`fp_build.py` records host semantic status even while finishing cross-build evidence and ultimately
returns that failure; successful packaging never relabels a semantic mismatch as PASS.

Cloudpunk's `src/vita/region.c:28–48,309–323` informed checked logging and the required `fopen` before
`freopen` newlib initialization ordering, already used in 01 and retained here. Its completed-log
validator (`tests/test_vita_log.py:1011–1046`) informed rejecting incomplete evidence; this report uses
post-close publication rather than an unchecked exit marker. Xenogears'
`vita/MILESTONE-RETURN-PROTOCOL.md` informed artifact/settings/run identity and separating emulator
from hardware evidence. No donor packaging, BIOS, renderer, MIPS CPU or fiber code was copied.
Their performance figures are not Wind Waker evidence.

## Historical executions and current runtime identity

At source `3876626a6a4c63990e84904afc21f009caa66704`:

- First diagnostic attempt: 60-second timeout, status 124, no report/installed app, 0/71 cases observed.
  Original cause remains UNESTABLISHED. Evidence is in
  `build-vita-ww-03/{first-real-launch-3959eb8615944b77819dc976291c029e,vita3k-run-d653caeb1c604fa190fee0c8316e33b5}/`.
- One traced diagnostic attempt: status 153 after 2.268 seconds. Trace lines 20080–20082 show
  `memfd_create("pulseaudio")`, `ftruncate(...,67108864) = -1 EFBIG`, and `SIGXFSZ`. The temporary
  observer's inherited 20 MiB RLIMIT_FSIZE caused this failure. It does not explain the first timeout.
  VPK appeared only in argv; no package file open or FP report was observed. Evidence is in
  `build-vita-ww-03/traced-startup-e6cdc633d81a4da9ba27ef9979462cc1/`.
- Separate GUI-only setup used new private project profiles and authorized installed system firmware.
  The user confirmed Welcome. The relocated runtime reached its main window in 0.803 seconds;
  Wayland surface configuration, buffer attach/commit and frame callback were recorded. Actual process
  mappings matched the copied runtime/Qt/Wayland inodes. All 1,493 firmware files were visible in its
  actual namespace. No firmware OPEN/ACCESS was observed in that empty-GUI startup, so this was **not**
  an exercised firmware-state API or guest boot. Its 900-second owned watchdog expired and waited.
  Read-only evidence: `/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/vita3k/gui-runs/6aa76e5c788e4e1caa8f275dcfde3981/local-adoption-fbfae8841e53439db2d5d034b00a4c89/`.

Current accepted local stack:
`/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack`.

| Input | SHA-256 |
| --- | --- |
| Completed stack `manifest.json` | `d31bd0c2f332b93b312cf1e5f79d1978dcd23e5bb755cab4cbcdd6aa75c67282` |
| Copied `vita3k/Vita3K` | `cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30` |
| Copied `vita3k-env` tree identity | `db1e7575006de88916d3b16fe62c435fbfccdfec168bf4b8f9643862c65582e2` |
| Installed `firmware/vs0`, 1,473 files | `e9c61115e5444b1d8167d3dc40306f0f5d3f28f0df8bd579ca01bdedc2599fad` |
| Installed `firmware/sa0`, 20 files | `7880fabe7d6d23edfaf9f641ce82cba93b31be6a110dc011603919f5a79b29d8` |

The copy owner's `USAGE.md:101–120` and `checks/relocation/changes.json` record the accepted DT_RPATH
change from `$ORIGIN:/home/fred/projects/xgr-tools/vita3k-env/lib` to
`$ORIGIN:$ORIGIN/../vita3k-env/lib`. The original before-image is preserved. This is a new executable
identity, not a claim of identical bytes or certified source/Boost provenance. Installed firmware
version and PUP provenance remain UNVERIFIED; no `version.txt` was found and none is fabricated.

### Historical source inspection

Original read-only inspected candidate (superseded for future launches by the copied ELF above):
`/home/fred/projects/xgr-tools/vita3k-dev/bin/Vita3K`.
SHA-256 **`fc87e31f385bd40830677fbb6b8cd22f303e6711d362060120ada812cc0c3f9c`**.
It is byte-identical to `vita3k-src/build/linux-ninja-gcc/bin/Vita3K`. `strings` and the build's generated
`vita3k/config/version.cpp` identify **Vita3K v0.2.1 1-bbd5c36**. The source checkout is instead at
**`a32b6aedb619ca1358758679466bd780d9443059`**, one renderer fix beyond
**`bbd5c3624a06572fe4f16f67564f53a7540e1f42`**, and `external/boost` is dirty.
The inspected main/config/path source files are identical between those commits; compile commands
point at those local files. This establishes candidate identity and the local build match, **not a
complete clean source-to-binary provenance assertion**. The banner alone cannot decide which renderer
change or dependency modifications are present. No emulator build, firmware installation or wrapper
execution was attempted to resolve this.

Static source references (same contents at both revisions):

- `vita3k/main.cpp:75–92`: QApplication and path/log initialization precede command-line parsing.
- `vita3k/app/src/app_init.cpp:261–292`: SDL executable directory, or APPIMAGE's parent, can select
  portable mode and override all profile roots.
- `app_init.cpp:326–405`: Linux HOME/XDG defaults; writable Vita filesystem defaults to
  `XDG_DATA_HOME/Vita3K/Vita3K`; data/config/cache/log/patch directories can be created immediately.
- `vita3k/config/src/config.cpp:341–379`: positional VPK, `--console` and `--backend-renderer` are
  real source-defined options. `main.cpp:218–249` installs that VPK and schedules auto-boot.
- `main.cpp:257–265` still creates a Qt main window. Offscreen operation and process exit after
  guest completion remain unverified; a timeout must not become success.

The coordinator originally accepted the architect's recommendation to reuse exactly
`fc87e31f385bd40830677fbb6b8cd22f303e6711d362060120ada812cc0c3f9c`, consciously superseding this
report's original proposal to block on complete clean-source mapping. The source/Boost mapping above
remains **UNVERIFIED**; no claim is made about which renderer fix is embedded. A result means
“this fixture on this runtime hash and recorded libraries/settings”, not certification of an upstream
commit, physical Vita or gameplay speed. OS containment must protect host state independently of the
binary matching inspected source. No emulator rebuild is required by this checkpoint; the subsequently
accepted copied-stack relocation is recorded above.

`fp_launch.py` still defaults to **stage only**. `--execute-reviewed` is an explicit operator gate,
not permission granted by documentation or passing tests. Required gates are corrected fixture,
sandbox and process review PASS; recorded exposed identities/settings and fresh report; and separate
coordinator authorization for the **exact launch**. The plan explicitly records
`runtime_source_mapping: UNVERIFIED`; the source-revision argument is an observed label only.

## Corrected isolation and proposed launch

The launcher uses a new `<out>/vita3k-run-<uuid>/` with mode-0700 children:
`home`, `config`, `cache`, `data`, `state`, `work`, `evidence`, `runtime`, `xdg-runtime`,
`config-system`, `data-system`. Existing roots or symlink ancestors are rejected; nothing is deleted.
It copies the verified ELF and only its public `data`, `icons`, `shaders-builtin`, `translations`
asset directories, rejecting asset symlinks. With `--firmware`, only the explicitly supplied installed
`vs0` and `sa0` trees are copied. Empty trees, symlinks, unsupported file types and existing targets are
rejected; modes and every source/staged file identity are checked. No parent profile, `ux0`, config,
app, save or key is copied. After verified firmware staging only, the new private config selects the
private VitaFS, completes Welcome, retains `warn-missing-firmware: true`, and disables network update
checks. Source and staged firmware identities are checked again after execution.

The generated sandbox command uses installed `/usr/bin/bwrap`:

```text
--unshare-all --die-with-parent --new-session
--ro-bind /usr /usr --ro-bind /etc /etc
--symlink usr/lib /lib --symlink usr/lib64 /lib64
--proc /proc --dev /dev --tmpfs /tmp
--ro-bind <owned-run-root> /run/ww03
--ro-bind <owned-run-root>/runtime /stack/vita3k
--chdir /stack/vita3k --clearenv
--bind <owned-run-root>/home /run/ww03/home
--bind <owned-run-root>/config /run/ww03/config
--bind <owned-run-root>/cache /run/ww03/cache
--bind <owned-run-root>/data /run/ww03/data
--bind <owned-run-root>/state /run/ww03/state
--bind <owned-run-root>/work /run/ww03/work
--bind <owned-run-root>/xdg-runtime /run/ww03/xdg-runtime
--ro-bind <copied-vita3k-env> /stack/vita3k-env
--ro-bind <owned-run-root>/data/Vita3K/Vita3K/vs0 /run/ww03/data/Vita3K/Vita3K/vs0
--ro-bind <owned-run-root>/data/Vita3K/Vita3K/sa0 /run/ww03/data/Vita3K/Vita3K/sa0
--ro-bind /run/user/1000/wayland-0 /run/ww03/xdg-runtime/wayland-0
```

Explicit environment (each passed with `--setenv`): HOME=`/run/ww03/home`, PATH=`/usr/bin`,
LANG=`C.UTF-8`; XDG_CONFIG_HOME/CACHE_HOME/DATA_HOME/STATE_HOME point to corresponding private
children, XDG_RUNTIME_DIR to `xdg-runtime`, XDG_CONFIG_DIRS to `config-system`, XDG_DATA_DIRS to
`data-system`. LD_LIBRARY_PATH=`/stack/vita3k-env/lib`, QT_PLUGIN_PATH=`/stack/vita3k-env/lib/qt6/plugins`,
SDL_AUDIODRIVER=`dummy`, LIBGL_ALWAYS_SOFTWARE=`1`, FONTCONFIG_FILE=`/etc/fonts/fonts.conf`.
Without a display argument, QT_QPA_PLATFORM and SDL_VIDEODRIVER are `offscreen`. The proposed launch
explicitly selects the authorized Wayland socket: QT_QPA_PLATFORM=`wayland`, WAYLAND_DISPLAY=`wayland-0`,
SDL_VIDEODRIVER unset. The socket must be a real Unix socket owned by the current user; no whole
runtime directory, display-session bus, shared home, APPIMAGE/APPDIR or physical device tree is
inherited. This display connection is a separate authorized permission, not a claim of unchanged
headless isolation. Network and PID namespaces remain private. Only the seven named private
profile/work children are writable host storage; both runtime aliases and the evidence directory are
read-only in the sandbox. Parent directories cannot be replaced from writable children. The executable
is a copied file at `/stack/vita3k/Vita3K`,
not a symlink into a shared writable installation. Portable mode cannot select an existing shared
directory. Unavailable namespaces fail closed; there is no unsandboxed fallback.

The proposed guest command is `/stack/vita3k/Vita3K --console --backend-renderer OpenGL fp.vpk`,
with the protected `/stack/vita3k` cwd. The package argument is a fixed basename: no absolute slash,
traversal or writable-directory lookup. This also preserves the relocated ELF's sibling-library RPATH.
Inspected `config.cpp:331–346` enables CLI11 Windows-style options and extras before defining the
positional content path. Absolute `/runtime/fp.vpk` being parsed as an option remains a **hypothesis**
for the first attempt; relative argv avoids that ambiguity without claiming to have reproduced the
parser defect on the opaque binary.
Its `plan.json` records every argument, timeout, source/package/runtime identity and staged asset hash.
The child receives stdin from `DEVNULL`. Its preopened stdout/stderr log descriptor intentionally
remains writable; it cannot alter `plan.json` or create/redirect `result.json` through the filesystem.
Host evidence creation uses directory-descriptor traversal without symlinks and exclusive/no-follow
file creation, including plan, process log and result. Existing evidence destinations are rejected.
`process.log` and `result.json` remain private. Timeout handling signals only the Popen-owned process
group; the PID namespace and `--die-with-parent` contain descendants, including a fake detached child.
No global process kill, existing-profile deletion, console access or Internet access is used.
The runner rejects inherited finite RLIMIT_FSIZE and never sets a file-size limit or runs a tracer.
Its CLI deadline is finite, positive and at most 30 seconds. The process record separates actual exit
from timeout, malformed-report and validated-report shutdown reasons.

Proposed command, **not executed in this correction pass** (variables defined in the build runbook):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B scripts/vita/armv7/fp_launch.py \
  --runtime "$STACK/vita3k/Vita3K" \
  --runtime-sha256 cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30 \
  --runtime-source-revision 'observed-banner:bbd5c36;local-RPATH-relocation;mapping:UNVERIFIED' \
  --libraries "$STACK/vita3k-env" --firmware "$STACK/firmware" \
  --wayland-socket /run/user/1000/wayland-0 \
  --build-evidence "$OUT/fp-evidence.json" --out "$OUT" --timeout 30 --execute-reviewed
```

Omitting `--execute-reviewed` stages and records the plan only; it does not invoke the runtime.
Every real attempt creates its own UUID and must use the checkpoint's fresh artifact identities.

### Exposed public-library identity

The entire copied public `$STACK/vita3k-env` tree is exposed read-only at
`/stack/vita3k-env`. `fp_libraries.py` inventories every file's bytes/hash and mode, directory mode,
symlink text, resolved in-tree target/type and target-file hash. Escaping or dangling links and
unsupported file types fail closed. File metadata changes during hashing are rejected. Canonical
JSON of these entries determines the tree identity. `libraries-before.json` is recorded immediately
before execution and referenced by the protected plan; `libraries-after.json` is checked afterward.
A mutation or failed verification makes the run INCOMPLETE and nonzero, regardless of a passing
report. Fake tests mutate only their own public-library stand-in.

Historical pre-copy read-only inventory (not the current relocated tree identity):

- Tree SHA-256: `07910a2b4c1a18a5c07ae2f808b5a6220061c8021461105b7b29139abda1ae8c`.
- 50,628 regular files, 3,769 directories including the root, 2,611 in-tree symlinks.
- Records: `build-vita-ww-03/review-round1/{public-libraries.json,runtime-identity.json}`.
- Runtime ELF hash was rechecked and matches the accepted hash above.

This is before/after verification of a live read-only-mounted tree, not a frozen filesystem snapshot;
transient host-side modifications restored between checks cannot be ruled out. `/usr` and `/etc`
remain live read-only system mounts and are not included in this library-tree digest. Thus neither
the tree digest nor executable hash certifies every host OS dependency. System firmware has its own
explicit, narrow identity/copy protocol; no player files are inspected. Actual execution records fresh before/after
identities rather than reuse this inspection's inventory as execution evidence.

## Verification and checkpoint artifacts

| Check | Observed result |
| --- | --- |
| AArch64 FP diagnostic | 71/71 PASS; actual bits equal the table-generated literal expectations; all path checks pass |
| FP ARMv7 ELF/SELF/VPK | Build PASS, title BWWF00003; exactly `eboot.bin` and `sce_sys/param.sfo` |
| Fixture Python regressions | 57/57 PASS with copied native GCC: prior 50 plus seven deterministic deadline regressions |
| 01 corpus / contract / pinned GXRuntime CTest | 1/1, 5/5, 3/3 PASS |
| 01 complete host executables and both VPKs | PASS |
| Repo audit and eight direct baseline files | PASS; 53 baseline tests |
| Prior real diagnostic startups | INCOMPLETE, 0/71 observed; timeout and instrumentation-induced SIGXFSZ documented above |
| Copied-stack GUI-only startup | Main window verified; no guest execution or exercised firmware-state API |
| Renewed diagnostic / physical Vita / Windows CI / full game | NOT RUN |

Genuine short red/green observations, saved under `build-vita-ww-03/logs/`:

- `report-semantic-{red,green}.log`: initial validator returned 0 for a complete FAIL; after validation
  of case bits/verdicts/totals it returns 1. Stale identity rejection remains passing.
- `publication-{red,green}.log`: a real unbuffered `/dev/full` write leaves a sticky FILE error even
  after the descriptor is restored to `/dev/null`. Close-only publication incorrectly returned 0;
  shared flush+ferror+fclose handling returns failure and does not publish. Existing fault tests stay green.
- `launcher-isolation-{red,green}.log`: the fake initially observed the host working directory;
  the sandboxed implementation observes only its private working directory and read-only runtime.
  Additional integration regressions cover a different network namespace, stale/symlink paths,
  process exit 7, semantic FAIL with emulator exit 0, missing reports and timeout descendant cleanup.

### Review correction round 1

The original launcher was frozen read-only with `git show
e0c3f6bf1d41c34aabc574a1890ae7064a67d586:scripts/vita/armv7/fp_launch.py` in
`build-vita-ww-03/review-round1/fp_launch-e0c3f6b.py`. No checkout/history or other worktree was changed.
The following are **executed regressions**, not static exploit predictions. Each red was run against
that snapshot, then the same scenario was run green against the correction. All attack targets and
processes were owned test fixtures; the outside-run sentinel was still inside the owned test output.

```sh
WW_FP_LAUNCH_SOURCE="$PWD/build-vita-ww-03/review-round1/fp_launch-e0c3f6b.py" \
  python3 vita/feasibility/armv7/test_fp_review.py LauncherReviewTest.TEST_NAME -v
python3 vita/feasibility/armv7/test_fp_review.py LauncherReviewTest.TEST_NAME -v
```

| Finding / scenario (`TEST_NAME`) | Red observation → corrected result |
| --- | --- |
| 1: `test_sandbox_cannot_redirect_result_or_modify_plan` | Fake could create a result symlink and modify plan; host collection overwrote the owned outside-run sentinel → sandbox evidence paths read-only, sentinel and plan unchanged, preopened logging works. `test_host_result_rejects_preexisting_symlink` also reproduced the unsandboxed overwrite → exclusive/no-follow host creation rejects it. |
| 2: `test_fake_receives_eof_instead_of_caller_stdin` | Fake read `caller input must stay private` from inherited fd0 → reads EOF with explicit DEVNULL. |
| 3: `test_replaced_source_package_is_rejected_after_staging` | Controlled replacement during asset staging was accepted under the old package identity → actual staged hash mismatch rejected before request/command. |
| 4: `test_fifo_report_does_not_block_collection` | Collector blocked past its owned 2-second test watchdog → prompt nonzero INCOMPLETE. Oversized-file and abnormal-exit tests also ran red/green: old code parsed oversized input and labeled valid reports PASS after timeout/crash → bounded read and INCOMPLETE disposition. |
| 5: `test_library_mutation_invalidates_otherwise_passing_fake_run` | Valid fake PASS plus host-side mutation of an owned test library returned 0 → before/after mismatch returns nonzero INCOMPLETE, retains raw reported PASS, and records byte/symlink identities. |
| 6: disposition | Coordinator-approved exact runtime-hash reuse replaces the proposed clean-source hold explicitly above. Source mapping remains UNVERIFIED and execution is still forbidden pending review and exact-launch authorization. |

Logs: `build-vita-ww-03/review-round1/logs/{evidence,stdin,package,collection,libraries}-{red,green}.log`.
Additional checks cover symlink parents (`evidence-expanded-red.log`), replacement of an opened
report path with a FIFO, retargeting a library symlink between files with identical bytes, and a
complete passing fake run with unchanged libraries. No import error is counted as
red evidence. The FP vectors, native CPU/runtime, compiler flags and 01 entrypoints are unchanged.

The initial new validator's import failure is not counted as semantic red evidence. New fixture and
emulator plumbing are an R&D integration: the 71-case actual host execution and ARM disassembly are
their meaningful checks, rather than tests that reproduce the implementation. Initial build-only
seams needed the lock's non-git entries handled and the FP code range declared to the existing
composite generator; these were harness corrections, not runtime semantic fixes.

### Copied-stack relaunch correction

Evidence root: `build-vita-ww-03-relaunch-fae48ad887c6463c8a6f9bf7068536c6/`.
Its read-only `fp_launch-3876626.py` was obtained with `git show`; history, existing evidence and GUI
profiles were not rewritten. Logs record the commands and actual failures/successes:

- `logs/relative-package-{red,green}.log`: the old fake invocation received `/runtime/fp.vpk` and failed
  its argv assertion. Corrected execution receives exactly `fp.vpk` in protected `/stack/vita3k`, reads
  the verified package, resolves a sibling public-library marker and cannot write into the package cwd.
- `logs/completion-{red,green}.log`: a complete fake report followed by a lingering process formerly
  reached timeout 124. The corrected monitor stops it as status 125 with explicit incomplete process
  disposition. The frozen-code adapter merely accepts and ignores the new report-identity argument;
  the old runner itself actually runs and times out. No import/signature error is counted as this red.
- `logs/file-limit-{red,green}.log`: the old runner started an owned marker-writing process under a
  finite inherited file-size limit. The corrected runner rejects that condition before spawning.
  An unlimited fake also successfully creates a 64 MiB memfd.
- Firmware and display feature tests first reported missing CLI/API support (not emulator defects),
  then passed actual staging and fake Unix-socket execution. Only `vs0`/`sa0` are copied, checked and
  read-only mounted; unrelated owned `ux0` input is excluded. Invalid/empty/FIFO/symlink firmware cannot
  disable Welcome; existing files remain untouched and mutations invalidate identity. The fake display
  client reaches only its selected socket with private XDG, without the neighboring sentinel/session bus.
- Expanded tests preserve lingering semantic FAIL, detect changed report bytes after shutdown, and
  prove completion cleanup stops a detached owned child's heartbeat while a separate owned witness
  remains alive. All prior stale/FIFO/report/logging/security/semantic regressions remain in the suite.

The compiler/CPU/oracle/emitted PPC sources and pinned references are unchanged. The tests exercise
launcher behavior using owned fake processes, not an emulator substitute claiming ARM FP results.
The `6edf104` build/check logs remain historical evidence. Review blocked that launch plan on the
deadline race below; it was never used for renewed real execution.

### Review round 2: absolute launch deadline

Evidence root: `build-vita-ww-03-deadline-c6f417fb8b79483a9165b0d6273bfde1/`.
The sole consolidated blocker at `6edf10434cd2313eae091b68c5ba4e8b78b7b160` was acceptance after
the launch deadline: `poll()` could return a late zero exit, or report validation could consume the
remaining budget before a wait used its stale value. Both paths could become COMPLETE/PASS.

Only the launch deadline, its regression tests and this report change. `remaining_time()` checks one
absolute deadline at every acceptance boundary, recalculates every execution wait and throws into
the existing timeout disposition. The collector keeps the valid report's diagnostic bits while
status 124 prevents COMPLETE/PASS. Report-driven shutdown before expiry remains 125, and all existing
process ownership, PID namespace, stdin, evidence, input and publication safeguards remain intact.

The seven new tests use a controlled monotonic clock and a `Popen` test double; no real-time sleeps,
emulator invocation or live PID signalling occur in these tests. They publish a regular, closed
71-case report and use the actual validator/collector. Frozen `fp_launch-6edf104.py` came from
`git show`, without an API adapter:

- `logs/observer-red.log`: report/exit at simulated 30.02 s, observer wake at 30.06 s produced
  COMPLETE/PASS. `observer-green.log` rejects the same conditions as 124/INCOMPLETE.
- `logs/validation-red-frozen.log` and `validation-red-poll-fix.log`: validation crossing to 30.02 s
  while the child exits zero still produced COMPLETE/PASS, including after the first localized poll
  fix. `validation-green.log` records the second short loop's correction.
- `logs/deadline-suite-red-frozen.log`: all seven assertions fail against the frozen checkpoint for
  their intended behaviors. Further cases cover late natural waits with/without report monitoring,
  deadline expiry during exit grace, grace reduced by validation time, and an exit observed exactly
  at the deadline. `wait-red-partial.log` preserves the remaining three failures before the final guard.
- `logs/deadline-suite-green.log`: all seven pass. The full suite also retains early successful exit,
  semantic FAIL, early lingering-report 125, detached owned-child cleanup and unrelated witness survival.

New exact-commit manifests, logs and `proposed-launch.json` supersede the prior plan as new artifacts;
the prior plan, builds and evidence are preserved. The new source-bound ELF/SELF/VPK must be used.
**Renewed real launches: 0/2. Observed ARMv7 FP cases: 0/71.** Follow-up review and exact-launch
authorization are still required. Opaque runtime/source/Boost and firmware provenance limitations
are unchanged; this correction supplies no emulator, game, renderer or physical performance result.

Current artifact locations: `$OUT/{host-fp,vita-fp}/ww_fixture`, `$OUT/vita-fp/{eboot.bin,ww_fp.vpk}`.
Host run records are fresh `$OUT/host-run-<uuid>/{fp.request,fp.report,process.log}`. The full 01
campaign is rebuilt in `$BASELINE_OUT`; earlier 01/03 evidence remains preserved at its original paths.
An accepted runtime diagnostic FAIL supports REVISE and a narrowly scoped reproduction-driven repair,
never GO. Neither GUI setup nor the failed prior startups establish ARMv7 correctness.
