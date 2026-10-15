"""Static canonical executable qualification; never a module adjudicator.

Only literal, unpreprocessed emitted table syntax is admitted. No C is executed.
Translated DOL inventory provenance is deliberately not inferred from a header.
"""

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

SPEC_SHA256 = 'c89b1585a9e3e6ad7b3fa90e71caf8b692ab6be03f4a730e73d81353557736f3'
DELIVERY = 'd08dbc8d1b8242d501a4d7599748ed08a1c6285e'
COVERAGE_MANIFEST_SHA256 = 'a1e96bc3ffbf0d356b76e2e4fe638943334390e9074638b6945071c5418a5a51'
COVERAGE_SOURCE = Path('/home/fred/projects/zeldaWWHDRecomp')
COVERAGE_SOURCE_HEAD = '23a8f348ec6c9ef4ab22fb69f2df4e412ffc9d10'
COVERAGE_CLAIM = 'Audited historical identity + structural derived coverage only'
COVERAGE_TAGGED_SCOPE = 'C0000000 <= address < C0400000; clear 40000000; set ctx->pc; call_original after replacement/host hooks'
INHERITED_CAPTURE_PATHS = {
    9: 'inherited/bridge/bridge9.ship-code-input',
    19: 'inherited/grass/grass19.ship-code-input',
    60: 'inherited/inherited/ikada.ship-code-input',
    81: 'inherited/toripost/toripost81.ship-code-input',
    84: 'inherited/inherited/wood84.ship-code-input',
    168: 'inherited/inherited/ship-inherited.ship-code-input',
    400: 'inherited/inherited/title-recovered.ship-code-input',
}
INHERITED_ROOT_SHA256 = '66512728d80f0c396e72b86564119fbc4c6c205e93885a69968a8af8411dc7f8'
INHERITED_PRIOR = COVERAGE_SOURCE / 'build-f5/preserved-worktrees/ww146-toripost-20261015'
INHERITED_PRIOR_MANIFEST_SHA256 = '15a06a44293d2eb147c095801b51177785ef30bdddb905d16a636e8685633582'
INHERITED_PRIOR_RECEIPT_SHA256 = '62d4a1e5de36a6836d2cace8f4826ef903740532004bb13991d3ceede594c3f7'


class UnqualifiedCoverage(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, offset):
    require(0 <= offset <= len(data) - 4, 'truncated word')
    return struct.unpack_from('>I', data, offset)[0]


def fnv(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def relocate(raw, sections, module_id, dol_text):
    require(len(raw) >= 0x4C and u32(raw, 0) == module_id, 'REL identity/header')
    count, table = u32(raw, 0xC), u32(raw, 0x10)
    require(count == len(sections) and 0 < count <= 256 and
            table >= 0x4C and table + count * 8 <= len(raw), 'section table bounds')
    images = {}
    for index, section in enumerate(sections):
        flags, size = struct.unpack_from('>II', raw, table + index * 8)
        offset = flags & ~1
        require((section['index'], section['offset'], section['size'], section['executable']) ==
                (index, offset, size, bool(flags & 1)), 'section metadata mismatch')
        require(not size or 0 < section['linked_start'] <= 0xFFFFFFFF - size,
                'section address bounds')
        if size and offset:
            require(offset >= table + count * 8 and offset + size <= len(raw), 'section bounds')
            images[index] = bytearray(raw[offset:offset + size])
    imports, size, start = u32(raw, 0x28), u32(raw, 0x2C), u32(raw, 0x24)
    require(size > 0 and size % 8 == 0 and size <= 256 and imports >= 0x4C and
            imports + size <= len(raw) and start >= 0x4C, 'import table bounds')
    entries = [struct.unpack_from('>II', raw, at) for at in range(imports, imports + size, 8)]
    starts = sorted(cursor for _, cursor in entries)
    require(len(set(starts)) == len(starts), 'shared relocation streams unsupported')
    external = set()
    for imported, cursor in entries:
        if imported not in (0, module_id):
            raise UnqualifiedCoverage('unsupported import')
        require(start <= cursor <= len(raw) - 8 and (cursor - start) % 8 == 0,
                'relocation stream bounds/alignment')
        patch = None
        offset = 0
        limit = next((other for other in starts if other > cursor), len(raw))
        while cursor + 8 <= limit:
            delta, kind, target, symbol = struct.unpack_from('>HBBI', raw, cursor)
            cursor += 8
            if kind == 203:
                break
            if kind == 202:
                require(target < count and sections[target]['size'] > 0, 'patch section absent')
                patch, offset = sections[target], 0
                continue
            require(patch is not None, 'missing patch section')
            offset += delta
            require(offset <= patch['size'], 'delta exceeds section bounds')
            if kind == 201:
                continue
            if kind not in (1, 4, 6, 10):
                raise UnqualifiedCoverage('unsupported relocation type')
            width = 2 if kind in (4, 6) else 4
            require(patch['index'] in images and offset + width <= patch['size'] and
                    offset % width == 0 and (patch['linked_start'] + offset) % width == 0,
                    'patch bounds/alignment/storage')
            destination = None
            if imported == module_id:
                require(target < count, 'target section absent')
                destination = sections[target]
                require(symbol < destination['size'], 'target section bounds')
                address = destination['linked_start'] + symbol
            else:
                require(0x80000000 <= symbol < 0x81800000, 'external address bounds')
                address = symbol
            value = address
            if kind == 6:
                value = (address + 0x8000) >> 16
            if kind == 10:
                coverage = dol_text if destination is None else (
                    [(destination['linked_start'], destination['linked_start'] + destination['size'])]
                    if destination['executable'] else [])
                require(address % 4 == 0 and any(a <= address and address + 4 <= b for a, b in coverage),
                        'branch target outside executable text')
                pc = patch['linked_start'] + offset
                displacement = ((address | (pc & 0x40000000)) - pc) & 0xFFFFFFFF
                if displacement >= 0x80000000:
                    displacement -= 0x100000000
                word = u32(raw, patch['offset'] + offset)
                require(patch['executable'] and -0x2000000 <= displacement <= 0x1FFFFFC and
                        displacement % 4 == 0 and word >> 26 == 18 and not word & 2,
                        'branch encoding/displacement')
                value = (word & 0xFC000003) | (displacement & 0x03FFFFFC)
                if destination is None:
                    external.add(address)
            struct.pack_into('>H' if width == 2 else '>I', images[patch['index']], offset,
                             value & ((1 << (width * 8)) - 1))
        else:
            raise ValueError('unterminated relocation stream')
    return {index: bytes(image) for index, image in images.items()}, sorted(external)


def block(text, ctype, name, array=True):
    text = re.sub(r'//[^\n]*', '', text)
    for line in text.splitlines():
        if line.lstrip().startswith('#'):
            require(re.fullmatch(r'#define MODULE_(?:REL_DATA_COUNT|REL_LIFECYCLE_COUNT|'
                                r'REL_LIFECYCLE_V2_COUNT|SHIP_CONTRACT_PRESENT|'
                                r'REL_INIT_CONTRACTS_PRESENT|CODE_RANGE_COUNT|SMC_RANGE_COUNT|'
                                r'CHUNK_RANGE_COUNT) [0-9]+u?', line) is not None,
                    'unsupported preprocessor directive')
    require('/*' not in text, 'block commented C unsupported')
    suffix = r'\[\]' if array else ''
    matches = re.findall(r'^static const ' + re.escape(ctype) + r'\s+' + re.escape(name) +
                         suffix + r'\s*=\s*\{(.*?)\n?\};', text, re.S | re.M)
    require(len(matches) == 1, f'missing/duplicate C table {name}')
    return matches[0]


def literal_definition(text, name):
    text = re.sub(r'//[^\n]*', '', text)
    values = re.findall(r'^#define ' + re.escape(name) + r' ([0-9]+)u?$', text, re.M)
    require(len(values) == 1, f'missing/duplicate literal definition {name}')
    require(re.fullmatch(r'0|[1-9][0-9]*', values[0]) is not None, f'noncanonical literal definition {name}')
    return int(values[0])


def rows(body, pattern):
    matches = list(re.finditer(pattern, body))
    remainder = body
    for match in reversed(matches):
        remainder = remainder[:match.start()] + remainder[match.end():]
    require(not remainder.strip(), 'unsupported table syntax')
    return [match.groups() for match in matches]


NUM = r'(0x[0-9A-Fa-f]+|[0-9]+)u'


def numbers(values):
    return tuple(int(value, 0) for value in values)


def bind(row, expected, capture, data_text, table_text):
    section = row['exec']
    fields = tuple(section[key] for key in ('index', 'offset', 'linked_start', 'size'))
    require(len(expected) == section['size'], 'oracle section size')
    require(capture == bytes.fromhex(row['capture_magic_hex']) + struct.pack('>I', 1) +
            struct.pack('>IIII', *fields) + expected, 'strict capture mismatch')
    inherited_names = {9: 'bridge', 19: 'grass', 60: 'ikada', 81: 'toripost',
                       84: 'wood', 168: 'ship', 400: 'title'}
    prefix = 's_' + inherited_names.get(row['id'], Path(row['basename']).stem)
    array = prefix + '_canonical_' + str(section['index'])
    body = block(data_text, 'u8', array)
    require(re.fullmatch(r'\s*(?:[0-9]+\s*,\s*)+', body) is not None, 'array syntax')
    values = [int(value) for value in body.split(',') if value.strip()]
    require(all(0 <= value <= 255 for value in values) and bytes(values) == expected,
            'canonical array mismatch')
    descriptors = rows(block(data_text, 'BlueWakeRelData', prefix + '_canonical'),
                       r'\{\s*' + r'\s*,\s*'.join([NUM] * 4) + r'\s*,\s*(\w+)\s*\},\s*')
    selected = [d for d in descriptors if int(d[1], 0) == section['index']]
    require(len(selected) == 1 and numbers(selected[0][:4]) ==
            (row['id'], section['index'], section['linked_start'], section['size']) and
            selected[0][4] == array and all(int(d[0], 0) == row['id'] for d in descriptors),
            'canonical descriptor mismatch')
    contracts = exported_contracts(data_text, strict=False)
    associations = [entry for entry in contracts if int(entry[0], 0) == row['id']]
    require(len(associations) == 1 and associations[0][2:4] ==
            (prefix + '_raw_rel', prefix + '_canonical') and
            int(associations[0][4], 0) == len(descriptors), 'module canonical association mismatch')
    require(literal_definition(data_text, 'MODULE_REL_INIT_CONTRACTS_PRESENT') == 1,
            'invalid contract presence marker')
    contract_set = block(data_text, 'BlueWakeRelInitContractSetV1', 's_rel_init_contract_set', array=False)
    exported = re.fullmatch(r'\s*' + NUM + r',\s*' + NUM + r',\s*(\w+),\s*(\w+),\s*' +
                            NUM + r',\s*"[^"\n]+"\s*', contract_set)
    require(exported is not None, 'unsupported exported contract set syntax')
    version, dol_size, dol_array, contract_array, count = exported.groups()
    require(int(version, 0) == 1 and int(dol_size, 0) > 0 and dol_array == 's_ship_raw_dol' and
            contract_array == 's_rel_init_contracts' and int(count, 0) == len(contracts),
            'exported contract set association/count mismatch')
    ranges = [numbers(pair) for pair in rows(block(table_text, 'StaticRecompRange', 's_chunk_ranges'),
              r'\{\s*' + NUM + r'\s*,\s*' + NUM + r'\s*\},\s*')]
    hashes = rows(block(table_text, 'u64', 's_chunk_hashes'), r'\s*0x([0-9A-Fa-f]+)ull,\s*')
    require(literal_definition(table_text, 'MODULE_CHUNK_RANGE_COUNT') == len(ranges) == len(hashes),
            'exported chunk count differs from runtime range/hash array lengths')
    frozen = [tuple(pair) for pair in row['translation']['chunk_ranges']]
    cursor = section['linked_start']
    chunks = []
    for start, end in frozen:
        require(start == cursor and start % 4 == 0 and end % 4 == 0 and
                start < end <= section['linked_start'] + len(expected), 'frozen chunk range coverage')
        payload = expected[start - section['linked_start']:end - section['linked_start']]
        chunks.append(dict(start=start, end=end, sha256=sha(payload), fnv1a64=f'{fnv(payload):016X}'))
        cursor = end
    require(cursor == section['linked_start'] + len(expected), 'missing frozen ranges')
    relevant = [(i, pair) for i, pair in enumerate(ranges)
                if pair[0] < cursor and pair[1] > section['linked_start']]
    require([pair for _, pair in relevant] == frozen, 'runtime ranges differ from frozen ranges')
    for (index, _), chunk in zip(relevant, chunks):
        require(int(hashes[index][0], 16) == int(chunk['fnv1a64'], 16), 'runtime hash mismatch')
    return dict(executable_sha256=sha(expected), chunks=chunks)


def verify_translations(directory, pins):
    require(any(name.endswith('.c') for name in pins), 'missing frozen translation C pins')
    actual = {str(path.relative_to(directory)) for path in directory.rglob('*.c')}
    require(actual == {name for name in pins if name.endswith('.c')}, 'translation C inventory mismatch')
    result = {}
    for name, pin in pins.items():
        path = directory / name
        require(not Path(name).is_absolute() and '..' not in Path(name).parts,
                'translation path escapes directory')
        require(path.is_file() and sha(path.read_bytes()) == pin, f'frozen translation mismatch: {name}')
        result[name] = pin
    return result


def verify_receipts(row, binding, chunks, contract):
    require(chunks == binding['chunks'], 'canonical chunk receipt mismatch')
    sections = [section for section in contract['canonical_sections']
                if section['index'] == row['exec']['index']]
    require(len(sections) == 1 and
            all(sections[0].get(key) == value for key, value in row['exec'].items()) and
             sections[0].get('sha256') == binding['executable_sha256'], 'canonical section receipt mismatch')


def certificate_identity(fields):
    return sha(json.dumps(fields, sort_keys=True, separators=(',', ':')).encode())


def certificate_preimage(row, dol, raw, processed, canonical):
    sections = row['sections']
    backed = [s for s in sections if s['size'] and s['offset']]
    require(set(canonical) == {s['index'] for s in backed}, 'canonical section inventory')
    executable, = [s for s in sections if s['size'] and s['executable']]
    fields = ('index', 'offset', 'linked_start', 'size')
    require({k: executable[k] for k in fields} == row['exec'], 'certificate executable layout')
    lifecycle = {}
    for i, name in enumerate(('prolog', 'epilog', 'unresolved')):
        index, offset = raw[0x30 + i], u32(raw, 0x34 + i * 4)
        require(index == executable['index'] and offset % 4 == 0 and
                offset + 4 <= executable['size'], 'certificate lifecycle bounds')
        lifecycle[name] = sections[index]['linked_start'] + offset
    tables = {}
    for name, key in [('ctors', 'ctor'), ('dtors', 'dtor')]:
        if row[key]['table_offset'] != 0:
            raise UnqualifiedCoverage('certificate table offset semantics not qualified')
        section = sections[row[key]['section_index']]
        require(section['index'] in canonical and not section['executable'] and
                section['size'] % 4 == 0, 'certificate table layout')
        values = struct.unpack('>' + 'I' * (section['size'] // 4), canonical[section['index']])
        require(values and values[-1] == 0 and all(value % 4 == 0 and
                executable['linked_start'] <= value < executable['linked_start'] + executable['size']
                for value in values[:-1]), 'certificate table targets/terminator')
        tables[name] = section['linked_start']
    return dict(version=1, module_id=row['id'], dol_size=len(dol), dol_sha256=sha(dol),
                rel_size=len(raw), rel_sha256=sha(raw), processed_rel_sha256=sha(processed),
                section_alignment={'identity': u32(raw, 0x40), 'processed': u32(processed, 0x40)},
                exec={key: executable[key] for key in fields}, lifecycle=lifecycle, **tables,
                chunks=[list(pair) for pair in row['translation']['chunk_ranges']],
                section_count=len(backed), canonical_sections=[
                    {key: s[key] for key in fields} | {'sha256': sha(canonical[s['index']]),
                     'raw_sha256': sha(raw[s['offset']:s['offset'] + s['size']])} for s in backed])


def compare_certificate_receipt(expected, receipt):
    require(set(receipt) == set(expected) | {'build_identity', 'sha256'} and
            certificate_identity({key: value for key, value in receipt.items()
                                  if key not in ('build_identity', 'sha256')}) ==
            certificate_identity(expected), 'complete certificate preimage mismatch')
    identity = certificate_identity(expected)
    require(receipt['build_identity'] == identity, 'certificate identity mismatch')
    return identity


def exported_contracts(data_text, strict=True):
    identity = '[0-9a-f]{64}' if strict else '[^"\n]+'
    return rows(block(data_text, 'BlueWakeRelInitContractV1', 's_rel_init_contracts'),
                r'\{\s*' + NUM + r'\s*,\s*' + NUM + r'\s*,\s*(\w+),\s*(\w+),\s*' +
                NUM + r',\s*' + NUM + r',\s*' + NUM + r',\s*' + NUM +
                r',\s*&s_rel_lifecycle_v2\[(\d+)\],\s*"(' + identity + r')"\},\s*')


def bind_full_contract_set(data_text, policy_text, contracts, expected_ids):
    actual = exported_contracts(data_text)
    policy_body = re.sub(r'^#define WW_ACTOR_(?:COUNT [0-9]+u|POLICY_PIN 0x[0-9a-f]{16}ull|'
                         r'POLICY_IDENTITY "inline-zero-v1-fnv1a64-[0-9a-f]{16}"|'
                         r'SET_IDENTITY "[0-9a-f]{64}")\n?', '', policy_text, flags=re.M)
    policy = rows(policy_body, r'\{\s*([1-9][0-9]*)u,\s*"([0-9a-f]{64})",\s*(true|false),\s*'
                  r'WW_ACTOR_SDK_(EXISTING|OWNER_PUBLISHED|CERTIFIED_INLINE_ZERO)\},?\s*')
    ids = [int(entry[0], 0) for entry in actual]
    policy_ids = [int(entry[0]) for entry in policy]
    require(len(ids) == len(set(ids)) == len(expected_ids) and set(ids) == set(expected_ids),
            'actual exported contract fullset membership mismatch')
    require(len(policy_ids) == len(set(policy_ids)) == len(ids) and set(policy_ids) == set(ids),
            'policy fullset membership mismatch')
    require({int(entry[0]) for entry in policy if entry[2] == 'false'} ==
            set(expected_ids) & {60, 168, 400}, 'policy nonactor fullset mismatch')
    require(set(contracts) == {str(mid) for mid in expected_ids}, 'receipt fullset membership mismatch')
    actual_pins = {int(entry[0], 0): entry[-1] for entry in actual}
    require(actual_pins == {int(entry[0]): entry[1] for entry in policy} ==
            {int(mid): value['build_identity'] for mid, value in contracts.items()},
            'actual exported fullset/policy/receipt identity mismatch')
    return actual, policy


def literal_bytes(data_text, name):
    body = block(data_text, 'u8', name)
    require(re.fullmatch(r'\s*(?:(?:0x[0-9A-Fa-f]{2}|[0-9]+)\s*,\s*)+', body) is not None,
            'array syntax')
    values = [int(value.strip(), 16 if value.strip().startswith('0x') else 10)
              for value in body.split(',') if value.strip()]
    require(all(0 <= value <= 255 for value in values), 'array byte bounds')
    return bytes(values)


def bind_certificate(row, expected, raw, dol, canonical, data_text, receipt, actual, policy):
    import ship_contract

    identity = compare_certificate_receipt(expected, receipt)
    mid = row['id']
    cert, = [entry for entry in actual if int(entry[0], 0) == mid]
    selected, = [entry for entry in policy if int(entry[0]) == mid]
    require(cert[-1] == selected[1] == identity, 'derived certificate/exported/policy identity mismatch')
    prefix = 's_' + ship_contract.selected_module(mid)['name']
    require(cert[2] == prefix + '_raw_rel' and cert[3] == prefix + '_canonical',
            'certificate array association')
    require(tuple(int(cert[i], 0) for i in (1, 4, 5, 6, 7)) ==
            (len(raw), len(canonical), row['ctor']['section_index'], 0, row['ctor']['successor']),
            'exported certificate fields mismatch')
    exported_set = block(data_text, 'BlueWakeRelInitContractSetV1', 's_rel_init_contract_set', array=False)
    require(re.fullmatch(r'\s*1u,\s*' + str(len(dol)) + r'u,\s*s_ship_raw_dol,\s*'
                         r's_rel_init_contracts,\s*' + str(len(actual)) +
                         r'u,\s*"[0-9a-f]{64}"\s*', exported_set) is not None,
            'exported contract set DOL/count binding mismatch')
    require(literal_bytes(data_text, cert[2]) == raw and
            literal_bytes(data_text, 's_ship_raw_dol') == dol, 'exported raw identity mismatch')
    descriptors = rows(block(data_text, 'BlueWakeRelData', cert[3]),
                       r'\{\s*' + r'\s*,\s*'.join([NUM] * 4) + r'\s*,\s*(\w+)\s*\},\s*')
    require([numbers(entry[:4]) for entry in descriptors] ==
            [(mid, s['index'], s['linked_start'], s['size']) for s in row['sections']
             if s['size'] and s['offset']], 'complete canonical descriptor mismatch')
    for entry in descriptors:
        require(literal_bytes(data_text, entry[4]) == canonical[int(entry[1], 0)],
                'exported canonical section mismatch')
    lifecycle = [numbers(entry) for entry in rows(
        block(data_text, 'BlueWakeRelLifecycleV2', 's_rel_lifecycle_v2'),
        r'\{\s*' + r'\s*,\s*'.join([NUM] * 4) + r'\s*\},\s*')]
    index = int(cert[8])
    require(index < len(lifecycle) and lifecycle[index] ==
            (mid, *(expected['lifecycle'][name] for name in ('prolog', 'epilog', 'unresolved'))),
            'exported certificate lifecycle association mismatch')
    bss = sum(s['size'] for s in row['sections'] if not s['offset'])
    form = 'EXISTING' if mid in (60, 168, 400) else (
        'OWNER_PUBLISHED' if bss else 'CERTIFIED_INLINE_ZERO')
    require(selected[2:] == ('false' if mid in (60, 168, 400) else 'true', form),
            'policy actor/input form mismatch')
    sections = [s | {'bytes': canonical.get(s['index'], b'')} for s in row['sections']]
    emitted = ship_contract.emit_contract(dol, raw, sections, canonical, identity, mid)
    require(receipt['sha256'] == sha(emitted.encode()), 'certificate emitted text receipt mismatch')
    return dict(preimage=expected, build_identity=identity, fullset_count=len(actual),
                policy_sdk_contract=form, bss_size=bss)


def coverage_status():
    return dict(status='BLOCKED-incomplete', reason=(
        'No admitted DOL historical/structural coverage supplement was supplied. '
        'DOL text containment and current generated.h identity are not translated coverage proof. '
        'External REL24 targets and constructor walker require that evidence. '
        'Contract activation and independent method/code review remain pending.'))


def pinned_file(path, pin):
    data = path.read_bytes()
    require(len(data) == pin['size'] and sha(data) == pin['sha256'], f'pinned file identity mismatch: {path}')
    return data


def capture_input(row, evidence):
    if row['id'] not in INHERITED_CAPTURE_PATHS:
        return (evidence / 'captures/bulk' / row['capture_basename']).read_bytes(), None
    import ship_contract

    root = pinned_json(evidence / 'MANIFEST.json', INHERITED_ROOT_SHA256)
    expected = {str(mid) for mid in INHERITED_CAPTURE_PATHS}
    require(root['source_delivery_sha'] == DELIVERY and
            set(map(str, root['inherited_ids'])) == expected and len(root['inherited_ids']) == len(expected),
            'inherited delivery membership mismatch')
    relocation = root['relocated_manifests']['opening-d.json']
    require(relocation['original_sha256'] == INHERITED_PRIOR_MANIFEST_SHA256,
            'inherited prior manifest identity mismatch')
    manifest = pinned_json(evidence / 'manifests/opening-d.json', relocation['preserved_sha256'])
    prior = pinned_json(INHERITED_PRIOR / 'manifests/opening-d.json', INHERITED_PRIOR_MANIFEST_SHA256)
    require(set(manifest['modules']) == set(prior['modules']) == expected,
            'inherited producer membership mismatch')
    require(manifest['version'] == prior['version'] == 1 and
            manifest['dol_sha256'] == prior['dol_sha256'], 'inherited producer DOL identity mismatch')
    entry, old = manifest['modules'][str(row['id'])], prior['modules'][str(row['id'])]
    relative = INHERITED_CAPTURE_PATHS[row['id']]
    path = evidence / 'captures' / relative
    require(entry['capture'] == str(path) and path.resolve() == path.absolute() and
            old['capture'] == str(INHERITED_PRIOR / 'captures' / relative.removeprefix('inherited/')),
            'inherited capture path mismatch or alias')
    require(entry['capture_sha256'] == old['capture_sha256'] == root['captures'][relative],
            'inherited capture pin mismatch')
    identities = {'raw_sha256': row['raw_sha256'], 'processed_rel_sha256': row['processed_sha256'],
                  'translation_sha256': row['translation']['files_sha256']}
    require(all(entry[key] == old[key] == value for key, value in identities.items()),
            'inherited source identity mismatch')
    capture = path.read_bytes()
    require(len(capture) == row['expected_capture_size'] and sha(capture) == entry['capture_sha256'],
            'inherited capture identity mismatch')
    magic = ship_contract.selected_module(row['id'])['magic'].hex()
    require(row['capture_magic_hex'] == (b'REL00081'.hex() if row['id'] == 81 else magic),
            'unreviewed inherited capture namespace')
    return capture, dict(capture_path=str(path), capture_sha256=sha(capture), capture_magic_hex=magic,
                         dol_sha256=manifest['dol_sha256'],
                         delivery_manifest_sha256=INHERITED_ROOT_SHA256,
                         preserved_producer_manifest_sha256=relocation['preserved_sha256'],
                         prior_producer_manifest_sha256=INHERITED_PRIOR_MANIFEST_SHA256)


def pinned_json(path, pin):
    import ship_contract

    data = path.read_bytes()
    require(sha(data) == pin, f'pinned JSON identity mismatch: {path}')
    return json.loads(data, object_pairs_hook=ship_contract.unique_json_object)


def inherited_pin_comparison(current, prior, policy, base_policy):
    production = re.findall(r'#if WW05_TESTS_ONLY\n.*?\n#else\n(.*?)\n#endif\n};', base_policy, re.S)
    require(len(production) == 1, 'inherited base production policy membership')
    pattern = (r'^[ \t]*\{(\d+)u,\s*"([0-9a-f]{64})",\s*(true|false),\s*'
               r'WW_ACTOR_SDK_(EXISTING|OWNER_PUBLISHED|CERTIFIED_INLINE_ZERO)\},?[ \t]*$')
    base_rows = re.findall(pattern, production[0], re.M)
    current_rows = re.findall(pattern, policy, re.M)
    contracts = current['selected_rel_contracts']['contracts']
    old = prior['selected_rel_contracts']['contracts']
    require(set(old) == {str(mid) for mid in INHERITED_CAPTURE_PATHS} and
            len(base_rows) == len(INHERITED_CAPTURE_PATHS), 'inherited prior/base membership mismatch')
    result = {}
    for mid in INHERITED_CAPTURE_PATHS:
        key = str(mid)
        base = [entry for entry in base_rows if entry[0] == key]
        selected = [entry for entry in current_rows if entry[0] == key]
        require(len(base) == len(selected) == 1, 'inherited policy membership mismatch')
        pin = old[key]['build_identity']
        require(base[0][1] == selected[0][1] == contracts[key]['build_identity'] == pin,
                f'inherited pin mismatch: {mid}')
        require(base[0][2:] == selected[0][2:], f'inherited policy assignment changed: {mid}')
        result[key] = dict(prior_pin=pin, current_pin=selected[0][1], base_pin=base[0][1],
                           actor=selected[0][2] == 'true', policy_sdk_contract=selected[0][3])
    return result


def shared_prior_setup(receipt, policy_text, root):
    path = INHERITED_PRIOR / 'composite-batch/generation_receipt.json'
    prior = pinned_json(path, INHERITED_PRIOR_RECEIPT_SHA256)
    base_path = 'vita/host/actor_batch_policy.h'
    base = subprocess.check_output(['git', '-C', str(root), 'show',
                                   COVERAGE_SOURCE_HEAD + ':' + base_path], text=True)
    return dict(prior_wave_pins=inherited_pin_comparison(receipt, prior, policy_text, base),
                prior_generation_receipt_path=str(path),
                prior_generation_receipt_sha256=INHERITED_PRIOR_RECEIPT_SHA256,
                base_policy_revision=COVERAGE_SOURCE_HEAD, base_policy_path=base_path,
                base_policy_sha256=sha(base.encode()), status='VERIFIED-candidate',
                automatic_module_admission=False)


def check_coverage_source():
    command = ['git', '-C', str(COVERAGE_SOURCE)]
    require(subprocess.check_output(command + ['rev-parse', 'HEAD'], text=True).strip() ==
            COVERAGE_SOURCE_HEAD, 'coverage SOURCE revision mismatch')
    require(not subprocess.check_output(command + ['status', '--porcelain=v1'], text=True),
            'coverage SOURCE tree not clean')


def coverage_address(address):
    require(address % 4 == 0, 'coverage target alignment')
    if 0x80000000 <= address < 0x80400000:
        return address
    require(0xC0000000 <= address < 0xC0400000, 'address outside qualified alias scope')
    return address - 0x40000000


def coverage_dependency(directory, row, external, walker, dol, composite):
    if directory is None:
        return coverage_status()
    import ship_contract

    def decode(data):
        return json.loads(data, object_pairs_hook=ship_contract.unique_json_object)

    manifest_bytes = (directory / 'manifest.json').read_bytes()
    if sha(manifest_bytes) != COVERAGE_MANIFEST_SHA256:
        raise UnqualifiedCoverage('unadmitted coverage manifest identity')
    manifest = decode(manifest_bytes)
    require(manifest['schema'] == 'ww149b-dol-coverage-manifest-v1', 'coverage manifest schema')
    check_coverage_source()
    inputs = manifest['inputs']
    for name, pin in inputs.items():
        require(Path(name).is_absolute(), 'coverage input path must be absolute')
        pinned_file(Path(name), pin)
    outputs = manifest['scripts_and_outputs']
    require(set(outputs) == {'inventory.py', 'test_inventory.py', 'REPORT.md', 'test-results.txt',
                             'inventory.json', 'inputs.json'}, 'coverage dependency set mismatch')
    checked = {name: pinned_file(directory / name, pin) for name, pin in outputs.items()}
    require(decode(checked['inputs.json']) == inputs, 'coverage input inventory mismatch')
    inventory = decode(checked['inventory.json'])
    require(inventory['schema'] == 'ww149b-dol-coverage-supplement-v1' and
            inventory['source_head'] == COVERAGE_SOURCE_HEAD and inventory['claim'] == COVERAGE_CLAIM and
            inventory['partition_instruction_limit'] == 4096 and
            inventory['aliases']['tagged_fallback'] == COVERAGE_TAGGED_SCOPE and
            inventory['target_census']['canonical_replay_qualified'] is False and
            inventory['target_census']['uncovered'] == 0, 'coverage admission scope mismatch')
    spec_pins = [pin for name, pin in inputs.items() if Path(name).name == 'spec-table.json']
    require(len(spec_pins) == 1 and spec_pins[0]['sha256'] == SPEC_SHA256, 'coverage spec identity')
    dol_pins = [pin for name, pin in inputs.items() if name.endswith('/private/main.dol')]
    require(len(dol_pins) == 1 and dol_pins[0] == {'sha256': sha(dol), 'size': len(dol)},
            'coverage DOL identity mismatch')
    sections = []
    expected_chunks = []
    dol_text = dol_text_sections(dol)
    for index in range(7):
        size = u32(dol, 0x90 + index * 4)
        if not size:
            continue
        start = u32(dol, 0x48 + index * 4)
        sections.append(dict(index=index, file_offset=u32(dol, index * 4),
                             start=f'{start:08X}', end_exclusive=f'{start + size:08X}'))
        for chunk_start in range(start, start + size, 4096 * 4):
            expected_chunks.append((f'chunk_{len(expected_chunks):04d}_text{index}_{chunk_start:08X}.c',
                                    chunk_start, min(chunk_start + 4096 * 4, start + size)))
    require(inventory['text_sections'] == sections, 'coverage DOL text section mismatch')
    chunks = inventory['chunks']
    require(len(chunks) == len(expected_chunks), 'coverage derived partition count')
    by_name = {}
    for chunk, (name, start, end) in zip(chunks, expected_chunks):
        slots = (end - start) // 4
        require((chunk['basename'], chunk['start'], chunk['end_exclusive'], chunk['function']) ==
                (name, f'{start:08X}', f'{end:08X}', f'func_{start:08X}') and
                chunk['slots'] == chunk['labels'] == slots and
                chunk['ordered_slots_sha256'] == sha(''.join(f'{pc:08X}\n' for pc in range(start, end, 4)).encode()),
                'coverage chunk association mismatch')
        by_name[name] = (start, end)
    digest = sha(''.join(f'{chunk["sha256"]} {chunk["basename"]}\n' for chunk in
                         sorted(chunks, key=lambda item: item['basename'])).encode())
    require(inventory['dol_inventory_sha256'] == digest, 'coverage DOL inventory digest')
    source_directories = {Path(name).parent for name in inputs if Path(name).suffix == '.c'}
    require(len(source_directories) == 2 and all(
        {path.name for path in directory.glob('*.c')} == set(by_name) for directory in source_directories),
        'current source DOL inventory or delivered DOL inventory mismatch')
    headers = [(name, pin) for name, pin in inputs.items() if name.endswith('/composite-batch/generated.h')]
    require(len(headers) == 1, 'coverage delivered header association')
    header_path, header_pin = headers[0]
    pinned_file(composite / 'generated.h', header_pin)
    delivered_directory = Path(header_path).parent / 'chunks_dol'
    delivered_pins = {Path(name).name: pin for name, pin in inputs.items()
                      if Path(name).parent == delivered_directory}
    require(set(delivered_pins) == set(by_name) == {path.name for path in (composite / 'chunks_dol').glob('*.c')},
            'actual delivered DOL inventory mismatch')
    for chunk in chunks:
        pin = delivered_pins[chunk['basename']]
        require(pin['sha256'] == chunk['sha256'], 'coverage delivered hash association')
        pinned_file(composite / 'chunks_dol' / chunk['basename'], pin)
    modules = [module for module in inventory['rels'] if module['id'] == row['id']]
    require(len(modules) == 1 and modules[0]['processed_sha256'] == row['processed_sha256'] and
            modules[0]['raw_reverse_fixups_sha256'] == row['raw_sha256'] and
            modules[0]['path'] == 'build-f5/ww05/private/rels/' + row['basename'] and
            inputs[str(COVERAGE_SOURCE / modules[0]['path'])]['sha256'] == row['processed_sha256'],
            'coverage module identity mismatch')

    def containing(address):
        physical = coverage_address(address)
        require(any(start <= physical and physical + 4 <= end for start, end in dol_text),
                'coverage target outside DOL text')
        found = [(name, start) for name, (start, end) in by_name.items() if start <= physical < end]
        require(len(found) == 1, 'coverage target missing qualified chunk')
        name, start = found[0]
        return physical, name, (physical - start) // 4

    targets = {int(target['target'], 16): target for target in inventory['targets']}
    require(len(targets) == len(inventory['targets']), 'duplicate coverage target')
    module_targets = {pc for pc, target in targets.items()
                      if any(record[0] == row['id'] for record in target['records'])}
    require(set(external) == module_targets, 'independently derived module targets differ from inventory')
    for address in external:
        physical, name, slot = containing(address)
        target = targets[physical]
        require(target['chunk'] == name and target['slot'] == slot and target['label_line'] > 0 and
                target['tagged_alias'] == f'{physical + 0x40000000:08X}', 'coverage target association mismatch')
    physical, name, slot = containing(walker)
    walker_inventory = inventory['walker']
    require(walker_inventory['start'] == f'{physical:08X}' and physical in module_targets,
            'constructor walker is outside admitted walker scope')
    entries = [entry for entry in walker_inventory['entries'] if entry['pc'] == f'{physical:08X}']
    require(len(entries) == 1 and entries[0]['tagged'] == f'{physical + 0x40000000:08X}' and
            entries[0]['slot'] == slot, 'constructor walker entry association mismatch')
    offset, size = walker_inventory['file_offset'], walker_inventory['size']
    end = int(walker_inventory['end_exclusive'], 16)
    spans = [section for section in sections if int(section['start'], 16) <= physical <
             end <= int(section['end_exclusive'], 16)]
    require(len(spans) == 1, 'constructor walker span outside admitted DOL text')
    section = spans[0]
    require(offset == section['file_offset'] + physical - int(section['start'], 16) and
            size == end - physical and sha(dol[offset:offset + size]) == walker_inventory['span_sha256'],
            'constructor walker DOL span identity mismatch')
    check_coverage_source()
    return dict(status='VERIFIED-candidate', claim=COVERAGE_CLAIM,
                manifest_sha256=COVERAGE_MANIFEST_SHA256, source_revision=COVERAGE_SOURCE_HEAD,
                consumed_input_count=len(inputs), dependencies=outputs, dol_inventory_sha256=digest,
                delivered_dol_chunks=len(chunks), independently_derived_module_targets=len(external),
                walker=f'{physical:08X}', limits=[
                    'Historical identity and derived structural coverage only; not original emission-event proof.',
                    'No instruction-to-C semantic equivalence, physical-alias qualification or runtime validation.',
                    'Contract NOT ACTIVE; corrective review and explicit coordinator activation remain required.',
                    'Not PASS-v2, report adoption, fresh emission or gameplay qualification.'])


def dol_text_sections(dol):
    require(len(dol) >= 0x100, 'truncated DOL header')
    result = []
    for index in range(7):
        offset, address, size = (u32(dol, base + index * 4) for base in (0, 0x48, 0x90))
        if not size:
            continue
        require(offset >= 0x100 and offset + size <= len(dol) and address % 4 == 0 and
                size % 4 == 0 and 0x80000000 <= address < address + size <= 0x81800000,
                'DOL executable bounds/alignment')
        require(all(address + size <= a or address >= b for a, b in result), 'overlapping DOL text')
        result.append((address, address + size))
    require(result, 'missing DOL text')
    return result


def verify_inputs(row, raw, processed):
    for name, data in [('raw', raw), ('processed', processed)]:
        require(len(data) == row[name + '_size'] and sha(data) == row[name + '_sha256'],
                name + ' REL identity mismatch')
    fixed = bytearray(raw)
    for fixup in row['fixups']:
        require(fixup['width'] == 4 and fixup['offset'] == 0x40 and fixup['after'] == 4 and
                u32(raw, 0x40) == fixup['before'], 'unsupported processing fixup')
        struct.pack_into('>I', fixed, 0x40, 4)
    require(bytes(fixed) == processed, 'processing fixup mismatch')
    alignment = u32(raw, 0x44) or 4
    require(alignment & (alignment - 1) == 0, 'BSS alignment')
    cursor = (len(raw) + alignment - 1) // alignment * alignment
    for section in row['sections']:
        size, offset = section['size'], section['offset']
        linked = 0 if not size else row['linked_base'] + (offset or cursor)
        require(section['linked_start'] == linked, 'frozen linked layout mismatch')
        if size and not offset:
            cursor = (cursor + size + alignment - 1) // alignment * alignment


def implementation_pins():
    root = Path(__file__).resolve().parents[1]
    files = ['scripts/verify_canonical_executable.py', 'tests/test_canonical_executable.py',
             'tests/test_canonical_coverage.py', 'tests/test_canonical_inherited.py',
             'tests/test_canonical_certificate.py',
             'scripts/ship_contract.py', 'scripts/bulk_rel_catalog.json', 'scripts/prepare.py',
             'scripts/wwhd/formats.py']
    return {name: sha((root / name).read_bytes()) for name in files}


def qualify(args):
    import prepare
    import ship_contract

    spec_bytes = args.spec.read_bytes()
    require(sha(spec_bytes) == SPEC_SHA256, 'frozen spec identity mismatch')
    spec = json.loads(spec_bytes, object_pairs_hook=ship_contract.unique_json_object)
    row, = [row for row in spec['modules'] if row['id'] == args.module]
    require(row['bulk_certificate_candidate'], 'module outside admitted candidate scope')
    head = subprocess.check_output(['git', '-C', str(args.delivery), 'rev-parse', 'HEAD'], text=True).strip()
    require(head == DELIVERY, 'delivery revision mismatch')
    require(not subprocess.check_output(['git', '-C', str(args.delivery), 'status', '--porcelain=v1']),
            'delivery worktree not clean')
    dependencies = implementation_pins()
    root = Path(__file__).resolve().parents[1]
    for name in dependencies:
        if name.startswith('scripts/') and name != 'scripts/verify_canonical_executable.py':
            pinned = subprocess.check_output(['git', '-C', str(root), 'show', DELIVERY + ':' + name])
            require(sha(pinned) == dependencies[name], 'unreviewed dependency drift: ' + name)
    with args.iso.open('rb') as stream:
        require(args.iso.stat().st_size == spec['source']['size'] and
                hashlib.file_digest(stream, 'sha256').hexdigest() == spec['source']['sha256'],
                'owned ISO identity mismatch')
        stream.seek(0)
        header = prepare.validate_disc_header(stream)
        info = prepare.extract_dol_info(stream, header['dol_offset'])
        stream.seek(header['dol_offset'])
        dol = stream.read(info['total_size'])
    require(len(dol) == spec['source']['dol_size'] and sha(dol) == spec['source']['dol_sha256'],
            'ISO DOL identity mismatch')
    raw, source = ship_contract.read_disc_member(args.iso, args.module)
    require(source == ({'member': row['member']} if row['source_kind'] == 'loose' else
                       {'archive': row['archive'], 'member': row['member']}), 'ISO member provenance')
    processed = bytearray(raw)
    struct.pack_into('>I', processed, 0x40, 4)
    verify_inputs(row, raw, bytes(processed))
    canonical, external = relocate(raw, row['sections'], args.module, dol_text_sections(dol))
    again, _ = relocate(processed, row['sections'], args.module, dol_text_sections(dol))
    require(canonical == again, 'raw/processed canonical mismatch')
    payload = canonical[row['exec']['index']]
    composite = args.evidence / 'composite-batch'
    capture, inherited = capture_input(row, args.evidence)
    if inherited is not None:
        require(inherited['dol_sha256'] == sha(dol), 'inherited capture DOL identity mismatch')
    capture_row = row if inherited is None else row | {'capture_magic_hex': inherited['capture_magic_hex']}
    binding = bind(capture_row, payload, capture,
                   (composite / 'rel_data.inc').read_text(), (composite / 'module_tables.inc').read_text())
    receipt = json.loads((composite / 'generation_receipt.json').read_text(),
                          object_pairs_hook=ship_contract.unique_json_object)
    data_text = (composite / 'rel_data.inc').read_text()
    policy_text = (args.delivery / 'vita/host/bulk_actor_policy.inc').read_text()
    actual, policy = bind_full_contract_set(data_text, policy_text,
        receipt['selected_rel_contracts']['contracts'], ship_contract.ACTOR_BATCHES['bulk'])
    expected = certificate_preimage(row, dol, raw, bytes(processed), canonical)
    certificate = bind_certificate(row, expected, raw, dol, canonical, data_text,
        receipt['selected_rel_contracts']['contracts'][str(args.module)], actual, policy)
    shared_setup = shared_prior_setup(receipt, policy_text, root)
    if inherited is not None:
        inherited['prior_wave_pins'] = shared_setup['prior_wave_pins']
        inherited['prior_generation_receipt_sha256'] = INHERITED_PRIOR_RECEIPT_SHA256
    chunk_receipts = json.loads((args.evidence / 'receipts/delivery-canonical-chunks.json').read_text(),
                                object_pairs_hook=ship_contract.unique_json_object)
    verify_receipts(row, binding, chunk_receipts[str(args.module)],
                    receipt['selected_rel_contracts']['contracts'][str(args.module)])
    chunks = {Path(name).name: pin for name, pin in row['translation']['files_sha256'].items()
              if name.startswith('chunks/') and name.endswith('.c')}
    require(len(chunks) == row['translation']['chunk_count'], 'frozen chunk source inventory')
    translated = verify_translations(composite / ('chunks_' + Path(row['basename']).stem + '_' +
                                                str(args.module)), chunks)
    walker_pc = row['candidate_module_spec']['ctor_return_pc'] - 4
    at = walker_pc - row['exec']['linked_start']
    word = u32(payload, at)
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    target = (walker_pc + displacement) & 0xFFFFFFFF
    require(word >> 26 == 18 and word & 3 == 1 and target == row['ctor']['walker_address'] and
            (target & ~0x40000000) in external, 'constructor walker branch mismatch')
    coverage = coverage_dependency(args.dol_coverage, row, external, target, dol, composite)
    result = dict(status=coverage['status'], module_id=args.module, delivery=head,
                 spec_sha256=sha(spec_bytes), dependencies=dependencies, binding=binding,
                 certificate_binding=certificate, check5_shared_setup=shared_setup,
                python=sys.version, implementation_revision=subprocess.check_output(
                    ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                translated_c=translated, external_branch_target_count=len(external),
                dol_sha256=sha(dol), raw_rel_sha256=sha(raw), processed_rel_sha256=sha(processed),
                coverage=coverage, limits=[
                    'Qualification candidate only; no PASS-v2 or report upgrade.',
                    'No fresh emission, runtime, gameplay, or hardware qualification.'])
    if inherited is not None:
        result['inherited_capture'] = inherited
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', type=int, required=True)
    parser.add_argument('--spec', type=Path, required=True)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--delivery', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--dol-coverage', type=Path,
                        help='Exact admitted historical/structural DOL supplement, not a trust override')
    args = parser.parse_args()
    try:
        result = qualify(args)
    except UnqualifiedCoverage as error:
        print(json.dumps(dict(status='BLOCKED-incomplete', reason=str(error)), sort_keys=True))
        return 2
    except (ValueError, OSError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        print(json.dumps(dict(status='FAIL-mismatch-or-unsupported', error=str(error)), sort_keys=True))
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result['status'] == 'VERIFIED-candidate' else 2


if __name__ == '__main__':
    raise SystemExit(main())
