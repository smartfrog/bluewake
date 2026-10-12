import subprocess
import unittest

import test_host


class GpuFinishTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.setUpClass()

    def probe(self, scenario):
        result = subprocess.run([str(test_host.OUT / 'gpu_finish_probe'), scenario], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_confirmed_backend_finish_publishes_pe_and_leaves_guest_sleep_code_intact(self):
        self.probe('finish')

    def test_repeated_zero_charge_entry_does_not_refence_the_same_fifo_work(self):
        self.probe('repeat')

    def test_failed_backend_finish_does_not_publish_pe_completion(self):
        self.probe('fence-failure')

    def test_missing_backend_cannot_invent_gpu_completion(self):
        self.probe('missing-renderer')

    def test_disabled_stream_cannot_invent_gpu_completion(self):
        self.probe('disabled-stream')

    def test_failed_trace_does_not_publish_unrecorded_pe_completion(self):
        self.probe('trace-failure')

    def test_guest_pi_mask_is_respected_after_a_confirmed_finish(self):
        self.probe('masked')

    def test_later_fifo_work_requires_a_new_confirmed_backend_finish(self):
        self.probe('new-work')

    def test_exact_sourced_flush_nop_suffix_stays_gathered_without_blocking_submitted_gpu_work(self):
        for size in range(1, 32):
            with self.subTest(size=size):
                self.probe('flush-padding-' + str(size))

    def test_non_nop_or_unsourced_gather_suffix_still_blocks_completion(self):
        for shape in ('byte', 'origin', 'pc', 'sp'):
            with self.subTest(shape=shape):
                self.probe('bad-padding-' + shape)

    def test_failed_frontier_capture_stops_before_backend_completion(self):
        self.probe('frontier-trace-failure')

    def test_async_draw_done_return_uses_confirmed_backend_work_and_the_real_pe_path(self):
        self.probe('async-finish')

    def test_async_finish_failure_cannot_publish_completion(self):
        self.probe('async-fence-failure')

    def test_async_finish_is_idempotent_for_the_same_fifo_work(self):
        self.probe('async-repeat')

    def test_async_finish_requires_its_owned_observed_return_state(self):
        for shape in ('unowned', 'bad-caller', 'bad-queue', 'bad-sda', 'bad-saved-register', 'aliased-queue'):
            with self.subTest(shape=shape):
                self.probe('async-' + shape)

    def test_async_finish_retains_only_its_exact_sourced_set_draw_done_nop_suffix(self):
        for size in range(1, 32):
            with self.subTest(size=size):
                self.probe('async-flush-padding-' + str(size))

    def test_async_unconsumed_work_or_bad_padding_cannot_publish_completion(self):
        for shape in ('pending-command', 'pending-gather', 'pending-fifo', 'pending-capture',
                      'pending-read', 'bad-padding-byte', 'bad-padding-origin', 'bad-padding-pc', 'bad-padding-sp'):
            with self.subTest(shape=shape):
                self.probe('async-' + shape)

    def test_only_the_owned_observed_draw_done_caller_and_queue_are_supported(self):
        for shape in ('unowned', 'bad-caller', 'bad-queue', 'bad-sda', 'aliased-queue'):
            with self.subTest(shape=shape):
                self.probe(shape)

    def test_unconsumed_fifo_gather_capture_or_command_refuses_completion(self):
        for shape in ('pending-command', 'pending-gather', 'pending-fifo', 'pending-capture', 'pending-read'):
            with self.subTest(shape=shape):
                self.probe(shape)
