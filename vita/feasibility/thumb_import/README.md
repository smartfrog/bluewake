# Public Thumb import probe

Standalone synthetic homebrew source. No game pipeline, translated objects, disc,
assets, private inputs, RecompCore, renderer or emulator dependency. It is not a
game-crash fix or an ABI/FPS qualification. No production flags are changed.

`probe_run` opens `app0:eboot.bin` read-only through SDK `sceIoOpen`, closes it,
then copies 240 bytes between distinct static 16-byte-aligned guarded buffers.
Every source/destination byte and every guard byte is checked. No heap is used by
the probe itself. One copy is intentional: repetition/ring behavior is not the
minimum import-path discriminator. SDK startup may independently use libc.

Records go to `ux0:data/public-thumb-import-probe.log` (a dedicated overwritten
diagnostic file, no game/player data). The order is `control-enter`,
`control-pass`, `copy-enter`, `copy-leave`, `status-pass`, each newline terminated.
Any failed/short record write stops immediately. Exit codes: 0 success, 2 import
control failure, 3 copy/guard/source mismatch, 4 record/open/close failure.
No `copy-leave` means no demonstrated return, not proof of its cause.

PF10's 240-byte struct assignment lowered to a direct Thumb BL to SDK `memcpy`.
`copy.c` makes that call explicitly; it does not reproduce the surrounding ABI
gate or struct semantics. Per-TU `-fno-builtin-memcpy` prevents builtin lowering;
`-fno-optimize-sibling-calls` retains BL plus return; no LTO is used. No volatile
function pointer, custom memcpy, manual stub or synthetic veneer replaces the
SDK chain. The gate intentionally fails if compiler output changes shape.
The linker generates the interworking veneer; vita-elf-create later converts
the raw import metadata. The veneer literal is a relative layout distance, not
PF10's fixed `0x110` requirement.

## Build/check only, existing tools

Use a new external build directory and the existing pinned SDK installation.
On the current Linux AArch64 host the canonical SDK used by PF10 is:

```sh
export VITASDK=/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack/vitasdk
SRC=vita/feasibility/thumb_import
BUILD=/absolute/new/evidence/build
TMPDIR=/tmp/opencode python3 -B -m unittest discover -s "$SRC" -p test_probe.py
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
cmake --build "$BUILD" --parallel 1 --verbose
python3 -B "$SRC/artifact_gate.py" "$BUILD/public_thumb_import" --sdk "$VITASDK" --copy-object "$BUILD/CMakeFiles/public_thumb_import.dir/copy.c.obj"
python3 -B "$SRC/artifact_tests.py" "$BUILD/public_thumb_import"
```

Host tests wrap memcpy only to inject corruption; they are not runtime import
qualification. Artifact tests mutate the actual public ELF in memory and reject
wrong NID, absent import, local memcpy, missing BL and wrong veneer destination.
The static gate decodes linked instructions and raw SDK import metadata and
checks objdump/readelf output plus the copy object's undefined memcpy relocation.

Stop at the raw public ELF. No SELF/VPK targets or converter configuration are
provided. Planned dedicated app ID: `BWTH00001` (unassigned; check before use).
Packaging needs independent code **and artifact** review, explicit authorization,
and the canonical stock converter recipe/pins. Execution additionally requires
the separately owned watcher repair/review and a new authorized runtime trial.
Neither compilation nor static checks qualify the converted/live import or
explain PF10. Do not install, run, publish, reuse game objects or patch emulators
as part of this build/check task.
