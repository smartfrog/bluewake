# WW85: publish pristine lifecycle offsets for the SDK linker

## Diagnosis before the fix

Started clean at `8c6f92e138d8bfc6122c9f5f8f42ab39f51bf6dc` on
`vita-integration`; isolated branch `vita-ww85`, worktree
`/home/fred/projects/zeldaWWHDRecomp-ww85`. Remote branches were checked;
`gh pr list` is unavailable because `gh` is absent. Nothing was pushed.
Private receipts are under `/tmp/opencode/ww85/`.

**The host is at fault.** It publishes absolute lifecycle addresses into REL
header fields that the game's SDK linker still treats as section-relative
offsets. The loader does not add a base at its indirect call.

The private DOL (`build-f5/ww05/private/main.dol`, SHA-256
`680e347ef8c77739165908cc6a375e5af714f354494daca17a1c34e53c275bec`)
was decoded using a copy of WW73's `ppc_dis.py`, adapted for the SPR field
order and indirect branches. `loader.disasm` proves this call sequence:

| PC | Instruction | Meaning |
| --- | --- | --- |
| `80240ED8` | `lwz r3,16(r31)` | Load module base from its controller's `+0x10` publication |
| `80240EDC` | `lwz r12,52(r3)` | Load **module header `+0x34`**, not a GOT slot or section entry |
| `80240EE0` | `mtctr r12` | Copy that word directly into CTR |
| `80240EE4` | `bctrl` | Call CTR; LR becomes `80240EE8` |
| `80240EE8` | `stw r3,24(r31)` | Store the prolog's result in the controller |

At the base commit, `vita/host/rel_service.c:292-310` copies the verified pristine
REL and records its hash. `rel_link.c:355-367` initializes canonical linked data
and BSS, not the scratch header. `rel_link.c:369` publishes the separate
controller's `+0x10`; lines 377-379 then call
`ship_contract.c:54-63`, which writes absolute addresses into scratch
`+0x34/+0x38/+0x3C` and hashes that mutated image.

`disc_service.c:181-190` intercepts **do_load**, not do_link. The host's linked
flag is not an SDK input and does not suppress the subsequent translated SDK
linker. WW79's private `ANALYSIS.md` documents that call ordering.

The DOL's SDK stores, independently decoded in `sdk-fixup.disasm`, are:

- `803051F8/FC/80305200`: header `+0x10` becomes module + section-table offset.
- `80305248/54/58`: each nonzero section entry gains the module base, retaining
  its executable flag. `80305270` writes the separately supplied BSS base.
- `803052B4`: read prolog section byte at header `+0x30`.
- `803052C0/C4/CC/D0`: index the already-rebased table by section * 8 and clear
  its executable bit to get the absolute section start.
- `803052C8/D4/D8`: load header `+0x34`, add that absolute section start, and
  store the sum back to `+0x34`.
- `80305300` and `80305328`: analogous epilog/unresolved writebacks.

The exact pristine ship REL SHA-256 is
`945a6493a7f9d353657b07048b26e63c8fd53ca147df9d1b5378c44f0ef2280e`.
All three lifecycle section indices are 1. The section's file offset is `F4`,
but the **prolog's section-relative header offset is zero**, not `F4`:

| Field | Pristine | Host publication at base | SDK result at base |
| --- | --- | --- | --- |
| prolog `+0x34` | `00000000` | `81F900F4` | `03F201E8` |
| epilog `+0x38` | `0000002C` | `81F90120` | `03F20214` |
| unresolved `+0x3C` | `00000058` | `81F9014C` | `03F20240` |

For the prolog, the host first writes `M + F4 + 0 = 81F900F4`; SDK store
`80305258` independently makes section 1's flags `81F900F5`. SDK `803052D4`
then adds `81F900F4 + (81F900F5 & ~1)`, producing `03F201E8` modulo 2^32.
That matches WW84's recorded target, `r3=81F90000`, `r12=03F201E8`, and
`lr=80240EE8`. Writer attribution is static disassembly plus the inspected
host path; WW84 did not separately sample live header words or writer PCs.

With pristine offsets preserved, one SDK fixup produces `81F900F4`.
The unchanged owned-executable resolver maps that staging address to linked
`C1B100F4` (canonical `81B100F4`), the standing approved prolog contract.

## Bounded repair plan

Remove the host's three lifecycle writes; leave all lifecycle offsets pristine
until the SDK rebases them. Keep exact raw identity validation, publication's
whole-image hash checkpoint, WW81 section validation, WW83 full-image ICBI,
and every prolog/constructor/return gate fail-closed. Do not refresh the hash
after simulated or real SDK mutations. The ship option remains off by default.

Tests and checks will be recorded below after a tests-first red/green cycle.
No emulator run is authorized in this round. Independent review, integration,
and the real trial remain separate coordinator gates.
