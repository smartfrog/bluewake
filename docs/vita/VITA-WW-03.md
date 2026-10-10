# VITA-WW-03: bounded ARMv7 floating-point diagnostic

2026-10-10 · [ticket](https://trello.com/c/DcwHCjuc) · game-free, opt-in

## Pre-launch checkpoint: REVISE, execution gate pending

The AArch64 Linux diagnostic passes **71/71** independently specified result-bit cases. The same
emitted synthetic PowerPC functions and pristine RecompCore CPU build into a Vita ARMv7 VPK.
The isolated launcher passes fake-process tests, including network/filesystem isolation, detached
descendant cleanup and preserving semantic failure when the emulator process exits zero.

**Real Vita3K: NOT RUN**, including `--help` and `--version`. Physical Vita/VitaShell: NOT RUN.
Independent review and coordinator authorization are mandatory before the original worker launches
the emulator. This is a checkpoint, not completion of 03 or a GO recommendation. Runtime correction
must be scoped after reproduction. VITA-WW-04 remains separate.

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
`build-vita-ww-03/fp-evidence.json`; 01 evidence is in `build-vita-ww-01/evidence.json`.
Do not identify a package by filename alone. Rebuilding embeds the source revision and changes
hashes; SELF/VPK packaging can also vary. The handoff includes that revision's artifact hashes.

Dependencies are private pristine clones under this worktree's ignored `ref/vita-ww-01/`, copied
with `git clone --no-hardlinks` from the integration tree's pinned references. Both campaigns check
the product lock/profile and verify dependency SHA and clean status before and after building.
No dependency, runtime pin, default platform build or game data is modified.

Commands actually used on AArch64 Linux (`7.1.13-402.asahi.fc44.aarch64+16k`):

```sh
python3 scripts/vita/armv7/fp_build.py --vita \
  --cc /home/fred/cloudpunk-vita/tools/env/bin/aarch64-conda-linux-gnu-gcc \
  --cmake /home/fred/.local/bin/cmake --ninja /home/fred/.local/bin/ninja --jobs 2
python3 scripts/vita/armv7/build.py --vita \
  --cc /home/fred/cloudpunk-vita/tools/env/bin/aarch64-conda-linux-gnu-gcc \
  --cmake /home/fred/.local/bin/cmake --ninja /home/fred/.local/bin/ninja --jobs 2
CC=/home/fred/cloudpunk-vita/tools/env/bin/aarch64-conda-linux-gnu-gcc \
  python3 -m unittest discover -s vita/feasibility/armv7 -p 'test_*.py' -v
bash scripts/audit_repo.sh
```

Also run each of `tests/test_{composite,direct_calls,inline_gpr,guest_checkpoints,frame_tail,
signing_entitlements,sample_owners,one_game_guard}.py` directly with Python. These are 53 tests;
the composite file has its own driver. `gh pr list` was attempted but `gh` is unavailable.
The remote-branch check showed only `origin/main` and its HEAD alias. No public action was taken.

Tool versions: native GCC 16.2.0, Vita GCC 15.2.0, CMake 4.4.3, Ninja
1.13.2.git.kitware.jobserver-pipe-1, Python 3.14.8. No installation was needed.

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
on Vita it is the VPK, hashed by the launcher before its immutable staged copy is installed.
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
The launcher also preserves nonzero process status and timeout (124). An emulator exit of zero with
a valid semantic FAIL still returns 1. Infrastructure/report failure returns nonzero separately.
`fp_build.py` records host semantic status even while finishing cross-build evidence and ultimately
returns that failure; successful packaging never relabels a semantic mismatch as PASS.

Cloudpunk's `src/vita/region.c:28–48,309–323` informed checked logging and the required `fopen` before
`freopen` newlib initialization ordering, already used in 01 and retained here. Its completed-log
validator (`tests/test_vita_log.py:1011–1046`) informed rejecting incomplete evidence; this report uses
post-close publication rather than an unchecked exit marker. Xenogears'
`vita/MILESTONE-RETURN-PROTOCOL.md` informed artifact/settings/run identity and separating emulator
from hardware evidence. No donor packaging, BIOS, renderer, MIPS CPU or fiber code was copied.
Their performance figures are not Wind Waker evidence.

## Static Vita3K inspection and isolation contract

Read-only inspected candidate:
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

`fp_launch.py` defaults to **stage only**. `--execute-reviewed` is an explicit operator gate, not
permission granted by this report. Before execution, review must resolve the provenance mapping
above and authorize this exact launcher/source/artifact. The bounded prerequisite is a maintainer's
existing build record mapping the candidate hash to its source/dependency changes, or verification of
another already-installed candidate with established provenance. A new emulator build/install is
outside this ticket's authorization.

The launcher uses a new `build-vita-ww-03/vita3k-run-<uuid>/` with mode-0700 children:
`home`, `config`, `cache`, `data`, `state`, `work`, `evidence`, `runtime`, `xdg-runtime`,
`config-system`, `data-system`. Existing roots or symlink ancestors are rejected; nothing is deleted.
It copies the verified ELF and only its public `data`, `icons`, `shaders-builtin`, `translations`
asset directories, rejecting asset symlinks. Existing profiles/configs/firmware/apps/saves are not staged.

The generated sandbox command uses installed `/usr/bin/bwrap`:

```text
--unshare-all --die-with-parent --new-session
--ro-bind /usr /usr --ro-bind /etc /etc
--symlink usr/lib /lib --symlink usr/lib64 /lib64
--proc /proc --dev /dev --tmpfs /tmp
--bind <owned-run-root> /run/ww03
--ro-bind <owned-run-root>/runtime /run/ww03/runtime
--ro-bind <owned-run-root>/runtime /runtime
--chdir /run/ww03/work --clearenv
--ro-bind <public-vita3k-env> /runtime-libs
```

Explicit environment (each passed with `--setenv`): HOME=`/run/ww03/home`, PATH=`/usr/bin`,
LANG=`C.UTF-8`; XDG_CONFIG_HOME/CACHE_HOME/DATA_HOME/STATE_HOME point to corresponding private
children, XDG_RUNTIME_DIR to `xdg-runtime`, XDG_CONFIG_DIRS to `config-system`, XDG_DATA_DIRS to
`data-system`. LD_LIBRARY_PATH=`/runtime-libs/lib`, QT_PLUGIN_PATH=`/runtime-libs/lib/qt6/plugins`,
QT_QPA_PLATFORM=`offscreen`, SDL_VIDEODRIVER=`offscreen`, SDL_AUDIODRIVER=`dummy`,
LIBGL_ALWAYS_SOFTWARE=`1`. No APPIMAGE, APPDIR, session bus, display socket, shared home or physical
device tree is inherited. Network and PID namespaces are private. Only the owned root is writable
host storage; runtime aliases are both read-only. The executable is a copied file at `/runtime/Vita3K`,
not a symlink into a shared writable installation. Portable mode cannot select an existing shared
directory. Unavailable namespaces fail closed; there is no unsandboxed fallback.

The proposed guest command is `/runtime/Vita3K --console --backend-renderer OpenGL /runtime/fp.vpk`.
Its `plan.json` records every argument, timeout, source/package/runtime identity and staged asset hash.
`process.log` and `result.json` remain private. Timeout handling signals only the Popen-owned process
group; the PID namespace and `--die-with-parent` contain descendants, including a fake detached child.
No global process kill, existing-profile deletion, console access or network operation is used.

## Verification and checkpoint artifacts

| Check | Observed result |
| --- | --- |
| AArch64 FP diagnostic | 71/71 PASS; actual bits equal the table-generated literal expectations; all path checks pass |
| FP ARMv7 ELF/SELF/VPK | Build PASS, title BWWF00003; exactly `eboot.bin` and `sce_sys/param.sfo` |
| Fixture Python regressions | 25/25 PASS, including existing logger faults and new publication/validator/fake-launcher tests |
| 01 corpus / contract / pinned GXRuntime CTest | 1/1, 5/5, 3/3 PASS |
| 01 complete host executables and both VPKs | PASS |
| Repo audit and eight direct baseline files | PASS; 53 baseline tests |
| Real Vita3K / physical Vita / Windows CI / full game | NOT RUN |

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

The initial new validator's import failure is not counted as semantic red evidence. New fixture and
emulator plumbing are an R&D integration: the 71-case actual host execution and ARM disassembly are
their meaningful checks, rather than tests that reproduce the implementation. Initial build-only
seams needed the lock's non-git entries handled and the FP code range declared to the existing
composite generator; these were harness corrections, not runtime semantic fixes.

Exact artifact locations: `build-vita-ww-03/{host-fp,vita-fp}/ww_fixture`,
`build-vita-ww-03/vita-fp/{eboot.bin,ww_fp.vpk}`. Host run records are fresh
`build-vita-ww-03/host-run-<uuid>/{fp.request,fp.report,process.log}`. Baseline artifacts and evidence
stay in `build-vita-ww-01/`. An accepted runtime diagnostic FAIL would support REVISE and a narrowly
scoped reproduction-driven correction; it can never support GO. Missing emulator execution remains
an explicit checkpoint blocker, not acceptable ARMv7 correctness evidence.
