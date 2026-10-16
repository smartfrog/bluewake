import pathlib
import struct
import sys
import unittest

from artifact_gate import Elf, check


ELF = pathlib.Path(sys.argv.pop(1)).read_bytes()


class ArtifactTests(unittest.TestCase):
    def test_actual_sdk_artifact_passes(self):
        self.assertEqual(check(ELF)["function_nid"], "14e9dbd7")

    def test_invalid_nid_fails(self):
        elf = Elf(ELF)
        data = bytearray(ELF)
        struct.pack_into("<I", data, elf.offset(elf.symbol("sceClibMemcpy")[0] + 8, 4), 0)
        with self.assertRaisesRegex(ValueError, "NID"):
            check(data)

    def test_missing_import_fails(self):
        elf = Elf(ELF)
        data = bytearray(ELF)
        struct.pack_into("<I", data, elf.symbol_offsets["sceClibMemcpy"], 0)
        with self.assertRaisesRegex(ValueError, "missing symbol"):
            check(data)

    def test_local_memcpy_fails(self):
        elf = Elf(ELF)
        data = bytearray(ELF)
        address = elf.symbol("memcpy")[0] & ~1
        data[elf.offset(address, 4):elf.offset(address, 4) + 4] = bytes.fromhex("704700bf")
        with self.assertRaisesRegex(ValueError, "B.W"):
            check(data)

    def test_wrong_veneer_target_fails(self):
        elf = Elf(ELF)
        data = bytearray(ELF)
        address = (elf.symbol("__sceClibMemcpy_from_thumb")[0] & ~1) + 12
        struct.pack_into("<I", data, elf.offset(address, 4), 0)
        with self.assertRaisesRegex(ValueError, "reach import"):
            check(data)

    def test_copy_bypassing_memcpy_fails(self):
        elf = Elf(ELF)
        data = bytearray(ELF)
        address = (elf.symbol("probe_copy")[0] & ~1) + 4
        data[elf.offset(address, 4):elf.offset(address, 4) + 4] = bytes.fromhex("00bf00bf")
        with self.assertRaisesRegex(ValueError, "Thumb BL"):
            check(data)


if __name__ == "__main__":
    unittest.main()
