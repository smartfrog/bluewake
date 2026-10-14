# WW60: second-request collection, bounded first slice

## Outcome and scope

Module 1's resident publication is now checked through its verified profile-list
global, not through the expired stack controller that originally requested it.
A certified synthetic second REL can link while that controller's storage is
cleared or reused. Actor ownership, explicit unlink, immutable section metadata,
relocation certification and lifecycle rejection remain unchanged.

This is the smallest first slice, **not complete real ship admission**. The
real ship's nontrivial lifecycle and executable relocation contract is still
unsupported. Removing those gates would exceed this mission's admission-only
scope. No emulator launch, private game build, integration or actor activation
was performed. Independent review remains pending.

Initial worktree was clean at
`8ded80b4f2e265aad5a75eb78eaaa94771d5645c`, matching WW59's recorded launcher
identity. The requested SHA omitted `ad5`. Branch: `ww60-second-rel-admission`.
Remote branches were checked; `gh pr list` was unavailable because `gh` is not
installed. Nothing was pushed.

## Phase A: evidence before implementation

Private journal inspected read-only:
`build-f5/ww56-ww59/data/Vita3K/Vita3K/ux0/data/bluewake-ww-fixture/ww05.log`
in `/home/fred/projects/zeldaWWHDRecomp`.

- **5435:** first request, `f_pc_profile_lst`, controller `803A71C0`.
- **5436:** module 1 materialized, 16,500 bytes, scratch `81822E80`, owner
  `803A71C0`.
- **5437:** module 1 linked, profile global publication `C1F80178`, 2,012 bytes.
- **5455–5459:** the same DVD thread is already sleeping with stack frames
  at `803A71B0`, `803A71C8`, `803A71E0` and `803A7208`. The controller's
  publication word at `803A71D0` is in this reused stack storage.
- **139655:** presentation 296.
- **139669:** the game requests `d_a_ship`, controller `80AE0918`, empty
  publication, at `80240744`.
- **139670:** host program exception at the same PC, bits `00000001`.
- The complete journal search found only the four REL lines above: no unlink,
  `OSUnlink`, release or further materialization trace between the requests.
  Absence of a trace alone does not prove an instruction never ran.
- `ww05.report:8` contains the exact host collection STOP. The journal/report
  does **not** contain the changed value of the first controller's publication
  word; no value is invented here.
- `/tmp/opencode/ww56-ww59/guest-disposition.json:1267–1272,1328–1341`
  records the first REL, exception, 55.728-second guest duration, 296
  presentations and absent native PID after cleanup.
  `result.json:349124–349129` records the same `guest_stop`.

Code references below are at the initial HEAD, before this slice:

- `vita/host/rel_service.h:15–34` already defines owner-qualified images,
  linked/unlinked flags and **512 slots**. This was not a single-slot failure.
- `vita/host/rel_service.c:109–133` collects all images. Lines 114–117 compare
  every linked image with `owner + 0x10` and produce the observed STOP. The
  comparison runs before reading the second REL (`219–249`).
- `rel_service.c:89–106` requires completed explicit actor unlink, pins live
  dependencies, and explicitly disallows unloading resident module 1.
- `vita/host/rel_link.c:467–528` verifies the profile export, publishes the
  controller pointer and global, then records `linked = true`.
- `vita/host/boot_host.c:297–315` refuses native actor unlink without an
  epilog/destruction contract. `disc_service.c:171–190` performs request,
  materialization and link before returning to the guest.
- `scripts/vita/ww56/stop_rules.py:18–27,64–69` reacts to the exception; it
  does not implement or cause the host's publication rejection.
- Desktop `runtime/host/src/main.c:2425–2450,2487–2571` has owner-qualified
  scratch slots. `2604–2678` maps executable aliases by module/section;
  `2685–2697` remaps lifecycle tables; `9303–9334` materializes on the authentic
  request and publishes module 1's resident profile global. This accommodates
  disjoint tagged modules rather than one replacement publication. No desktop
  or historical ship/title run was repeated here.

The missing distinction is **temporary controller lifetime versus resident
profile data lifetime**, not missing multi-owner storage or an actor OSUnlink
implementation. Read-only decompilation corroborates the private generated
caller: `zeldaret/tww`'s `src/c/c_dylink.cpp:604–620` constructs a local
profile-list controller, links it and returns. `include/DynamicLink.h:35,51`
and `src/DynamicLink.cpp:20–36,114–120` show the empty derived destructor and
base no-op unlink contract, not the actor epilog/OSUnlink path.

The private generated caller independently confirms construction at `r1 + 8`
and destruction before return:
`build-f5/ww54/composite/chunks_dol/chunk_0008_text1_800216E0.c:22345–22482`.
The base destructor changes its vtable before force-unlink:
`chunk_0143_text1_8023D6E0.c:36943–36986`.
These files were only inspected; no generated source or game bytes were copied
into the change.

## Phase B: minimal semantics and TDD

The existing profile-export selector supplies module 1's exact expected global
publication. One private helper is reused by collection and live-module lookup.
Every other module still requires its controller to publish its owned scratch.
No slots, owner tables, aliases, unlink implementation or relocation validator
were replaced. Original controller storage is not repaired or overwritten.

All builds were serial (`-j 1`) after checking `/proc/*/comm` for live Vita3K.
Linux AArch64, GCC, isolated build `/tmp/opencode/ww60-host`, pinned read-only
RecompCore `ddd031b2a7f45fcbda37a49daac9b308b8d95da1`.

- Test-first red: 64 REL test methods, two failing subcases (expired and reused
  controller), all 62 pre-existing methods passing.
  `/tmp/opencode/ww60-red-build.log`, `ww60-red-tests.log`.
- First green: 64/64. `ww60-green-build.log`, `ww60-green-tests.log`.
- Final REL characterization: **65/65**, including the next nontrivial lifecycle
  rejection after controller expiry. `ww60-final-rel-tests.log`.
- Broader Python host regressions: **681/681**.
  `ww60-host-regressions-final.log`. The initial broader run had six setup errors
  because the isolated vector emitter was absent; it was built from the pinned
  producer with public synthetic inputs and the suite was rerun successfully.
- CTest: **51/51**, `ww60-ctest.log`.
- `bash scripts/audit_repo.sh` and `git diff --check`: pass.

The external runner `/tmp/opencode/ww60-tests.py` selects this worktree/build and
skips the shared setup's `-j 2` builds. Existing public generated vector fixtures
were reused for the host build. No foreign source, dependency pin or producer
patch was edited. Hardware gameplay, Windows CI and a Vita ARM game build were
not tested.

## Next mission

Real ship admission still reaches the static lifecycle rejection in
`vita/host/rel_link.c:168–174` before executable relocation certification. This
is a predicted frontier from code, not a newly observed game stop.

Reuse, after review, the explicitly ship-only, default-off WW44 vertical slice
preserved in `build-f5/preserved-worktrees/ww44-paused-source.tar` and described
by `ww44-PAUSED.md:13–43,46–60`. It has the real translated caller/prolog path,
owned constructor walk, inherited-register checks and exact canonical relocation
certification, with recorded 68/68 synthetic tests. Its real composite/export
wiring remains unverified. WW60 did not import any of that work. Epilog,
unresolved entry and native actor unlink must remain closed until separately
certified; admitting a request is not actor activation.
