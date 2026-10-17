"""Incremental first-fault watcher tests on synthetic logs; no emulator or run."""

import hashlib
import json
import os
from pathlib import Path
import signal
import tempfile
import unittest
from unittest.mock import patch

import launch
from first_fault import (HEAD_LINES, TAIL_LINES, WINDOW_LINES, FirstFaultWatch,
                         Stop)

GUEST_EXCEPTION = "[exception] pc=00000000 srr0=00000000"
HOST_READ = "|E| [MemoryRead]: Invalid read of uint32_t at address: 0x92ef44"
HOST_WRITE = "|E| [MemoryWrite]: Invalid write of uint32_t at addr: 0x0, val = 0xdd2cb604"
EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()


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

    def entries(self, stats):
        return {entry["role"]: entry for entry in stats["logs"]}

    def test_watcher_stops_on_first_guest_exception_in_new_bytes(self):
        self.guest.write_text("".join(f"boot {index}\n" for index in range(150)))
        self.host.write_text(HOST_READ + "\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        with self.guest.open("a") as stream:
            stream.write(GUEST_EXCEPTION + "\n")
            stream.write("flood\n" * 10000)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))

    def test_host_only_fault_stops_through_watcher(self):
        self.host.write_text("boot\n" + HOST_READ + "\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("host-invalid-access", HOST_READ))

    def test_unprefixed_invalid_read_is_not_a_host_fault(self):
        self.host.write_text("note: Invalid read was handled\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertIsNone(watch.poll())

    def test_second_poll_detects_a_fault_appended_after_the_first_poll(self):
        self.guest.write_text("boot\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertIsNone(watch.poll())
        with self.guest.open("a") as stream:
            stream.write(GUEST_EXCEPTION + "\n")
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))

    def test_fault_split_across_a_partial_last_line_is_detected_when_completed(self):
        self.guest.write_text("boot\n" + GUEST_EXCEPTION[:7])
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertIsNone(watch.poll())
        with self.guest.open("a") as stream:
            stream.write(GUEST_EXCEPTION[7:] + "\n")
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))

    def test_truncated_and_regrown_log_restarts_scanning(self):
        self.guest.write_text("old\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertIsNone(watch.poll())
        self.guest.write_text(GUEST_EXCEPTION + "\n" + "new\n" * 100)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))

    def test_replaced_log_restarts_scanning(self):
        self.guest.write_text("old\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertIsNone(watch.poll())
        replacement = self.guest.with_suffix(".next")
        replacement.write_text(GUEST_EXCEPTION + "\n")
        os.replace(replacement, self.guest)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))

    def test_empty_auxiliary_log_is_sampled_without_aborting(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n")
        self.host.write_text("")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))
        stats = watch.write_samples()
        host = self.entries(stats)["host"]
        self.assertEqual((host["present"], host["bytes"], host["lines"], host["sha256"],
                          host["fault_index"], host["head_lines"], host["fault_lines"],
                          host["tail_lines"]),
                         (True, 0, 0, EMPTY_SHA256, 0, 0, 0, 0))
        directory = self.samples / host["sample"]
        for name in ("head.txt", "fault.txt", "tail.txt"):
            self.assertEqual((directory / name).read_bytes(), b"")

    def test_host_sample_tails_the_log_when_a_guest_exception_preempts_host_scan(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n")
        self.host.write_text("".join(f"host {index}\n" for index in range(1000)))
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))
        stats = watch.write_samples()
        host = self.entries(stats)["host"]
        directory = self.samples / host["sample"]
        fault_lines = (directory / "fault.txt").read_text().splitlines()
        tail_lines = (directory / "tail.txt").read_text().splitlines()
        self.assertEqual(fault_lines, tail_lines)
        self.assertEqual(fault_lines, [f"host {index}" for index in range(700, 1000)])

    def test_rewrite_after_stop_invalidates_the_line_anchor(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n" + "old\n" * 100)
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))
        self.guest.write_text("".join(f"new {index}\n" for index in range(500)))
        stats = watch.write_samples()
        guest = self.entries(stats)["guest"]
        self.assertFalse(guest["anchor_verified"])
        self.assertEqual(guest["fault_anchor"], "tail")
        self.assertEqual(guest["fault_index"], guest["lines"] - 1)

    def test_fault_file_is_centred_on_the_fault_line(self):
        rows = [f"line {index}" for index in range(20000)]
        rows[5000] = GUEST_EXCEPTION
        self.guest.write_text("\n".join(rows) + "\n")
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))
        stats = watch.write_samples()
        guest = self.entries(stats)["guest"]
        fault_lines = (self.samples / guest["sample"] / "fault.txt").read_text().splitlines()
        self.assertEqual(len(fault_lines), WINDOW_LINES)
        self.assertEqual(fault_lines[WINDOW_LINES // 2], GUEST_EXCEPTION)
        self.assertEqual(fault_lines[0], "line 4850")
        self.assertEqual(fault_lines[-1], "line 5149")

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

    def test_sha256sums_and_verification_match_the_written_files(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n" + "".join(f"line {i}\n" for i in range(1000)))
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples)
        self.assertEqual(watch.poll(), Stop("guest-exception", GUEST_EXCEPTION))
        stats = watch.write_samples()
        sums = {}
        for row in (self.samples / "SHA256SUMS").read_text().splitlines():
            digest, name = row.split("  ", 1)
            sums[name] = digest
        verification = json.loads((self.samples / "verification.json").read_text())
        self.assertTrue(verification["checks"]["files_present"])
        self.assertTrue(verification["checks"]["line_counts_match"])
        self.assertTrue(verification["checks"]["hashes_match"])
        self.assertEqual({entry["name"] for entry in verification["files"]},
                         {f"{entry['sample']}/{kind}.txt"
                          for entry in stats["logs"] if entry["present"]
                          for kind in ("head", "fault", "tail")} | {"flood-stats.json"})
        for entry in stats["logs"]:
            if not entry["present"]:
                continue
            for kind in ("head", "fault", "tail"):
                relative = f"{entry['sample']}/{kind}.txt"
                content = (self.samples / relative).read_bytes()
                self.assertEqual(hashlib.sha256(content).hexdigest(), sums[relative])
                self.assertEqual(content.count(b"\n"), entry[f"{kind}_lines"])
        self.assertEqual(hashlib.sha256((self.samples / "flood-stats.json").read_bytes()).hexdigest(),
                         sums["flood-stats.json"])

    def test_host_fault_wins_over_ceiling_through_watcher(self):
        self.host.write_text(HOST_READ + "\n" + "x" * 2000)
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples, ceiling_bytes=1024)
        self.assertEqual(watch.poll(), Stop("host-invalid-access", HOST_READ))

    def test_ceiling_is_inclusive_at_the_real_phase_log_limit(self):
        with self.guest.open("wb") as stream:
            stream.truncate(launch.PHASE_LOG_LIMIT)
        watch = FirstFaultWatch(self.guest, (self.host,), self.samples,
                                ceiling_bytes=launch.PHASE_LOG_LIMIT)
        self.assertEqual(watch.poll(), Stop("log-size-ceiling", str(self.guest)))

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

    def test_empty_process_log_does_not_abort_runner_sampling(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n")
        (self.phase / "evidence/process.log").write_text("")
        observer = launch.first_fault_observer(
            self.root, self.phase, lambda pid: self.fail("delegate ran after a fault"),
            stop=lambda pid: None)
        observer(4242)
        stats = json.loads((self.phase / "evidence/first-fault-sample/flood-stats.json").read_text())
        process = [entry for entry in stats["logs"]
                   if entry["path"].endswith("process.log")][0]
        self.assertEqual((process["present"], process["bytes"], process["lines"],
                          process["sha256"], process["fault_index"], process["head_lines"],
                          process["fault_lines"], process["tail_lines"]),
                         (True, 0, 0, EMPTY_SHA256, 0, 0, 0, 0))

    def test_sample_terminates_at_the_size_captured_at_stop(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n" + "before\n" * 100)
        captured = self.guest.stat().st_size

        def stop(pid):
            with self.guest.open("a") as stream:
                stream.write("after-stop\n" * 10000)

        observer = launch.first_fault_observer(
            self.root, self.phase, lambda pid: self.fail("delegate ran after a fault"),
            stop=stop)
        observer(4242)
        stats = json.loads((self.phase / "evidence/first-fault-sample/flood-stats.json").read_text())
        guest = [entry for entry in stats["logs"] if entry["role"] == "guest"][0]
        self.assertEqual(guest["bytes"], captured)
        self.assertEqual(guest["lines"], 101)
        tail = (self.phase / "evidence/first-fault-sample" / guest["sample"] / "tail.txt")
        self.assertNotIn("after-stop", tail.read_text())

    def test_samples_are_attempted_when_the_stop_call_fails(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n")

        def stop(pid):
            raise RuntimeError("synthetic stop failure")

        observer = launch.first_fault_observer(
            self.root, self.phase, lambda pid: self.fail("delegate ran after a fault"),
            stop=stop)
        with self.assertRaisesRegex(RuntimeError, "synthetic stop failure"):
            observer(4242)
        self.assertTrue((self.phase / "evidence/first-fault-sample/flood-stats.json").is_file())

    def test_observer_uses_the_owned_group_stop_by_default(self):
        self.guest.write_text(GUEST_EXCEPTION + "\n")
        stopped = []
        with patch.object(launch, "stop_owned_group",
                          side_effect=lambda pid: stopped.append(pid)):
            observer = launch.first_fault_observer(
                self.root, self.phase, lambda pid: self.fail("delegate ran after a fault"))
            observation = observer(4242)
        self.assertEqual(stopped, [4242])
        self.assertEqual(observation["stop_reason"], "first-fault")


class StopOwnedGroupTest(unittest.TestCase):
    def test_returns_when_the_group_is_gone_after_sigterm(self):
        clock = FakeClock()
        calls = []

        def killpg(pid, signal_number):
            calls.append((pid, signal_number))
            if signal_number == 0:
                raise ProcessLookupError

        with patch.object(launch.os, "killpg", side_effect=killpg):
            outcome = launch.stop_owned_group(4242, clock=clock, sleep=clock.sleep)
        self.assertEqual(outcome, "terminated")
        self.assertEqual(calls, [(4242, signal.SIGTERM), (4242, 0)])

    def test_returns_when_the_group_is_already_absent(self):
        clock = FakeClock()
        with patch.object(launch.os, "killpg", side_effect=ProcessLookupError):
            outcome = launch.stop_owned_group(4242, clock=clock, sleep=clock.sleep)
        self.assertEqual(outcome, "absent")

    def test_sends_sigkill_after_the_bounded_grace(self):
        clock = FakeClock()
        calls = []
        with patch.object(launch.os, "killpg",
                          side_effect=lambda pid, signal_number: calls.append((pid, signal_number))):
            outcome = launch.stop_owned_group(4242, grace_seconds=2, poll_seconds=0.5,
                                               clock=clock, sleep=clock.sleep)
        self.assertEqual(outcome, "killed")
        self.assertEqual(calls[0], (4242, signal.SIGTERM))
        self.assertEqual(calls[-1], (4242, signal.SIGKILL))
        self.assertIn((4242, 0), calls)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


if __name__ == "__main__":
    unittest.main()
