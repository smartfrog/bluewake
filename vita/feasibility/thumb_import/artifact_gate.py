"""Check the linked public ELF, not source spelling or a replacement memcpy."""

import argparse
import json
import pathlib
import re
import struct
import subprocess


class Elf:
    def __init__(self, data):
        self.data = data
        if data[:7] != b"\x7fELF\x01\x01\x01":
            raise ValueError("requires little-endian ELF32")
        header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        if header[1] != 40 or not header[6] & 0x400:
            raise ValueError("requires ARM hard-float ELF")
        shoff, shsize, shnum = header[5], header[10], header[11]
        self.sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shsize)
                         for i in range(shnum)]
        self.symbols = {}
        self.symbol_offsets = {}
        for section in self.sections:
            if section[1] != 2:
                continue
            strings = self.sections[section[6]]
            names = data[strings[4]:strings[4] + strings[5]]
            for offset in range(section[4], section[4] + section[5], section[9]):
                name, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, offset)
                text = names[name:names.find(b"\0", name)].decode()
                if text:
                    self.symbols[text] = (value, size, index)
                    self.symbol_offsets[text] = offset

    def symbol(self, name):
        if name not in self.symbols:
            raise ValueError("missing symbol: " + name)
        return self.symbols[name]

    def offset(self, address, count):
        for section in self.sections:
            if section[1] != 8 and section[3] <= address and address + count <= section[3] + section[5]:
                return section[4] + address - section[3]
        raise ValueError("address outside file-backed section")

    def read(self, address, count):
        offset = self.offset(address, count)
        return self.data[offset:offset + count]


def branch_target(address, raw, link):
    first, second = struct.unpack("<HH", raw)
    if first & 0xf800 != 0xf000 or second & 0xd000 != (0xd000 if link else 0x9000):
        raise ValueError("expected Thumb BL" if link else "expected Thumb B.W")
    sign = (first >> 10) & 1
    i1 = 1 ^ ((second >> 13) & 1) ^ sign
    i2 = 1 ^ ((second >> 11) & 1) ^ sign
    displacement = (sign << 24) | (i1 << 23) | (i2 << 22) | ((first & 1023) << 12) | ((second & 2047) << 1)
    if sign:
        displacement -= 1 << 25
    return address + 4 + displacement


def require(condition, message):
    if not condition:
        raise ValueError(message)


def control_calls_open(address, code, target):
    for offset in range(0, len(code) - 3, 2):
        first, second = struct.unpack_from("<HH", code, offset)
        if first & 0xf800 == 0xf000 and second & 0xd001 == 0xc000:
            sign = (first >> 10) & 1
            i1 = 1 ^ ((second >> 13) & 1) ^ sign
            i2 = 1 ^ ((second >> 11) & 1) ^ sign
            displacement = (sign << 24) | (i1 << 23) | (i2 << 22) | ((first & 1023) << 12) | ((second & 2046) << 1)
            if sign:
                displacement -= 1 << 25
            if ((address + offset + 4) & ~3) + displacement == target:
                return True
    return False


def check(data):
    elf = Elf(data)
    copy, size, _ = elf.symbol("probe_copy")
    memcpy, memcpy_size, _ = elf.symbol("memcpy")
    veneer, _, _ = elf.symbol("__sceClibMemcpy_from_thumb")
    stub, _, stub_section = elf.symbol("sceClibMemcpy")
    require(copy & 1 and memcpy & 1 and veneer & 1 and not stub & 3, "wrong instruction state")
    copy &= ~1
    memcpy &= ~1
    veneer &= ~1
    require(size == 10 and elf.read(copy, 4) == bytes.fromhex("f02208b5"), "copy must set r2=240, preserve r0/r1, push LR")
    require(branch_target(copy + 4, elf.read(copy + 4, 4), True) == memcpy, "copy BL bypasses memcpy")
    require(elf.read(copy + 8, 2) == bytes.fromhex("08bd"), "missing interworking return")
    require(memcpy_size == 4, "local memcpy implementation is forbidden")
    require(branch_target(memcpy, elf.read(memcpy, 4), False) == veneer, "memcpy bypasses SDK veneer")
    require(elf.read(veneer, 12) == bytes.fromhex("7847fde700c09fe50ff08ce0"), "wrong Thumb-to-ARM veneer")
    literal = struct.unpack("<i", elf.read(veneer + 12, 4))[0]
    require((veneer + 16 + literal) & 0xffffffff == stub, "veneer does not reach import stub")
    section = elf.sections[stub_section]
    require(section[1] == 1 and section[2] & 4 and section[3] <= stub < section[3] + section[5], "missing executable import section")
    require(struct.unpack("<IIII", elf.read(stub, 16)) == (0, 0xcae9ace6, 0x14e9dbd7, 0xe320f000), "wrong SCE import library/NID/stub")
    control, control_size, _ = elf.symbol("probe_control")
    control_code = elf.read(control & ~1, control_size)
    open_address, _, open_section = elf.symbol("sceIoOpen")
    require(open_section == stub_section, "missing independent SceLibKernel control")
    require(struct.unpack("<IIII", elf.read(open_address, 16)) == (0, 0xcae9ace6, 0x6c60ac61, 0xe320f000), "wrong sceIoOpen import")
    require(control_calls_open(control & ~1, control_code, open_address), "control does not BLX SDK sceIoOpen")
    for name in ("source", "destination"):
        address, count, _ = elf.symbol(name)
        require(address % 16 == 0 and count == 272, "invalid aligned guarded buffer")
    return {"copy_bl": hex(copy + 4), "memcpy": hex(memcpy), "veneer": hex(veneer),
            "veneer_literal": hex(literal), "import_stub": hex(stub),
            "library_nid": "cae9ace6", "function_nid": "14e9dbd7", "runtime_tested": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--sdk", type=pathlib.Path, required=True)
    parser.add_argument("--copy-object", type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = check(args.elf.read_bytes())
    disassembly = subprocess.check_output([str(args.sdk / "bin/arm-vita-eabi-objdump"), "-d", str(args.elf)], text=True)
    metadata = subprocess.check_output([str(args.sdk / "bin/arm-vita-eabi-readelf"), "-h", "-A", "-W", str(args.elf)], text=True)
    copy_metadata = subprocess.check_output([str(args.sdk / "bin/arm-vita-eabi-readelf"), "-s", "-r", "-W", str(args.copy_object)], text=True)
    require(re.search(r"bl\s+[0-9a-f]+ <memcpy>", disassembly), "objdump did not confirm actual BL memcpy")
    require("Tag_ABI_VFP_args: VFP registers" in metadata and "Tag_CPU_arch: v7" in metadata, "readelf ABI mismatch")
    require(re.search(r"R_ARM_THM_CALL\s+00000000\s+memcpy", copy_metadata) and
            re.search(r"GLOBAL\s+DEFAULT\s+UND\s+memcpy", copy_metadata), "copy object must have an unresolved external Thumb memcpy call")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
