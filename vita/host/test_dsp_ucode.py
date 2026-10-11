import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))


def synthetic_dol():
    code = bytes(range(128))
    dol = bytearray(256) + code
    for offset, value in ((0x1C, 256), (0x64, 0x8039F5B0), (0xAC, 128)):
        struct.pack_into('>I', dol, offset, value)
    return dol, code


class DspUcodeTest(unittest.TestCase):
    def test_exact_identified_init_is_extracted_from_one_complete_dol_section(self):
        import dsp_ucode
        dol, code = synthetic_dol()
        with patch.object(dsp_ucode, 'INIT_UCODE_SHA256', hashlib.sha256(code).hexdigest()):
            self.assertEqual(dsp_ucode.extract_init_ucode(dol), code)

    def test_changed_init_is_rejected_even_in_a_well_formed_dol(self):
        import dsp_ucode
        dol, _ = synthetic_dol()
        with self.assertRaisesRegex(ValueError, 'identity'):
            dsp_ucode.extract_init_ucode(dol)

    def test_truncated_dol_header_is_rejected(self):
        import dsp_ucode
        with self.assertRaisesRegex(ValueError, 'header'):
            dsp_ucode.extract_init_ucode(bytes(255))

    def test_truncated_init_payload_is_rejected(self):
        import dsp_ucode
        dol, _ = synthetic_dol()
        with self.assertRaisesRegex(ValueError, 'payload'):
            dsp_ucode.extract_init_ucode(dol[:-1])

    def test_overlapping_dol_sections_cannot_choose_an_arbitrary_reference(self):
        import dsp_ucode
        dol, _ = synthetic_dol()
        for offset, value in ((0x20, 256), (0x68, 0x8039F5B0), (0xB0, 128)):
            struct.pack_into('>I', dol, offset, value)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            dsp_ucode.extract_init_ucode(dol)

    def test_missing_init_mapping_is_rejected(self):
        import dsp_ucode
        dol, _ = synthetic_dol()
        struct.pack_into('>I', dol, 0x64, 0x8039F600)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            dsp_ucode.extract_init_ucode(dol)


if __name__ == '__main__':
    unittest.main()
