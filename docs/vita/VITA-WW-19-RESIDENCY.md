# VITA-WW-19 — REL residency during the boot: what the runs measure

2026-10-13 · follow-up to [VITA-WW-14](VITA-WW-14.md) (the measurement harness), unlocked by lane 16
(the REL provider loads, links and executes). Branch `vita-ww-19-relresidency`, based on `7eaf9e3`.

**What is proven**: the REL provider's real events in 103 logged runs (24 reached the REL frontier, 22
materialized, 21 linked), all of them exactly **one** module; the MEM1 footprint the boot actually
writes (non-zero counts, extent and growth between two full 24 MiB dumps); the per-module compiled
footprint of all 415 RELs from the real link map (188.25 MiB, matching ticket 14's 188.3 MiB readelf
figure); and the split arithmetic against the current artifact (the unsplit nominal is 456.9 MB, the
post-split base is 158.6 MB).
**What is not**: no gameplay active set — the boot stops before the game streams its world; the
ticket-14 `[rel-load]`/`[rel-unload]` markers are **not emitted by the host** (only
`[rel-frontier]`/`[rel-materialized]`/`[rel-link]` exist), so the resident set below is "materialized
and never reported released"; no physical-Vita measurement; no generator split exists yet.

## 1. What the host reports today

The ticket-14 contract defined `[rel-load]`/`[rel-unload]`; the harness (`scripts/vita/relset/`) reads
them. The host lane does not emit them yet. What the runs carry instead:

| Marker | Meaning | Seam |
| --- | --- | --- |
| `[rel-frontier] name= object= module= pc=` | the guest asked for a module at the archived materialization edge (`module=0` = nothing linked yet) | `vita/host/disc_service.c:181` |
| `[rel-materialized] module= name= bytes= guest= owner= source= backend=` | the raw image was copied into owned scratch | `vita/host/rel_service.c:108` |
| `[rel-link] module= owner= guest= profile= bytes= contract=` | the owned module-1 contract was published | `vita/host/rel_link.c:50` |

Consequences, kept explicit everywhere below: a resident set derived from these markers cannot see a
release, and the ticket-14 harness verdict for every existing run is still
`not measured: no REL events in the evidence`. The new tools in this delivery read the markers that
exist and refuse to invent the pair that does not.

## 2. The measured residency (all runs, all scenes reached)

Scanned every `build-f5/ww05/vita3k-run-*/…/ww05.log` (read-only, never written):

| Quantity | Value |
| --- | ---: |
| run directories / with `ww05.log` | 105 / 103 |
| runs that reached the REL frontier | 24 |
| runs that materialized a REL | 22 |
| runs that linked it | 21 |
| distinct REL modules ever materialized | **1** (`f_pc_profile_lst`, module id 1) |
| runs that requested another module | 0 |

The two runs that stopped at the frontier without materializing are the unit-11 diagnostic
(`a802fc25…`, refusal by design) and the unit-12 catalog mismatch (`30c30fe7…`, producer naming
defect, since corrected).

The measured table (newest run `1351063a0837427b87d242d99ace55ec`, source
`4f5c0745ca802f4183be616e730558841398b1cb`):

| REL | id | raw bytes | scratch capacity | scratch address | load window (cycles) | resident to end? |
| --- | ---: | ---: | ---: | --- | --- | --- |
| `f_pc_profile_lst` | 1 | 16,500 | 16,512 (32-byte rounded) | `0x81822E80` | 78,787,771 → 78,800,368 | yes (single slot, no release reported) |

The window is the last `cycles=` stamp before the marker and the first after it: the host stamps
cycles on progress/edge lines, not on the REL lines, so the load cycle is an interval. The linked
profile data is `0xC1F80178`, 2,012 bytes, contract `static-composite-profile-list`.

Why exactly one module, in the host's own terms: the Vita provider has **one** slot
(`host->rel_image`) and refuses a second materialization outright
(`vita/host/rel_service.c:81`, *"REL image is already materialized"*). No run has ever asked twice.

## 3. The boot frontier: no gameplay has been reached

| Run | cycles | decoded | presented | STOP |
| --- | ---: | ---: | ---: | --- |
| `1351063a…` (newest family) | 85,390,497 | 8,963 | 8 | deferred DSP post-bootstrap task command (`CC005002`, audio=deferred) |
| `6ee5339a…` (longest) | 493,979,208 | 265 | 0 | SelectThread wait plateau, then stopped by its ceiling |

The load happens at 78.79 M cycles, i.e. **after** the profile-list request and **before** the first
presented frame. The game has not yet streamed a single actor, scene or object REL: the 415-module
catalog is untouched beyond module 1. There is no "loading rate" to extrapolate from — one request in
85.4 M cycles is a one-off, not a rate, and the longest run (493.98 M cycles, in the scheduler wait)
adds none. Any gameplay active-set number must wait for the boot to pass the DSP STOP.

## 4. MEM1 and the scratch, measured

The newest run captured two full raw physical MEM1 dumps (25,165,824 bytes each, guest base
`0x80000000`) with manifests:

| Snapshot | cycles | command | presented | non-zero bytes | non-zero extent |
| --- | ---: | ---: | ---: | ---: | --- |
| 1 (`ww05.mem1-1.bin`) | 14,491,219 | 990 | 0 | 3,119,063 (12.4%) | `0x80000020`..`0x815304E7` |
| 2 (`ww05.mem1-2.bin`) | 24,342,927 | 1,962 | 1 | 3,123,663 (12.4%) | unchanged |

Growth between them: **5,120 bytes newly written, 520 cleared, 13,135 bytes changed**. These are
lower bounds on occupancy — zero-valued live data is invisible to a byte scan — and they cover the
boot *before* the REL load (snapshot 2 precedes 78.79 M cycles).

The REL scratch is **not in MEM1 at all**: `0x81822E80` sits above MEM1's end (`0x81800000`), inside
the shared scratch window `0x81820000..0x81F80000` (7,733,248 bytes). The measured scratch footprint
is 16,512 bytes. The run's `ALLOC` line cannot show it: that line is written **before** the guest run
loop (`vita/host/main.c:144-153`), so it is a startup snapshot — every run, from 98 cycles to 493.98 M,
reports the same fixed backings (only `host=`, the build's `sizeof`, varies):

| Backing | Bytes |
| --- | ---: |
| MEM1 | 25,165,824 |
| VM | 33,554,432 |
| locked cache | 16,384 |
| ARAM | 16,777,216 |
| code snapshot | 3,364,032 |
| exported REL aliases (fixed table, `alias_count=1900`) | 1,542,820 |

The 1,542,820-byte alias table is the composite's linked REL *data* sections, present from the first
cycles; it is what the module-1 link borrows (`0xC1F80178`), and it stays resident under any split.

## 5. The catalog: what a split would move (real link map)

`scripts/vita/relset/rel_footprint.py` reads the built `ww05.map` (artifact
`artifacts-c5a63623be104ea0ad64e7446d88a18c`, source `fe73d512…`), groups each
`chunk_NNNN_rel1_<BASE>.c.obj` under the module whose base is the greatest not after the chunk (a
module's fifth chunk spills into the next 64 KiB slot), and joins the translator catalog and the
verified raw sizes:

| Quantity | Value |
| --- | ---: |
| modules | 415 |
| compiled footprint (`.text` + `.rodata` of the chunks) | 197,397,962 B = **188.25 MiB** |
| raw `.rel` bytes (verified importer output) | 6,996,836 B = 6.67 MiB |
| smallest / median / mean | 6,638 / 275,072 / 475,658 B |
| p90 / p99 / largest | 1,155,344 / 2,717,848 / 3,422,030 B (`d_a_npc_md`, id 147) |

| Compiled size | Modules | Total |
| --- | ---: | ---: |
| < 50 KB | 22 | 0.68 MiB |
| 50–100 KB | 69 | 4.86 MiB |
| 100–200 KB | 73 | 10.50 MiB |
| 200–500 KB | 124 | 37.90 MiB |
| 500 KB–1 MB | 77 | 52.73 MiB |
| ≥ 1 MB | 50 | 81.58 MiB |

The ten largest modules (`d_a_npc_md`, `d_a_npc_ji1`, `d_a_bk`, `d_a_bgn`, `d_a_bpw`, `d_a_bdk`,
`d_a_tn`, `d_a_mo2`, `d_a_ship`, `d_a_bst`) are 26.4 MiB together — a 14.0% share for 2.4% of the
modules. Module 1 itself is 16,500 raw / **7,110 compiled** bytes.

## 6. The 452 vs 448 answer, with this build's numbers

The ticket's projection was 452.4 MB nominal (351,467,732 B PT_LOAD + 96 MiB heap + 256 KiB worker)
against ~448 MB usable. Re-measured on the current artifact:

| Line | Bytes | MB |
| --- | ---: | ---: |
| PT_LOAD memory (current artifact) | 355,978,236 | 356.0 |
| + 96 MiB heap container | 100,663,296 | 100.7 |
| + 256 KiB worker reservation | 262,144 | 0.3 |
| **nominal today** | **456,903,676** | **456.9** |
| ticket-era nominal (351,467,732 PT_LOAD) | 452,393,172 | 452.4 |
| usable on Vita (ticket figure, not re-measured here) | ~448,000,000 | ~448 |

**The overrun is confirmed and has grown**: the unsplit nominal is 456.9 MB, 8.9 MB over the ~448 MB
figure (the ticket saw 4.4 MB). The build's PT_LOAD has grown 4,510,504 B since the ticket-era
artifact, so the ticket's arithmetic is now conservative by that amount. The REL chunks are
192,950,418 B of the 343,010,946 B `.text` (56.3%) plus 4,447,544 B of `.rodata`.

The split's arithmetic, using the measured 197,397,962 B:

| Line | Bytes | MB |
| --- | ---: | ---: |
| PT_LOAD − REL chunks | 158,580,274 | 158.6 |
| + 96 MiB heap + worker | 259,505,714 | 259.5 |
| + 72 MiB heap (ticket's reduction) + worker | 234,339,890 | 234.3 |
| headroom vs ~448 MB (96 MiB heap) | 188,494,286 | 188.5 |
| headroom vs ~448 MB (72 MiB heap) | 213,660,110 | 213.7 |
| worst case, all 415 RELs resident (72 MiB heap) | 431,737,852 | 431.7 |

Two honest readings of that table:

- **The split plus the heap reduction makes the nominal fit.** 234.3 MB base + the full 197.4 MB REL
  set = 431.7 MB < ~448 MB. Even with every REL resident, the nominal total fits — but only 16.3 MB
  would remain for GPU, textures, staging, audio and everything else, which is why demand loading is
  still the design: it is what buys the margin back (the difference between the 197.4 MB worst case
  and the real active set).
- **The ticket's ~215 MB ceiling checks out**: 213.7 MB of headroom with the 72 MiB heap is exactly
  the "RELs actifs + GPU + divers ≤ ~215 Mo" line, and the 158.6 MB post-split base matches the
  ticket's "résident ≈ 159 MiB".

### Uncertainty, explicitly

1. **The ~448 MB usable figure is the ticket's**, not measured here; this project has no physical Vita
   result and does not claim one.
2. **The realized split saving is unmeasured.** 197,397,962 B is the linked per-chunk sum of this
   build; the generator scoping (ticket 14) may add alignment/padding or keep shared tables resident.
   Treat 158.6 MB as an arithmetic floor, not a built artifact.
3. **The gameplay active set is unmeasured.** It needs the boot to pass the DSP STOP and reach the
   actor/scene streaming; nothing in these runs bounds it. The catalog brackets it: mean 475,658 B,
   median 275,072 B per module, but *which* modules are live together is exactly what ticket 14's
   harness must measure.
4. **The heap reduction 96 → 72 MiB is a ticket decision**, not implemented; the current artifact has
   the 96 MiB container.

## 7. The demand-loading design (the overrun is confirmed)

**What moves.** The 197,397,962 B of compiled REL chunks (56.3% of `.text`). The linked *data*
sections (1,542,820 B of aliases) stay: they are already separate from the code and module 1's link
borrows them.

**Who decides unloading.** The guest's module manager, never the host. The desktop already mirrors the
guest's lifecycle through the module object: a slot is live while the object still points at it
(`runtime/host/src/main.c:2425`):

    static bool host_rel_slot_is_live(CPUState* cpu, BlueWakeRelSlot slot) {
        return slot.owner != 0u && mem_read32(cpu, slot.owner + 0x10u) == slot.address;
    }

**The `[rel-unload]` points in `main.c`.** The desktop's `host_materialize_rel` already reclaims dead
slots lazily, at the next materialization, in three places — each is a natural `[rel-unload]`
emission point for the slot(s) it retires:

| Point | Lines | What it does |
| --- | --- | --- |
| dead-slot reuse for the same owner | `main.c:2516-2523` | keeps an address whose owner asked again |
| dead-slot reuse for a new address | `main.c:2526-2533` | hands a retired slot's address to a new module |
| overlapping dead-slot clearing | `main.c:2554-2562` | zeroes retired slots the new allocation overlaps |
| fresh scratch | `main.c:2537-2551` | first-fit over `0x81820000..0x81F80000` (`main.c:198-199`) excluding live slots and linked sections |
| alias drop | `main.c:2564` | `host_remove_rel_aliases(address, address+capacity)` (`main.c:2406`) |

The load marker belongs where the module becomes resident — the success branch that already prints
`[rel] archived resource materialized …` (`main.c:9337-9340`), which is the ticket-14 contract's
stated point. `[rel-unload]` at the reclaim sweep is **lazy**: the guest released the module earlier,
so the reported residency is longer than the real one. That is the conservative direction (it
overstates, never understates) and the harness caveat already says so; an eager check on the
liveness predicate would tighten it but costs a poll.

**What the Vita lane must add.** Today it has one slot and refuses a second module
(`vita/host/rel_service.c:81-82`); it must grow to the desktop's multi-slot model
(`BlueWakeRelSlot {owner, address, capacity}`, `BLUEWAKE_MAX_REL_SLOTS 512`, `main.c:114-129`), and its
scratch window (`0x81820000..0x81F80000`, 7,733,248 B — sized for raw images, largest raw 154,876 B)
must be re-sized for **compiled** images, largest 3,422,030 B. That window's size is the eviction
policy: with the 72 MiB heap the full set fits (431.7 MB), so a generous arena avoids thrash, but the
arena is the budget line the active-set measurement must size.

**Next measurement.** The gameplay active set, via the existing harness:
`python3 scripts/vita/relset/run.py --out … --label … --events <run.log> --scene title|outset` the
moment the host emits the pair, or
`python3 scripts/vita/relset/residency_report.py --run <fixture> --evidence build-f5/ww05 --map
build-f5/ww05/artifacts-*/ww05.map --out …` for the markers that exist today. The cheapest path to it
is the next lane-16 unit (the DSP task seam that stops the newest runs at `CC005002`), then the same
watched boot.

## 8. What this does NOT prove

- **No gameplay active set, no per-scene set.** One module at the boot frontier is not "the game uses
  one REL"; it is "the game had not started streaming yet".
- **No `[rel-load]`/`[rel-unload]` measurement.** The markers do not exist; every `run.py` verdict on
  real evidence is still `not measured`.
- **No physical Vita number.** Every byte here is a host-side measurement or a link-map sum.
- **No generator split, no built post-split artifact.** The 158.6 MB base is arithmetic on this
  build's PT_LOAD.
- **MEM1 non-zero counts are lower bounds**, not occupancy; and the two dumps precede the REL load.

## 9. Checks

- `python3 -m unittest discover -s vita/feasibility/relset` → **77 tests, all green** (44 existing,
  33 new: 11 footprint, 12 residency, 7 MEM1, 3 runner). The real-evidence tests read
  `build-f5/ww05` read-only and skip where it is absent.
- `scripts/audit_repo.sh` → AUDIT PASS; `scripts/check_attribution.py` → no bot/AI attribution.
- No run was started for this measurement; no `build-f5` file was written. Outputs (ignored):
  `build-f19/relresidency/newest-boot/`.
- New files only: `scripts/vita/relset/{rel_footprint,boot_residency,mem1_regions,residency_report}.py`,
  `vita/feasibility/relset/test_{rel_footprint,boot_residency,mem1_regions,residency_report}.py`, this
  document.
