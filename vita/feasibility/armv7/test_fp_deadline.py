import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from test_fp_review import ROOT, launch
from fp_vectors import cases


class DeadlineTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="build-fp-deadline-", dir=ROOT)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "run"
        launch.create_run(self.root)
        self.now = 100.0
        self.process = mock.MagicMock()
        self.process.pid = 12345
        self.process.returncode = None
        self.process.__enter__.side_effect = self.start
        self.process.poll.side_effect = lambda: self.process.returncode
        self.process.wait.side_effect = self.finish
        self.clock = mock.Mock()
        self.clock.monotonic.side_effect = lambda: self.now
        self.clock.sleep.side_effect = AssertionError("unexpected observer sleep")

    def start(self):
        self.now = 129.98
        return self.process

    def finish(self, timeout=None):
        if self.process.returncode is None:
            self.process.returncode = 0
        return self.process.returncode

    def publish(self):
        lines = ["RUN run source package"]
        lines += [f"CASE {case['id']} mode={case['mode']} expected={case['expected']} "
                  f"actual={case['expected']} path=1 PASS" for case in cases()]
        lines.append(f"END PASS cases={len(cases())} failures=0")
        report = self.root / launch.REPORT_DIRECTORY / "fp.report"
        report.parent.mkdir(parents=True)
        partial = report.with_name("fp.partial")
        partial.write_text("\n".join(lines) + "\n")
        partial.rename(report)

    def terminate(self, pid, sig):
        self.assertEqual(pid, self.process.pid)
        self.process.returncode = -sig

    def run_observer(self, monitor=True):
        with mock.patch.object(launch, "time", self.clock), \
                mock.patch.object(launch.subprocess, "Popen", return_value=self.process), \
                mock.patch.object(launch.os, "killpg", side_effect=self.terminate):
            status = launch.run_owned(["fake-only"], self.root, 30,
                                      report_identity=("run", "source", "package") if monitor else None)
        process = json.loads((self.root / "evidence/process.json").read_text())
        result_status = launch.collect_result(self.root, "run", "source", "package", status, process=process)
        result = json.loads((self.root / "evidence/result.json").read_text())
        return result_status, result

    def assert_expired(self, status, result):
        self.assertEqual(status, 124, result)
        self.assertEqual(result["process"]["stop_reason"], "timeout")
        self.assertEqual(result["completion"], "INCOMPLETE")
        self.assertEqual(result["semantic"], "UNAVAILABLE")
        self.assertEqual(result["reported_semantic"], "PASS")

    def test_observer_wakes_after_deadline_to_a_valid_report_and_zero_exit(self):
        def delayed_poll():
            if self.process.returncode is None:
                self.now = 130.02
                self.publish()
                self.finish()
                self.now = 130.06
            return self.process.returncode

        self.process.poll.side_effect = delayed_poll
        status, result = self.run_observer()
        self.assertEqual(result["process_exit"], 0)
        self.assert_expired(status, result)

    def test_validation_crosses_deadline_while_child_exits_zero(self):
        self.publish()
        validate = launch.validate

        def delayed_validation(*args):
            verdict = validate(*args)
            self.now = 130.02
            self.finish()
            return verdict

        with mock.patch.object(launch, "validate", side_effect=delayed_validation):
            status, result = self.run_observer()
        self.assertEqual(result["process_exit"], 0)
        self.assert_expired(status, result)

    def wait_crossing_deadline(self, monitor):
        self.publish()
        waits = []

        def late_exit(timeout=None):
            waits.append((timeout, 130 - self.now))
            self.now = 130.06
            return self.finish()

        self.process.wait.side_effect = late_exit
        status, result = self.run_observer(monitor=monitor)
        self.assertEqual(result["process_exit"], 0)
        self.assert_expired(status, result)
        for timeout, remaining in waits:
            self.assertLessEqual(timeout, remaining)

    def test_report_exit_wait_returns_after_deadline(self):
        self.wait_crossing_deadline(monitor=True)

    def test_unmonitored_exit_wait_returns_after_deadline(self):
        self.wait_crossing_deadline(monitor=False)

    def test_report_exit_grace_expiring_at_deadline_is_timeout(self):
        self.publish()

        def expired_wait(timeout=None):
            if self.process.returncode is None:
                self.now = 130.0
                raise launch.subprocess.TimeoutExpired("fake-only", timeout)
            return self.process.returncode

        self.process.wait.side_effect = expired_wait
        self.assert_expired(*self.run_observer())

    def test_validation_time_reduces_natural_exit_grace(self):
        self.publish()
        validate = launch.validate
        waits = []

        def consumes_budget(*args):
            verdict = validate(*args)
            self.now = 129.99
            return verdict

        def early_exit(timeout=None):
            waits.append((timeout, 130 - self.now))
            self.now = 129.995
            return self.finish()

        self.process.wait.side_effect = early_exit
        with mock.patch.object(launch, "validate", side_effect=consumes_budget):
            status, result = self.run_observer()
        self.assertEqual(status, 0, result)
        self.assertEqual(result["completion"], "COMPLETE")
        self.assertEqual(result["semantic"], "PASS")
        self.assertEqual(len(waits), 1)
        timeout, remaining = waits[0]
        self.assertGreater(timeout, 0)
        self.assertLessEqual(timeout, remaining)

    def test_zero_exit_observed_exactly_at_deadline_is_timeout(self):
        def deadline_poll():
            if self.process.returncode is None:
                self.publish()
                self.finish()
                self.now = 130.0
            return self.process.returncode

        self.process.poll.side_effect = deadline_poll
        self.assert_expired(*self.run_observer())


if __name__ == "__main__":
    unittest.main()
