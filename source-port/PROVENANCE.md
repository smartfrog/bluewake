# Provenance — source base imported from SwitchWaker

- Repository: https://github.com/centollOS/SwitchWaker
- Commit: `04372277d03f934e1e563ae302bba9f565c5cb5d` (`main`, 2026-10-13)
- Method: file copy (no Git history imported), exact pin; the local changes are documented in
  "Local changes vs upstream" below (the three GCC flag gates, then the ILP32 corrections)
- Import date: 2026-10-17
- Imported by: `scripts/source_port/import_switchwaker.sh` (this repository)
- Manifest: `source-port/IMPORT_MANIFEST.sha256` (LC_ALL=C sorted, `sha256sum -c` format,
  2 584 files, 30 894 659 bytes as committed; manifest sha256
  `dac5fab815fa620db28a6eccabeb4c2f9edd14af2d1139dc351885cbd5e9c8be`)
- Rule: refresh this header (file count, bytes, manifest sha256) whenever the manifest is
  regenerated; the manifest records the current hashes, so it must be regenerated after any
  change to an imported file.

## What is here

| Path | Origin | License |
| --- | --- | --- |
| `game/` | the Wind Waker decompilation (`snrubrm/tww` fork plus the port's `TARGET_PC` hunks and the `zeldaret/tww` convergence) | CC0-1.0 (`game/LICENSE`) |
| `native/CMakeLists.txt`, `native/cmake/` | the port's build system | MIT (`LICENSE` in the repository root of the source) |
| `native/README.md`, `native/LICENSE` | the port's native-build documentation and the upstream repository's MIT license text, copied to `native/LICENSE` | MIT |
| `native/sdk/`, `native/include/sdk/`, `native/include/pc/`, `native/check/`, `native/tools/`, `native/src/pc/` | the game-specific SDK over Aurora and its run harness | MIT |
| `native/include/helpers/`, `native/src/helpers/` | big-endian/offset-pointer helpers, from Dusklight at `40457c6` | CC0-1.0 |
| `native/patches/aurora/` | the Aurora patch set (0001-0021) the port's harness and renderer need | MIT, as Aurora |
| `native/dsp_hle/` | Dolphin's DSP HLE behind `cos_sdk`, adapted from this repository's own `runtime/host/src/dsp_hle_backend.cpp` and `runtime/host/src/dsp_common_shim.cpp` lineage | GPL-2.0-or-later (`native/dsp_hle/LICENSE`, SPDX headers) |

The destination mirrors the upstream repository root for the imported trees (`game/` and
`native/` keep their names), so the port's own `CMakeLists.txt`, `cmake/` and `tools/` work
unchanged (`COS_ROOT` is `../game` from `native/`; `native/tools/gen_assets.sh` reads
`../../game/UPSTREAM`).

Beyond `PLAN_IMPORT.md`'s group list, the build system, the harness and the review require:
`native/CMakeLists.txt` (the project root the CMake modules hang from), `native/src/pc/` (the run
harness `cos_pc`, globbed by `native/cmake/executable.cmake` and linked by `cos_pc_tests`),
`native/patches/aurora/` (the harness calls Aurora APIs the patch set adds, e.g.
`AuroraSetContentScale`), `native/README.md` (referenced by six imported files) and
`native/LICENSE` (the upstream MIT text, copied from the repository root).

`native/README.md` mentions that the decompilation fork completes its remaining functions "with AI
assistance" in its prose; that is upstream provenance, not an attribution line, and the file is
kept verbatim.

## What is not here

`switch/` (the Switch platform layer, deko3d renderer, uam), `native/data/`
(`initial_pipeline_cache.db`), `native/tools/dksh_cache/`, the Switch-only Aurora patches
(`switch/native/aurora/patches/`), their `docs/`, `scripts/`, root documents and CI. None of it
is needed for the host build or the planned Vita work.

No disc data, no game assets, no saves and no binaries: the source repository tracks none
(checked), and the import script refuses any file outside its list.

## Local changes vs upstream

The first changes, on top of the exact copy (all in `native/cmake/GameConfig.cmake`, all for
the GCC host the smoke build runs on; the upstream build is written for clang):

- `-ferror-limit=50` is gated on Clang/AppleClang, because GCC rejects it (its equivalent is
  `-fmax-errors=N`).
- `-Wno-changes-meaning` is added for GNU compilers, because the decompiled code names union
  members after their types (`JAISequenceMgr.h`'s `PortArg`) and GCC 14+ diagnoses that as an
  error where clang accepts it.
- `-Wno-narrowing` is added for GNU compilers, because `-sizeof(...)` in a template argument
  (`JUTConsole.h`'s `ConsoleList`) narrows `unsigned long` to `int`; clang's
  `-Wno-c++11-narrowing` suppresses it and GCC's flag is `-Wno-narrowing` (a `-Woverflow` warning
  remains, which is not fatal).

Upstream hash as imported:
`d43c94d1b9acf40edcf2a15280123b1cfed1c62b7459eda2bbbd211eea3975f6`; current:
`12888b67f766fa649dfe2c38b172335f7bfe7cded0bff0eba3a2d5a49be4371a`. The manifest entry records
the current hash.

### ILP32 corrections (branch `source-port/ilp32`, 2026-10-17)

The pre-Vita unit from the sealed census (`build-f5/runtime-next/ilp32-census-c415b56e`): the same
sources must compile on LP64 and ILP32, so every change keeps the LP64 behaviour and adds the
32-bit path. Changed imported files:

- `native/include/helpers/endian.h`: little-endian detection through `__BYTE_ORDER__` (the compiler
  list had no ARM), `BSWAP64` defined on big-endian hosts, and a global little-endian
  `static_assert`.
- `game/include/JSystem/JKernel/JKRExpHeap.h`: the block header's padding (`mPad`) is LP64 only, so
  `sizeof(CMemBlock)` is 0x20 on LP64 and the GameCube's 0x10 on ILP32; the `static_assert`
  follows the width.
- the eleven heap-size sites (`m_Do_machine.cpp`, `f_op_actor_mng.cpp`, `m_Do_main.cpp`,
  `d_particle.cpp`, `d_mesg.cpp`, `d_meter.cpp`, `d_message_paper.cpp`, `d_auction_screen.cpp`,
  `d_minigame_starter.cpp`, `d_ky_thunder.cpp`, `d_s_play.cpp`) use the per-target macros of
  `native/include/pc/cos_heap_sizes.h`: the LP64 factors are unchanged, ILP32 keeps the GameCube
  values (the system heap's fixed 32 MiB is LP64 only).
- `game/include/f_op/f_op_msg_mng.h` and `game/src/f_op/f_op_msg_mng.cpp`:
  `#pragma push_macro("_C")` around `fopMsgM_unk_struct` and around the unit's body, because
  VitaSDK's `<ctype.h>` defines `_C` as an octal constant and the struct has a field named `_C`;
  libstdc++'s own ctype headers still see the macro.
- `native/CMakeLists.txt`: the `cos_ilp32_check` target (the assertion profile), built only when
  `CMAKE_SIZEOF_VOID_P` is 4.
- `native/cmake/layout_check.cmake`: `-ferror-limit=0` is gated on Clang (GCC rejects it), so the
  host layout check builds under GCC too.

New local files, not from upstream and therefore not in the manifest:
`native/include/pc/cos_pointer_width.h` (host pointer width), `native/include/pc/cos_heap_sizes.h`
(the heap-size policy and its per-site table) and `native/check/ilp32_check.cpp` (the ILP32
assertion profile). Journal: `docs/status/VITA_ILP32_CORRECTIONS_2026-10-17.md`.

### Vita build and run (E1/E2 and the M1 follow-ups, 2026-10-17/18)

The E1 skeleton made the imported tree build for the Vita and the E2 harness made it run headless;
the later P-A/M1 commits added the profile slice and the first visible frame. These are the imported
files changed since the ILP32 corrections (all guarded on the target or on a CMake option, or
behaviour-preserving, so the host builds keep their behaviour):

- `native/CMakeLists.txt`: non-PIC on 32-bit targets (`vita-elf-create` rejects the
  `R_ARM_JUMP_SLOT` relocations of a PIE link), the Vita/static-init/profile-slice includes and the
  `cos_ilp32_check` target.
- `native/cmake/Aurora.cmake`: build-local abseil patches for newlib (`tm_gmtoff`, ELF mem image),
  empty `Tracy::TracyClient` and `sqlite3` targets on the GX-less build, PIC cleared on every
  target, and `COS_AURORA_LIBS` listing only the Aurora libraries the option set built.
- `native/cmake/dsp_hle.cmake` and `sdk.cmake`: the `COS_DSP_HLE` option (ON by default; OFF on the
  Vita, where `native/vita/stubs_dsp.cpp` supplies the entry points).
- `native/cmake/executable.cmake`: Dawn's include directories only when `dawn::webgpu_dawn` exists.
- `native/sdk/include/cos_sdk/host_alloc.h`: newlib's `memalign`, because `std::aligned_alloc` is
  absent and `posix_memalign` is declared but does not link there.
- `native/sdk/src/audio/AI.cpp`: the AI DMA thread asks for a 256 KiB stack, because PTE's pthread
  default (4 KiB) is overrun by SDL's resampler.
- `native/sdk/tests/`: printed skips for the AI, DSP, GX and child-process checks on the Vita, the
  newlib allocation path, and the host_alloc counter's recognition of the C++ runtime's TLS
  teardown node.
- `native/src/pc/harness/pc_smoke.cpp`: the static-init check moved to
  `native/src/pc/harness/pc_static_init.cpp` behind `native/include/pc/pc_static_init.h`, so the
  Vita link needs no PC harness.
- `game/src/d/d_camera.cpp`: the `cos_r3b_camera_draw` export under `COS_R3B_CAMERA_DRAW`.
- `game/src/d/actor/d_a_npc_zl1.cpp`: `daNpc_Zl1_c::checkOrder` takes the `Vec` cast through
  `&a_start_pos[0]`.

New local files, not from upstream and therefore not in the manifest: `native/vita/` (boot,
display, GX/GD/DVD/DSP/libc stubs, actor/scene services, profile list and slice tool, link checks),
the four `native/cmake/vita*.cmake` modules, the new harnesses in `native/check/`,
`native/src/pc/harness/pc_static_init.cpp`, `native/include/pc/pc_static_init.h`,
`native/patches/abseil/` and Aurora patches 0022-0023.

## Attribution

Human authorship only. The imported files contain no AI attribution lines (grep for
`Co-Authored-By`, `Anthropic`, `Claude`, `Copilot`, `OpenAI`, `ChatGPT`: 0 occurrences in
`game/` and `native/`), and no Git history was imported — the upstream commit messages carry
`Co-Authored-By: Claude` lines, which is why the copy method was chosen over `git subtree`.

Credits:

- `game/`: the `zeldaret/tww` contributors and `snrubrm` (the fork that completes the
  remaining functions); upstream pins in `game/UPSTREAM` (`snrubrm/tww` `b09eebc`,
  convergence `zeldaret/tww` `a1854d47`).
- `native/`: Pulpparty, depende3000 and the SwitchWaker contributors (MIT).
- `native/include/helpers/`, `native/src/helpers/`: TwilitRealm/Dusklight (CC0).
- `native/dsp_hle/`: adapted from the recompilation project's `runtime/host/src/`
  (this repository's lineage) over Dolphin's DSP HLE (GPL-2.0-or-later).

The imported `LICENSE` files are kept verbatim; `RIGHTS_AND_LICENSES.md` records the addition.
