"""Incremental first-fault watcher tests on synthetic logs; no emulator or run."""

import json
from pathlib import Path
import tempfile
import unittest

import launch
from first_fault import (HEAD_LINES, TAIL_LINES, WINDOW_LINES, FirstFaultWatch,
                         Stop)

GUEST_EXCEPTION = "[exception] pc=00000000 srr0=00000000"
HOST_READ = "|E| [MemoryRead]: Invalid read of uint32_t at address: 0x92ef44"


class FirstFaultWatchTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir="/tmp/opencode")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.guest = self.root / "guest/ww05.log"
        self.host = self.root / "host/process.log"
        self.samples = self.root / "evidence/first-fault-sample"
        self.guest.parent.mkdir()
        self.host.parent.mkdir()

    def test_watcher_stops_on_first_guest_exception_in_new_bytes(self):
        self.guest.write_text("".join(f"boot {index}\n" for index in range(150)))
        self.host.write_text(HOST_READ + "\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        with self.guest.open("a") as stream:
            stream.write(GUEST_EXCEPTION + "\n")
            stream.write("flood\n" * 10000)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))

    def test_watcher_writes_bounded_sample_before_stop(self):
        rows = [f"line {index}" for index in range(20000)]
        rows[5000] = GUEST_EXCEPTION
        self.guest.write_text("\n".join(rows) + "\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))
        watch.write_samples()
        stats = json.loads((self.samples / "flood-stats.json").read_text())
        guest = [entry for entry in stats["logs"] if entry["role"] == "guest"][0]
        self.assertEqual((guest["head_lines"], guest["fault_lines"], guest["tail_lines"]),
                         (HEAD_LINES, WINDOW_LINES, TAIL_LINES))
        self.assertEqual(stats["deletion"], "none")
        self.assertTrue(self.guest.is_file())
        self.assertTrue((self.samples / guest["sample"] / "head.txt").is_file())
        self.assertTrue((self.samples / guest["sample"] / "fault.txt").is_file())
        self.assertTrue((self.samples / guest["sample"] / "tail.txt").is_file())

    def test_watcher_stops_on_ceiling_and_samples_the_flood_end(self):
        self.guest.write_text("x\n" * 4096)
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples, ceiling_bytes=2048)
        self.assertEqual(watch.poll(), Stop("log-size-ceiling", str(self.guest)))
        watch.write_samples()
        stats = json.loads((self.samples / "flood-stats.json").read_text())
        guest = [entry for entry in stats["logs"] if entry["role"] == "guest"][0]
        self.assertGreater(guest["fault_index"], 3000)
        self.assertLessEqual(guest["fault_lines"], WINDOW_LINES)
        self.assertLessEqual(guest["tail_lines"], TAIL_LINES)


class FirstFaultRunnerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir="/tmp/opencode")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.phase = self.root / "phase"
        (self.phase / "evidence").mkdir(parents=True)
        self.guest = self.root / launch.REPORT_DIRECTORY / "ww05.log"
        self.guest.parent.mkdir(parents=True)

    def test_runner_observer_keeps_the_delegate_when_no_fault_occurs(self):
        calls = []

        def delegate(pid):
            calls.append(pid)
            return {"stop_reason": "inactive"}

        observer = launch.first_fault_observer(self.root, self.phase, delegate,
                                               stop=lambda pid: None)
        self.assertEqual(observer(4242), {"stop_reason": "inactive"})
        self.assertEqual(calls, [4242])
        self.assertFalse((self.phase / "evidence/first-fault-sample").exists())

    def test_runner_observer_stops_before_writing_samples(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n")
        events = []

        def stop(pid):
            events.append(("stop", (self.phase / "evidence/first-fault-sample").exists()))

        observer = launch.first_fault_observer(
            self.root, self.phase, lambda pid: self.fail("delegate ran after a fault"),
            stop=stop)
        observation = observer(4242)
        self.assertEqual(observation["stop_reason"], "first-fault")
        self.assertEqual(observation["first_fault"],
                         {"reason": "guest-exception", "detail": GUEST_EXCEPTION})
        self.assertEqual(events, [("stop", False)])
        self.assertTrue((self.phase / "evidence/first-fault-sample/flood-stats.json").is_file())


if __name__ == "__main__":
    unittest.main()
