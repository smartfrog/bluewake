import struct


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
