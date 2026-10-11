"""Identify the documented init program without publishing its private bytes."""

import hashlib
import struct

INIT_UCODE_SOURCE = 0x8039F5B0
INIT_UCODE_BYTES = 128
INIT_UCODE_SHA256 = '5b873ee91f33b0884574b814d7f46d81e283394de91b8b2ea9073ac218dee350'


def extract_init_ucode(dol):
    if len(dol) < 256:
        raise ValueError('truncated DOL header for DSP init identity')
    mappings = []
    for count, offsets, addresses, sizes in ((7, 0, 0x48, 0x90), (11, 0x1C, 0x64, 0xAC)):
        for index in range(count):
            offset, address, size = (struct.unpack_from('>I', dol, base + 4 * index)[0]
                                     for base in (offsets, addresses, sizes))
            if address <= INIT_UCODE_SOURCE and INIT_UCODE_SOURCE + INIT_UCODE_BYTES <= address + size:
                start = offset + INIT_UCODE_SOURCE - address
                if offset < 256 or offset + size > len(dol):
                    raise ValueError('truncated or invalid DOL payload for DSP init identity')
                mappings.append(start)
    if len(mappings) != 1:
        raise ValueError('DSP init reference requires exactly one complete DOL mapping')
    code = bytes(dol[mappings[0]:mappings[0] + INIT_UCODE_BYTES])
    if hashlib.sha256(code).hexdigest() != INIT_UCODE_SHA256:
        raise ValueError('DSP init ucode identity differs from the documented program')
    return code
