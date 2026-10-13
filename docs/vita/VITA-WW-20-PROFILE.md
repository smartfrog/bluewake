# VITA-WW-20 — real-boot CPU profile

2026-10-13. **This cold boot is predominantly compiling ARMv7 code, not running
it:** Dynarmic compilation/initialization accounts for **82.25% of the main
guest thread's user-PC samples**, generated JIT code for **17.06%**. The full
process also spends **45.72% of sampled CPU on software presentation**, and
the measured guest wall contains a **3.43 s post-user-sampling tail** around
the known crash/reaping. These are not steady-state game-FPS costs.

## Run and method

- Private root: `/home/fred/projects/zeldaWWHDRecomp/build-f6/ww20`;
  `runs/profile-r1`, run `500f48e76e044cf388b47f8f2d395c8c`.
- Clean launcher `af00b865c230fa3016647de0a08890c22c6b0dad`; preserved O1 guest
  `9fe81ed4c99ac182df2b6f23cee199f57f04fa07`, host O2, same ancestor-build gate
  and VPK as [the backend A/B](VITA-WW-20-CPU-BACKEND.md). No rebuild or pin change.
- Vita3K SHA-256 `cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30`;
  emitted config `381632427de4ad4730e5a28a206e8f74d41f03c811b330994b35e14c560bed63`,
  `cpu-opt=true`, Info, `--continue-gx 64`. Same private disc read-only bind.
- **78,789,721 cycles / 9.996463 s = 7,881,760 cycles/s**. Report after its
  request header is identical to the A/B reports: PC `80240744`, REL-provider
  stop, 64 validated commands, `rendered=0`. Exit 133 is preserved.
- Linux/Asahi M1 Pro, 10 CPUs; other lanes' compiler workers were active.
  Watched CPU/log/guest growth throughout; the unchanged 30 s silence-and-low-CPU
  abort stayed armed. No timeout/idle abort, writes to `build-f5`, network or push.

`perf` and `gdb` binaries are absent, but `perf_event_paranoid=2`,
`kptr_restrict=0`, `ptrace_scope=0`; unprivileged `perf_event_open` probes for
CPU-clock, task-clock and hardware cycles succeed. Instead of installing tools
or changing privileges, a private native recorder uses the **same kernel perf
API**: per-thread `PERF_COUNT_SW_CPU_CLOCK`, 1 ms period (nominal 1,000 Hz),
user IP/TID/time/period and user frame-pointer callchain; kernel/hypervisor
excluded. It attaches only to the guest's owned Vita3K descendant, enumerates
threads every 10 ms, snapshots changed maps every 20 ms, drains rings every
2 ms, and stops/reaps with that phase. No ptrace pauses or kernel-stack fallback.

**10,462 samples, 46 opened threads, zero lost records.** A separate busy-loop
calibration collected 2,413 samples, all resolving to `busy_calibration`.
Offline ELF load-segment and function-size lookup uses the exact staged binary
and local libraries; `addr2line` independently confirms four hot symbols.
Anonymous 128 MiB pages plus `DynarmicCPU::run` callchains identify the CPU JIT
cache; small anonymous executable pages on llvmpipe threads are graphics JIT,
**not guest execution**. No ARMv7/PowerPC function attribution is invented.

## Cost hierarchy

Whole-process denominator: all **10,462** user-PC samples, weighted by their
identical 1 ms period. Callchains assign each sample to **one** bucket; the
following rows are disjoint, not overlapping inclusive stacks.

| Cost | Samples | Whole-process share |
|---|---:|---:|
| llvmpipe software-presentation workers | 4,216 | 40.30% |
| Dynarmic native compilation/initialization, including its allocator/IR work | 3,550 | 33.93% |
| SELF loading, decompression and relocation | 877 | 8.38% |
| Generated Dynarmic guest/dispatcher/prelude code, unresolved | 711 | 6.80% |
| Presentation renderer thread (swap/copy/LLVM work) | 567 | 5.42% |
| Qt/UI startup and handling | 452 | 4.32% |
| Other native / unresolved function | 71 | 0.68% |
| HLE/imports outside compilation | 18 | 0.17% |

The main `ww-fixture` thread, TID **1193538**, contributes **4,163 samples
(39.79% of process samples)**. Its distinct, more useful execution denominator:

| Main guest thread | Samples | Thread share |
|---|---:|---:|
| Dynarmic native compilation/initialization | 3,424 | **82.25%** |
| Generated guest/dispatcher/prelude code | 710 | **17.06%** |
| HLE/imports outside compilation | 15 | 0.36% |
| Other native / unresolved function | 13 | 0.31% |
| SELF loading | 1 | 0.02% |

Selected **self** hot symbols, not inclusive totals:

| Symbol | Process share | Main guest-thread share |
|---|---:|---:|
| `tinfl_decompress` → `mz_uncompress` → `load_self` | 7.94% | — (loader/main UI thread) |
| `Dynarmic::Backend::Arm64::EmitArm64` | 3.64% | 9.15% |
| `RegAlloc::UpdateAllUses` | 2.84% | 7.11% |
| `RegAlloc::ValueLocation` | 1.43% | 3.60% |
| `RegAlloc::AssertAllUnlocked` | 1.36% | 3.39% |
| `RegAlloc::ValueInfo` | 1.24% | 3.07% |
| `Optimization::VerificationPass` | 0.53% | 1.30% |

Inclusive guest-thread stacks confirm the interpretation: `AddressSpace::GetOrEmit`
**76.22%**, `AddressSpace::Emit` **58.54%**, `EmitArm64` **36.25%**, `GenerateIR`
**16.60%**. These overlap and **must not be added**. The sampled register-allocation
family alone is **23.64% self** of the guest thread; the two assertion scans plus
IR verification are **5.96% self**, partly overlapping that family.

No native `spdlog`/`logging` sample appears. Native HLE/journal-writing wrappers
are not a dominant user-CPU bucket, but that is **not proof that journal cost is
zero**: ARMv7 formatting/seams share the unresolved generated-code bucket, and
kernel I/O is excluded. `/proc` records about **4.10 user + 0.52 system CPU s**
for the fixture thread; the system component is **11.3%** of its counted CPU,
not attributed to journal writes by this profile. Directly quantifying journal
formatting versus PPC execution requires guest-PC/JIT-block attribution.

## Three priorities and their theoretical ceilings

The residual 14.9–19.9× hypothesis requires removing roughly **93.3–95.0% of
serial time**. CPU shares from parallel threads are not serial wall shares.
For each stated denominator, the idealized deletion ceiling is `1 / (1 - f)`;
it assumes zero replacement cost and is **not a measured improvement**.

1. **Reduce or amortize cold Dynarmic compilation first.** Caching/warming the
   actual boot blocks, a faster native compiler path, or an equivalent native-host
   control targets **82.25% of guest-thread user samples**: ideal **5.63× on that
   thread's sampled CPU**, only **1.51× aggregate sampled process CPU** (33.93%).
   This cannot by itself establish the requested 15–20× wall gain. Within it,
   register allocation's 23.64% gives only **1.31×** if entirely eliminated;
   checks/verification's 5.96% gives **1.06×**, not a giant assertions-flag win.
   The source corroborates per-IR scans (`backend/arm64/emit_arm64.cpp:220–255`,
   `reg_alloc.cpp:216–243`) and unconditional IR verification
   (`a32_address_space.cpp:164–179`), at Dynarmic `86458a0b…`. No checks disabled.
2. **Fix teardown and define an execution-only measurement endpoint.** Last user
   sample to profiler stop is **3.427664 s**, **34.29% of guest wall**; the main
   process is subsequently observed as a zombie. The journal records the known
   crash; `core_pattern` pipes to systemd-coredump. That daemon was not profiled,
   so the entire tail cannot be attributed to it. Deleting the whole tail would
   improve this *boot metric* at most **1.52×**, to **11.995M cycles/s**, still
   **20.26×** below 243M. It provides **no demonstrated gameplay FPS gain**;
   retain the WW12b full-wall number and label any new endpoint separately.
3. **Remove unnecessary software presentation from CPU diagnostics.** llvmpipe
   workers plus the renderer consume **45.72% of process samples**: ideal
   **1.84× aggregate sampled CPU**. A verified opt-in headless/no-present path or
   hardware rendering could free those resources. Their work overlaps the guest
   on other cores, so **no wall/FPS speedup follows directly**. Keep sandbox/device
   isolation intact. SELF loading (8.38%, CPU ceiling 1.09×) is the next cold-boot
   target, not a steady-state game bottleneck.

**Do not multiply these ceilings or extrapolate them to a physical Vita.** The
large cold-JIT, loader and post-crash costs explain why this boot's cycles/s
cannot certify a 30 FPS steady-state gap. Next gate: profile a validated warmed
execution interval/scene, excluding startup and teardown *explicitly*, with
guest-PC attribution before choosing module/seam optimizations. Physical Vita
executes ARMv7 natively; this profile measures an ARM64 emulator on Linux.

## Evidence and checks

Private evidence remains under the root above: per-run
`phases/guest/evidence/profile/{samples.jsonl,maps.txt,threads.txt,attach.json}`;
`runs/profile-r1/{profile-report.json,profile-costs.json,profile-resolved.jsonl}`;
`profiles/calibration-1/validation.json`; `evidence/profile-identity.json` holds
method and SHA-256 identities for inputs, recorder, resolver and evidence.
Replay: `WW05_BUILD_ROOT=<root> python3 -B <root>/tools/watch_profile.py <new-label>`
with the unchanged backend report's launch arguments and ancestor receipt;
then run `tools/report_profile.py <label>` and `tools/profile_costs.py <label>`.

Checks: strict native recorder build, calibrated symbol resolution, owned-PID/
sample-count/zero-loss checks, disjoint percentages, identical guest report and
config, **12 activity tests passed**, repository audit/whitespace and attribution
checks passed. Documentation only is committed; no game/profile payload or
runtime change. No Windows CI, physical Vita or rendered FPS validation.
