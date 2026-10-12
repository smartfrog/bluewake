from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import bridge


class SnapshotTransportTests(unittest.TestCase):
    def test_older_bridge_cannot_accept_sparse_zero_padding(self):
        result = SimpleNamespace(returncode=0, stdout='bridge ok=1 programs=1\n', stderr='')
        with patch.object(bridge.subprocess, 'run', return_value=result):
            output, refusal = bridge.run_stream(Path('gx_bridge'), b'\0', Path('mem1.bin'),
                                                ranges_path=Path('mem1.ranges'))
        self.assertIsNone(output)
        self.assertIn('lacks guarded snapshot support', refusal)

    def test_snapshot_guard_is_passed_only_to_child_process(self):
        result = SimpleNamespace(returncode=0,
                                 stdout='memory coverage=GX_MEM1_RANGES_V1 ranges=1 mapped_bytes=64\n',
                                 stderr='')
        with patch.object(bridge.subprocess, 'run', return_value=result) as runner:
            output, refusal = bridge.run_stream(Path('gx_bridge'), b'\0', Path('mem1.bin'),
                                                ranges_path=Path('mem1.ranges'))
        self.assertIsNone(refusal)
        self.assertIn('coverage=GX_MEM1_RANGES_V1', output)
        self.assertEqual(runner.call_args.kwargs['env']['GX_BRIDGE_MEM1_RANGES'],
                         str(Path('mem1.ranges').resolve()))

    def test_legacy_replay_does_not_inherit_an_unrelated_guard(self):
        result = SimpleNamespace(returncode=0, stdout='bridge ok=1\n', stderr='')
        with patch.dict(bridge.os.environ, {'GX_BRIDGE_MEM1_RANGES': '/unrelated'}), \
                patch.object(bridge.subprocess, 'run', return_value=result) as runner:
            bridge.run_stream(Path('gx_bridge'), b'\0', None)
        self.assertNotIn('GX_BRIDGE_MEM1_RANGES', runner.call_args.kwargs['env'])


if __name__ == '__main__':
    unittest.main()
