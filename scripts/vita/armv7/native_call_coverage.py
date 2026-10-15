#!/usr/bin/env python3
"""Inventory every call instruction in the reused suspect object, without game payload output."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

INSTRUCTION = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4,8}\s+)+)\s*(\S+)\s*(.*)$')
RELOCATION = re.compile(r'^\s*([0-9a-f]+):\s+(R_ARM_\S+)\s+(\S+)')
WRAPPED = {'ppc_guest_alias_resolve', 'ppc_fadds', 'ppc_fcmp', 'ppc_fctiw', 'ppc_fdivs',
           'ppc_fmul', 'ppc_fmuls', 'ppc_fp_available', 'ppc_frsp', 'ppc_frsqrte',
           'ppc_fsub', 'ppc_fsubs', 'ppc_psq_load', 'ppc_psq_store'}
CALLBACKS = {3400: 'external_read', 3404: 'external_write', 3420: 'host_call',
             3464: 'spr_read', 3468: 'spr_write', 3472: 'cache_control', 3416: 'instruction_fallback'}


def command(*arguments):
    return subprocess.check_output([str(value) for value in arguments], text=True)


def symbols(nm, path):
    result = {}
    for line in command(nm, '-n', '-S', path).splitlines():
        match = re.fullmatch(r'([0-9a-f]+)\s+([0-9a-f]+)\s+\S\s+(\S+)', line)
        if match:
            result[match[3]] = (int(match[1], 16), int(match[2], 16))
    return result


def elf_sections(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('expected little-endian ELF32')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<10I', data, header[6] + i * header[11]) for i in range(header[12])]
    names = sections[header[13]]
    table = data[names[4]:names[4] + names[5]]
    return data, {table[row[0]:].split(b'\0', 1)[0].decode(): row for row in sections}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--object', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--objdump', default='/home/fred/vitasdk/bin/arm-vita-eabi-objdump')
    parser.add_argument('--nm', default='/home/fred/vitasdk/bin/arm-vita-eabi-nm')
    args = parser.parse_args()
    original_symbols = symbols(args.nm, args.object)
    linked_symbols = symbols(args.nm, args.elf)
    original_start = original_symbols['func_802AD6E0'][0]
    linked_start = linked_symbols['func_802AD6E0'][0]
    delta = linked_start - original_start
    text = command(args.objdump, '-dr', args.object)
    linked = command(args.objdump, '-d', '--start-address=' + str(delta),
                     '--stop-address=' + str(linked_start + original_symbols['func_802AD6E0'][1]), args.elf)
    linked_instructions = {}
    for line in linked.splitlines():
        match = INSTRUCTION.match(line)
        if match:
            linked_instructions[int(match[1], 16)] = (match[3], match[4])
    relocations = {}
    for line in text.splitlines():
        match = RELOCATION.match(line)
        if match:
            relocations[int(match[1], 16)] = match[3]
    inventory, previous = [], []
    for line in text.splitlines():
        match = INSTRUCTION.match(line)
        if not match:
            continue
        offset, encoding, mnemonic, operand = int(match[1], 16), match[2], match[3], match[4]
        if mnemonic not in ('bl', 'blx', 'bl.w'):
            previous.append((offset, mnemonic, operand))
            previous = previous[-12:]
            continue
        target = relocations.get(offset)
        wrapper, covered, layout = None, False, 'unresolved-indirect'
        if target in WRAPPED:
            wrapper, covered = '__wrap_' + target, True
            layout = ('hard-float-d0-d1' if target in ('ppc_fcmp', 'ppc_fctiw', 'ppc_frsqrte') else
                      'r0-r3-plus-3-stack-words' if target in ('ppc_psq_load', 'ppc_psq_store') else 'r0-r3')
        elif target is None and '<' in operand:
            target = operand.split('<', 1)[1].split('>', 1)[0]
            layout = 'local-private-helper'
        elif target is None:
            register = operand.strip()
            for prior_index in range(len(previous) - 1, -1, -1):
                prior_offset, prior_mnemonic, prior_operand = previous[prior_index]
                if prior_mnemonic.startswith('ldr'):
                    load = re.match(re.escape(register) + r', \[r4, #(\d+)\]', prior_operand)
                    if load and int(load[1]) in CALLBACKS:
                        target = CALLBACKS[int(load[1])]
                        wrapper, covered, layout = 'installed-assembly-shim', True, 'CPUState-callback'
                        break
                    load = re.match(re.escape(register) + r', \[(r\d+)(?:, #0)?\]', prior_operand)
                    if load:
                        base = load[1]
                        for definition_offset, definition_mnemonic, definition_operand in reversed(previous[:prior_index]):
                            if definition_operand.startswith(base + ',') and definition_mnemonic.startswith(('movw', 'movt')):
                                if relocations.get(definition_offset) == 'g_mem_write_journal':
                                    target, wrapper, covered, layout = 'g_mem_write_journal', 'ww_native_journal', True, 'r0-r2-installed-pointer-if-non-NULL'
                                break
                            if definition_operand.startswith(base + ',') and not definition_mnemonic.startswith(('cmp', 'tst', 'str')):
                                break
                        if covered:
                            break
                if prior_operand.startswith(register + ',') and prior_mnemonic not in ('cmp', 'tst', 'str', 'str.w'):
                    break
        actual = linked_instructions.get(delta + offset)
        if actual is None or actual[0] not in ('bl', 'blx', 'bl.w'):
            raise ValueError('linked call instruction missing or relaxed unexpectedly at ' + hex(delta + offset))
        veneer = actual[1].split('<', 1)[1].split('>', 1)[0] if '<' in actual[1] else None
        if wrapper and wrapper.startswith('__wrap_') and (wrapper not in (veneer or '')):
            raise ValueError('external call does not resolve to its wrapper: ' + str(target))
        inventory.append({'object_instruction': hex(offset), 'instruction': hex(delta + offset),
                          'continuation': hex(delta + offset + len(''.join(encoding.split())) // 2),
                          'mnemonic': mnemonic, 'real_target': target or 'unresolved-register-' + operand,
                          'wrapper': wrapper, 'veneer_or_linked_target': veneer,
                          'argument_layout': layout, 'covered': covered,
                          'limitation': None if covered else 'private/local or unresolved indirect; not exonerated'})
        previous = []
    data, sections = elf_sections(args.elf)
    wrapper = linked_symbols['__wrap_func_802AD6E0'][0] | 1
    table_refs = []
    for name, (base, size) in linked_symbols.items():
        if name != 's_dolrecomp_chunk_fns' and not name.startswith('s_fns.'):
            continue
        for section in sections.values():
            if section[3] <= base and base + size <= section[3] + section[5] and section[1] != 8:
                raw = data[section[4] + base - section[3]:section[4] + base - section[3] + size]
                for i in range(0, len(raw) - 3, 4):
                    if struct.unpack_from('<I', raw, i)[0] == wrapper:
                        table_refs.append(hex(base + i))
    if not table_refs:
        raise ValueError('chunk function-pointer table does not reference its tail wrapper')
    r4_writes = [{'instruction': hex(address), 'mnemonic': instruction[0], 'operands': instruction[1]}
                 for address, instruction in linked_instructions.items()
                 if address >= linked_start and re.match(r'r4,', instruction[1]) and
                 not instruction[0].startswith(('str', 'cmp', 'tst', 'stm'))]
    result = {'object': str(args.object), 'elf': str(args.elf),
              'object_sha256': hashlib.sha256(args.object.read_bytes()).hexdigest(),
              'elf_sha256': hashlib.sha256(data).hexdigest(), 'calls': inventory,
              'summary': dict(Counter('covered' if row['covered'] else 'uncovered' for row in inventory)),
              'chunk_entry_wrapper_table_references': table_refs,
              'chunk_explicit_r4_writes': r4_writes,
              'journal': 'installed pointer wrapped if non-NULL; fixed header slot 1280 records pointer/real target/NULL at arming; no trial so actual state remains unobserved',
              'limits': ['All BL/BLX in the reused object inventoried, including its local helper bodies.',
                         'Unresolved indirect calls remain uncovered; no speculative register dataflow claim.',
                         'Same-translation-unit calls and mid-function entry can bypass link wrappers.',
                         'No trial and no native core; native fault classification unresolved.']}
    args.out.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
