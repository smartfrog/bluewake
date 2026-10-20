import importlib
import os
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/vita'))

import vita_workspace  # noqa: E402


class VitaWorkspaceTest(unittest.TestCase):
    def tearDown(self):
        importlib.reload(vita_workspace)

    def test_default_stack_is_repository_local(self):
        self.assertEqual(vita_workspace.STACK, ROOT / 'build-vita-tools/local-stack')

    def test_override_selects_every_derived_path(self):
        override = ROOT / 'build-vita-tools' / 'alternate'
        with patch.dict(os.environ, {'BLUEWAKE_VITA_STACK': str(override)}):
            importlib.reload(vita_workspace)
            self.assertEqual(vita_workspace.STACK, override)
            self.assertEqual(vita_workspace.VITASDK, override / 'vitasdk')
            self.assertEqual(vita_workspace.RENDERER_SDK, override / 'vitasdk/arm-vita-eabi')
            self.assertEqual(vita_workspace.VITA3K, override / 'vita3k/Vita3K')
            self.assertEqual(vita_workspace.FIRMWARE, override / 'firmware')
            self.assertEqual(vita_workspace.VITAGL, override / 'vitasdk/arm-vita-eabi/lib/libvitaGL.a')
            self.assertEqual(vita_workspace.SHADER_COMPILER,
                             override / 'firmware/ur0/data/libshacccg.suprx')

    def test_host_tools_resolve_from_the_distribution(self):
        with patch.dict(os.environ):
            for name in ('CMAKE', 'NINJA', 'CTEST', 'CC'):
                os.environ.pop(name, None)
            importlib.reload(vita_workspace)
            self.assertEqual(vita_workspace.CMAKE, Path(shutil.which('cmake')))
            self.assertEqual(vita_workspace.NINJA, Path(shutil.which('ninja')))
            self.assertEqual(vita_workspace.CTEST, Path(shutil.which('ctest')))
            self.assertEqual(vita_workspace.HOST_CC, Path(shutil.which('gcc')))

    def test_relative_override_is_rejected(self):
        with patch.dict(os.environ, {'BLUEWAKE_VITA_STACK': 'relative/stack'}):
            with self.assertRaises(ValueError):
                importlib.reload(vita_workspace)


if __name__ == '__main__':
    unittest.main()
