# Selected ship/title producer certificates

Unit 1 only. The host remains ship-only; title initialization is **unadmitted**.
No SDK-list, pending-call, renderer, dependency pin or runtime change is included.

The ordinary generator still emits the byte-identical legacy ship certificate.
`--selected-rel-provenance MANIFEST --title-rel-sha256 SHA256` opts into producer
certificates for exactly modules 168 and 400. This is not a runtime enable switch.
Independent review of the producer evidence and new composite digest is required
before integration or admission.

`staticrecomp_get_rel_init_contracts_v1()` returns a versioned set with one common
raw DOL identity and two `BlueWakeRelInitContractV1` entries. Each entry references
the existing Lifecycle V2 metadata and `BlueWakeRelData` section format, original
REL bytes, constructor table location and checked walker successor. The legacy
ship getter references the same ship byte arrays, sections and build identity.
Title's canonical data references the normal relocated exports; only its captured
executable bytes need a new canonical array. Old generated modules return no set.

Header selectors/offsets are checked against each selected module's own recorded
layout and complete translated coverage. Constructor tables are section-derived;
title has a four-byte empty terminator, ship has an executable target and terminator.
Each selected successor must follow its captured `bl` to the translated constructor
walker. There is no universal prolog/epilog spacing rule or bulk module admission.
Replay retains only relocation types 1/4/6/10 and self/DOL imports.

## Provenance manifest

Private JSON, never committed with private captures or generated sources:

```text
version: 1
producer_identity: SHA-256 of the independently reviewable source/tool/input proof
dol_sha256: full original DOL hash
modules: exactly "168" and "400", each containing:
  raw_sha256: original disc member hash
  processed_rel_sha256: actual processed producer input hash
  capture: actual emitter-input sidecar path (relative to manifest or absolute)
  capture_sha256: complete sidecar hash
  translation_sha256: every module-output relative path and content hash
```

The generator checks these bindings against its actual inputs and all selected
translation files, then compares **all** captured executable bytes and metadata
with its bounded replay. A manifest hash alone is not a proof of capture: review
must establish the same-array emitter hook and unchanged source reproduction.
`SHIP168\0` and diagnostic `TITLE400` sidecars use the existing capture format:
eight-byte magic, big-endian count, then index/file offset/linked address/size and
complete section bytes. Successful producer exit is required before consumption.

Ship comes from original `RELS.arc`; title comes from original loose FST
`rels/d_a_title.rel`, through the existing disc/FST and Yaz0 readers. Original
title SHA-256 is `1710650607e2142bf3889c5f5fc666424a9af9bfa7b37c0f400598729f6141ea`.
The processed alignment word is 4 versus original 8; every other byte must match.
Failures invalidate the generation receipt. Default generation remains unchanged;
selected generation deliberately changes title's raw chunk hash to its captured
canonical hash and adds certificate metadata/data.

Synthetic checks: `python3 -B tests/test_composite.py` and
`python3 -B tests/test_selected_rel_contracts.py`. Private reproduction evidence is
kept separately from old launch receipts. Certificates prove neither lifecycle
execution, a ship/title scene, physical feasibility nor 30 FPS.
