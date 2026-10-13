# WW43: tagged REL branch validation

## Finding and correction

Base: `7914025aa41a1d78bf6e693455f59b2cdf921243`, initially clean.
Pinned DolRecomp: `b8b534591cba8ca7cd43943a655ee6e2591cf5de`, read-only and
clean before and after this work. No producer patch or dependency pin changes.

`src/frontend/container/rel.c:246-271` tags an untagged target with bit 30
when the patch address is tagged, for both REL24 and REL14. It then computes
`(s64)(s32)(target - patch_address)` and checks alignment and signed limits.
`vita/host/rel_link.c` previously subtracted the untagged DOL address directly
from the tagged patch address. This rejected otherwise matching exports.

The correction applies the producer's tag and signed subtraction only to
displacement calculation, after the existing target validation. Original
addresses still determine MEM1 bounds, translated coverage, and section bounds.
No reusable displacement helper exists in the host. The tag operation applies
to self targets too, as the producer does; it never removes an existing tag.

The existing REL24 displacement rejection fixture now places its compiled
patch at `0xC39900F4` rather than `0xC09900F4`: its DOL target `0x80240744`
is genuinely out of range after tagging. The old fixture's apparent excessive
distance was the bug itself. Its rejection assertion remains unchanged.

## Checks on Linux x86-64

Only source-only synthetic fixtures were built and run, sequentially with
`cmake --build /tmp/opencode/ww43-host --target rel_materialize_probe -j 1`.
The existing tests-only CMake target uses pinned read-only RecompCore. Its three
synthetic generated vector C files were copied from the existing services-test
build, without running a translator or an emulator.

The external runner `/tmp/opencode/ww43-tests.py` points `test_host.OUT` to
this isolated build and skips the existing shared-build setup (which uses
`-j 2`); the normal synthetic disc fixture setup and tests still run.

- Red: 60 tests, 10 failing tagged-target subcases, all 52 original
  REL/materialization/constructor tests passing. Log:
  `/tmp/opencode/ww43-red-final.log`.
- Green: 62 tests passing (52 original plus 10 new parameterized tests).
  Log: `/tmp/opencode/ww43-green-final.log`.
- Coverage includes REL24/REL14, tagged/untagged DOL and self destinations,
  asymmetric target tagging, exact signed limits and one aligned step beyond,
  target/patch alignment, exclusive MEM1 end, old immediate replacement,
  opcode/condition/LK preservation, AA/wrong-opcode rejection, and mismatching
  compiled export rejection. Tagged raw DOL symbols remain rejected.
- `bash scripts/audit_repo.sh`: pass. `git diff --check`: pass.

Executable relocations remain uncertified and rejected; live ownership,
constructor, dependency, lifecycle, raw-hash and prelinked-export gates remain
in place. Passing validation is not permission to activate a real actor.
No game, ARM build, native emulator, network, installation, foreign edit,
hardware test or Windows CI was performed. Independent review remains for the
coordinator before integration.
