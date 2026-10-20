import subprocess
import unittest

import test_host


class FrameDepthClearTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_host.StartupHostTest.build(targets=('vita_gl_backend_tests',))

    def test_each_acquired_frame_clears_only_depth_once_before_draws(self):
        subprocess.run([str(test_host.OUT / 'vita_gl_backend_tests'), 'depth-clear'], check=True)

    def test_depth_clear_failure_is_reported(self):
        subprocess.run([str(test_host.OUT / 'vita_gl_backend_tests'), 'depth-clear-error'], check=True)


if __name__ == '__main__':
    unittest.main()
