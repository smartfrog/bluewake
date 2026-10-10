"""Exact synthetic diagnostic title identity, not a substring match."""

import struct


def verify_contents(archive, title=b"BWWM00004\0"):
    label = "memory" if title == b"BWWM00004\0" else "ARM08"
    if archive.getinfo("eboot.bin").file_size > 16 * 1024 * 1024 or archive.getinfo("sce_sys/param.sfo").file_size > 65536:
        raise ValueError(f"{label} package exceeds bounded payload size")
    if archive.read("eboot.bin")[:4] != b"SCE\0":
        raise ValueError(f"{label} payload is not SELF")
    sfo = archive.read("sce_sys/param.sfo")
    if len(sfo) < 20:
        raise ValueError(f"truncated {label} SFO")
    magic, version, keys, values, count = struct.unpack_from("<5I", sfo)
    if magic != 0x46535000 or version != 0x101 or not 20 + count * 16 <= keys <= values <= len(sfo):
        raise ValueError(f"invalid {label} SFO structure")
    titles = []
    for index in range(count):
        key, kind, length, maximum, offset = struct.unpack_from("<HHIII", sfo, 20 + index * 16)
        start = keys + key
        end = sfo.find(b"\0", start, values)
        if start >= values or end < 0 or length > maximum or values + offset + maximum > len(sfo):
            raise ValueError(f"invalid {label} SFO entry")
        if sfo[start:end] == b"TITLE_ID":
            if kind != 0x204:
                raise ValueError(f"{label} title is not a string")
            titles.append(sfo[values + offset:values + offset + length])
    if titles != [title]:
        raise ValueError(f"not the exact {label} diagnostic title")
