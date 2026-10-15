# WW123: stop before the invalid-read diagnostic

This implements WW121 §7 rank 2, **not an emulator fix or an edge recorder**.
Only game-free synthetic processes were executed for this assignment. A real
trial needs separate approval. Captured memory is private: never commit, upload,
attach or publish the receipt directory.

## Trigger and exact binary

Linux/AArch64 only; Python standard library, existing `nm` and `eu-stack`.
No gdb, dependencies, installs, configuration changes, Vita3K changes or core requests.
The production profile requires the exact PIE Vita3K SHA-256:
`cea2b91f05d9d03bb577b9c414e0da54d3b234f97dc4b23537561e800b1fbf30`.

### Default: native address-fault stop, no injected breakpoint

Use `--trigger native-fault` (the default). Seize all threads and stop on the
**existing** SIGSEGV/SIGBUS signal-delivery stop with `si_addr=0x400000d71`,
before the emulator's signal handler runs. This is the preferred 4-GiB guest
reservation base `0x400000000` plus guest `0xd71` (WW121 §4 and the retained
maps; V `vita3k/mem/src/mem.cpp:65–101,114–129`). The Vita capture verifies
the address against the **captured** MemState base+0xd71, rather than assuming
any fault at the expected address has the right CPU instance.

The optimized load goes through native fastmem, or through a non-null absolute
page-table entry pointing at the same protected null page; this native trap
precedes the invalid-read callback. D `backend/arm64/emit_arm64_memory.cpp:
405–440,525–569,630–638` and `backend/exception_handler_posix.cpp:235–244`
describe the load/fallback redirection. **No instructions, general registers,
or debug registers are changed.** The stopped host PC is the actual native
load, not a callback return inferred from lazy guest PC state.

If the load already uses a callback-only path, or the reservation base differs,
this trigger can miss: report timeout/inconclusive, never silently switch to
the more intrusive fallback. This exact Vita3K path is statically supported,
but it was not dynamically validated on a game in this unit.

### Explicit fallback: cold invalid-read callback breakpoint

The tool also resolves `_ZN19ArmDynarmicCallback10MemoryReadIjEET_j` with `nm`,
then adds `0x34` and the executable's offset-zero mapping base. On the pinned
binary this is **`0x1072574`**, `MemoryRead<uint32_t>+0x34`:

| Native instruction | Meaning at the stop |
| --- | --- |
| `1072540: sub sp,sp,#0x440` | Callback frame size |
| `1072544: stp x29,x30,[sp]` | Original caller return PC at `[sp+8]`; live x30 is already the return from `is_valid_addr` |
| `1072554: mov x24,x0` | Callback instance in x24 |
| `1072558: str w1,[sp,#108]` | Actual access argument at `[sp+0x6c]` |
| `1072570: tbnz w0,#0,1072a10` | Validity branch |
| **`1072574: stp x21,x22,[sp,#32]`** | First instruction of invalid path; matched bytes `f5 5b 02 a9` |
| `1072584: bl spdlog::default_logger_raw` | Not yet executed |
| `1072a1c..20: cmp w1,w19; b.hi 1072574` | Read below host-page-size also reaches this path |

Source: V `vita3k/cpu/src/dynarmic_cpu.cpp:162–174,191–193`.
V is Vita3K `bbd5c3624a06572fe4f16f67564f53a7540e1f42`;
D below is its Dynarmic `86458a0bd369d63ba4c2ef812cacbb6c9080c065`,
with paths relative to `src/dynarmic/`.

`--trigger callback-breakpoint --allow-hardware-breakpoint` uses a hardware
execution breakpoint, not a software patch. The tool seizes
each thread and uses `PTRACE_SETREGSET(NT_ARM_HW_BREAK=0x402)` slot 0,
address = runtime `1072574`, control = `0x1e5` (enabled, EL0, four-byte execute).
It verifies the readback and `SIGTRAP/TRAP_HWBKPT=4` plus stopped PC. This avoids
trapping every valid memory read and never writes instructions or general
registers. **It is not SIGKILL-safe on this kernel.** A strengthened synthetic
test killed the tracer before a same-thread access to the armed breakpoint;
the target died with SIGTRAP (-5). No death-PC/siginfo receipt was captured,
so a retained hardware event versus a pending ptrace trap is not distinguished.
The earlier new-worker-only test did
not expose this. Negative receipts are preserved in
`/tmp/opencode/ww123/dry-verified/` and `dry-tests-verified.log`. This discovery
is why native-fault stopping became the default. Never hard-kill the fallback
tracer; INT/TERM/HUP must be allowed to restore its debug state. This fallback
needs explicit operator acknowledgement and is not an unattended-safe default.

Save all debug-register bytes, refuse pre-existing enabled breakpoints, but
write **only the 24-byte header/slot-0 prefix**. Writing the kernel's whole
16-slot regset attempts to allocate nonexistent slots on this host and gives
ENOSPC, even when the later slots are disabled. Restore the original address
and disabled state; Linux normalizes control bits for a newly allocated disabled
slot, so restoration is semantic, not bit-for-bit. Before/after bytes are retained.

The first cold-path hit must contain `addr=0xd71`. An earlier different invalid
read is **inconclusive and left stopped**, never skipped by single-stepping.
The deterministic WW120 log has no earlier invalid read. This trigger covers
the observed 32-bit read, not every read width or arbitrary guest faults.

## Attach, continue, freeze and detach

1. Verify PID executable identity, SHA-256, little-endian AArch64 PIE header,
   symbol uniqueness, maps and instruction bytes. Receipts must be a fresh
   directory (mode 0700, files 0600). An already stopped target is refused.
2. Hold a pidfd for signaling, send SIGSTOP, wait for the thread group to stop,
   seize every TID with `TRACECLONE|TRACEEXEC`, interrupt/wait every thread.
3. Arm the native fault-address predicate, or (only for the explicit fallback)
   each thread's breakpoint. Send SIGCONT and continue. New `TRACECLONE` children
   are traced, and fallback breakpoints armed, before first execution. Exec
   aborts the observation. **Other native SIGSEGV/SIGBUS are forwarded unchanged** (including
   siginfo) so Dynarmic/Vita3K's normal fastmem/protection handlers can run;
   D `backend/exception_handler_posix.cpp:235–244` redirects the native PC to
   the slow-path trampoline. Suppressing unrelated native faults would prevent
   normal execution. Signals other than the selected native fault/breakpoint
   abort the observation and are retained for delivery at detach.
4. At the native fault or fallback breakpoint, leave the faulting thread stopped **before diagnostic
   code**, SIGSTOP the process, interrupt all other threads, drain clone/stop
   events and verify the `/proc/PID/task` set equals the stopped traced set.
   Other threads stop shortly afterward, not at an atomic common instant.
5. Capture read-only state; for the fallback only, restore hardware slot 0 on
   every TID and verify it is disabled. Detach, injecting SIGSTOP where no
   application signal is pending and preserving the original native fault otherwise.
   Verify all threads are stopped and all `TracerPid` values are zero.
6. Only now run `eu-stack --pid PID -n 64` with an eight-second timeout and
   `DEBUGINFOD_URLS=''`. The two tracers never compete for ownership. Preserve
   its stdout/stderr/return code and verify the process remains stopped.

**Default disposition: detached, alive, SIGSTOP held.** There is deliberately
no automatic resume or target kill. SIGCONT on a real Vita3K would allow the
already-known corrupt-state/Capstone cascade to continue; use the owning
harness's cleanup instead. Synthetic tests explicitly SIGCONT and require
normal exit, including normal handling of the original native fault or execution
of the fallback's trapped instruction. The synthetic fault handler also proves
that resumption does not silently discard the original SIGSEGV.

Wait is at most 180 seconds (default 120), each stop/cleanup wait is at most
five seconds, process memory capture is at most 15 seconds/64 MiB, threads at
most 128, and reverse-map nodes at most 500,000. Errors, timeout and
INT/TERM/HUP take the same cleanup path and return nonzero with partial
receipts. No EXITKILL, POKETEXT, SETREGSET of general registers, SIGKILL,
SIGABRT, core request or target function call is used. A hard-killed tracer
cannot run cleanup or write a final receipt. A synthetic test proves the default
native-fault mode can be killed while armed and its same-thread target still
handles the fault and exits normally; it has no persistent artificial breakpoint.
This is **not** true of the fallback (see the negative experiment above). These tests
are evidence for the tested paths, not a mathematical guarantee against kernel,
hardware, external harness termination, disk errors or arbitrary process failure.

## Capture and layout evidence

- `receipt.json`: PID/starttime/exe/hash, trigger/hit TID, actual invalid
  address, exact native load PC (default) or callback return PC (fallback),
  **all-thread x0–x30/SP/PC/PSTATE**, bounded
  raw frame-pointer walks, outcome and cleanup verification. Frame-pointer
  walks preserve raw return addresses, including PAC bits, and are not promised
  to unwind JIT code. `eu-stack.txt` is the all-thread symbolized unwind attempt;
  nonzero/timeout means incomplete and causes tool exit 2, not invented frames.
- `events.json`: armed/hit/exit milestones and raw breakpoint siginfo;
  `events.jsonl`: append-only full ptrace event/forwarded-native-fault sequence.
  The faulting thread's last native SIGSEGV/SIGBUS siginfo and registers are
  retained in `receipt.json`. Its PC is also reverse-mapped and its bytes
  copied. This is labeled a preceding native fault, not assumed to be this
  access; the Vita profile compares its address with guest-memory-base+0xd71.
- `maps.txt`, `status.txt`, per-thread FP/SIMD regsets and up to 64 KiB of each
  native stack from SP upward. `memory-index.json` gives binary dump addresses,
  lengths and SHA-256, also on partial capture failure.
- `vita-state.json` and `jit-impl.bin`: pointer chain, guest thread ID, full
  stored A32 state, stored descriptor, native ownership candidates and
  RSB data. The pointer chain is **not** the PPC CPUState:

  ```text
  callback = x24                                      [fallback]
  callback = *((x28-0x180)+8)                         [native fault]
  Vita CPUState* = *(callback+8); guest thread ID = u32(CPUState)
  DynarmicCPU* = *(callback+16)
  Jit* = *(DynarmicCPU+16); Impl* = *(Jit+8)
  A32JitState = Impl+0x180; A32AddressSpace = Impl+0x2f0
  A32 regs[16] = A32JitState+24; upper descriptor = +88
  ```

  Verify live x28 equals this JIT state (D `backend/arm64/abi.h:20`). V
  callback fields: `dynarmic_cpu.cpp:125–134`; native getters:
  `DynarmicCPU::get_reg @106f8e4+8`, `Jit::Regs @10bece0`, and
  `Jit::Run @10bf700+0x28,+0x50` confirm every numeric offset. Full A32 state
  is 0x170 bytes (flags, 16 GPRs including guest PC/SP/LR, descriptor, 64 FP
  words, exclusive state), D `backend/arm64/a32_jitstate.h:17–42`.
  Stored guest registers/PC may be lazy; the native registers and Run spills
  preserve additional live values. Do not claim an instruction-precise guest
  context solely from the stored state.
  For native faults x28 is the live A32 state, so the Impl is x28-0x180.
  Its first UserConfig field is the callback pointer at Impl+8:
  D `backend/arm64/a32_interface.cpp:153–156`,
  `interface/A32/config.h:122–123`. The pointer chain must lead back to the
  same x28; a guessed/unmapped/wrong instance fails loudly.
- Current forward map: raw 24-byte robin buckets from `[space+0x40,+0x48)`,
  occupied count at `space+0x68`; distance -1 denotes empty, descriptor at
  bucket+8, entry at +16. See `AddressSpace::ClearCache @10c89c0+0xc..0x60`.
  `current-block-map.json` decodes the current descriptor→native entries.
- Reverse map: root at `space+0x98`, count at +0xb0; each libstdc++ tree node
  has color/parent/left/right/key/value at +0/+8/+16/+24/+32/+40. Captured as
  `reverse-block-map.json`, count/cycle/duplicate checked, with all node links.
  `ReverseGetLocation @10c88e0+4..0x40` and
  `ClearCache @10c89c0+0x64..0x9c` establish the layout. Native ownership uses
  the nearest lower native entry, just like D `address_space.cpp:45–60`.
  **It is not an extent proof**: the reverse map can include older invalidated
  blocks (`address_space.h:76–80`), and no old epoch is reconstructed.
- Copy bounded native bytes around saved callback LR, the recovered underlying
  load return, RSB code pointers and predecessor-map entry points. These are
  offline evidence; never call the target's `ReverseGetLocation` or disassembler.

### Host Run stack / RSB

D `backend/arm64/stack_layout.h:19–40` gives a 0x4a0-byte StackLayout:
eight `{u64 target_descriptor, u64 code_ptr}` at +0..0x7f; 64 16-byte spills
at +0x80; `u32 rsb_ptr` at +0x480; cycles at +0x488; saved FPCR at +0x490;
check bit at +0x494. This is the **host** Run stack, not the guest ARM SP.

At a native JIT load fault **Run SP is the stopped SP**; no callback frame
exists yet. x30 is a JIT scratch register, **not a valid native caller LR**,
so `native_return_pc` is null for this mode. The exact native load PC, its
reverse map, RSB and saved host Run caller frame supply the evidence instead.
For a direct callback, Run SP is stopped SP+0x440. For the optimized page-table
or fastmem fallback it is **SP+0x440+0x290**: `EmitWrappedReadCallTrampoline` saves
17 GPRs (rounded to 0x90) plus all 32 Q registers (0x200). Its saved original
x30 is at wrapper SP+0x80. The tool recognizes wrapped-read32 return PCs by
the prelude's wrapped-read32/64 boundaries (`space+0x218/+0x220`), recovers
that x30, and copies the Run layout. Sources: D `a32_address_space.cpp:24–72,
193–200,225–244`, `abi.cpp:39–55,69–77`, `abi.h:73`,
`emit_arm64_memory.cpp:405–440,525–569`.

Copy 0x580 bytes, including the 0x4a0 StackLayout plus Run's 0xe0 callee-save
area. Saved host x29/x30 are at Run SP+0x4f0/+0x4f8; retain them as
`host_run_frame_pointer`/`host_run_return_pc`. D `abi.h:72`,
`abi.cpp:39–55,69–77` and `a32_address_space.cpp:227` establish that layout.

The callback wrapper itself tail-branches, so `[sp+8]-4` on a direct path is
the generated call site; on a wrapped path it is the trampoline's BLR, and
the separately recovered saved x30 identifies the generated caller. Verify
the dumped bytes before labeling this a native load. An unrecognized route
is explicitly labeled `direct-or-unrecognized`; its RSB is only a candidate.
The stack dump also retains the surrounding words for independent reconstruction.
Per-Run RSB reset and cache-lifetime uncertainty remain as in WW121 §5.

### PPC CPU and guest memory

`WwHost` begins with PPC CPUState (`vita/host/boot_host.h:36–38`). The known
guest `WwHost/&host->cpu` is `0x98005090`, supported by WW120's guest r0/r8;
do **not** treat guest r4=1 as a CPU pointer. Capture 0xe00 bytes and PPC
`cpu.pc` at +0x280, plus a page-spanning 4 KiB guest ARM stack dump. This known
address is a hypothesis carried forward from the exact payload, not discovered
by scanning or substituted for the Vita CPUState above.

Vita MemState is `*(VitaCPUState+8)`. Pinned native
`MemoryRead<uint32_t>+0x73c` reads its memory base at **MemState+0x88**
(the Memory unique_ptr has a std::function deleter; it is not the first word).
Use-page-table is +0x118 and table pointer +0x120, confirmed by
`MemoryRead+0x4e4..0x4fc`. Translation uses the captured absolute-offset page
table when enabled, otherwise base+guest-address, per V `mem/ptr.h:57–64`.
Each guest page is translated separately. Mapped/readable checks precede every
read; unreadable state is a nonzero partial-capture result, not hidden failure.

## Real-run recipe — NOT executed in WW123

First obtain approval and the preserved **plain WW120 VPK** from the coordinator.
The old `/var/tmp/bluewake-ww120` workspace no longer exists at preparation time;
do not use its stale run-script paths or rebuild/substitute a guest. The required
VPK hash is `0cca1412d7d5855f1e409ec4a7004e4257e8a71ed04b063008282d9fb528bb49`.
The following uses the unchanged main checkout only as a read-only launcher;
all staging and receipts go elsewhere. Re-run WW56 read-only admission (`--check`)
in the same uncapped service environment before authorizing the launch.

In terminal 1 (keep actively watching CPU/logs; the existing harness's 30-second
hang rule remains enabled):

```bash
export MAIN=/home/fred/projects/zeldaWWHDRecomp
export TOOL=/var/tmp/bluewake-ww123
export VPK=/absolute/path/to/coordinator-approved-plain-WW120.vpk
export LIVE=/tmp/opencode/ww123-live
export H=5da001734102387b3c55d3003b56a7549dfb8966
mkdir -m 700 "$LIVE"
systemd-run --user --unit=ww123t1-supervisor.service --slice=app.slice --wait --pipe -- \
  /usr/bin/python3 -B "$MAIN/scripts/vita/ww56/run.py" \
  --expected-head "$H" --guest-source "$H" \
  --vpk "$VPK" --vpk-sha256 0cca1412d7d5855f1e409ec4a7004e4257e8a71ed04b063008282d9fb528bb49 \
  --disc "$MAIN/Legend of Zelda, The - The Wind Waker (USA).iso" \
  --disc-sha256 ec2d4ee790f48bee3d4c10feed166370df895f474e8db3f9af3b16ee001436df \
  --wayland-socket /run/user/1000/wayland-0 \
  --run-id ww123t1 --run-root-base /var/tmp/bluewake-ww123-run --receipts-root "$LIVE" \
  --supervisor-unit ww123t1-supervisor.service \
  --guest-budget-bytes 5368709120 --guest-headroom-bytes 5905580032 \
  --rel-settle-seconds 0 --present-grace-seconds 0 --guest-ceiling-seconds 180 \
  > "$LIVE/supervisor.log" 2>&1
```

In terminal 2, start **before** launch; attach as soon as the guest gate execs
Vita3K, not after TlogoE (its remaining window may be shorter than attach). Set
the same TOOL/LIVE variables. The guest-ready PID is the exec-preserved Vita3K
PID, not the supervisor/installer; confirm both exe and owned cgroup:

```bash
python3 -B - <<'PY'
import json, os, pathlib, time
live = pathlib.Path(os.environ['LIVE'])
ready = live / 'ww56-ww123t1/guest-ready.json'
exe = pathlib.Path('/var/tmp/bluewake-ww123-run/ww56-ww123t1/runtime/Vita3K')
deadline = time.monotonic() + 150
while time.monotonic() < deadline:
    if ready.exists() and exe.exists():
        identity = json.loads(ready.read_text())
        pid = identity['pid']
        proc = pathlib.Path('/proc') / str(pid)
        if (proc / 'exe').exists() and os.path.samefile(proc / 'exe', exe):
            assert (proc / 'cgroup').read_text().strip().split('::', 1)[1] == identity['cgroup']
            os.execv('/usr/bin/python3', ['python3', '-B',
                os.environ['TOOL'] + '/scripts/vita/ww56/native_stopper.py',
                '--pid', str(pid), '--exe', str(exe), '--out', str(live / 'capture'),
                '--trigger', 'native-fault', '--wait-seconds', '120'])
    time.sleep(0.02)
raise SystemExit('guest exec deadline; no attach occurred')
PY
```

Do not wait 30 seconds at the **intentional** stop. Once receipt confirms
`cleanup.detached`, `group_stopped`, zero `tracer_pids`, and the unwind attempt
is finished, promptly stop only the owned trial units:

```bash
systemctl --user stop ww56-ww123t1-guest.service ww56-ww123t1-install.service ww123t1-supervisor.service
```

This is harness-managed trial cleanup, not a stopper kill or a natural crash
receipt. A resumed Vita3K can still crash; no recovery is claimed. If exit 2,
inspect the partial receipt first, then perform the same owned-unit cleanup;
never leave a resident paused guest. Preserve supervisor CPU/log watch,
postflight identity checks and process-absence receipts as in WW56 RUNBOOK.

Expected receipts: `events.json` armed→hit, invalid_address 3441, matching
native fault address and exact load PC (or fallback breakpoint PC), all
threads/regsets/stacks, both block maps, native bytes,
guest/PPC state, qualified RSB, `eu-stack.txt`, and explicit detach/stop proof.
The stored guest PC alone cannot decide a wrong native edge. Even a correct
reverse-map identity at d238 does **not** identify its preceding edge or prove
the stale-cache/RSB hypothesis; rank 3's epoch/edge ring may still be needed.

## Dry validation and fallbacks

```bash
WW123_DRY_ROOT=/tmp/opencode/ww123/dry \
  python3 -B -m unittest discover -s scripts/vita/ww56 -p 'test_*stopper*.py' -v
python3 -B -m unittest discover -s scripts/vita/ww56 -p 'test_*.py' -v
```

The first command requires a fresh `dry/` and retains synthetic ELF, tool
receipts and normal-exit logs. Tests cover a new post-attach worker thread,
breakpoint/capture/all-thread eu-stack/detach/SIGCONT/normal exit, timeout,
SIGTERM cleanup, native-fault capture with original SIGSEGV delivery after
detach, native SIGSEGV handler forwarding before the fallback callback,
default-mode tracer SIGKILL with successful same-thread target continuation, hash
refusal without stop, and wrong-address fail-closed. Separate in-process
synthetic structures test native-fault, direct and wrapped RSB recovery, guest/page-table
translation, forward/reverse maps and rejected inconsistent layouts. They
validate decoding, not the live Vita3K heap layout at the real fault.

If native-fault stopping misses, report the timeout. The explicit callback
breakpoint fallback is implemented and dry-tested, but carries the hard-kill
hazard above. If its hardware slots are unavailable/ENOSPC, fail before release;
do not silently change mechanisms. Further alternatives needing separate review:

1. Software BRK at the same cold path, all threads seized, save exact word,
   restore/verify before detach. No hot read-path overhead, but tracer SIGKILL
   can leave a fatal trap instruction; it does not solve the fallback's
   hard-kill safety problem. A best-effort guardian still has a race.
2. Hardware **data** watchpoint on a validated native address: guest 0xd71 is
   a callback argument, usually not a readable native load address. A watchpoint
   at 0x400000d71 need not fire when a page-table fallback avoids the access;
   alignment/slot restrictions and per-thread propagation still apply.
3. Callback-entry breakpoint: reliable ABI x0=self/w1=addr, but every valid
   read traps; filtering by single-step is prohibitively intrusive. A delayed
   TlogoE attach plus a very short single-step window is timing-sensitive and
   can miss the fault completely. Neither is a drop-in fallback.
4. If a different invalid read occurs first or a wrapper/layout check fails,
   keep the partial stop evidence. Change the discriminator only with review;
   do not resume through diagnostics while guessing offsets.
