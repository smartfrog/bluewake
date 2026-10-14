import hashlib
import json
import struct
from pathlib import PurePosixPath


SHIP_EXEC = (1, 244, 0xC1B100F4, 55804)
SHIP_LIFECYCLE = (0xC1B100F4, 0xC1B10120, 0xC1B1014C)
SHIP_TABLES = ((2, 0xC1B1DAF0), (3, 0xC1B1DAF8))


def read_disc_member(path):
    import prepare
    from wwhd.formats import MAX_FILE, rarc, yaz0

    with open(path, 'rb') as stream:
        header = prepare.validate_disc_header(stream)
        if header['revision'] != 0:
            raise ValueError('ship identity requires GZLE01 revision 0')
        files = prepare.read_fst(stream, header['fst_offset'], header['fst_size'])
        archives = [entry for entry in files if not entry['is_dir'] and entry['path'] == 'RELS.arc']
        if len(archives) != 1 or not 0 < archives[0]['size'] <= MAX_FILE:
            raise ValueError('ship identity requires exactly one bounded RELS.arc')
        stream.seek(archives[0]['offset'])
        archive = stream.read(archives[0]['size'])
        if len(archive) != archives[0]['size']:
            raise ValueError('ship identity RELS.arc is truncated')
    members = [(name, data) for name, data in rarc(archive)
               if PurePosixPath(name).name == 'd_a_ship.rel']
    if len(members) != 1:
        raise ValueError('ship identity requires exactly one disc archive member d_a_ship.rel')
    name, data = members[0]
    return yaz0(data), {'archive': 'RELS.arc', 'member': name}


def verify_layout(raw, sections, chunks):
    if not 0x4C <= len(raw) <= 0x100000 or struct.unpack_from('>I', raw)[0] != 168:
        raise ValueError('ship contract requires a bounded raw module 168')
    count, table = struct.unpack_from('>II', raw, 0xC)
    if count != len(sections) or not 4 <= count <= 256 or table + count * 8 > len(raw):
        raise ValueError('ship contract raw section table is invalid')
    for index, section in enumerate(sections):
        flags, size = struct.unpack_from('>II', raw, table + index * 8)
        if (section['index'] != index or section['offset'] != flags & ~1 or
                section['size'] != size or section['executable'] != bool(flags & 1) or
                (size and section['offset'] and section['offset'] + size > len(raw))):
            raise ValueError('ship contract section metadata disagrees with raw REL bytes')
    bss_size = sum(section['size'] for section in sections if not section['offset'])
    if bss_size != struct.unpack_from('>I', raw, 0x20)[0]:
        raise ValueError('ship contract BSS size differs from its raw header')
    executable = [section for section in sections if section['executable'] and section['size']]
    if len(executable) != 1 or tuple(executable[0][key] for key in (
            'index', 'offset', 'linked_start', 'size')) != SHIP_EXEC:
        raise ValueError('ship contract executable section offset/address/size mismatch')
    for index, (name, expected) in enumerate(zip(('prolog', 'epilog', 'unresolved'), SHIP_LIFECYCLE)):
        section = raw[0x30 + index]
        offset = struct.unpack_from('>I', raw, 0x34 + index * 4)[0]
        if section != 1 or sections[section]['linked_start'] + offset != expected:
            raise ValueError(f'ship contract {name} address mismatch')
    for (index, expected), name in zip(SHIP_TABLES, ('ctors', 'dtors')):
        section = sections[index]
        if (section['executable'] or section['size'] != 8 or not section['offset'] or
                section['linked_start'] != expected or
                section['offset'] != expected - (SHIP_EXEC[2] - SHIP_EXEC[1])):
            raise ValueError(f'ship contract {name} address/size mismatch')
    cursor = SHIP_EXEC[2]
    for start, end in chunks:
        if start != cursor or end <= start or end > SHIP_EXEC[2] + SHIP_EXEC[3] or (end - start) & 3:
            raise ValueError('ship contract requires complete translated executable chunks')
        cursor = end
    if cursor != SHIP_EXEC[2] + SHIP_EXEC[3]:
        raise ValueError('ship contract requires complete translated executable chunks')


def relocate_ship(raw, sections, dol_ranges):
    """Replay only WW63's admitted relocation semantics from ordinary REL bytes."""
    result = {section['index']: bytearray(raw[section['offset']:section['offset'] + section['size']])
              for section in sections if section['size'] and section['offset']}
    imports, size = struct.unpack_from('>II', raw, 0x28)
    if size == 0 or size % 8 or size // 8 > 32 or imports + size > len(raw):
        raise ValueError('ship contract relocation import table is invalid')
    relocation_start = struct.unpack_from('>I', raw, 0x24)[0]
    for at in range(imports, imports + size, 8):
        module, cursor = struct.unpack_from('>II', raw, at)
        if module not in (0, 168):
            raise ValueError('ship contract imports an unsupported module')
        if cursor < relocation_start or cursor + 8 > len(raw):
            raise ValueError('ship contract import points outside relocations')
        patch = None
        offset = 0
        while cursor + 8 <= len(raw):
            delta, kind, index, symbol = struct.unpack_from('>HBBI', raw, cursor)
            cursor += 8
            if kind == 203:
                break
            if kind == 202:
                if index >= len(sections) or not sections[index]['size']:
                    raise ValueError('ship contract relocation selects an absent patch section')
                patch = sections[index]
                offset = 0
                continue
            offset += delta
            if patch is None or offset > patch['size']:
                raise ValueError('ship contract relocation patch offset exceeds its section')
            if kind == 201:
                continue
            width = 2 if kind in (4, 6) else 4
            if (kind not in (1, 4, 6, 10) or offset + width > patch['size'] or
                    offset % width or not patch['offset']):
                raise ValueError('ship contract relocation type, storage or alignment is unsupported')
            destination = None
            if module == 168:
                if index >= len(sections):
                    raise ValueError('ship contract relocation target section is absent')
                destination = sections[index]
                if not destination['linked_start'] or symbol >= destination['size']:
                    raise ValueError('ship contract relocation target exceeds its section')
                address = destination['linked_start'] + symbol
            else:
                if not 0x80000000 <= symbol < 0x81800000:
                    raise ValueError('ship contract relocation DOL target is outside MEM1')
                address = symbol
            value = address
            if kind == 10:
                coverage = dol_ranges
                if destination is not None:
                    coverage = ([(destination['linked_start'], destination['linked_start'] + destination['size'])]
                                if destination['executable'] else [])
                if address & 3 or not any(start <= address and address + 4 <= end for start, end in coverage):
                    raise ValueError('ship contract branch target lacks translated executable coverage')
                patch_address = patch['linked_start'] + offset
                displacement = ((address | (patch_address & 0x40000000)) - patch_address) & 0xFFFFFFFF
                if displacement & 0x80000000:
                    displacement -= 0x100000000
                word = struct.unpack_from('>I', raw, patch['offset'] + offset)[0]
                if (not -0x2000000 <= displacement <= 0x1FFFFFC or
                        displacement & 3 or word >> 26 != 18 or word & 2):
                    raise ValueError('ship contract branch displacement or instruction is not representable')
                value = (word & ~0x03FFFFFC) | (displacement & 0x03FFFFFC)
            elif kind == 6:
                value = (value + 0x8000) >> 16
            struct.pack_into('>H' if width == 2 else '>I', result[patch['index']], offset,
                             value & ((1 << (width * 8)) - 1))
        else:
            raise ValueError('ship contract relocation stream has no bounded end')
    return {index: bytes(data) for index, data in result.items()}


def normal_contract(raw_dol, processed_ship, sections, chunks, dol_ranges, identity_ship):
    verify_layout(processed_ship, sections, chunks)
    verify_layout(identity_ship, sections, chunks)
    if (len(identity_ship) != len(processed_ship) or identity_ship[:0x40] != processed_ship[:0x40] or
            identity_ship[0x44:] != processed_ship[0x44:]):
        raise ValueError('ship identity and processed inputs differ outside header alignment word 0x40')
    processed_alignment = struct.unpack_from('>I', processed_ship, 0x40)[0]
    identity_alignment = struct.unpack_from('>I', identity_ship, 0x40)[0]
    if processed_alignment != 4 or not identity_alignment or identity_alignment & (identity_alignment - 1):
        raise ValueError('ship contract requires processed alignment 4 and a valid disc alignment')
    if not 0 < len(raw_dol) <= 0x800000:
        raise ValueError('ship contract requires a bounded full raw DOL')
    canonical = relocate_ship(processed_ship, sections, dol_ranges)
    if canonical != relocate_ship(identity_ship, sections, dol_ranges):
        raise ValueError('ship disc section replay differs from processed canonical sections')
    for section in sections:
        if section['size'] and section['offset'] and not section['executable']:
            if canonical[section['index']] != section['bytes']:
                raise ValueError('ship contract canonical data differs from normal relocated exports')
    for (index, _), name in zip(SHIP_TABLES, ('ctors', 'dtors')):
        target, terminator = struct.unpack('>II', canonical[index])
        if target & 3 or not SHIP_EXEC[2] <= target < SHIP_EXEC[2] + SHIP_EXEC[3] or terminator:
            raise ValueError(f'ship contract {name} requires an owned executable target and terminator')
    fields = {
        'version': 1, 'module_id': 168,
        'dol_size': len(raw_dol), 'dol_sha256': hashlib.sha256(raw_dol).hexdigest(),
        'rel_size': len(identity_ship), 'rel_sha256': hashlib.sha256(identity_ship).hexdigest(),
        'processed_rel_sha256': hashlib.sha256(processed_ship).hexdigest(),
        'section_alignment': {'identity': identity_alignment, 'processed': processed_alignment},
        'exec': dict(zip(('index', 'offset', 'linked_start', 'size'), SHIP_EXEC)),
        'lifecycle': dict(zip(('prolog', 'epilog', 'unresolved'), SHIP_LIFECYCLE)),
        'ctors': SHIP_TABLES[0][1], 'dtors': SHIP_TABLES[1][1],
        'chunks': [list(chunk) for chunk in chunks],
        'section_count': len(canonical),
        'canonical_sections': [{key: section[key] for key in ('index', 'offset', 'linked_start', 'size')} |
                               {'sha256': hashlib.sha256(canonical[section['index']]).hexdigest(),
                                'raw_sha256': hashlib.sha256(identity_ship[section['offset']:
                                    section['offset'] + section['size']]).hexdigest()}
                               for section in sections if section['size'] and section['offset']],
    }
    identity = hashlib.sha256(json.dumps(fields, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    text = emit_contract(raw_dol, identity_ship, sections, canonical, identity)
    fields['build_identity'] = identity
    fields['sha256'] = hashlib.sha256(text.encode()).hexdigest()
    return text, fields, canonical[SHIP_EXEC[0]]


def read_canonical(data, sections):
    if len(data) < 12 or data[:8] != b'SHIP168\0':
        raise ValueError('ship canonical sidecar has an invalid header')
    count = struct.unpack_from('>I', data, 8)[0]
    expected = [section for section in sections if section['executable'] and section['size']]
    if count != len(expected):
        raise ValueError('ship canonical executable section count mismatch')
    cursor = 12
    result = {}
    for section in expected:
        if cursor + 16 > len(data):
            raise ValueError('ship canonical section metadata is truncated')
        fields = struct.unpack_from('>IIII', data, cursor)
        cursor += 16
        if fields != (section['index'], section['offset'], section['linked_start'], section['size']):
            raise ValueError('ship canonical section address/offset/size mismatch')
        if cursor + section['size'] > len(data):
            raise ValueError('ship canonical section bytes are truncated')
        result[section['index']] = data[cursor:cursor + section['size']]
        cursor += section['size']
    if cursor != len(data):
        raise ValueError('ship canonical sidecar has trailing bytes')
    return result


def emit_contract(raw_dol, raw_ship, sections, canonical, build_identity):
    lines = ['#define MODULE_SHIP_CONTRACT_PRESENT 1']
    for name, data in [('s_ship_raw_dol', raw_dol), ('s_ship_raw_rel', raw_ship)] + [
            (f's_ship_canonical_{section["index"]}', canonical[section['index']] if section['executable'] else section['bytes'])
            for section in sections if section['size'] and section['offset']]:
        lines.append(f'static const u8 {name}[] = {{')
        for at in range(0, len(data), 32):
            lines.append(','.join(str(value) for value in data[at:at + 32]) + ',')
        lines.append('};')
    lines.append('static const BlueWakeRelData s_ship_canonical[] = {')
    count = 0
    for section in sections:
        if not section['size'] or not section['offset']:
            continue
        count += 1
        lines.append(f'{{168u,{section["index"]}u,0x{section["linked_start"]:08X}u,{section["size"]}u,s_ship_canonical_{section["index"]}}},')
    lines += ['};', 'static const BlueWakeShipContractV1 s_ship_contract = {',
              f'1u,{len(raw_dol)}u,s_ship_raw_dol,{len(raw_ship)}u,s_ship_raw_rel,s_ship_canonical,{count}u,"{build_identity}"', '};']
    return '\n'.join(lines) + '\n'
