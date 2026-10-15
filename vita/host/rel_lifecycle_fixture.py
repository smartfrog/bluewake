import struct

from test_ship_prolog import branch, fnv, instructions


def load(register, value):
    return [0x3C000000 | register << 21 | value >> 16,
            0x60000000 | register << 21 | register << 16 | value & 0xFFFF]


def sdk_program(raw, resident=False, fixed=True):
    word = lambda at: struct.unpack_from('>I', raw, at)[0]
    code = [0x3CC08000, 0x80E630CC, 0x2C070000, 0x4182000C, 0x90670004,
            0x48000008, 0x906630C8, 0x90E30008, 0x38A00000, 0x90A30004, 0x906630CC]

    def store(offset, value, relative=False, width=4):
        code.extend(load(5, value))
        if relative:
            code.append(0x7CA32A14)
        code.append({4: 0x90A30000, 2: 0xB0A30000, 1: 0x98A30000}[width] | offset)

    for field in (0x10, 0x24, 0x28, 0x48):
        store(field, word(field), True)
    store(0x33, 0, width=1)
    table = word(0x10)
    for index in range(1, word(0xC)):
        flags, size = word(table + index * 8), word(table + index * 8 + 4)
        if flags:
            store(table + index * 8, flags, True)
        elif size:
            code.append(0x90830000 | (table + index * 8))
            store(0x33, index, width=1)
    for index in range(3):
        section = raw[0x30 + index]
        if section:
            store(0x34 + index * 4, (word(table + section * 8) & ~1) + word(0x34 + index * 4), True)
    code.extend([0x3CC08000, 0x80C630D0, *load(5, word(0x14)), 0x7CA53214, 0x90A30014])
    imports = word(0x28)
    trimmed = False
    for at in range(imports, imports + word(0x2C), 8):
        module = word(at)
        cursor = word(at + 4)
        patch = 0
        while not resident or module in (0, word(0)):
            delta, kind, section, symbol = struct.unpack_from('>HBBI', raw, cursor)
            cursor += 8
            if kind == 203:
                break
            if kind == 202:
                patch = word(table + section * 8) & ~1
                continue
            patch += delta
            if kind == 201:
                continue
            value = symbol + ((word(table + section * 8) & ~1) if module else 0)
            code.extend(load(5, value))
            if module:
                code.append(0x7CA42A14 if word(table + section * 8) == 0 else 0x7CA32A14)
            if kind == 6:
                code.extend([*load(6, 0x8000), 0x7CA53214, 0x54A5843E])
            if kind == 10:
                code.extend(load(6, patch))
                code.extend([0x7CC33214, 0x7CA62850, 0x54A501BA,
                             *load(6, word(patch) & ~0x03FFFFFC), 0x7CA53378])
            code.append((0xB0A30000 if kind in (4, 6) else 0x90A30000) | patch)
        store(at + 4, word(at + 4), True)
        if fixed and not trimmed and module in (0, word(0)):
            store(0x2C, at - imports)
            trimmed = True
    code.append(0x4E800020)
    return code


def sdk_expected(raw, guest, previous, bss, next_image=0, fixed=True):
    expected = bytearray(raw)
    word = lambda at: struct.unpack_from('>I', raw, at)[0]
    put = lambda at, value: struct.pack_into('>I', expected, at, value)
    put(4, next_image)
    put(8, previous)
    for field in (0x10, 0x24, 0x28, 0x48):
        put(field, guest + word(field))
    expected[0x33] = 0
    table = word(0x10)
    for index in range(1, word(0xC)):
        flags, size = word(table + index * 8), word(table + index * 8 + 4)
        if flags:
            put(table + index * 8, guest + flags)
        elif size:
            put(table + index * 8, bss)
            expected[0x33] = index
    for index in range(3):
        section = raw[0x30 + index]
        if section:
            put(0x34 + index * 4, guest + (word(table + section * 8) & ~1) + word(0x34 + index * 4))
    imports = word(0x28)
    for at in range(imports, imports + word(0x2C), 8):
        module, cursor = word(at), word(at + 4)
        patch = 0
        while True:
            delta, kind, section, symbol = struct.unpack_from('>HBBI', raw, cursor)
            cursor += 8
            if kind == 203:
                break
            if kind == 202:
                patch = word(table + section * 8) & ~1
                continue
            patch += delta
            if kind == 201:
                continue
            value = symbol + ((guest + (word(table + section * 8) & ~1)
                               if word(table + section * 8) else bss) if module else 0)
            if kind == 10:
                put(patch, word(patch) & ~0x03FFFFFC | (value - guest - patch) & 0x03FFFFFC)
            elif kind in (4, 6):
                struct.pack_into('>H', expected, patch, ((value + 0x8000) >> 16 if kind == 6 else value) & 0xFFFF)
            else:
                assert kind == 1
                put(patch, value)
        put(at + 4, guest + word(at + 4))
    if fixed:
        put(0x2C, 0)
    return expected


def ikada_fixture(output, code):
    pc = 0xC15500F4
    words = [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x3C60C155, 0x38630174,
             branch(pc + 20, 0xC0241178), branch(pc + 24, 0xC02410B4),
             0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020] + [0x60000000] * 13
    ctor = [0x3C80C156, 0x388481A0, 0x80A40000, 0x38A50001, 0x90A40000, 0x4E800020,
            0x60000000, 0x60000000]
    code[pc], code[pc + 96] = words, ctor
    canonical = struct.pack('>32I', *(words + ctor))
    raw = bytearray(1024)
    for at, value in ((0, 60), (0xC, 21), (0x10, 0x4C), (0x1C, 3), (0x20, 532),
                      (0x24, 0x1A0), (0x28, 0x190), (0x2C, 16), (0x38, 44), (0x3C, 88),
                      (0x40, 8), (0x44, 8), (0x48, 0x190)):
        struct.pack_into('>I', raw, at, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    sections = [(1, pc, 0xF4, 128), (2, 0xC1550174, 0x174, 8),
                (3, 0xC155017C, 0x17C, 8), (4, 0xC1550184, 0x184, 4),
                (5, 0xC1550188, 0x188, 4), (6, 0xC15581A0, 0, 532)]
    for index, _, offset, size in sections:
        struct.pack_into('>II', raw, 0x4C + index * 8, offset | int(index == 1), size)
    raw[0xF4:0x174] = canonical
    for at, value in ((0x100, 0x3C600000), (0x104, 0x38630000), (0x108, 0x48000001),
                      (0x10C, 0x48000001), (0x154, 0x3C800000), (0x158, 0x38840000)):
        struct.pack_into('>I', raw, at, value)
    struct.pack_into('>4I', raw, 0x190, 60, 0x1A0, 0, 0x200)
    entries = ((0, 202, 2, 0), (0, 1, 1, 96), (0, 202, 1, 0), (14, 6, 2, 0),
               (4, 4, 2, 0), (80, 6, 6, 0), (4, 4, 6, 0), (0, 203, 0, 0))
    for at, entry in enumerate(entries):
        struct.pack_into('>HBBI', raw, 0x1A0 + at * 8, *entry)
    for at, entry in enumerate(((0, 202, 1, 0), (20, 10, 0, 0x80241178),
                                 (4, 10, 0, 0x802410B4), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x200 + at * 8, *entry)
    (output / 'lifecycle_ikada.rel').write_bytes(raw)
    lines = [f'static const u8 ikada_raw[] = {{{",".join(map(str, raw))}}};',
             f'static const u8 ikada_code[] = {{{",".join(map(str, canonical))}}};',
             'static const u8 ikada_ctors[] = {0xC1,0x55,0x01,0x54,0,0,0,0};',
             'static const u8 ikada_dtors[8];', 'static const u8 ikada_data[4];',
             'static const StaticRecompRelSection ikada_sections[] = {']
    lines.extend(f'{{60u,{index}u,0x{address:08X}u,{size}u}},' for index, address, _, size in sections)
    lines += ['};', 'static const BlueWakeRelData ikada_canonical[] = {']
    for index, address, _, size in sections[:5]:
        array = {1: 'ikada_code', 2: 'ikada_ctors', 3: 'ikada_dtors', 4: 'ikada_data', 5: 'ikada_data'}[index]
        lines.append(f'{{60u,{index}u,0x{address:08X}u,{size}u,{array}}},')
    lines.extend(['};', 'static const u64 ikada_hashes[] = {',
                  f'0x{fnv(canonical[:96]):016X}ull,0x{fnv(canonical[96:]):016X}ull', '};'])
    (output / 'ikada_fixture.inc').write_text('\n'.join(lines) + '\n')
    return raw


def fixture(output):
    code = instructions()
    code[0x8000B000] = code[0x80007000]
    code[0x80240EA0] = [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x93E1000C, 0x7C7F1B78,
                       branch(0x80240EB4, 0x80240744), 0x807F0010,
                       0x809F0014, 0x60000000, 0x60000000, branch(0x80240EC8, 0x80305448),
                       0x60000000, 0x60000000, 0x60000000, 0x60000000, 0x81830034, 0x7D8903A6, 0x4E800421,
                       0x83E1000C, 0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020]
    title_pc = 0xC1E000DC
    title_words = [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x3C60C1E0, 0x38631F28,
                   branch(title_pc + 20, 0xC0241178), branch(title_pc + 24, 0xC02410B4),
                   0x80010014, 0x7C0803A6, 0x38210010, 0x4E800020] + [0x60000000] * 13
    code[title_pc] = title_words
    raw = bytearray(512)
    for at, value in ((0, 400), (0xC, 8), (0x10, 0x4C), (0x1C, 3), (0x24, 0x140),
                      (0x28, 0x130), (0x2C, 16), (0x38, 44), (0x3C, 88), (0x40, 8), (0x48, 0x13C)):
        struct.pack_into('>I', raw, at, value)
    raw[0x30:0x33] = bytes([1, 1, 1])
    struct.pack_into('>II', raw, 0x54, 0xA1, 96)
    struct.pack_into('>II', raw, 0x5C, 0x100, 4)
    canonical = struct.pack('>24I', *title_words)
    raw[0xA0:0x100] = canonical
    for at, value in ((0xAC, 0x3C600000), (0xB0, 0x38630000), (0xB4, 0x48000001), (0xB8, 0x48000001)):
        struct.pack_into('>I', raw, at, value)
    struct.pack_into('>4I', raw, 0x130, 400, 0x140, 0, 0x170)
    for index, entry in enumerate(((0, 202, 1, 0), (14, 6, 2, 0), (4, 4, 2, 0), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x140 + index * 8, *entry)
    for index, entry in enumerate(((0, 202, 1, 0), (20, 10, 0, 0x80241178),
                                   (4, 10, 0, 0x802410B4), (0, 203, 0, 0))):
        struct.pack_into('>HBBI', raw, 0x170 + index * 8, *entry)
    (output / 'lifecycle_title.rel').write_bytes(raw)
    code[0x80305448] = [0x80A30000, 0x2C0500A8, 0x41820008,
                        branch(0x80305454, 0x80009000, False), branch(0x80305458, 0x80008000, False)]
    for start, image in ((0x80008000, (output / 'ship.rel').read_bytes()), (0x80009000, raw)):
        sdk = sdk_program(image)
        for at in range(0, len(sdk), 60):
            code[start + at * 4] = sdk[at:at + 60]
    walker = code[0xC0241178]
    walker[:0] = [0x3CC08000, 0x80A65000, 0x38A50001, 0x90A65000]
    walker[9] = branch(0xC024119C, 0xC02411A8, False)
    walker[18] = branch(0xC02411C0, 0xC02411A8, False)
    from rel_profile_block_fixture import fixture as profile_fixture
    profile_code, profile_raw = profile_fixture(output)
    small = bytearray(profile_raw[:256])
    for field in (0x24, 0x28, 0x2C):
        struct.pack_into('>I', small, field, 0)
    (output / 'resident_profile_small.rel').write_bytes(small)
    for address in (0xC1F800D4, 0x80240C9C, 0x80240D2C, 0x80240D6C,
                    0x80241044, 0x802B07EC, 0x802B0704):
        code[address] = profile_code[address]
    code[0x80240D6C][-1] = branch(0x80240D80, 0x80240B00, False)
    code[0x80240B00] = [branch(0x80240B00, 0x80241044), 0x807F0010,
                        branch(0x80240B08, 0x80240EDC, False)]
    for start, target in ((0x80240B40, 0x80240C9C), (0x80240B80, 0x80240D00)):
        code[start] = [0x9421FFF0, 0x7C0802A6, 0x90010014, 0x93E1000C, 0x7C7F1B78,
                       branch(start + 20, 0x80240744), branch(start + 24, target, False)]
    code[0x80240D00] = [0x807F0010, 0x809F0014, branch(0x80240D08, 0x80305424),
                       branch(0x80240D0C, 0x80240EDC, False)]
    code[0x80305424] = [branch(0x80305424, 0x8000B800, False)]
    sdk = sdk_program((output / 'ship.rel').read_bytes(), fixed=False)
    for at in range(0, len(sdk), 60):
        code[0x8000B800 + at * 4] = sdk[at:at + 60]
    ikada = ikada_fixture(output, code)
    code[0x80305424] = [0x80A30000, 0x2C05003C, 0x41820008,
                       branch(0x80305430, 0x8000B800, False), branch(0x80305434, 0x8000D000, False)]
    sdk = sdk_program(ikada, fixed=False)
    for at in range(0, len(sdk), 60):
        code[0x8000D000 + at * 4] = sdk[at:at + 60]
    sdk = sdk_program(raw)
    sdk[-1:-1] = [0x38600001]
    for at in range(0, len(sdk), 60):
        code[0x8000C000 + at * 4] = sdk[at:at + 60]
    code[0x80305448] = [0x80A30000, 0x2C050001, 0x40820008,
                       branch(0x80305454, 0x8000A000, False), 0x2C0500A8, 0x41820008,
                        branch(0x80305460, 0x80305500, False), branch(0x80305464, 0x80008000, False)]
    code[0x80305500] = [0x7CA802A6, *load(6, 0x80240D4C), 0x7C053000, 0x41820008,
                       branch(0x80305514, 0x80009000, False), branch(0x80305518, 0x8000C000, False)]
    sdk = sdk_program(small, resident=True)
    sdk[-1:-1] = [0x38600001]
    for at in range(0, len(sdk), 60):
        code[0x8000A000 + at * 4] = sdk[at:at + 60]
    emitter = bytearray()
    for address, words in code.items():
        emitter.extend(struct.pack('>II', address, len(words)))
        emitter.extend(struct.pack(f'>{len(words)}I', *words))
    (output / 'lifecycle.instructions').write_bytes(emitter)
    lines = [f'static const u64 lifecycle_profile_hash=0x{fnv(small[0x80:0xAC]):016X}ull;',
             f'static const u8 lifecycle_title_raw[] = {{{",".join(map(str, raw))}}};',
             f'static const u8 lifecycle_title_code[] = {{{",".join(map(str, canonical))}}};',
             'static const u8 lifecycle_title_ctors[4];',
             'static const BlueWakeRelData lifecycle_title_canonical[] = {',
             '{400u,1u,0xC1E000DCu,96u,lifecycle_title_code},{400u,2u,0xC1E01F28u,4u,lifecycle_title_ctors}};',
             f'static const u64 lifecycle_title_hash = 0x{fnv(canonical):016X}ull;',
             'static const StaticRecompRelSection lifecycle_title_sections[] = {',
             '{400u,1u,0xC1E000DCu,96u},{400u,2u,0xC1E01F28u,4u}};']
    ship = (output / 'ship.rel').read_bytes()
    for name, value in (('ship', sdk_expected(ship, 0x81820100, 0x81820000, 0x807FF620)),
                        ('ship_nonfixed', sdk_expected(ship, 0x81820100, 0x81820000, 0x807FF620, fixed=False)),
                        ('ship_nonfixed_with_title', sdk_expected(ship, 0x81820100, 0x81820000, 0x807FF620, 0x818325E0, fixed=False)),
                        ('ship_with_title', sdk_expected(ship, 0x81820100, 0x81820000, 0x807FF620, 0x818325E0)),
                         ('title', sdk_expected(raw, 0x818325E0, 0x81820100, 0)), ('title_pristine', raw),
                         ('title_with_ikada', sdk_expected(raw, 0x818325E0, 0x81820100, 0, 0x818327E0)),
                         ('ikada', sdk_expected(ikada, 0x818327E0, 0x818325E0, 0x807FE000, fixed=False))):
        lines.append(f'static const u64 lifecycle_expected_{name}_hash=0x{fnv(value):016X}ull;')
    lines.append('static const StaticRecompRange lifecycle_ranges[] = { {0x80240744u,0x80240748u},')
    lines.extend(f'{{0x{address & ~0x40000000 if address < 0xC1000000 else address:08X}u,'
                 f'0x{(address & ~0x40000000 if address < 0xC1000000 else address) + 4 * len(words):08X}u}},'
                 for address, words in code.items())
    lines.append('};')
    lines.append('static const struct {u32 start; u32 end; void (*run)(CPUState*);} lifecycle_chunks[] = {')
    lines.extend(f'{{0x{address & ~0x40000000:08X}u,0x{(address & ~0x40000000) + 4 * len(words):08X}u,func_{address:08X}}},'
                 for address, words in code.items())
    lines.append('};')
    lines.append('static void lifecycle_dol_text(CPUState* cpu, DolLayout* layout) {')
    dol = [(address, words) for address, words in code.items() if 0x80000000 <= address < 0xC0000000]
    for address, words in dol:
        lines.extend(f'mem_write32(cpu,0x{address + index * 4:08X}u,0x{value:08X}u);' for index, value in enumerate(words))
    lines.extend(['layout->text_address[0]=0x80007000u; layout->text_size[0]=0x3000u;', '}'])
    (output / 'rel_lifecycle_fixture.inc').write_text('\n'.join(lines) + '\n')
