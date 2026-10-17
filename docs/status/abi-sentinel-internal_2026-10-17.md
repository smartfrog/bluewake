# Dispatch ABI sentinel: separate the internal finish frame from a real mismatch

Date: 2026-10-17
Base: `52fc0074f861166da1cc4349ba8ac12292c878ab`
Branch: `fix/abi-sentinel-internal`

## What changed

Trial #14 stopped with `dispatch ABI mismatch: diagnostic stop` and `failure = 0x0C`
(`WW_ABI_SP | WW_ABI_REGISTER`). The capsule's `after` half was `ww_abi_finish`'s own frame at its first
`bl memcmp` (`r4 = &ww_abi_active`, `r5 = &ww_abi_state`, `after.sp = before.sp - 24`, `after.lr` inside
`ww_abi_finish`), so the stop was a fail-closed false positive and the capsule could not answer the
dispatch ABI question.

This change makes that condition explicit in both the capsule and the stop message:

- `WW_ABI_INTERNAL` (`0x80`, the next unused bit of `WwAbiDump.failure`) is set when the captured `after`
  is the sentinel-internal finish frame. Existing bits are untouched; `stop()` still freezes and writes the
  capsule and `ww_host_dispatch` still returns `-1`.
- The classifier is `ww_abi_internal_capture(before, after)`: `after.lr` in
  `[&ww_abi_finish, &ww_abi_finish + WW_ABI_FINISH_SPAN)` or `after.sp == before.sp - 24` (the
  `push {r4,r5,r6,lr}` + `sub sp,#8` prologue signature). It reads only the two captured snapshots and the
  function address, so it is testable without the sentinel runtime.
- `ww_abi_stop_reason(failure, io_error)` selects the message in one place. A capsule with the internal bit
  now stops with `dispatch ABI mismatch: internal finish-frame capture` instead of the generic
  `dispatch ABI mismatch: diagnostic stop`. Real mismatches without the bit keep the old message.

## Capsule layout

Unchanged size and version. `WwAbiDump` stays 3,896 B, `WwAbiPair` stays 240 B, the file stays 4,136 B.

| field | offset | note |
|---|---|---|
| `failure` | 36 | `uint32_t`; bit 7 (`0x80`) is `WW_ABI_INTERNAL`, additive only |
| `complete` | 12 | `TLPC` last, unchanged |
| `version` | 4 | still `1`; old readers see one unknown bit and keep every known bit |

The trial #14 capsule would now record `failure = 0x8C` (`WW_ABI_SP | WW_ABI_REGISTER | WW_ABI_INTERNAL`)
rather than `0x0C`. The `before`/`after` bytes and the ring are unchanged.

`WW_ABI_FINISH_SPAN` is 256 B: it covers the prologue and the first `memcmp` call site in both the sealed
build (first call at `ww_abi_finish + 0x46`, function 0xC4 B) and the ARM `-O3` object built here
(function 0xEC B). In the sealed build the span ends below `ww_abi_install`/`ww_abi_gate`. In the ARM `-O3`
object it reaches about 20 B into `ww_abi_init` (which starts at `+0xEC`), but a real post-dispatch `lr`
lands in `ww_abi_gate`, so the practical false-positive risk is negligible.

The classifier compares the stored 32-bit `lr` with a 32-bit-truncated function address. The capsule field
is `uint32_t`; truncating both sides keeps the test correct on hosts whose `__TEXT`/PIE base is above 4 GiB.

## Tests

Host predicate and capsule tests, no Vita3K:

```
python3 -B -m unittest -v test_native_ctx          # cwd vita/host
Ran 5 tests in 0.598s — OK (skipped=1: ARM compile test, SDK not on PATH)
```

With the SDK on `PATH`, the ARM compile/disassembly check also runs:

```
PATH=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack/bin:$PATH \
  python3 -B -m unittest -v test_native_ctx.DispatchAbiTest
Ran 4 tests in 0.481s — OK
```

`DispatchAbiTest.test_dispatch_boundaries` runs 23 probe scenarios, including the five new ones:
`internal-classifier` (predicate true only for the two signatures, span edge excluded),
`internal-capsule` (SP+register+internal, trial #14 shape), `internal-lr` (lr alone), `real-mismatch`
(sp+72 with an outside lr keeps exactly `WW_ABI_SP | WW_ABI_REGISTER`, no internal bit) and
`internal-absent` (clean dispatch has no marker). `test_dispatch_stop_uses_the_classified_reason` checks
`boot_host.c` uses the shared selector.

`test_capture_is_recognized_at_a_high_host_address` rebuilds the same probe with `-fPIE -pie`, asserts its
`ww_abi_finish` address does not fit in 32 bits (`require-high-address`), and reruns
`internal-classifier`, `internal-capsule`, `internal-lr` and `real-mismatch`. Before the 32-bit truncation
fix this test aborted on `internal-lr`; it passes now.

The ARM object check is also run directly (warning-free):

```
$ arm-vita-eabi-gcc -std=gnu11 -O3 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -mthumb \
    -ffp-contract=off -fno-fast-math -Wall -Wextra -Werror -DWW05_DISPATCH_ABI_SENTINEL=1 \
    -I vita/host -c vita/host/native_ctx_sentinel.c -o /tmp/.../sentinel-vita.o
```

Not run here: `vita/host/test_native_ctx_off.py` needs `build-f5/services-tests` and an initialized
`ref/` tree, neither of which exists in this fresh worktree; the boot_host change is inside the
`#if WW05_DISPATCH_ABI_SENTINEL` region, so the default build is unaffected. No Vita3K run, no package,
no emulator patch, no hardware.

## Risks

- The lr half of the classifier uses a fixed span. If `ww_abi_finish` grows past 256 B before its first
  `memcmp`, that half misses; the `sp - 24` half still recognizes the observed capture. If a future link
  places another function inside the span, a real return whose `lr` lands there would be mislabelled as
  internal. The gate still stops in every case; only the message changes.
- A genuine dispatch violation that returns to `sp - 24` is indistinguishable from the internal capture and
  would carry the internal bit. Its SP/register bits are still recorded and it still stops.
- Diagnostic only: no gameplay, timing or rendering change, and the sentinel stays default-OFF.
