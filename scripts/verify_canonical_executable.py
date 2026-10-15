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
        require(imported in (0, module_id), 'unsupported import')
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
            require(kind in (1, 4, 6, 10), 'unsupported relocation type')
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


def block(text, ctype, name):
    text = re.sub(r'//[^\n]*', '', text)
    for line in text.splitlines():
        if line.lstrip().startswith('#'):
            require(re.fullmatch(r'#define MODULE_(?:REL_DATA_COUNT|REL_LIFECYCLE_COUNT|'
                                r'REL_LIFECYCLE_V2_COUNT|SHIP_CONTRACT_PRESENT|'
                                r'REL_INIT_CONTRACTS_PRESENT|CODE_RANGE_COUNT|SMC_RANGE_COUNT|'
                                r'CHUNK_RANGE_COUNT) [0-9]+u?', line) is not None,
                    'unsupported preprocessor directive')
    require('/*' not in text, 'block commented C unsupported')
    matches = re.findall(r'^static const ' + re.escape(ctype) + r'\s+' + re.escape(name) +
                         r'\[\]\s*=\s*\{(.*?)\n?\};', text, re.S | re.M)
    require(len(matches) == 1, f'missing/duplicate C table {name}')
    return matches[0]


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
    contracts = rows(block(data_text, 'BlueWakeRelInitContractV1', 's_rel_init_contracts'),
                     r'\{\s*' + NUM + r'\s*,\s*' + NUM +
                     r'\s*,\s*(\w+),\s*(\w+),\s*' + NUM + r',\s*' + NUM + r',\s*' +
                     NUM + r',\s*' + NUM + r',\s*&s_rel_lifecycle_v2\[\d+\],\s*"[^"\n]+"\},\s*')
    associations = [entry for entry in contracts if int(entry[0], 0) == row['id']]
    require(len(associations) == 1 and associations[0][2:4] ==
            (prefix + '_raw_rel', prefix + '_canonical') and
            int(associations[0][4], 0) == len(descriptors), 'module canonical association mismatch')
    ranges = [numbers(pair) for pair in rows(block(table_text, 'StaticRecompRange', 's_chunk_ranges'),
              r'\{\s*' + NUM + r'\s*,\s*' + NUM + r'\s*\},\s*')]
    hashes = rows(block(table_text, 'u64', 's_chunk_hashes'), r'\s*0x([0-9A-Fa-f]+)ull,\s*')
    require(len(ranges) == len(hashes), 'runtime range/hash count')
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


def coverage_status():
    return dict(status='BLOCKED-incomplete', reason=(
        'No independently reviewed pinned DOL translated inventory provenance is admitted. '
        'DOL text containment and current generated.h identity are not translated coverage proof. '
        'External REL24 targets and constructor walker require that evidence. '
        'Contract activation and independent method/code review remain pending.'))


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
    binding = bind(row, payload, (args.evidence / 'captures/bulk' / row['capture_basename']).read_bytes(),
                   (composite / 'rel_data.inc').read_text(), (composite / 'module_tables.inc').read_text())
    receipt = json.loads((composite / 'generation_receipt.json').read_text(),
                         object_pairs_hook=ship_contract.unique_json_object)
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
    return dict(status='BLOCKED-incomplete', module_id=args.module, delivery=head,
                spec_sha256=sha(spec_bytes), dependencies=dependencies, binding=binding,
                python=sys.version, implementation_revision=subprocess.check_output(
                    ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                translated_c=translated, external_branch_target_count=len(external),
                dol_sha256=sha(dol), raw_rel_sha256=sha(raw), processed_rel_sha256=sha(processed),
                coverage=coverage_status(), limits=[
                    'Qualification candidate only; no PASS-v2 or report upgrade.',
                    'No fresh emission, runtime, gameplay, or hardware qualification.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', type=int, required=True)
    parser.add_argument('--spec', type=Path, required=True)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--delivery', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = qualify(args)
    except (ValueError, OSError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        print(json.dumps(dict(status='FAIL-mismatch-or-unsupported', error=str(error)), sort_keys=True))
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
