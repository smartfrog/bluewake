import json
from pathlib import Path
import unittest
from unittest import mock

import test_fp_deadline as fp_tests
import mem_report
from test_mem_report import target_report

launch = fp_tests.launch


class MemoryDeadlineTest(unittest.TestCase):
    def setUp(self):
        fp_tests.DeadlineTest.setUp(self)

    def start(self):
        return fp_tests.DeadlineTest.start(self)

    def finish(self, timeout=None):
        return fp_tests.DeadlineTest.finish(self, timeout)

    def terminate(self, pid, sig):
        return fp_tests.DeadlineTest.terminate(self, pid, sig)

    def publish(self):
        path = self.root / launch.REPORT_DIRECTORY / "mem.report"
        path.parent.mkdir(parents=True)
        path.write_text(target_report("run", "source", "package"))

    def observe(self):
        with mock.patch.object(launch, "time", self.clock), \
                mock.patch.object(launch.subprocess, "Popen", return_value=self.process), \
                mock.patch.object(launch.os, "killpg", side_effect=self.terminate):
            status = launch.run_owned(["fake-memory-only"], self.root, 30,
                                     report_identity=("run", "source", "package"), memory=True)
        process = json.loads((self.root / "evidence/process.json").read_text())
        status = launch.collect_result(self.root, "run", "source", "package", status, process=process, memory=True)
        result = json.loads((self.root / "evidence/result.json").read_text())
        self.assertEqual(status, 124)
        self.assertEqual(result["completion"], "INCOMPLETE")
        self.assertEqual(result["reported_semantic"], "PASS")

    def test_memory_validation_crossing_deadline_cannot_accept_zero_exit(self):
        self.publish()
        validate = mem_report.validate

        def late_validation(*args, **kwargs):
            result = validate(*args, **kwargs)
            self.now = 130.02
            self.finish()
            return result

        with mock.patch.object(mem_report, "validate", side_effect=late_validation):
            self.observe()

    def test_memory_poll_at_deadline_cannot_accept_zero_exit(self):
        self.publish()

        def late_poll():
            self.finish()
            self.now = 130
            return self.process.returncode

        self.process.poll.side_effect = late_poll
        self.observe()


if __name__ == "__main__":
    unittest.main()
