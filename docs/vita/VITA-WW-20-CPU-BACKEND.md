# VITA-WW-20 — Vita3K CPU backend

2026-10-13. **The ARMv7 guest already uses Dynarmic's ARM64 JIT with
optimizations enabled.** There is no interpreter/JIT or old/new backend switch
in this stack. Six watched runs measure **8.154M cycles/s on vs 4.754M off
(1.715×)** for `cpu-opt`. That advantage is **already enabled**, not a new gain
available to close the remaining **29.8×** throughput gap. No physical-Vita or
rendered-FPS result is claimed.

## Supported, default and active

Emulator: `build-vita-tools/local-stack/vita3k/Vita3K`, AArch64 ELF, SHA-256
`cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30`.
Observed banner: `Vita3K v0.2.1 1-bbd5c36`. Source references below use
`bbd5c3624a06572fe4f16f67564f53a7540e1f42`, available offline in
`/home/fred/projects/xgr-tools/vita3k-src`.

| Question | Evidence and conclusion |
|---|---|
| CPU engine | `vita3k/cpu/src/cpu.cpp:41–52` unconditionally constructs `DynarmicCPU`; `vita3k/cpu/CMakeLists.txt:1–25` builds only that implementation. `dynarmic_cpu.cpp:335–352` constructs `Dynarmic::A32::Jit`, `arch_version=v7`; `:366–375` calls `jit->Run()`. |
| Host backend, old/new | Dynarmic submodule `86458a0bd369d63ba4c2ef812cacbb6c9080c065`, `src/dynarmic/CMakeLists.txt:340–389`, selects `backend/arm64` and Oaknut at build time on ARM64. No runtime old/new selector exists. The x64 backend targets a different host, not an alternate for this binary. |
| Interpreter | `dynarmic_cpu.cpp:272–274`'s `InterpreterFallback` only logs an unimplemented instruction; it does **not** execute an interpreter. Unicorn is a Dynarmic test dependency, not a selectable Vita3K CPU engine. This qualifies WW12b's fallback description. |
| Default | `vita3k/config/include/config/config.h:177` and `vita3k/config/include/config/state.h:62`: `cpu-opt=true`. `dynarmic_cpu.cpp:340–349`: safe IR optimizations plus direct page-table/fastmem access when memory logging is off; false disables both. Neither value disables the JIT. |
| Active | Read-only snapshot of 100 `build-f5/ww05/vita3k-run-*/phases/guest/evidence/config-emitted.yml`: all `cpu-opt: true`, `cpu-pool-size: 10`; 81 Trace and 19 Info. `backend-renderer: OpenGL` and `audio-backend: SDL` are **not** CPU selectors. Each new run's journal confirms its selected CPU optimization state and Info logging. |
| CPU pool | `cpu-pool-size` is declared with default 10, but no consumer occurs elsewhere in this source's `vita3k/` tree. There is no evidenced pool-size speed lever here. |

The binary independently corroborates the source: `nm -C` finds
`Dynarmic::A32::Jit::Run` and `Dynarmic::Backend::Arm64::A32AddressSpace`.
Disassembly at `0x106f0dc` calls the Dynarmic CPU constructor; `make_jit` selects
`0xffff` versus zero at `0x1070220–0x1070238`, then calls the A32 JIT constructor
at `0x1070358`. CPU implementation files are byte-identical at the banner
revision and the two available checkout HEADs (`a32b6aed…`, `3a070fab…`).
Whole-binary provenance remains banner-based, as recorded by the launcher;
no reproducible full-source mapping is asserted.

## Isolated A/B

Private evidence root: `/home/fred/projects/zeldaWWHDRecomp/build-f6/ww20`.
The verified, preserved WW12b O1 guest snapshot, private DOL/inputs and watcher
were copied with independent inodes and artifact SHA-256 checks. No writes to
`build-f5`, another lane, the pins or the local stack.

- Guest: `9fe81ed4c99ac182df2b6f23cee199f57f04fa07`, module O1, host O2;
  unchanged VPK SHA-256 `96f2385b3ed9e637065a87751b94363a2b0f3419b1dc2d3820dbb380900bceef`.
- Clean launcher checkpoint: `7eaf9e3b2a05d12e854a7d1abf31287cad04c091`.
  Existing `--baseline-build` checks keep guest and launcher identities separate.
- Private `tools/launch_backend.py` reuses WW05 and the existing WW09 `alt=True`
  config helpers, retaining source/artifact, install, config, report and idle
  gates. No production launcher/config modification.
- Order: on-r1, off-r1, on-r2, off-r2, on-r3, off-r3; fresh sandbox per run,
  verified private disc read-only bound, network namespace unshared.
- WW12b metric: validated `ww05.report` `STATE cycles` divided by guest
  `process.json` `duration_seconds`; three runs per setting, median throughput.
  Both settings use `log-level: 2` (Info). CPU seconds below are secondary,
  sampled per-PID peak increments within the watcher's guest window.
- Watcher samples CPU and log/guest growth every 0.25 s and prints live tails
  and CPU every 2 s. WW05 aborts silence **and** CPU below 5% for 30 s,
  TERM/KILL/reap. No run reached the 600 s ceiling or the idle cutoff.

Reproduction, using the existing private disc path (omit the bracketed option
for on, supply it for off):

```sh
WW05_BUILD_ROOT=<evidence root> python3 -B <evidence root>/tools/watch_run.py <label> \
  [--cpu-opt-off] --wayland-socket /run/user/1000/wayland-0 \
  --timeout 600 --idle-timeout 30 --continue-gx 64 --disc '<private ISO>' \
  --baseline-build <evidence root>/artifacts-56516c64b6804b1a81a5c6bb5c91e35b/build-evidence.json
```

Config SHA-256 (identical within each setting; emitted configs differ **only**
in `cpu-opt`, with the same accepted runtime `user-id: 00` mutation):

| Setting | Staged | Emitted guest |
|---|---|---|
| on | `741c970a568c58f87c72ffd7657026e756bb9a1c9c69e236afa28b8e8ad9a02d` | `381632427de4ad4730e5a28a206e8f74d41f03c811b330994b35e14c560bed63` |
| off | `ce15463901cabe08a791835c56580541441bb6b353c20d660a5dc91b28fe550b` | `3bf81cb4cd05f01af051a515752d34d113741192a1a437a210e42390a50aec48` |

All six reports match byte-for-byte after excluding their request-token header:
**78,789,721 cycles**, PC `80240744`, same REL-materialization stop, 64 verified
journal commands, `END frontier=PASS live=0 rendered=0`. All guest processes
exit 133 at the known post-report teardown crash, preserved rather than hidden.
No unimplemented-instruction fallback appears in their journals.

| Run / UUID prefix | cpu-opt | Guest wall s | Cycles/s | Sampled tree CPU s |
|---|---|---:|---:|---:|
| on-r1 / 3c936a1b | true | 9.748370 | 8,082,348 | 19.05 |
| off-r1 / 89f28412 | false | 36.317309 | 2,169,481 | 34.39 |
| on-r2 / 106576b8 | true | 9.662547 | 8,154,136 | 15.77 |
| off-r2 / b1a9ec03 | false | 16.357623 | 4,816,697 | 42.42 |
| on-r3 / acd14142 | true | 9.241015 | 8,526,090 | 10.42 |
| off-r3 / 58cf5e03 | false | 16.573526 | 4,753,951 | 42.55 |
| **on median** | **true** | **9.662547** | **8,154,136** | |
| **off median** | **false** | **16.573526** | **4,753,951** | |

Thus off delivers **58.3%** of current throughput; turning it off is a
**41.7% regression**, not a speed fix. There is no alternate engine to A/B
without rebuilding/changing Vita3K, outside this experiment.

## Recommendation and limits

Keep `cpu-opt=true`. For the WW12b proxy target of 243M cycles/s, the measured
Info baseline still needs **29.80×**: this frontier's wall budget is **0.324 s**,
versus 9.663 s measured. Even assuming the proposed module flags gain of
1.5–2× (**not measured here**), throughput becomes only **12.23–16.31M**;
execution must still supply **19.87–14.90×**, removing roughly **95–93%** of
the remaining time. Do not budget another interpreter→JIT windfall: it has
already happened. Next, profile per-thread cold JIT compilation, generated
guest execution, memory/context/dispatch and service callbacks; compare an
equivalent native host path to separate module cost from ARMv7 emulation cost
before choosing execution changes. These are measurement gates, not promised
speedups. A physical Vita executes ARMv7 natively: speeding up Vita3K's ARM64
JIT alone does not speed up the handheld.

Host: Linux/Asahi Apple M1 Pro, 10 CPUs, **not physical Vita**. Four WW12b O2
compiler workers ran concurrently; watch journals record load. Guest wall
includes Qt startup, boot/JIT work and post-report teardown, not steady-state
execution or delivered FPS. `off-r1` contains ~19 s of silent, near-zero-CPU
Qt startup, recovering before the 30 s cutoff. It is retained, not subtracted
or entirely attributed to the JIT. The median mitigates that outlier; these
three pairs are not an unloaded-hardware performance certification.

Private evidence: `runs/<label>/{summary.json,watch.jsonl,*process.json,
*config-emitted.yml,*ww05.report}`, original phase evidence and
`evidence/{matrix.json,copy.json,experiment-identity.json,
historical-config-inventory.json,cpu-source-identities.json,*disassembly.txt}`.
The matrix holds full UUIDs, report/config hashes and exact unrounded values.
No game, firmware or saved player data is added to the repository.

Verification: source/symbol/disassembly checks; approved one-line config and
artifact/pin preflight; **six validated equivalent frontiers**, config-identity
and throughput checks; profile tests **19 passed**, activity tests **12 passed**;
repository audit and whitespace check passed. GitHub PR checks and Windows CI
were not queried or run: this task is offline and documentation-only.
