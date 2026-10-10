from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/vita/armv7"))


def host_report(token="a" * 32, source="b" * 40, package="c" * 64):
    sizes = (25165824, 33554432, 16384, 64)
    touched = (393280, 524352, 320, 64)
    lines = [f"MEM {token} {source} {package} method=bounded-v1 phase=guest platform=HOST",
             "ABI pointer=8 cpu=3552 version=6 ram_offset=3456 external_offset=3472",
             "RESERVED heap=100663296 worker=262144 cpu_subset_worker=3552",
             "UNKNOWN native_highwater largest_block physical_limits residency game_peak",
             *[f"OWNER {name} requested={size} mapped={size} touched={written} views={views}"
               for name, size, written, views in zip(("mem1", "vm", "cache", "alias"), sizes, touched, (1, 1, 1, 2))]]
    unknown = "available=0 result=UNKNOWN size=UNKNOWN user=UNKNOWN cdram=UNKNOWN phycont=UNKNOWN valid=UNKNOWN"
    lines += [f"SNAP real-{phase} {unknown}" for phase in ("before", "live", "after")]
    lines += ["REAL peak=58736704 live=0 pattern=1 alias=1 bounds=1 PASS"]
    for name, size in zip(("user", "cdram", "phycont"), (4096, 262144, 1048576)):
        lines += [f"BLOCK {name} requested={size} mapped=0 touched=0 views=0 available=0 "
                  "uid=UNKNOWN base=UNKNOWN info=UNKNOWN free=UNKNOWN access=UNKNOWN type=UNKNOWN pattern=0 SKIP"]
        lines += [f"SNAP {name}-{phase} {unknown}" for phase in ("before", "live", "after")]
    lines += ["SDK peak=0 max_blocks=0 live=0 SKIP", "END real=PASS sdk=SKIP failures=0"]
    return "\n".join(lines) + "\n"


def target_report(token="a" * 32, source="b" * 40, package="c" * 64):
    text = host_report(token, source, package).replace("platform=HOST", "platform=VITA")
    text = text.replace("pointer=8 cpu=3552 version=6 ram_offset=3456 external_offset=3472",
                        "pointer=4 cpu=3496 version=6 ram_offset=3428 external_offset=3436")
    text = text.replace("cpu_subset_worker=3552", "cpu_subset_worker=3496")
    text = text.replace("available=0 result=UNKNOWN size=UNKNOWN user=UNKNOWN cdram=UNKNOWN phycont=UNKNOWN valid=UNKNOWN",
                        "available=1 result=0 size=16 user=999999999 cdram=23456 phycont=34567 valid=1")
    for pool, size in enumerate((4096, 262144, 1048576)):
        block_type = (0x0C20D060, 0x09408060, 0x0C80D060)[pool]
        text = text.replace(f"requested={size} mapped=0 touched=0 views=0 available=0 uid=UNKNOWN base=UNKNOWN info=UNKNOWN free=UNKNOWN access=UNKNOWN type=UNKNOWN pattern=0 SKIP",
                            f"requested={size} mapped={size} touched={(size // 4096 + 1) * 64} views=1 available=1 uid={100 + pool} base=0 info=0 free=0 access=6 type={block_type} pattern=1 PASS")
    return text.replace("SDK peak=0 max_blocks=0 live=0 SKIP", "SDK peak=1048576 max_blocks=1 live=0 PASS").replace("sdk=SKIP", "sdk=PASS")


class MemoryReportTest(unittest.TestCase):
    def test_host_report_retains_unknown_target_measurements(self):
        from mem_report import validate
        self.assertEqual(validate(host_report(), "a" * 32, "b" * 40, "c" * 64), 0)

    def test_host_report_cannot_certify_a_guest(self):
        from mem_report import validate
        with self.assertRaisesRegex(ValueError, "platform"):
            validate(host_report(), "a" * 32, "b" * 40, "c" * 64, expected_platform="VITA")

    def test_raw_incoherent_positive_counters_are_preserved_not_rebalanced(self):
        from mem_report import validate
        self.assertEqual(validate(target_report(), "a" * 32, "b" * 40, "c" * 64), 0)

    def test_wrong_target_abi_is_not_accepted(self):
        from mem_report import validate
        with self.assertRaisesRegex(ValueError, "ABI"):
            validate(target_report().replace("version=6", "version=7"), "a" * 32, "b" * 40, "c" * 64)

    def test_malformed_or_dishonest_reports_are_rejected(self):
        from mem_report import validate
        text = host_report()
        alterations = ("", text[:-1], text.replace("method=bounded-v1", "method=fp71"),
                       text.replace("phase=guest", "phase=install"), text.replace("a" * 32, "d" * 32),
                       text.replace("touched=393280", "touched=25165824"),
                       text.replace("views=2", "views=3"), text.replace("peak=58736704", "peak=100663296"),
                       text.replace("native_highwater", "native_highwater=0"),
                       text.replace("user=UNKNOWN", "user=0"),
                       text.replace("bounds=1 PASS", "bounds=0 PASS"),
                       text.replace("failures=0", "failures=1"), text + "END\n")
        for altered in alterations:
            with self.subTest(text=altered):
                with self.assertRaises(ValueError):
                    validate(altered, "a" * 32, "b" * 40, "c" * 64)

    def test_raw_negative_snapshot_has_a_nonzero_semantic_terminal(self):
        from mem_report import validate
        text = target_report().replace("user=999999999", "user=-4").replace("valid=1", "valid=0")
        text = text.replace("bounds=1 PASS", "bounds=1 FAIL").replace("pattern=1 PASS", "pattern=1 FAIL")
        text = text.replace("live=0 PASS", "live=0 FAIL").replace("real=PASS sdk=PASS failures=0", "real=FAIL sdk=FAIL failures=2")
        self.assertEqual(validate(text, "a" * 32, "b" * 40, "c" * 64), 1)


if __name__ == "__main__":
    unittest.main()
