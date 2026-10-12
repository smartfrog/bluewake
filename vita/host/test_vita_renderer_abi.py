from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDK = Path('/home/fred/projects/zeldaWWHDRecomp/build-vita-tools/local-stack/vitasdk')
RENDERER_SDK = Path('/home/fred/cloudpunk-vita/tools/vitasdk/arm-vita-eabi')


class VitaRendererAbiTest(unittest.TestCase):
    def test_vita_backend_exports_the_c_interface_symbol(self):
        pin = ROOT / 'ref/vita-ww-01/RecompCore/GXRuntime'
        with tempfile.TemporaryDirectory(dir='/tmp/opencode', prefix='ww16-renderer-abi-') as directory:
            obj = Path(directory) / 'backend.o'
            subprocess.run([str(SDK / 'bin/arm-vita-eabi-g++'), '-std=c++20', '-O2',
                            '-mcpu=cortex-a9', '-mfpu=neon', '-mfloat-abi=hard',
                            '-isystem', str(RENDERER_SDK / 'include'), '-I' + str(ROOT / 'vita/renderer'),
                            '-I' + str(pin / 'include'), '-I' + str(pin / 'graphics/gxcore/include'),
                            '-I' + str(pin / 'graphics/frontend/include'),
                            '-c', str(ROOT / 'vita/renderer/vita_gl_backend.cpp'), '-o', str(obj)], check=True)
            symbols = subprocess.check_output([str(SDK / 'bin/arm-vita-eabi-nm'),
                                               '--defined-only', str(obj)], text=True)
            self.assertEqual(sum(line.split()[-1] == 'ww_renderer_vita_backend'
                                 for line in symbols.splitlines()), 1)


if __name__ == '__main__':
    unittest.main()
