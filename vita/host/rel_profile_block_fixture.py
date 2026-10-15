import struct

from rel_lifecycle_fixture import load, sdk_program
from test_ship_prolog import branch, fnv, instructions


def fixture(output):
    code = instructions()
    del code[0x80240EA0]
    prolog = [0x9421FFF0, 0x7C0802A6, 0x90010014, *load(3, 0xC1F80170),
              branch(0xC1F800E8, 0xC0241178), 0x38600001,
              0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020]
    code[0xC1F800D4] = prolog
    raw = bytearray(1024)
    for at, value in ((0, 1), (0xC, 5), (0x10, 0x4C), (0x1C, 3), (0x24, 0x180),
                      (0x28, 0xD0), (0x2C, 24), (0x40, 8), (0x44, 1), (0x48, 0xC4)):
        struct.pack_into('>I', raw, at, value)
    raw[0x30] = 1
    for index, offset, size in ((1, 0x81, 44), (2, 0xB0, 4), (4, 0xB4, 16)):
        struct.pack_into('>II', raw, 0x4C + index * 8, offset, size)
    raw[0x80:0xAC] = struct.pack('>11I', *prolog)
    struct.pack_into('>3I', raw, 0xB4, 0x11223344, 0x55667788, 0xAABBCCDD)
    for at, module, stream, patch, section, symbol in (
            (0xD0, 168, 0x180, 8, 2, 0), (0xD8, 1, 0x1A0, 0, 4, 4),
            (0xE0, 0, 0x1C0, 4, 0, 0x80001000)):
        struct.pack_into('>II', raw, at, module, stream)
        for index, entry in enumerate(((0, 202, 4, 0), (patch, 1, section, symbol), (0, 203, 0, 0))):
            struct.pack_into('>HBBI', raw, stream + index * 8, *entry)
    (output / 'resident_profile.rel').write_bytes(raw)
    code[0x80240C9C] = [0x807F0010, 0x8003001C, 0x28000003, 0x41800250,
                        0x83A30048, 0x381D001F, 0x541C0034, 0x7FC3E214,
                        0x38800000, branch(0x80240CC0, 0x802B07EC), 0x2C030000,
                        0x40800064, branch(0x80240CCC, 0x80001004, False)]
    code[0x80240D2C] = [0x80BF0010, 0x80050020, 0x7C9C0214, 0x7C041840,
                        0x40800008, 0x7CA32B78, 0x7FC4F378, branch(0x80240D48, 0x80305448),
                        0x2C030000, 0x418200A8, branch(0x80240D54, 0x80240D6C, False)]
    code[0x80240D6C] = [0x807F0010, 0x80030020, 0x7C9C0214, 0x38A00000,
                        branch(0x80240D7C, 0x802B0704), branch(0x80240D80, 0x80240EB0, False)]
    code[0x80240EB0] = [branch(0x80240EB0, 0x80241044), *([0x60000000] * 9),
                        0x807F0010, 0x81830034, 0x7D8903A6, 0x4E800421,
                        0x907F0018, 0x38600001, branch(0x80240EF0, 0x80001004, False)]
    code[0x80241044] = [0x7C0802A6, 0x90010004, 0x807F0010, 0x38800000,
                        branch(0x80241054, 0x802B07EC), 0x80010004, 0x7C0803A6, 0x4E800020]
    code[0x802B07EC] = [0x3860FFFF, 0x4E800020]
    code[0x802B0704] = [0x3860FFFF, 0x4E800020]
    code[0x80305448] = [branch(0x80305448, 0x8000A000, False)]
    code[0x80305424] = [branch(0x80305424, 0x80008000, False)]
    for start, image, resident, fixed in ((0x8000A000, raw, True, True),
            (0x80008000, (output / 'ship.rel').read_bytes(), False, False)):
        sdk = sdk_program(image, resident=resident, fixed=fixed)
        sdk[-1:-1] = [0x38600001]
        for at in range(0, len(sdk), 60):
            code[start + at * 4] = sdk[at:at + 60]
    emitter = bytearray()
    for address, words in code.items():
        emitter.extend(struct.pack('>II', address, len(words)))
        emitter.extend(struct.pack(f'>{len(words)}I', *words))
    (output / 'profile.instructions').write_bytes(emitter)
    lines = [f'static const u64 profile_code_hash=0x{fnv(raw[0x80:0xAC]):016X}ull;',
             'static const StaticRecompRelSection resident_sections[] = {',
             '{1u,1u,0xC1F800D4u,44u},{1u,2u,0xC1F80170u,4u},{1u,4u,0xC1F80178u,16u}};',
             'static const u8 resident_ctors[4];',
             'static const u8 resident_data[16]={0};',
             'static const StaticRecompRange profile_ranges[] = {']
    lines.extend(f'{{0x{address & ~0x40000000 if address < 0xC1000000 else address:08X}u,'
                 f'0x{(address & ~0x40000000 if address < 0xC1000000 else address) + 4 * len(words):08X}u}},'
                 for address, words in code.items())
    lines.extend(['};', 'static const struct {u32 start;u32 end;void (*run)(CPUState*);} profile_chunks[] = {'])
    lines.extend(f'{{0x{address & ~0x40000000:08X}u,0x{(address & ~0x40000000) + 4 * len(words):08X}u,func_{address:08X}}},'
                 for address, words in code.items())
    lines.append('};')
    (output / 'rel_profile_block_fixture.inc').write_text('\n'.join(lines) + '\n')
    return code, raw
