# VITA-WW-22 — Persisting the Dynarmic JIT cache between Vita3K runs

2026-10-13 · investigation only — **no run and no build were started**, and `ref/` and
`build-f5/` were read only. Worktree `/tmp/opencode/ww22`, branch `vita-ww-22-jitcache`,
based on `8e1535c` (trunk).

**Headline.** The pinned emulator cannot do this today, and there is no switch to turn
on: **Dynarmic has no persisted or serializable code cache, and Vita3K never writes
compiled blocks anywhere.** The A32 JIT's code cache is an anonymous, process-private,
read–write–execute `mmap` that dies with the process and is indexed by in-memory maps;
the only public cache operations are `ClearCache()` and `InvalidateCacheRange()`
(`Dynarmic` `interface/A32/a32.h:20–106`). The only "cache" options in Vita3K are the
*texture* and *shader* caches (`vita3k/config/include/config/config.h:154,197`). Making
the cache reusable is a Dynarmic-fork project (a save/restore format for emitted code
**plus** its relocations, block metadata and invalidation bookkeeping) or a
process-level checkpoint — not a setting. If such a cache existed and were correct,
the ideal ceiling on the compile-dominated path is **39–41 min → ≈ 7 min** (guest
thread, WW20/WW21 numbers), but the 82.25 % compile share was measured on the **early
boot only**, so that number is an extrapolation, not a measured warm result.

## 1. Sources inspected

| What | Revision / identity | Where (read-only) |
| --- | --- | --- |
| Vita3K source at the binary's banner revision | `bbd5c3624a06572fe4f16f67564f53a7540e1f42` (`Vita3K v0.2.1 1-bbd5c36`) | `/home/fred/projects/xgr-tools/vita3k-src` (checkout HEAD `a32b6aed…`; the CPU files are byte-identical to the banner revision per [VITA-WW-20](VITA-WW-20-CPU-BACKEND.md)) |
| Dynarmic submodule (the A32 JIT) | `86458a0bd369d63ba4c2ef812cacbb6c9080c065` | `external/dynarmic` in that tree, and `/home/fred/projects/vita3k/external/dynarmic` (same revision, identical files) |
| Running binary | SHA-256 `cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30` | `build-vita-tools/local-stack/vita3k/Vita3K` |
| Emitted run configuration used by the CI runs | `cpu-opt: true`, `cpu-pool-size: 10`, `backend-renderer: OpenGL` | one `build-f5/ww05/vita3k-run-*/phases/guest/evidence/config-emitted.yml`, read-only |

`ref/vita-ww-01/` in this repository holds the DolRecomp and RecompCore pins only; it
contains no Vita3K or Dynarmic source. The pinned emulator sources live in the two
locations above, as recorded by [VITA-WW-20](VITA-WW-20-CPU-BACKEND.md).

## 2. Answer: in-memory only, and nothing to enable

| Claim | Source |
| --- | --- |
| The public A32 JIT API is only `Run`, `Step`, `ClearCache`, `InvalidateCacheRange`, `Reset`, `HaltExecution`, register/CPSR/FPSCR access, `ClearExclusiveState`, `IsExecuting`, `DumpDisassembly`, `Disassemble`. There is **no save/load/serialize** for compiled code. | `external/dynarmic/src/dynarmic/interface/A32/a32.h:20–106` |
| `UserConfig` has no persistence option. The one cache-related knob is `code_cache_size = 128 * 1024 * 1024` bytes by default (max ~128 MiB on arm64 hosts). | `interface/A32/config.h:122–239` (line 239) |
| Vita3K never overrides that size and never constructs a cache file: no `code_cache_size` appears anywhere in `vita3k/`. | `grep -rn code_cache_size vita3k/` → none |
| The code cache is `mmap(nullptr, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON|MAP_PRIVATE)` and `munmap`-ed on destruction. Nothing file-backed, nothing named, nothing persisted. | `external/oaknut/include/oaknut/code_block.hpp:44,59` |
| The block index, reverse index, per-block relocation info and reference lists live in in-memory maps that are destroyed with the JIT. | `backend/arm64/address_space.h:78–81` |
| Fastmem per-block "do not fastmem" markers are another in-memory set. | `backend/arm64/fastmem.h:34–53` |
| Cache invalidation bookkeeping (guest ranges → block descriptors) exists only in memory. | `backend/block_range_information.h:19–23`; `backend/arm64/a32_address_space.cpp:184–186` |
| The JIT even discards the whole cache when less than 1 MiB of code space remains — the cache is not a stable, append-only artifact even inside one run. | `backend/arm64/address_space.cpp:106–112` |
| Vita3K only ever *invalidates* the cache: `invalidate_jit_cache` is the single cache call in the emulator and the CPU interface. | `vita3k/cpu/src/cpu.cpp:167–168`, `cpu/impl/dynarmic_cpu.h:95`, `cpu/src/dynarmic_cpu.cpp:519` |
| Each guest thread that runs gets its own `DynarmicCPU` (and therefore its own JIT and its own 128 MiB code area); there is no sharing between threads. | `vita3k/kernel/src/thread.cpp:74`, `vita3k/cpu/src/cpu.cpp:41–52` |
| The profile independently saw exactly these pages: "anonymous 128 MiB pages plus `DynarmicCPU::run` callchains identify the CPU JIT cache". | [VITA-WW-20-PROFILE](VITA-WW-20-PROFILE.md):40–43 |

## 3. Why a cache file is not a small patch: what would have to be serialized

The emitted ARM64 code is not a self-contained, relocatable object. It is position- and
runtime-dependent by design:

1. **Host pointers are embedded as immediates.** The per-JIT prelude is emitted with
   `MOV Xpagetable, <host page-table address>` and `MOV Xfastmem, <host guest-RAM base>`
   (`backend/arm64/a32_address_space.cpp:233–236`), and its callback/exclusive/`conf`
   trampolines embed `this` and function pointers (`code.dx(info.this_ptr)`,
   `code.dx(info.fn_ptr)`, `code.dx(&conf)`, same file, 188–300). These come from the
   current process image (ASLR, heap, the guest RAM `mmap`, which is only *hinted* at
   `1ULL << 34` and can land elsewhere — `vita3k/mem/src/mem.cpp:76–96`).
2. **Block links patch absolute targets.** When a block is emitted,
   `LinkBlockLinks` writes a direct `B`/`ADRL` to the target block's host address
   (`backend/arm64/address_space.cpp:134–272,274–300`), and
   `RelinkForDescriptor` re-patches every referring block when a target is invalidated
   (`:303–330`). The relocation lists that make this possible are in `block_infos`, in
   memory only.
3. **Invalidation must be replayed faithfully.** The boot invalidates the cache at load
   and link time — SELF segment mapping (`vita3k/kernel/src/load_self.cpp:847`),
   export stubs (`:289,:337`), and later taiHEN hooks, `SceSysmem`/`SceCpuForDriver`
   and the debugger. A saved cache loaded into a fresh run would need the same
   invalidations, which requires persisting `block_ranges` (guest range → descriptors),
   not just the code bytes.
4. **Runtime state is part of the cache**: the fastmem do-not-fastmem set
   (`fastmem.h:53`) and the exception handler's registered code range + patch callbacks
   (`backend/arm64/address_space.cpp:24–35`) are created at JIT construction and updated
   as faults happen.
5. **Per-thread granularity**: every guest thread owns a JIT; a persisted cache would be
   N independent (code + metadata) images, with no cross-thread sharing.

The data volume would not be the problem — the code areas are at most 128 MiB each and
are mostly empty early in a boot, while the process RSS plateau is 3.78–4.12 GiB
(`VITA-WW-21-CI-PROJECTION.md:172–176`). The problem is that there is no relocation
model, no key format, no validation scheme, and no load path anywhere in the pinned code.

## 4. What adding persistence would take (if it were funded)

For each guest-thread JIT, a fork would have to:

- dump the emitted code region, its prelude **re-emitted** against the new process's
  callbacks/page table/fastmem base, and every block's entry point, size, location
  descriptor and relocation lists;
- persist `block_entries` / `reverse_block_entries` / `block_infos` / `block_references`
  and `block_ranges`, and the fastmem marker set;
- re-establish block links on load (or persist and relocate them);
- validate guest-code identity and replay the loader's invalidations, and handle the
  cache-full `ClearCache` semantics;
- handle version/config hashing (dynarmic revision, `arch_version`, `cpu-opt`,
  `hook_hint_instructions`, page-table/fastmem mode) so a stale cache is rejected.

That is a new subsystem, not a configuration entry. The upstream Dynarmic design
document never mentions persistence; no shipped emulator known to this investigation
persists Dynarmic-compiled blocks. The projects that do persist recompiled code
(RPCS3's PPU/SPU caches) rely on a compiler that can emit relocatable objects and on
hash-verified cache entries — none of which Dynarmic's ARM64 backend produces today.

## 5. Estimated gain if a correct warm cache existed

Measured shares ([VITA-WW-20-PROFILE](VITA-WW-20-PROFILE.md):62–70): on the main guest
thread, **3,424 of 4,163 samples (82.25 %)** are Dynarmic native
compilation/initialization and **710 (17.06 %)** generated guest code. WW20's own
ideal-deletion ceilings are **5.63×** on that thread and **1.51×** on the whole process
(33.93 % share; parallel threads' shares are not wall shares).

Applying the guest-thread ceiling to the WW21 projection (39.1 min for the 404 frames
from command 102936 to the title logo at 5.81 s/frame; 48.8 min to PRESS START):

| Boot stretch (cold, WW21) | Cold | Warm-cache ideal (×(1 − 0.8225)) |
| --- | ---: | ---: |
| frame 101 → title logo fully visible | 39–41 min | **≈ 6.8–7.4 min** |
| frame 101 → PRESS START | 49–51 min | **≈ 8.6–9.0 min** |
| full boot (615 s to frame 101 + stretch) → title logo | ≈ 49–51 min | **≈ 8.8–9.1 min** |

At the slower sibling cadence (9 s/frame) the same stretch is 60.6 min cold, so the
warm-cache ideal there is ≈ 10.8 min; the working range is **≈ 7–11 min vs 39–61 min**.
On the whole-process denominator the ceiling would be only 1.51× (≈ 26 min), but that
mix contains the overlapping llvmpipe present workers, which do not pace the boot.

Four limits on this number, all important:

1. **The 82.25 % share is an early-boot measurement**: the WW20 profile covers the
   first ≈ 78.8M cycles (the SELF load + first REL window, ≈ 10 s of guest wall), not
   the 39–41 min stretch. WW20 names a warmed mid/late-boot profile as the next gate.
   The steady 5.81 s/frame cadence with new code every frame, and the one warm burst at
   0.192 s/frame (5.2 fps, ×30 faster), are consistent with compile-bound pacing, but
   they are not a measurement of the compile share over the whole boot.
2. **The cache only covers code a previous run already translated.** The first boot to
   the title is unavoidably cold; persistence converts "every boot costs 40–60 min" into
   "one cold boot produces the cache, later re-runs to the same point are fast". A cache
   saved by the current CI extent (frame 101) does not contain the title path at all.
3. **Removing compilation does not remove the frames.** The guest still executes the
   same 404 frames (+ the Dolby/logo logic); only their compilation cost disappears.
4. **Replacement costs are non-zero** (dispatcher misses, unlinked branches, relinks),
   so the table is an ideal ceiling, not a forecast.

## 6. Alternatives that do not require a Dynarmic fork

- **A. Whole-process checkpoint/restore.** The compiled code *is* process memory, so a
  checkpoint of the emulator process captures the warm JIT, guest RAM, kernel state and
  threads without any serialization format. On this stack that means CRIU
  (`criu dump`/`restore`) or an equivalent: same host/kernel required, external sockets
  (Wayland, SDL audio) and the private disc bind need explicit policies, and the
  multi-threaded process has to be dumped at a clean point (the runs end in the known
  exit-133 teardown). The software-rendered llvmpipe stack has no GPU driver state to
  save, which makes it easier than a Vulkan/hardware path. Cost: a multi-GiB checkpoint
  per snapshot; benefit: a re-run resumes warm, minutes instead of an hour.
- **B. `fork()` warm-start ("zygote") hook.** Warm the emulator up, then `fork()` per
  experiment. No checkpoint file and no serialization, but only the forking thread
  survives a `fork()`, so the kernel/renderer/guest threads need a deliberate
  re-creation design that the emulator does not have. More invasive than CRIU, less
  invasive than a cache format.
- **C. Move compilation off the critical path.** Today every block is translated
  inline on the guest thread (`AddressSpace::GetOrEmit` inside the run loop); the other
  10 host CPUs are mostly idle while one thread compiles. A background/pre-translation
  pool, or pre-translating the module ahead of first execution, removes the wait
  without needing cross-run stability, addresses, or invalidation persistence. This is
  the structural fix; it is a Dynarmic/Vita3K change, not a setting.
- **D. Shrink the per-block compile cost.** WW20's measured ceilings for the
  register-allocator assertion scans and the unconditional IR verification
  (`a32_address_space.cpp:164–179`) are small: removing all of them gives only 1.06×.
  Keep this as a minor independent win, not a solution.
- **E. Run the boot on native ARMv7.** On a physical Vita the module runs natively and
  this entire cost does not exist; hardware availability has not been established
  ([VITA-WW-01](VITA-WW-01.md)). Until then the emulator is the only way to see the
  title screen.
- **F. Accept the window.** WW21 §7 already names the pragmatic path: a 60–75 min
  capture window with ≈ 8 GiB of host RAM sustained (the measured RSS plateau is
  3.78–4.12 GiB and the machine's floor is 2.5 GiB).

## 7. Recommendation

1. **Do not pursue "enable the JIT cache"**. It does not exist in the pinned Dynarmic
   or Vita3K, and no supported flag, file or environment variable can create it.
2. If a warm restart is wanted purely to iterate on the title capture, pilot
   **A (CRIU checkpoint/restore)** first: it reuses the existing binary and launcher and
   needs no emulator change. Budget the checkpoint size against host disk and RAM.
3. For a permanent boot-time reduction, open **C (background/pre-translation)** with the
   runtime owners; it is the alternative that removes the measured 82.25 % critical-path
   cost without a serialization format. Fund it only after WW20's stated next gate — a
   warmed mid/late-boot profile with guest-PC attribution — confirms the compile share
   over the part of the boot that matters.
4. Until either lands, the ≥ 60–75 min window with ≥ 8 GiB free stands for the title
   capture (F).

## 8. What was not done / limits

No emulator run, no build, no install, no config change, no push. `ref/` and `build-f5/`
were read only; no evidence payload was copied. The 39–41 min and PRESS START numbers
are the WW21 projections, not new measurements; the compile share is WW20's early-boot
profile. No physical-Vita measurement exists. The estimate assumes ideal deletion of
compilation with zero replacement cost and is labelled as a ceiling throughout.

## 9. Evidence pointers

- Dynarmic `interface/A32/a32.h:20–106`, `interface/A32/config.h:122–239`,
  `backend/arm64/address_space.h:78–81`, `backend/arm64/address_space.cpp:38–121,134–330`,
  `backend/arm64/a32_address_space.cpp:164–186,188–367,369–421`,
  `backend/arm64/fastmem.h:34–53`, `backend/block_range_information.h:19–23`,
  `externals/oaknut/include/oaknut/code_block.hpp:26–95`.
- Vita3K `vita3k/cpu/src/dynarmic_cpu.cpp:335–353,519`,
  `vita3k/cpu/src/cpu.cpp:41–52,167–168`, `vita3k/kernel/src/thread.cpp:74`,
  `vita3k/kernel/src/load_self.cpp:289,337,847`, `vita3k/mem/src/mem.cpp:65–127,76–96`,
  `vita3k/app/src/app_init.cpp:564–565`,
  `vita3k/config/include/config/config.h:154–197`,
  `vita3k/renderer/src/vulkan/renderer.cpp:943–963` (the only place `mapping_method`,
  i.e. the page-table path, is selected; the OpenGL runs use the fastmem path).
- Prior lanes: [VITA-WW-20-PROFILE](VITA-WW-20-PROFILE.md) (82.25 % compile share,
  ceilings), [VITA-WW-20-CPU-BACKEND](VITA-WW-20-CPU-BACKEND.md) (Dynarmic is the only
  engine; `cpu-opt` already on), [VITA-WW-21-CI-PROJECTION](VITA-WW-21-CI-PROJECTION.md)
  (39–41 min to the title, window sizing, RAM floor).
- Emitted run config: `build-f5/ww05/vita3k-run-*/phases/guest/evidence/config-emitted.yml`
  (`backend-renderer: OpenGL`, `cpu-opt: true`, `cpu-pool-size: 10`).
