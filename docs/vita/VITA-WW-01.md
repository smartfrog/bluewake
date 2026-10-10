# VITA-WW-01: ARMv7 and memory feasibility

2026-10-10 · [experiment contract](https://trello.com/c/yihyNQQJ) · experimental, opt-in, game-free

## Decision: REVISE

The public PowerPC fixture passes on the AArch64 Linux host and builds into two Vita ARMv7 VPKs
against the **real RecompCore CPUState and CPU implementation**. The production composite dispatcher,
direct-call preparation and host-boundary handshake are exercised, not replaced with a synthetic CPU.

**This is not GO, a playable port, or evidence of 30 FPS.** Vita3K and physical Vita execution are
PENDING. Full-game boot, scene and transition peaks and the usable application pools are UNKNOWN.
There is also a concrete ARMv7 floating-point control gap in the pinned runtime. The bounded campaign
stops here; fixing the runtime, porting the host or running another investigation needs separate approval.
Missing hardware access is not a platform STOP.

Confidence is high in the build/ABI observations and the need to revise; medium in the bounded stack
argument for this particular emitted fixture; low in overall game feasibility or performance. VITA-WW-02
remains blocked until 01 is integrated, its report is accepted **GO**, and its memory budget is defensible.
This delivery does not perform or approve integration.

## Frozen inputs and boundaries

| Input | Exact revision |
| --- | --- |
| BlueWake base | `2b097a4e174b2bb7114a8f2bed12af1ea0f7692f` |
| [RecompCore](https://github.com/chrissotraidis/RecompCore/tree/ddd031b2a7f45fcbda37a49daac9b308b8d95da1) | `ddd031b2a7f45fcbda37a49daac9b308b8d95da1` |
| [DolRecomp](https://github.com/elliotttate/DolRecomp/tree/b8b534591cba8ca7cd43943a655ee6e2591cf5de) | `b8b534591cba8ca7cd43943a655ee6e2591cf5de` |

The delivery revision is obtained with `git log -1 --format=%H -- docs/vita/VITA-WW-01.md`.
After validation, `build-vita-ww-01/evidence.json` records that revision as `bluewake_sha`.
The runner checks the product lock/profile pins and pristine dependency checkouts before and after
the campaign. It never changes either dependency, installs packages, changes clocks, boots a game,
installs a VPK, or accesses a disc. The user's reported GZLE01 USA revision 0 identity was not verified;
it is a prerequisite for any later private game measurement.

Only `scripts/vita/armv7/`, `vita/feasibility/armv7/` and this report are authored. The normal builds,
runtime, settings, dependency pins and existing platforms are untouched. Downloaded sources, emitted C,
fixture inputs and native artifacts stay under already-ignored `ref/vita-ww-01/` or a root `build-*` directory.
The small `synthetic.dol` is constructed from zeroes and a fixture header, not read from a disc;
its composite uses identity `SYNTH`. No game code, assets, saves, keys or signing material are included
or published.

## Reproduce

Prerequisites: native GCC, Python 3, Git, CMake, Ninja and, for `--vita`, an existing VitaSDK.
The defaults run host checks only. `--fetch` opts into downloading missing pinned public sources.
The generator is always native; its emitted C is compiled separately for host and Vita.

```sh
python3 scripts/vita/armv7/build.py --fetch
python3 scripts/vita/armv7/build.py --vita
```

Exact command used on this AArch64 Linux machine:

```sh
python3 scripts/vita/armv7/build.py --vita \
  --cc /home/fred/cloudpunk-vita/tools/env/bin/aarch64-conda-linux-gnu-gcc \
  --cmake /home/fred/.local/bin/cmake \
  --ninja /home/fred/.local/bin/ninja --jobs 2
```

Native GCC: **16.2.0**; Vita `arm-vita-eabi-gcc`: **15.2.0**; CMake: **4.4.3**;
Ninja: **1.13.2.git.kitware.jobserver-pipe-1**. Host kernel:
`Linux 7.1.13-402.asahi.fc44.aarch64+16k`. No Vita execution was performed on this machine.
Vita3K was not found on PATH; availability of a physical Vita has not been established.

Fixture and CPU flags: `-O2 -ffp-contract=off -fno-fast-math -fstack-usage`.
Fixture compilation also uses `-Wall -Wextra`, with implicit function declarations and pointer/integer
casts treated as errors. Vita adds `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard`.
Composite chunks use the unchanged production `cmake/composite/hidden_externs.h`.
The separate host GXRuntime build sets both `GXRUNTIME_ENABLE_AURORA=OFF` and
`GXRUNTIME_ENABLE_AURORA_RECOMP=OFF` and enables its upstream tests.

### Build contract

1. Build DolRecomp's `test_codegen_emit` and the small supplemental `ww_stack_emit` natively,
   with LLVM disabled. These link the pinned frontend/backend, **not `dr_cpu`**.
2. Emit the upstream public PPC corpus. `corpus` mode changes only its execution driver's CPU
   include and renames `main` at compile time; all reference checks remain unchanged.
3. Split emitted functions without changing their bodies, retaining each preceding loop helper
   with its function. Add a twelve-instruction synthetic guest routine that saves/restores LR and
   R1 and recursively uses `bctrl`.
4. Run the existing `scripts/generate_composite.py` and `scripts/windows/direct_calls.py` on
   those chunks. The resulting 20 chunks use the production headers, one direct-call rewrite,
   two indirect-call rewrites, four interpreter continuations and the real 871-address host watch list.
5. Link pristine RecompCore's six `src/core/cpu*.c` implementation sources plus `guest_memory.c`
   and `guest_memory_dirty.c` for both architectures. No alternative CPUState or CPU semantics are authored.

The one localized build-contract correction was necessary because RecompCore does not expose
`ppc_fp_available_inline`, `ppc_psq_load_inline` or `ppc_psq_store_inline`, which this pinned emitter
names. `prepare.py` extracts their **unchanged pinned DolRecomp wrappers** into an ignored build-only
include. They call RecompCore's real functions; no dependency file is patched and no runtime fix is made.
Generated `static inline` declarations with `noinline` still produce GCC `-Wattributes` warnings;
these warnings and the attributes are preserved, not suppressed or removed.

The composite ABI header is reused, but this is a standalone executable with linked CPU sources, not a test of the
full game's dynamic module loader, fixed-CPU/fixed-MEM1 variants, MMIO/HLE, DSP, audio or graphics host.
The desktop SDL/windowing and Aurora/Dawn/WebGPU stack is not a Vita backend; compiling this subset
does not establish compatibility of those full-host dependencies.

## Correctness and stack evidence

| Check | AArch64 host | Vita cross-build | Vita execution |
| --- | --- | --- | --- |
| Upstream corpus: integer, memory, deadline/quantum/observation invariance, interrupt boundary, direct/indirect dispatch, FP compare/add/paired-single merge | PASS, unchanged reference checks | PASS | PENDING |
| Shared alias: one owned 64-byte backing, two views, big-endian visibility, end-of-span rejection | PASS; actual pointer exceeds 32 bits and is preserved | PASS | PENDING |
| Auxiliary-VM to mirrored MEM1 bulk copy: 4,096 bytes, big-endian bytes and resolver boundaries | PASS | PASS | PENDING |
| Direct call: callee executes, rejected return-boundary observation stops continuation | PASS: `pc=80004104 r3=1 r4=21`, two queries | PASS | PENDING |
| Indirect call: real chunk lookup/function pointer, guest return and host depth restored | PASS: `pc=81234564 r3=0 r4=7`, two queries | PASS | PENDING |
| Sustained indirect guest recursion at depths 64 / 4,096 / 65,536 | PASS, bounded span below | PASS, assembly below | PENDING |
| Unmodified standalone GXRuntime host tests, including plain/global extended-MEM1 bounds | 3/3 PASS | Not a full Vita runtime build | PENDING |

FP coverage is deliberately narrower than a full semantic certification: the reference checks compare,
single add/record and paired-single merge. Quantized load/store wrappers compile but are not certified
by those checks. Alternate rounding modes, denormal handling and exhaustive FP behavior are not proven.

### Pointer and CPU ABI

`abi.c` embeds these compile-time values in `.fixture_abi`, independently readable without execution:

| Field (bytes except ABI version) | AArch64 host | Vita ARMv7 |
| --- | ---: | ---: |
| Host pointer / `uintptr_t` | 8 | 4 |
| `sizeof(CPUState)` | 3,552 | 3,496 |
| `GXRUNTIME_CPU_ABI_VERSION` | 6 | 6 |
| RAM pointer offset | 3,456 | 3,428 |
| External-pointer callback offset | 3,472 | 3,436 |

Guest PC/addresses remain `u32`; host buffers and callbacks remain host-width pointers, resolved by
offsets and aliases rather than casting a guest address into a host pointer. CPU size/offsets must not
be copied from the 64-bit build into ARMv7. The ARM ELF is little-endian ILP32, EABI5 hard-float,
ARMv7-A/Thumb-2, VFPv3/NEON with VFP-register arguments. Guest bytes stay big-endian.

### Stack observations

| Guest recursion depth | Host depth observed at callback | Host marker span (bytes) | Outer dispatches |
| ---: | ---: | ---: | ---: |
| 64 | 31 | 5,456 | 2 |
| 4,096 | 31 | 5,456 | 125 |
| 65,536 | 31 | 5,456 | 1,986 |

Every run restores `pc=81234564`, `r1=80200000` and `bw_direct_depth=0`. This is a measured
**host callback-marker span**, not the complete thread high-water mark or a Vita measurement.
The deepest guest stack writes approximately 1 MiB within the already-allocated MEM1, not an
additional 1 MiB of native stack.

ARM `.su` records and disassembly show persistent frames of 64 bytes for `func_80006000`,
32 for `bw_call_translated`, 24 for `fixture_dispatch`, and 3,576 for `stack_case` (including CPUState).
The unchanged production readiness guard compares depth against 31 and rejects depth 32 before
recursing. At most 32 nested translated calls plus the root give a persistent-frame subtotal
`33 * 64 + 32 * 32 + 24 + 3576 = 6,736` bytes for this path, before wrappers and runtime/library callees.
`bw_call_translated` contains actual `blx r6`, bracketed by depth increment/decrement and balanced
stack restoration. The readiness callback restores its frame before `bx r3`; the dispatcher restores
its frame before branching to its slow path. No terminal-call attribute or guarantee was deleted to
make compilation pass. Bounding comes from the existing guards and dispatcher fallback, not an
assumption that all ARM indirect calls will become tail calls.

The Vita runner requests an explicit **256 KiB worker stack**. Main/startup stack and libc/kernel
callee usage are not measured, and the compiler records do not cover SDK library frames. Repeated
`.su` function names retain the **maximum** frame rather than whichever file was visited last.
Hardware stack high-water and margin are still acceptance requirements.

### Known ARMv7 blocker

Pinned RecompCore `GXRuntime/src/core/cpu_interpreter_float.c:75-110`,
`ppc_arm_host_fp_mode()`, updates AArch64 FPCR or x86-64 MXCSR only. ARMv7 selects `(void)cpu`.
The ARM disassembly of `ppc_fpscr_control_updated` confirms that no host FPSCR update is emitted.
Consequently, guest FPSCR rounding/non-IEEE changes are not shown to govern subsequent native FP
operations on Cortex-A9. Default-mode fixture success cannot close this gap. A narrowly scoped upstream
fix with differential rounding/flush tests would be required; it was not attempted here.

## Memory: allocations are not residency or a game peak

All figures below use bytes or binary MiB. ELF sections, segment sizes and compiler frames are
**cross-build measurements**; heap sizes printed by successful scenarios are **host fixture allocations**.
None is a measurement of a running Vita or a full game. `calloc` success/zero initialization does not
establish physically committed or touched pages. Aliased views are not counted twice.

### Fixture inventory

| Category | Bounded evidence | Missing evidence |
| --- | --- | --- |
| CPU state | 3,496 bytes ARM; 3,552 host, stored on scenario stacks | Full host state/other threads |
| Guest MEM1 | 25,165,824 bytes per `cpu_init`; upstream corpus has two simultaneous allocations: **50,331,648** | Physical residency and full-game 32 MiB configuration |
| Auxiliary VM | Bulk scenario allocates **33,554,432** | Game utilization and touched-page peak |
| Locked cache | Bulk scenario allocates **16,384**; CPU tag/valid arrays are already in CPUState | Runtime utilization |
| Alias backing | Alias scenario owns **64** bytes once, shared by two views, in addition to its MEM1 | Loaded game's REL backing, sharing and transition lifetimes |
| Bulk peak guest backing | MEM1 + auxiliary VM + locked cache = **58,736,640** simultaneously | Allocator overhead, whole-process/native/GPU peak |
| Code / read-only tables | ARM synthetic function/loop text: corpus **50,580**, contract **53,054**; PC-resumption tables **1,212 / 1,260** | Full-game emitted code; no linear extrapolation is justified |
| Dispatch / lookup | Contract has two 4,096-entry caches: **32,768**; edge hash **16,384**; 20 function pointers **80**; starts/ends **160**; watch list **3,484** | Full-game module tables and other host caches |
| Alias / dirty metadata | ARM alias registry **65,536**, sorted index **16,384**, page epochs **65,536** | Additional full-host metadata/caches |
| Native stacks | Worker request **262,144**; compiled and host span evidence above | Real worker/main/other-thread high-water |
| Audio / ARAM / DSP | Not linked into these two fixtures; no fixture audio buffers | All game audio buffers, output rings, DSP state and ARAM utilization |
| GPU resources / caches / staging | No renderer, textures, render targets or upload buffers instantiated by these fixtures | Every application GPU/CDRAM pool and transition upload peak |
| Heap / loader / SDK | Vita newlib heap request **100,663,296** (96 MiB); SDK static data included in ELF below | Request success, allocator usage/fragmentation, main stack and loader/kernel overhead |

The bulk test explicitly writes and verifies 4,096 bytes in both VM and MEM1. The alias test verifies
its backing bytes; sustained recursion writes guest stack records. Whole-allocation page touching,
RSS, fragmentation and transient process high-water were **not** sampled. Contract scenarios run
sequentially and release their guest allocations; their per-scenario peaks must not be added together.
The 96 MiB heap reservation contains those guest allocations: do not add it to them as independent use.

| ARM artifact / section | Corpus | Contract |
| --- | ---: | ---: |
| ELF file bytes | 224,288 | 242,908 |
| SELF `eboot.bin` file bytes | 100,504 | 108,611 |
| VPK file bytes | 97,069 | 104,954 |
| `.text` | 138,648 | 145,560 |
| `.rodata` | 5,988 | 9,532 |
| `.data` | 2,536 | 2,640 |
| `.bss` | 367,620 | 482,348 |
| RX LOAD segment `MemSiz` | 145,668 | 156,124 |
| RW LOAD segment `MemSiz` | 370,172 | 485,004 |
| LOAD segment sum | **515,840** | **641,128** |

LOAD sizes already contain the sections and their dispatch/alias/dirty metadata; do not sum them again.
They exclude heap, thread stacks, pool/page rounding and platform reservations. Compressed VPK/SELF
file sizes are not resident memory. Each VPK has exactly `sce_sys/param.sfo` and `eboot.bin`.
Local artifacts are `build-vita-ww-01/vita-{corpus,contract}/`; they have not been installed or uploaded.
ELF SHA-256 values are recorded in `evidence.json`; packaging timestamps can change VPK hashes on rerun.

### Production source bounds, not measurements

The current module MEM1 contract is **32 MiB** (`cmake/composite/module_cpu_contract.h:19`);
the host expands its ordinary RAM allocation accordingly (`runtime/host/src/main.c:7358-7366`).
Pinned `GXRuntime/src/guest_memory.c:9-12` defaults to **32 MiB auxiliary VM + 16 KiB locked cache**;
`include/gxruntime/aram.h:16` and `src/aram.c:14` define/allocate **16 MiB ARAM**.
Retaining all four backings requires **83,902,464 bytes (80 MiB + 16 KiB)**, before code, REL aliases,
host state, audio, native stacks, caches or graphics. This is a source-derived backing subtotal,
not a boot allocation observation or an upper bound. Guest heap and guest stacks inside MEM1 are not
extra host allocations. RAM expansion/fixed-MEM1 initialization can also overlap the original 24 MiB
allocation with 32 MiB storage; transient behavior and allocation order need measurement.

Pinned desktop Aurora's `graphics/aurora/lib/gfx/common.hpp:178-185` requests 24 MiB uniforms,
24 MiB vertices, 8 MiB indices, 8 MiB storage and 24 MiB texture upload per staging buffer.
`common.cpp:62-65,1166-1177` creates **five 88 MiB staging buffers = 440 MiB**, plus four shared
device buffers totaling **64 MiB**, before textures, render targets and driver memory.
These are API allocation requests in a desktop backend, not residency or Vita pool measurements.
Transplanting this allocation policy is not a credible Vita budget; it does not establish that a
different bounded Vita renderer is impossible. Aurora was disabled throughout this experiment.

### Boot / scene / transition acceptance gaps

| Phase | Allocations / backing | Touched / resident | Transient peak | Available Vita pools |
| --- | --- | --- | --- | --- |
| Full-game boot | UNKNOWN; source subtotal above only | UNKNOWN | UNKNOWN, including RAM/module loading overlap | UNKNOWN |
| Representative scene, e.g. Outset | UNKNOWN for code/REL/cache/audio/GPU totals | UNKNOWN | UNKNOWN | UNKNOWN |
| Scene transition, old + new resources/uploads | UNKNOWN | UNKNOWN | UNKNOWN; no lifetime trace | UNKNOWN |

There is no working built full-game host in the integration checkout available for these measurements.
Creating an ARM Linux application port to obtain them is excluded. No game or save was opened.
These are blockers, not rows filled with the fixture numbers or measurements borrowed from another game.

### Conditional pool plan and safety margin

This is a proposed accounting envelope, **not an allocation commitment**. First measure separately
the application user pool `U`, graphics/CDRAM pool `G` and physical-contiguous pool `P` under the
actual launch configuration. The nominal 512 MiB is never substituted for `U`. The fixture records
`sceKernelGetFreeMemorySize` at entry/exit; these are snapshots after startup and the heap request,
not allocator high-water or proof that all pools are interchangeable.

- **User pool:** retain the 80 MiB + 16 KiB source backings until a separately validated design shows
  they can be reduced/shared. Add full-game executable/REL backing, CPU/dispatch/dirty metadata,
  all native thread reservations, audio/DSP/output buffers, CPU resource caches and bounded staging.
  Preserve shared alias ownership. Never assume unused `calloc` pages are free budget on Vita.
- **Graphics pool:** explicitly account for display/EFB/depth surfaces, textures, shaders and upload
  resources with their real alignment/placement. As arithmetic only, two 1,024-stride × 544 RGBA8
  color surfaces and one equally sized 4-byte depth surface would take **6,684,672 bytes (6.375 MiB)**;
  this excludes game render targets, textures, driver costs and uploads and is not a renderer proposal.
- **Contiguous pool:** reserve only for allocations demonstrated to need it. UNKNOWN capacity and
  largest satisfiable block must not be converted into spare user/GPU memory.
- **Margin:** initially leave **25% of each measured usable pool unassigned**, after separately
  accounting for measured boot/transition overlap. This conservative policy reflects currently unknown
  resource lifetimes, allocator rounding and fragmentation; it is not evidence that 25% suffices.
  Require `peak_user <= 0.75 * U`, `peak_graphics <= 0.75 * G` and, if used, `peak_contiguous <= 0.75 * P`,
  plus demonstrated allocation success and native-stack headroom. Revisit with real repeated transitions.

No numeric remaining allowance for textures/staging/audio/code can be defensibly computed yet.
A GO budget must balance all categories and worst simultaneous lifetimes in their actual pools, not
subtract the 32 MiB VM from nominal RAM or use compressed module size as resident code.

## Verification, feedback loops and next decision

Observed red/green loops: CPU-include seam, function splitting and loop-helper ownership; each of the
five named C contract scenarios failed before implementation and passed afterward. The first corpus
build failed on the three absent inline helpers, then passed after the single pinned-wrapper correction.
Final review reproduced `.su` undercount (**8 instead of 64 bytes**) in a parameterized regression,
then retained the maximum in both record orders. Its exact red/green logs are
`build-vita-ww-01/logs/stack-evidence-undercount-red.log` and `stack-evidence-green.log`.
This last change corrects evidence aggregation, not CPU/runtime behavior.

Local checks before delivery passed:

- Fixture Python tests: **5/5**; corpus CTest: **1/1**; contract CTest: **5/5**;
  pinned standalone GXRuntime CTest: **3/3**. Both complete host fixtures also executed directly.
- Both Vita ELF/SELF/VPK builds; ELF ABI, section, symbol, assembly and compiler stack inspection.
- `bash scripts/audit_repo.sh` and all **53** supplied source/synthetic baseline tests, running each
  file directly: `tests/test_{composite,direct_calls,inline_gpr,guest_checkpoints,frame_tail,
  signing_entitlements,sample_owners,one_game_guard}.py`. `test_composite.py` has its own 18-case
  driver and must not be silently skipped by unittest discovery.

The same campaign, audit and eight baseline files are rerun from the delivery commit during validation.
Command/output logs, `fixture.map`, `compile_commands.json`, `.su` files, `readelf`, `nm` and `objdump`
output remain private under `build-vita-ww-01/`. This report summarizes the bounded evidence so it does
not depend on committing generated material. Windows CI, full-game builds/play, Vita3K, console tests,
performance and full-game memory measurements are **not claimed**.

Alternatives considered:

| Alternative | Decision |
| --- | --- |
| Synthetic CPU or link DolRecomp `dr_cpu` | Reject: would not validate BlueWake's production CPU ABI/semantics |
| Rewrite dispatcher or strip terminal-call guarantees | Reject: existing production guards/fallback suffice for this fixture |
| Port the full desktop host/Aurora to ARM Linux or Vita in this ticket | Reject: excluded scope and unjustified allocation policy |
| Declare STOP from absent hardware or desktop staging sizes | Reject: neither proves Vita infeasibility |
| Keep the real CPU, produce game-free diagnostics, record REVISE | Selected: small reversible evidence, explicit missing gates |

Next authorized investigation should first arrange hardware access and upstream ARMv7 FPSCR tests.
Once separately approved, a tester can run both local game-free diagnostics on Vita3K and a physical
Vita without clock changes, identifying device, firmware, toolchain, source/artifact hashes and launch
configuration. Expected logs are `ux0:data/bluewake-ww-fixture/corpus.log` and `contract.log`;
require all PASS lines, ABI values, stable sustained-depth span and free-pool snapshots. Missing logs,
allocation failure or any semantic mismatch fail that gate. No console upload/install was done here.

Then measure a supported private full-game boot, a fixed representative scene and repeated transition
with the same revision/settings, recording allocated versus touched memory, largest allocation,
native-stack high-water, per-pool peaks, audio/cache sizes and old/new resource overlap. Only correctness
on actual ARMv7 plus that balanced budget can justify reconsidering GO. Normal-speed/30 FPS validation
is a later gameplay/performance gate, not a conclusion of this experiment.
