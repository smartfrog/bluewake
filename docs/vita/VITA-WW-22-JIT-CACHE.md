# VITA-WW-22 — Persisting the Dynarmic JIT cache between Vita3K runs

2026-10-13 · investigation only — **no run and no build were started**, and `ref/` and
`build-f5/` were read only. Worktree `/tmp/opencode/ww22`, branch `vita-ww-22-jitcache`,
based on `8e1535c` (trunk).

**Headline.** The pinned emulator cannot do this today, and there is no switch to turn
on: **Dynarmic has no persisted or serializable code cache, and Vita3K never writes
compiled blocks anywhere.** The A32 JIT's code cache is an anonymous, process-private,
read–write–execute `mmap` that dies with the process and is indexed by in-memory maps.
The public A32 JIT interface (`interface/A32/a32.h:20–106`) has exactly two cache
operations, `ClearCache()` and `InvalidateCacheRange()` (`:37–48`); it has **no save,
load or export** operation. The only "cache"
options in Vita3K are the *texture* and *shader* caches
(`vita3k/config/include/config/config.h:154,197`). Making the cache reusable is a
Dynarmic-fork project (a save/restore format for emitted code **plus** its relocations,
block metadata and invalidation bookkeeping) or a process-level checkpoint — not a
setting. No warm-restart route is validated or authorized here. §5 gives WW20's ideal
arithmetic only (≈ 7 min in place of 39–41 min if all compilation became free);
**no wall gain is established**: 82.25 % is the guest thread's early-boot user-sample
share, and it was measured on the early boot only.

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
| The complete public A32 JIT interface is `external/dynarmic/src/dynarmic/interface/A32/a32.h:20–106`; within it, the only cache operations are `ClearCache()` and `InvalidateCacheRange()` (`:37–48`). That interface contains **no save, load or export** operation for compiled code. | `external/dynarmic/src/dynarmic/interface/A32/a32.h:20–106`, cache operations at `:37–48` |
| `UserConfig` has no persistence option. The one cache-related knob is `code_cache_size = 128 * 1024 * 1024` bytes by default (max ~128 MiB on arm64 hosts). | `interface/A32/config.h:122–239` (line 239) |
| Vita3K never overrides that size and never constructs a cache file: no `code_cache_size` appears anywhere in `vita3k/`. | `grep -rn code_cache_size vita3k/` → none |
| The code cache is `mmap(nullptr, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON|MAP_PRIVATE)` and `munmap`-ed on destruction. Nothing file-backed, nothing named, nothing persisted. | `externals/oaknut/include/oaknut/code_block.hpp:44,59` |
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
   `MOV Xpagetable, <host page-table address>` and `MOV Xfastmem, <host guest-RAM base>`,
   and its callback/exclusive/`conf` trampolines embed `this` and function pointers
   (`code.dx(info.this_ptr)`, `code.dx(info.fn_ptr)`, `code.dx(&conf)`;
   `backend/arm64/a32_address_space.cpp:23–153,223–333`). These come from the current
   process image (ASLR, heap, the guest RAM `mmap`, which is only *hinted* at
   `1ULL << 34` and can land elsewhere — `vita3k/mem/src/mem.cpp:76–96`).
2. **Block links patch absolute targets.** When a block is emitted,
   `LinkBlockLinks` writes a direct `B`/`ADRL` to the target block's host address
   (`backend/arm64/address_space.cpp:134–272,274–300`), and
   `RelinkForDescriptor` re-patches every referring block when a target is invalidated
   (`:303–330`). The relocation lists that make this possible are in `block_infos`, in
   memory only.
3. **Invalidation must be replayed faithfully.** The load path rewrites guest code and
   invalidates exactly the affected ranges: when a module's function exports are loaded,
   the import stubs that bind to them are rewritten and each 3-word stub is invalidated
   in `load_func_exports` (`vita3k/kernel/src/load_self.cpp:254–289`, invalidate at
   `:289`); taiHEN hooks, `SceSysmem`/`SceCpuForDriver` and the debugger do the same for
   their patches. The unload paths (`:308–337`, `:819–848`) also invalidate; this
   investigation found no evidence of module unloading in the boot, so no boot-time
   unload claim is made here. The conclusion is only one of **consistency**: a saved
   cache loaded into a fresh run would have to keep the guest-range → block index
   (`block_ranges`) so that the same code mutations retire the same blocks, not just
   the code bytes.
4. **Runtime state is part of the cache**: the fastmem do-not-fastmem set
   (`fastmem.h:53`) and the exception handler's registered code range + patch callbacks
   (`backend/arm64/address_space.cpp:24–35`) are created at JIT construction and updated
   as faults happen.
5. **Per-thread granularity**: every guest thread owns a JIT; a persisted cache would be
   N independent (code + metadata) images, with no cross-thread sharing.

Data volume is bounded — each code area is at most 128 MiB by configuration, against a
measured process RSS plateau of 3.78–4.12 GiB (`VITA-WW-21-CI-PROJECTION.md:172–176`).
The blocker is structural: the pinned code has no relocation model, no cache key or
validation format, and no load path.

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

That is a new subsystem, not a configuration entry: nothing in the pinned Vita3K or
Dynarmic sources provides a cache format, a validation step or a load path.

## 5. Arithmetic ceiling if a correct warm cache existed (not a measurement)

Measured shares ([VITA-WW-20-PROFILE](VITA-WW-20-PROFILE.md):62–70): on the main guest
thread, **3,424 of 4,163 user-PC samples (82.25 %)** are Dynarmic native
compilation/initialization and **710 (17.06 %)** generated guest code. These are
sampled user-CPU shares of that one thread, not wall-time shares. WW20 itself labels
its ideal-deletion factors **5.63×** (that thread) and **1.51×** (whole process,
33.93 % share) as idealized ceilings, not measured improvements.

The table below is the same arithmetic: it multiplies the WW21 cold-cadence projection
(39.1 min for the 404 frames from command 102936 to the title logo at 5.81 s/frame;
48.8 min to PRESS START) by (1 − 0.8225) as if every compilation sample on the guest
thread became free. No cache exists, no warm run was made, and no wall behavior is
modelled.

| Boot stretch (cold, WW21) | Cold | Warm-cache arithmetic (×(1 − 0.8225)) |
| --- | ---: | ---: |
| frame 101 → title logo fully visible | 39–41 min | **≈ 6.8–7.4 min** |
| frame 101 → PRESS START | 49–51 min | **≈ 8.6–9.0 min** |
| full boot (615 s to frame 101 + stretch) → title logo | ≈ 49–51 min | **≈ 8.8–9.1 min** |

At the slower sibling cadence (9 s/frame) the same stretch is 60.6 min cold, so the
same arithmetic gives ≈ 10.8 min. On the whole-process denominator the ceiling would be
only 1.51× (≈ 26 min arithmetic); that denominator mixes threads that run in parallel
during a run, so it is not a wall bound either. The host has **10 CPUs in total**, and
the llvmpipe presentation workers run on that same host during these runs, so spare
capacity for other work is not established.

Four limits on this arithmetic, all important:

1. **The 82.25 % share is an early-boot measurement**: the WW20 profile covers the
   first ≈ 78.8M cycles (the SELF load + first REL window, ≈ 10 s of guest wall), not
   the 39–41 min stretch. WW20 names a warmed mid/late-boot profile as the next gate.
   The steady 5.81 s/frame cadence with new code every frame, and the one warm burst at
   0.192 s/frame, are consistent with compilation being on the critical path, but they
   do not measure the compile share over the whole boot.
2. **The cache only covers code a previous run already translated.** The first boot to
   the title is unavoidably cold; persistence would mean one cold boot produces the
   cache and later re-runs to the same point avoid re-translating what it covers —
   never measured. A cache saved by the current CI extent (frame 101) does not contain
   the title path at all.
3. **Removing compilation does not remove the frames.** The guest still executes the
   same 404 frames (+ the Dolby/logo logic); only their compilation cost is removed in
   the arithmetic.
4. **Replacement costs are non-zero** (dispatcher misses, unlinked branches, relinks),
   so the table is an ideal arithmetic ceiling, not a forecast.

## 6. Alternatives: availability, evidence and authorization

Nothing in this table has been executed or authorized in this lane. **CRIU, `fork()`,
pre-translation and compile-cost work (A–D) are not validated and not authorized inside
WW22**; any execution or code modification needs a separate ticket with its own scope
and evidence gates. F is technically available but also **not authorized by WW22**: the
60–75 min run needs the owner's explicit opening (WW21:181–183).

| Alternative | Available today | Evidence in this lane | Authorization |
| --- | --- | --- | --- |
| A. Process checkpoint/restore (e.g. CRIU `dump`/`restore`) | Tool present on this host (`/usr/bin/criu`, `/usr/sbin/criu`); permissions, stack compatibility (external sockets, multi-threaded process) and the checkpoint workflow are unvalidated | None — no checkpoint was taken | **No** — separate ticket |
| B. `fork()` warm-start ("zygote") hook | No such design in the emulator; a `fork()` keeps only the calling thread, so the kernel/renderer/guest threads would need a deliberate re-creation design | None | **No** — separate ticket |
| C. Background/pre-translation of blocks | Not present in the pinned Vita3K/Dynarmic; would modify the emulator or the runtime | None; the 82.25 % share is early-boot user samples of one thread | **No** — separate ticket (runtime change) |
| D. Lower per-block compile cost (assertions/IR verification) | No release switch exists; source change only | WW20's small ceilings (1.06× combined, itself idealized) | **No** — separate ticket |
| E. Native ARMv7 on a physical Vita | Hardware availability not established ([VITA-WW-01](VITA-WW-01.md)) | None; no physical-Vita result exists | **No** — out of this lane |
| F. Longer capture window (≥ 60–75 min, ≥ 8 GiB free) | Technically possible, no code change (WW21 §7) | WW21 projection plus the measured RAM stops; RSS plateau 3.78–4.12 GiB against the 2.5 GiB floor | **No** — requires the owner's explicit opening for a 60–75 min run (WW21:181–183) |

Notes on the table:

- A checkpoint/restore (A) resumes the process at the checkpoint state — JIT code,
  guest RAM, threads and kernel/service state together. It is not a fresh boot served
  from a cache file, and the result would be a warm resumption, not a validated
  boot-time comparison.
- F needs no new code, but it is **RAM-bound, not a gain** and **not authorized by
  WW22**: the measured RSS plateau is 3.78–4.12 GiB, the post-CI attempts stopped on
  host RAM at 70 s, 158 s and 263 s, and a 60–75 min run needs the owner's explicit
  opening (WW21:172–176,181–183).
- The llvmpipe presentation work runs on the same 10-CPU host as the emulator during
  these runs; no idle-capacity budget is assumed here.

## 7. Recommendation

1. **Do not pursue "enable the JIT cache"**: it does not exist in the pinned Dynarmic
   or Vita3K, and no supported flag, file or environment variable can create it.
2. No warm-restart route is validated or authorized by this lane. If a warm process is
   wanted for iterating on the title capture, the CRIU experiment (A) would need its
   **own ticket** — this document does not authorize that work and promises no result.
3. A permanent boot-time reduction would similarly need a **separate runtime ticket**
   (C, background/pre-translation), gated on WW20's stated next measurement (a warmed
   mid/late-boot profile with guest-PC attribution). No speed-up is claimed here.
4. Until such work is approved and validated, the ≥ 60–75 min window with ≥ 8 GiB free
   (F) remains the only no-code route, but it is RAM-blocked and needs the owner's
   explicit approval (WW21:181–183); WW22 does not authorize it.

## 8. What was not done / limits

No emulator run, no build, no install, no config change, no push. `ref/` and `build-f5/`
were read only; no evidence payload was copied. The 39–41 min and PRESS START numbers
are the WW21 projections, not new measurements; the compile share is WW20's early-boot
profile. No physical-Vita measurement exists. The §5 table assumes ideal deletion of
compilation with zero replacement cost and is labelled arithmetic throughout; **no wall
gain is established by this document.**

## 9. Evidence pointers

- Dynarmic `interface/A32/a32.h:20–106` (cache operations at `:37–48`),
  `interface/A32/config.h:122–239`,
  `backend/arm64/address_space.h:78–81`, `backend/arm64/address_space.cpp:38–121,134–330`,
  `backend/arm64/a32_address_space.cpp:23–153,164–186,223–333`,
  `backend/arm64/fastmem.h:34–53`, `backend/block_range_information.h:19–23`,
  `externals/oaknut/include/oaknut/code_block.hpp:26–95`.
- Vita3K `vita3k/cpu/src/dynarmic_cpu.cpp:335–353,519`,
  `vita3k/cpu/src/cpu.cpp:41–52,167–168`, `vita3k/kernel/src/thread.cpp:74`,
  `vita3k/kernel/src/load_self.cpp:254–289` (export load/invalidate),
  `:308–337` and `:819–848` (unload counterparts),
  `vita3k/mem/src/mem.cpp:65–127,76–96`,
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
