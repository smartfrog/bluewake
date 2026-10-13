# Diagnostic byte completion, v2

This is a per-artifact completion protocol, not a filesystem transaction or a gameplay change.
Historical captures and build receipts without a publication capability remain v1. They are not
retroactively certified by v2.

## Producer and reader contract

The producer exclusively creates a read/write candidate using `open(O_CREAT | O_EXCL)`, then
`fdopen` on that POSIX descriptor. Linux also uses `O_NOFOLLOW | O_CLOEXEC`. PNG encoding, MEM1
writing, report writing and hashing retain that one stream. After checked writes and `fflush`
and `ferror`, SHA-256 reads that same stream in 32 KiB chunks, before checked `fclose`. It never
reopens the candidate pathname to hash it and does not clone MEM1. SHA-256 is the existing pinned
RecompCore mbedTLS implementation, compiled with a SHA-256-only configuration.

Only then does the producer exclusively create an empty seal:

`<logical-name>.v2.<32-lowercase-hex-run-token>.<64-lowercase-hex-SHA256>.<byte-extent>.seal`

No payload or seal is renamed, linked, unlinked or truncated. The normal stored payload is
`<logical-name>.partial`; reports instead retain `ww05.partial`, `arm.partial`, `mem.partial`
or `fp.partial`. Logs and dispatch traces keep their original names and descriptors. Frame and
MEM1 metadata and replay manifests name the actual `.partial` files, not imaginary final files.
There is no global manifest and no atomic multi-file completion claim. Metadata and every payload
it references must each have a matching seal.

Readers select the version from the selected build receipt, not from the presence of a final file,
an `END` line or a filename extension. For v2 they require exactly one well-formed, empty, privately
owned, single-link seal with the expected token. A bounded no-follow descriptor read supplies one
byte buffer for extent/hash validation and parsing. PNG dimensions, pixels, RGB correspondence and
image hashes use those same bytes, not another pathname read. Existing source/package/token,
command, draw, time, frame and report contracts still apply. Missing, malformed, multiple, wrong-token
or wrong-hash seals refuse certification; v2 never falls back to v1. Historical reading is explicitly
available with `publication=1`. There is no new live gallery/discovery path.

## What completion does not mean

- A path replacement is never deleted. A seal still describes the producer's original descriptor
  bytes; different replacement bytes are rejected. It does **not** preserve or certify inode identity.
- Same-UID hostile authenticity is not claimed. Such a writer can forge seals or change bytes.
- Power-loss durability is not claimed. There is no `fsync` or durable-directory transaction.
- Byte completion is distinct from producer/process disposition. An empty seal can already exist
  when marker `close` fails: the producer returns failure, even though its sealed bytes may validate.
  Report validation cannot turn that failure, a timeout or an owned-process termination into success.
  An emulator exit is not evidence of the guest application's return status; the WW05 receipt leaves
  `producer_success` unknown and records process disposition separately.
- Unsealed candidates are retained as evidence, not certified captures. Previously sealed individual
  artifacts do not certify an unfinished multi-artifact capture or a successful run.

## Native gate (not satisfied by linking)

The inspected SDK's `arm-vita-eabi/lib/libc.a` has SHA-256
`c7d82130310a81b16444715d6cd884f49bb92d2c8b0494634da90a2411b10d61`.
Disassembly of `lib_a-syscalls.o:_open_r` shows path normalization, `_fcntl2sony`, `sceIoOpen`,
and `__vita_acquire_descriptor`/`__vita_fdmap`. Thus the result of newlib `open` is a mapped POSIX
descriptor, not a native `SceUID` passed to `fdopen`. `_fcntl2sony` maps newlib `O_CREAT=0x200`
and `O_EXCL=0x800` to the corresponding SCE flags; no truncation flag is requested here.

This proves the SDK flag/descriptor mapping only. It does **not** establish the exact runtime's
atomic exclusive-create or final-symlink behavior. `O_NOFOLLOW` in the SDK header is not proof of
an implementation and is not used to assert a Vita guarantee. Native acceptance remains blocked
until the intended runtime's collision behavior is sourced or validated and reviewed:

- **Linux host:** deterministic collision, replacement, byte-binding and error tests validate the
  host protocol. Vita-stub logging tests are host tests, not native-platform evidence.
- **Vita3K:** not run for this change. The authorized binary's source mapping remains unverified;
  the unrelated local source checkout is not proof of its behavior.
- **Physical Vita:** not run and no final-symlink/exclusive-create safety claim is made.
- **ARM compile/link:** game-free ABI/link evidence only, including retained-stream logging.

The game-free `capture_io_link --collisions <existing-file> <final-symlink>` probe is prepared for
later, separately authorized post-review execution. Supply only disposable, reviewer-owned fixtures,
including an existing-target and a dangling-target symlink in separate attempts where supported.
Record exact binary/SDK/runtime/configuration identities and fixture bytes/entry types before and
after. Require refusal (`EEXIST` or `ELOOP`), unchanged existing bytes and symlinks, and no created
dangling target. An unsupported fixture type or wrong error is not a pass. The probe must not be
run against player data. It has not been run on Vita3K or Vita here.
