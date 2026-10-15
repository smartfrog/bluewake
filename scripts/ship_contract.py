import hashlib
import json
import re
import struct
from pathlib import Path, PurePosixPath


SHIP_EXEC = (1, 244, 0xC1B100F4, 55804)
SHIP_LIFECYCLE = (0xC1B100F4, 0xC1B10120, 0xC1B1014C)
SHIP_TABLES = ((2, 0xC1B1DAF0), (3, 0xC1B1DAF8))
SELECTED_MODULES = (168, 400)
IKADA_SELECTED_MODULES = (60, 168, 400)
APPROVED_MODULES = {
    60: {'name': 'ikada', 'exec': (1, 244, 0xC15500F4, 22620),
         'lifecycle': (0xC15500F4, 0xC1550120, 0xC155014C),
         'tables': ((2, 0xC1555950), (3, 0xC1555958)), 'table_size': 8,
         'ctor_return_pc': 0xC155010C, 'magic': b'IKADA60\0',
         'section_count': 21, 'bss': (6, 0xC15581A0, 532)},
    168: {'name': 'ship', 'exec': SHIP_EXEC, 'lifecycle': SHIP_LIFECYCLE,
          'tables': SHIP_TABLES, 'table_size': 8, 'ctor_return_pc': 0xC1B1010C,
          'magic': b'SHIP168\0'},
    400: {'name': 'title', 'exec': (1, 220, 0xC1E000DC, 7756),
          'lifecycle': (0xC1E000DC, 0xC1E00108, 0xC1E00134),
          'tables': ((2, 0xC1E01F28), (3, 0xC1E01F2C)), 'table_size': 4,
          'ctor_return_pc': 0xC1E000F4, 'magic': b'TITLE400'},
}


def selected_module(module_id):
    if module_id not in APPROVED_MODULES:
        raise ValueError('REL contract requires an approved selected module')
    return APPROVED_MODULES[module_id]


def read_disc_member(path, module_id=168):
    import prepare
    from wwhd.formats import MAX_FILE, rarc, yaz0

    spec = selected_module(module_id)
    name = spec['name']
    with open(path, 'rb') as stream:
        header = prepare.validate_disc_header(stream)
        if header['revision'] != 0:
            raise ValueError(f'{name} identity requires GZLE01 revision 0')
        files = prepare.read_fst(stream, header['fst_offset'], header['fst_size'])
        if module_id == 400:
            members = [entry for entry in files if not entry['is_dir'] and entry['path'] == 'rels/d_a_title.rel']
            if len(members) != 1 or not 0 < members[0]['size'] <= MAX_FILE:
                raise ValueError('title identity requires exactly one bounded loose disc member')
            entry = members[0]
            stream.seek(entry['offset'])
            data = stream.read(entry['size'])
            if len(data) != entry['size']:
                raise ValueError('title identity disc member is truncated')
            return yaz0(data), {'member': entry['path']}
        archives = [entry for entry in files if not entry['is_dir'] and entry['path'] == 'RELS.arc']
        if len(archives) != 1 or not 0 < archives[0]['size'] <= MAX_FILE:
            raise ValueError(f'{name} identity requires exactly one bounded RELS.arc')
        stream.seek(archives[0]['offset'])
        archive = stream.read(archives[0]['size'])
        if len(archive) != archives[0]['size']:
            raise ValueError(f'{name} identity RELS.arc is truncated')
    basename = 'd_a_obj_ikada.rel' if module_id == 60 else 'd_a_ship.rel'
    members = [(member, data) for member, data in rarc(archive)
               if PurePosixPath(member).name == basename]
    if len(members) != 1:
        raise ValueError(f'{name} identity requires exactly one disc archive member {basename}')
    name, data = members[0]
    return yaz0(data), {'archive': 'RELS.arc', 'member': name}


def verify_layout(raw, sections, chunks, module_id=168):
    spec = selected_module(module_id)
    name, executable_layout = spec['name'], spec['exec']
    if not 0x4C <= len(raw) <= 0x100000 or struct.unpack_from('>I', raw)[0] != module_id:
        raise ValueError(f'{name} contract requires a bounded raw module {module_id}')
    count, table = struct.unpack_from('>II', raw, 0xC)
    if count != len(sections) or not 4 <= count <= 256 or table + count * 8 > len(raw):
        raise ValueError(f'{name} contract raw section table is invalid')
    for index, section in enumerate(sections):
        flags, size = struct.unpack_from('>II', raw, table + index * 8)
        if (section['index'] != index or section['offset'] != flags & ~1 or
                section['size'] != size or section['executable'] != bool(flags & 1) or
                (size and section['offset'] and section['offset'] + size > len(raw))):
            raise ValueError(f'{name} contract section metadata disagrees with raw REL bytes')
    bss_size = sum(section['size'] for section in sections if not section['offset'])
    if bss_size != struct.unpack_from('>I', raw, 0x20)[0]:
        raise ValueError(f'{name} contract BSS size differs from its raw header')
    if module_id == 60:
        backed = [section['index'] for section in sections if section['size'] and section['offset']]
        bss = [(section['index'], section['linked_start'], section['size'])
               for section in sections if section['size'] and not section['offset']]
        if (count != spec['section_count'] or backed != [1, 2, 3, 4, 5] or bss != [spec['bss']] or
                any(not section['size'] and (section['offset'] or section['executable']) for section in sections)):
            raise ValueError('ikada contract approved section/BSS layout mismatch')
    executable = [section for section in sections if section['executable'] and section['size']]
    if len(executable) != 1 or tuple(executable[0][key] for key in (
            'index', 'offset', 'linked_start', 'size')) != executable_layout:
        raise ValueError(f'{name} contract executable section offset/address/size mismatch')
    for index, (entry_name, expected) in enumerate(zip(('prolog', 'epilog', 'unresolved'), spec['lifecycle'])):
        section = raw[0x30 + index]
        offset = struct.unpack_from('>I', raw, 0x34 + index * 4)[0]
        if (section != executable_layout[0] or offset & 3 or offset + 4 > sections[section]['size'] or
                sections[section]['linked_start'] + offset != expected):
            raise ValueError(f'{name} contract {entry_name} address mismatch')
    for (index, expected), table_name in zip(spec['tables'], ('ctors', 'dtors')):
        section = sections[index]
        if (section['executable'] or section['size'] != spec['table_size'] or not section['offset'] or
                section['linked_start'] != expected or
                section['offset'] != expected - (executable_layout[2] - executable_layout[1])):
            raise ValueError(f'{name} contract {table_name} address/size mismatch')
    cursor = executable_layout[2]
    for start, end in chunks:
        if start != cursor or end <= start or end > executable_layout[2] + executable_layout[3] or (end - start) & 3:
            raise ValueError(f'{name} contract requires complete translated executable chunks')
        cursor = end
    if cursor != executable_layout[2] + executable_layout[3]:
        raise ValueError(f'{name} contract requires complete translated executable chunks')


def relocate_ship(raw, sections, dol_ranges, module_id=168):
    """Replay only WW63's admitted relocation semantics from ordinary REL bytes."""
    selected_module(module_id)
    result = {section['index']: bytearray(raw[section['offset']:section['offset'] + section['size']])
              for section in sections if section['size'] and section['offset']}
    imports, size = struct.unpack_from('>II', raw, 0x28)
    if size == 0 or size % 8 or size // 8 > 32 or imports + size > len(raw):
        raise ValueError('ship contract relocation import table is invalid')
    relocation_start = struct.unpack_from('>I', raw, 0x24)[0]
    for at in range(imports, imports + size, 8):
        module, cursor = struct.unpack_from('>II', raw, at)
        if module not in (0, module_id):
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
            if module == module_id:
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


def normal_contract(raw_dol, processed_rel, sections, chunks, dol_ranges, identity_rel,
                    module_id=168, canonical_input=None):
    spec = selected_module(module_id)
    module_name = spec['name']
    executable = spec['exec']
    verify_layout(processed_rel, sections, chunks, module_id)
    verify_layout(identity_rel, sections, chunks, module_id)
    if module_id != 168 and canonical_input is None:
        raise ValueError(f'selected {module_name} contract requires actual canonical emitter input')
    if (len(identity_rel) != len(processed_rel) or identity_rel[:0x40] != processed_rel[:0x40] or
            identity_rel[0x44:] != processed_rel[0x44:]):
        raise ValueError(f'{module_name} identity and processed inputs differ outside header alignment word 0x40')
    processed_alignment = struct.unpack_from('>I', processed_rel, 0x40)[0]
    identity_alignment = struct.unpack_from('>I', identity_rel, 0x40)[0]
    if processed_alignment != 4 or not identity_alignment or identity_alignment & (identity_alignment - 1):
        raise ValueError(f'{module_name} contract requires processed alignment 4 and a valid disc alignment')
    if module_id == 60 and identity_alignment != 8:
        raise ValueError('ikada contract requires original disc alignment 8')
    if not 0 < len(raw_dol) <= 0x800000:
        raise ValueError(f'{module_name} contract requires a bounded full raw DOL')
    canonical = relocate_ship(processed_rel, sections, dol_ranges, module_id)
    if canonical != relocate_ship(identity_rel, sections, dol_ranges, module_id):
        raise ValueError(f'{module_name} disc section replay differs from processed canonical sections')
    if canonical_input is not None:
        captured = read_canonical(canonical_input, sections, module_id)
        if captured != {s['index']: canonical[s['index']] for s in sections if s['executable'] and s['size']}:
            raise ValueError('selected contract replay differs from actual canonical emitter input')
        successor = spec['ctor_return_pc']
        word = struct.unpack_from('>I', captured[executable[0]], successor - executable[2] - 4)[0]
        displacement = word & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
        if (word >> 26 != 18 or word & 3 != 1 or
                successor - 4 + displacement != 0xC0241178 or
                not any(start <= 0x80241178 and 0x8024117C <= end for start, end in dol_ranges)):
            raise ValueError('selected contract constructor successor lacks its translated walker call')
    for section in sections:
        if section['size'] and section['offset'] and not section['executable']:
            if canonical[section['index']] != section['bytes']:
                raise ValueError(f'{module_name} contract canonical data differs from normal relocated exports')
    for (index, _), name in zip(spec['tables'], ('ctors', 'dtors')):
        values = struct.unpack('>' + 'I' * (spec['table_size'] // 4), canonical[index])
        if values[-1] or any(target & 3 or not executable[2] <= target < executable[2] + executable[3]
                             for target in values[:-1]):
            raise ValueError(f'{module_name} contract {name} requires an owned executable target and terminator')
    fields = {
        'version': 1, 'module_id': module_id,
        'dol_size': len(raw_dol), 'dol_sha256': hashlib.sha256(raw_dol).hexdigest(),
        'rel_size': len(identity_rel), 'rel_sha256': hashlib.sha256(identity_rel).hexdigest(),
        'processed_rel_sha256': hashlib.sha256(processed_rel).hexdigest(),
        'section_alignment': {'identity': identity_alignment, 'processed': processed_alignment},
        'exec': dict(zip(('index', 'offset', 'linked_start', 'size'), executable)),
        'lifecycle': {name: sections[identity_rel[0x30 + i]]['linked_start'] +
                      struct.unpack_from('>I', identity_rel, 0x34 + i * 4)[0]
                      for i, name in enumerate(('prolog', 'epilog', 'unresolved'))},
        'ctors': sections[spec['tables'][0][0]]['linked_start'],
        'dtors': sections[spec['tables'][1][0]]['linked_start'],
        'chunks': [list(chunk) for chunk in chunks],
        'section_count': len(canonical),
        'canonical_sections': [{key: section[key] for key in ('index', 'offset', 'linked_start', 'size')} |
                               {'sha256': hashlib.sha256(canonical[section['index']]).hexdigest(),
                                 'raw_sha256': hashlib.sha256(identity_rel[section['offset']:
                                    section['offset'] + section['size']]).hexdigest()}
                               for section in sections if section['size'] and section['offset']],
    }
    identity = hashlib.sha256(json.dumps(fields, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    text = emit_contract(raw_dol, identity_rel, sections, canonical, identity, module_id)
    fields['build_identity'] = identity
    fields['sha256'] = hashlib.sha256(text.encode()).hexdigest()
    return text, fields, canonical[executable[0]]


def read_canonical(data, sections, module_id=168):
    if len(data) < 12 or data[:8] != selected_module(module_id)['magic']:
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


def emit_contract(raw_dol, raw_rel, sections, canonical, build_identity, module_id=168):
    prefix = selected_module(module_id)['name']
    lines = ['#define MODULE_SHIP_CONTRACT_PRESENT 1'] if module_id == 168 else []
    arrays = [('s_ship_raw_dol', raw_dol)] if module_id == 168 else []
    for name, data in arrays + [(f's_{prefix}_raw_rel', raw_rel)] + [
            (f's_{prefix}_canonical_{section["index"]}', canonical[section['index']] if section['executable'] else section['bytes'])
            for section in sections if section['size'] and section['offset'] and
            (module_id == 168 or section['executable'])]:
        lines.append(f'static const u8 {name}[] = {{')
        for at in range(0, len(data), 32):
            lines.append(','.join(str(value) for value in data[at:at + 32]) + ',')
        lines.append('};')
    lines.append(f'static const BlueWakeRelData s_{prefix}_canonical[] = {{')
    count = 0
    for section in sections:
        if not section['size'] or not section['offset']:
            continue
        count += 1
        array = (f's_{prefix}_canonical_{section["index"]}' if module_id == 168 or section['executable']
                 else f's_rel_data_{module_id}_{section["index"]}')
        lines.append(f'{{{module_id}u,{section["index"]}u,0x{section["linked_start"]:08X}u,{section["size"]}u,{array}}},')
    lines.append('};')
    if module_id == 168:
        lines += ['static const BlueWakeShipContractV1 s_ship_contract = {',
                  f'1u,{len(raw_dol)}u,s_ship_raw_dol,{len(raw_rel)}u,s_ship_raw_rel,s_ship_canonical,{count}u,"{build_identity}"', '};']
    return '\n'.join(lines) + '\n'


def unique_json_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate producer provenance key: {key}')
        result[key] = value
    return result


def producer_inputs(manifest_path, raw_dol, rel_bins, entries, identities, selected_ids=SELECTED_MODULES):
    if selected_ids not in (SELECTED_MODULES, IKADA_SELECTED_MODULES):
        raise ValueError('producer requires an approved bounded selection')
    path = Path(manifest_path)
    manifest = json.loads(path.read_text(), object_pairs_hook=unique_json_object)
    if (manifest.get('version') != 1 or
            not re.fullmatch('[0-9a-f]{64}', manifest.get('producer_identity', ''))):
        raise ValueError('selected contract producer provenance header is invalid')
    if set(manifest.get('modules', {})) != {str(mid) for mid in selected_ids}:
        label = '168 and 400' if selected_ids == SELECTED_MODULES else '60, 168 and 400'
        raise ValueError(f'producer provenance requires exactly selected modules {label}')
    if manifest.get('dol_sha256') != hashlib.sha256(raw_dol).hexdigest():
        raise ValueError('selected contract producer DOL identity mismatch')
    captures = {}
    bindings = {}
    for mid in selected_ids:
        item = manifest['modules'][str(mid)]
        if (item.get('raw_sha256') != hashlib.sha256(identities[mid]).hexdigest() or
                item.get('processed_rel_sha256') != hashlib.sha256(rel_bins[mid]).hexdigest()):
            raise ValueError(f'selected module {mid}: producer REL identity mismatch')
        module_dir = entries[mid]['dir']
        translated = {str(p.relative_to(module_dir)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in sorted(module_dir.rglob('*')) if p.is_file()}
        if item.get('translation_sha256') != translated:
            raise ValueError(f'selected module {mid}: producer translation identity mismatch')
        capture = Path(item['capture'])
        if not capture.is_absolute():
            capture = path.parent / capture
        if not 12 <= capture.stat().st_size <= 0x100000:
            raise ValueError('selected contract producer capture is not bounded')
        data = capture.read_bytes()
        if item.get('capture_sha256') != hashlib.sha256(data).hexdigest():
            raise ValueError(f'selected module {mid}: producer capture identity mismatch')
        captures[mid] = data
        bindings[str(mid)] = {key: item[key] for key in (
            'raw_sha256', 'processed_rel_sha256', 'capture_sha256', 'translation_sha256')}
    return captures, {'version': 1, 'producer_identity': manifest['producer_identity'],
                      'dol_sha256': manifest['dol_sha256'], 'modules': bindings}


def emit_selected_set(raw_dol, contracts, lifecycle_ids, provenance):
    fields = {'version': 1, 'dol_size': len(raw_dol),
              'dol_sha256': hashlib.sha256(raw_dol).hexdigest(),
              'contracts': {str(mid): contracts[mid][1] for mid in sorted(contracts)},
              'initialization': {str(mid): {'ctor_table_section': selected_module(mid)['tables'][0][0],
                                            'ctor_table_offset': 0,
                                            'ctor_return_pc': selected_module(mid)['ctor_return_pc']}
                                 for mid in sorted(contracts)},
              'producer_provenance': provenance}
    identity = hashlib.sha256(json.dumps(fields, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    lines = ['#define MODULE_REL_INIT_CONTRACTS_PRESENT 1',
             'static const BlueWakeRelInitContractV1 s_rel_init_contracts[] = {']
    for mid in sorted(contracts):
        spec = selected_module(mid)
        contract = contracts[mid][1]
        prefix = spec['name']
        lifecycle_index = lifecycle_ids.index(mid)
        lines.append(f'{{{mid}u,{contract["rel_size"]}u,s_{prefix}_raw_rel,s_{prefix}_canonical,'
                     f'{contract["section_count"]}u,{spec["tables"][0][0]}u,0u,'
                     f'0x{spec["ctor_return_pc"]:08X}u,&s_rel_lifecycle_v2[{lifecycle_index}],'
                     f'"{contract["build_identity"]}"}},')
    lines += ['};', 'static const BlueWakeRelInitContractSetV1 s_rel_init_contract_set = {',
              f'1u,{len(raw_dol)}u,s_ship_raw_dol,s_rel_init_contracts,{len(contracts)}u,"{identity}"', '};']
    fields['build_identity'] = identity
    return ''.join(contracts[mid][0] for mid in sorted(contracts)) + '\n'.join(lines) + '\n', fields
