import hashlib
import struct


def identified_dol_range(dol, source, size, identity):
    if len(dol) < 256:
        raise ValueError('truncated DOL header for code identity')
    mappings = []
    for count, offsets, addresses, sizes in ((7, 0, 0x48, 0x90), (11, 0x1C, 0x64, 0xAC)):
        for index in range(count):
            offset, address, length = (struct.unpack_from('>I', dol, base + 4 * index)[0]
                                       for base in (offsets, addresses, sizes))
            if address <= source and source + size <= address + length:
                if offset < 256 or offset + length > len(dol):
                    raise ValueError('truncated or invalid DOL payload for code identity')
                mappings.append(offset + source - address)
    if len(mappings) != 1:
        raise ValueError('code reference requires exactly one complete DOL mapping')
    code = bytes(dol[mappings[0]:mappings[0] + size])
    if hashlib.sha256(code).hexdigest() != identity:
        raise ValueError('DOL code identity differs from the documented program')
    return code
