import importlib.util
import os
from pathlib import Path
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f5/ww05')


def load_game_build():
    spec = importlib.util.spec_from_file_location('ww05_game_build_root', ROOT / 'scripts/vita/ww05/game_build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuildRootOverrideTest(unittest.TestCase):
    def test_default_build_root_remains_the_shared_tree(self):
        with mock.patch.dict(os.environ):
            os.environ.pop('WW05_BUILD_ROOT', None)
            self.assertEqual(load_game_build().OUTPUT, DEFAULT_OUTPUT)

    def test_override_build_root_selects_the_named_tree(self):
        override = Path('/home/fred/projects/zeldaWWHDRecomp/build-f6/ww12b')
        with mock.patch.dict(os.environ, {'WW05_BUILD_ROOT': str(override)}):
            module = load_game_build()
        self.assertEqual(module.OUTPUT, override)
        self.assertEqual(module.checked_output(override), override)

    def test_relative_override_is_refused(self):
        with mock.patch.dict(os.environ, {'WW05_BUILD_ROOT': 'build-f6/ww12b'}):
            with self.assertRaisesRegex(ValueError, 'WW05_BUILD_ROOT'):
                load_game_build()


if __name__ == '__main__':
    unittest.main()
