# Vita source-port ILP32 corrections — 2026-10-17

Minimal ILP32 corrections on the imported source base, from the sealed census
`build-f5/runtime-next/ilp32-census-c415b56e` (the prerequisite of C3/E0). Scope: the corrections and
a compile-time harness only — no platform layer, no GXM backend, no full Vita build.

**Branch:** `source-port/ilp32`, base `source-port/import-base` at
`3719cc5786911d00b2b6770ba382d16d2ebcbc3c` (the approved import). No push, no pull request.

## What changed

- **Byte order (census §4, the only silent-bug class):** `native/include/helpers/endian.h` detects
  the host with `__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__` (the old compiler list had no ARM, so an
  ARMv7 build took the big-endian path), defines `BSWAP64` on big-endian hosts (it was missing, so
  `BE(u64)` could not compile there) and adds a global little-endian `static_assert`. The dependent
  site `game/src/m_Do/m_Do_lib.cpp:119-127` needs no change: it now takes its identity branch on
  ARM.
- **`CMemBlock` (census §5, option (a)):** `game/include/JSystem/JKernel/JKRExpHeap.h` pads the block
  header to 0x20 only on LP64; on ILP32 the fields already end at the GameCube's 0x10, and the
  `static_assert` follows the width. The `sizeof`-based accesses in `JKRExpHeap.cpp` adapt on their
  own; the `0x10` literals it keeps are alignment constants or sit in the GameCube-only branch.
- **Heaps (census §5):** `native/include/pc/cos_heap_sizes.h` holds the per-target policy (LP64
  factors unchanged, ILP32 keeps the GameCube values) with a table of every site, its GameCube value
  and its LP64 factor; `native/include/pc/cos_pointer_width.h` carries the host pointer width. The
  ten sites (`m_Do_machine.cpp`, `f_op_actor_mng.cpp`, `m_Do_main.cpp`, `d_particle.cpp`,
  `d_mesg.cpp` ×2, `d_meter.cpp`, `d_message_paper.cpp`, `d_auction_screen.cpp`,
  `d_minigame_starter.cpp`, `d_ky_thunder.cpp`) use the macros; the system heap's fixed 32 MiB is
  LP64 only. `JAIStreamMgr.cpp` already computes its buffers with `sizeof(pointer)` and needed no
  change.
- **`_C` (found by the ARMv7 compile test):** VitaSDK's `<ctype.h>` defines `_C` as an octal
  constant and `fopMsgM_unk_struct` has a field named `_C`; `push_macro`/`pop_macro` around the
  declaration in `game/include/f_op/f_op_msg_mng.h` hides it only there (a global `#undef` broke
  libstdc++'s `ctype_base.h`, which uses `_C`).
- **ILP32 assertion profile (census §7.1):** `native/check/ilp32_check.cpp` plus a CMake target built
  only when `CMAKE_SIZEOF_VOID_P` is 4. It asserts the pointer/size widths, `alignof(max_align_t)`,
  `sizeof(CMemBlock)`, the little-endian detection and swap semantics, `OffsetPtr`'s 4 bytes, the
  offset-commented pointer-width fields, and (through `d_save.h`) the packed save regions' sizes.
- **Layout check under GCC:** `native/cmake/layout_check.cmake` gated `-ferror-limit=0` on Clang
  (GCC rejects it), so the host layout check builds on the GCC host too.
- **ABI review (census §2.6-2.8), no change needed:** `%lx` sites cast to `(unsigned long)`
  (`JASDSPChannel.cpp:41`, `JKRAramPiece.cpp:40-41`), the only atomic is `std::atomic<u32>`
  (`JASTrack.cpp:27`), `va_arg` types pointers as pointers (`d_ev_camera.cpp:385`), and the `pack(1)`
  save regions carry 25 `STATIC_ASSERT(sizeof(...))` that the profile now compiles in the ILP32
  configuration.
- **Manifest (review rule B1):** `IMPORT_MANIFEST.sha256` regenerated from the current tree
  (2 584 files, 30 878 547 bytes, manifest sha256
  `587cc1951f3a2dfb3bff80c3b5cb4b45b9643f0f39c38dd41389fbae25f2b779`; `sha256sum -c` passes) and the
  `PROVENANCE.md` header and "Local changes" section updated.

## Evidence

### Host LP64 (aarch64 Linux, GCC 16.2.1, CMake 4.4.3, ninja 1.13)

```sh
cmake -S source-port/native -B <build> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCOS_ASSETS_DIR=<res headers> -DCOS_RECOMPCORE_DIR=<ref-recompcore>
ninja -C <build> cos_sdk_smoke cos_pc_tests cos_layout_check_host
<build>/cos_sdk_smoke    # "ok", exit 0
<build>/cos_pc_tests     # "ok", exit 0
```

- Configure and build: 0 errors.
- `cos_sdk_smoke` → `ok` (rc 0); `cos_pc_tests` → `ok` (rc 0).
- `cos_layout_check_host` builds: the 1 465 offset/size checks of the 213 disc-mapped structs hold
  on LP64 (the xfail list is empty and strict — the target builds exactly when they hold).
- `cos_ilp32_check` is not a target in the LP64 build (the 32-bit guard works).

### ARMv7 / ILP32 (VitaSDK `arm-vita-eabi-g++` 15.2.0)

- 13 units compile with `-c` (real codegen, game flags + `-fno-access-control` for the profile):
  the profile plus `JKRExpHeap.cpp`, `m_Do_machine.cpp`, `m_Do_main.cpp`, `m_Do_lib.cpp`,
  `f_op_actor_mng.cpp`, `d_particle.cpp`, `d_mesg.cpp`, `d_meter.cpp`, `d_message_paper.cpp`,
  `d_auction_screen.cpp`, `d_minigame_starter.cpp`, `d_ky_thunder.cpp` — 0 errors.
- The profile's assertions are evaluated by the ARM compiler: `sizeof(void*/uintptr_t/intptr_t/
  size_t/long) == 4`, `alignof(max_align_t) == 8`, `sizeof(CMemBlock) == 0x10`, `TARGET_LITTLE_ENDIAN
  == 1`, `BSWAP64`/`be64`/`BE<u64>::swap` byte order, `sizeof(OffsetPtr) == 4`, the 21
  offset-commented pointer-width fields, the packed save sizes.
- Layout replay (census §7.2): the generated check unit (1 465 checks, 213 structs) compiles with
  the Vita compiler — 0 errors, so ILP32 restores the GameCube offsets for every disc-mapped struct.
- `m_Do_lib.cpp` preprocessed for ARM: `mDoLib_cnvind32` is the identity (the little-endian branch).

### Not run

- Nothing at runtime: no `qemu-arm` on this machine and no Vita build; the heap margins
  (`COS_SMOKE=heap`) and the scenario sweeps stay phase 2.
- `cos_layout_check`'s `--gc-verify` step (clang for `--target=powerpc-unknown-eabi`): no clang on
  this machine; the comment check remains a clang-host step.

## Findings (census deviations, as measured)

1. The census prose says "25 champs" (§2.3/§7.1); its own list and a fresh grep both give **21**
   offset-commented pointer-width fields. The profile asserts those 21.
2. Two of them do not land on their GameCube offsets on ILP32, for reasons other than width:
   `JKRCompArchive::field_0x64` is at 0x68 (JKRArchive's PC-only `void** mFileData` side table,
   4 bytes on ILP32) and `mDoExt_MtxCalcAnmBlendTblOld::mUserArea` at 0x54 (the virtual base
   `J3DMtxCalc`'s Itanium-ABI layout is 4 bytes tighter than Metrowerks'). Both classes are
   runtime-only, not disc-mapped; the profile locks the measured offsets with a comment.
3. The `_C` collision (above) is a toolchain-level trap for this decomp on VitaSDK, not a width bug.
   The header's push/pop covers only the struct declaration, so `f_op_msg_mng.cpp` itself kept the
   macro and did not compile for VitaSDK either (review finding B1); the unit now hides `_C` for
   its whole body and the ARMv7 compile test covers it.
4. The layout check could not build under GCC before the Clang gate; the `--gc-verify` half stays
   clang-only by design.

## Commits on this branch

| Commit | What |
| --- | --- |
| `c5186d2` | detect the host byte order with `__BYTE_ORDER__`; `BSWAP64` for big-endian hosts; global LE assert |
| `227ee92` | `CMemBlock`: LP64-only padding, width-dependent `static_assert` |
| `5f479d9` | per-target heap-size header and the ten sites |
| `d330fb9` | `push_macro("_C")` around `fopMsgM_unk_struct` |
| `05082f7` | ILP32 assertion profile and its 32-bit-only CMake target |
| `96e7838` | layout check: gate `-ferror-limit=0` on Clang |
| `5186c6e` | journal, manifest and provenance for the ILP32 corrections |
| `db36632` | fix the profile's introduction (two offset exceptions named) and wrap its long lines |
| `ccae9d9` | list the fix in the branch table |
| `da5a2c6` | hide `_C` for the whole `f_op_msg_mng.cpp` unit (review B1) |
| `930b445` | `d_s_play` 2D heap through `COS_HEAP_X3` (review O1) |
| (this commit) | the review corrections in this journal (B1, O1, O2) |

## Review corrections (independent review, 2026-10-17)

Verdict: CHANGES REQUIRED — one blocking finding and two optional ones, everything else confirmed
(including the two locked offsets, 0x68 and 0x54). All three applied:

- **B1 (blocking): `f_op_msg_mng.cpp` did not compile for VitaSDK.** The header's push/pop around
  `fopMsgM_unk_struct` restores `_C`, so the unit's `sp40._C` expanded to `sp40.040`. The unit now
  hides `_C` for its whole body (`push_macro`/`undef` after the includes, `pop_macro` at the end),
  and the ARMv7 compile test covers it: the file compiles clean (15 units now, 0 errors; the
  preprocessed unit keeps `sp40._C` and has no `sp40.040`).
- **O1: the last inline scaling.** `d_s_play.cpp`'s 2D heap (`VERSION_SELECT(...) * 3`) now uses
  `COS_HEAP_X3`, like `d_message_paper`'s; ILP32 keeps the GameCube value (about 1 MiB less
  over-allocation).
- **O2: the layout replay's evidence.** The replay log now carries the command, the toolchain, the
  unit's hash, the assert count and an explicit `RESULT: 0 error, rc 0` line.

The refreshed evidence lives in the sealed revision
`build-f5/runtime-next/ilp32-corrections-review-*/`; `ilp32-corrections-3c69f8c7` stays as the
pre-review record.

## Next

- C3: bounded GXM proof (`PLAN_C3.md`), after the platform-layer decision (E0).
- Phase 2: measure the heap margins on a real ILP32 build (`COS_SMOKE=heap`, actor sweep), fix the
  Vita memory budget (the census's `mem1Size` 256 MiB has no Vita equivalent) and run the scenario
  order of census §7.4.
