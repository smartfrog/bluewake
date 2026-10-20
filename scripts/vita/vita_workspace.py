"""Private Vita workspace paths, resolved on any machine.

BLUEWAKE_VITA_STACK overrides the shared stack directory. Everything else is
relative to the repository, so a checkout works wherever it is cloned.
"""

import os
from pathlib import Path
import shutil

REPO = Path(__file__).resolve().parents[2]
STACK = Path(os.environ.get('BLUEWAKE_VITA_STACK', REPO / 'build-vita-tools/local-stack')).expanduser()
if not STACK.is_absolute():
    raise ValueError('BLUEWAKE_VITA_STACK must be an absolute path')

VITASDK = STACK / 'vitasdk'
RENDERER_SDK = VITASDK / 'arm-vita-eabi'
VITA3K = STACK / 'vita3k/Vita3K'
FIRMWARE = STACK / 'firmware'
VITAGL = STACK / 'vita3k/vitagl-source/libvitaGL.a'
SHADER_COMPILER = FIRMWARE / 'ur0/data/libshacccg.suprx'

CMAKE = Path(os.environ.get('CMAKE', shutil.which('cmake') or 'cmake'))
NINJA = Path(os.environ.get('NINJA', shutil.which('ninja') or 'ninja'))
CTEST = Path(os.environ.get('CTEST', shutil.which('ctest') or 'ctest'))
HOST_CC = Path(os.environ.get('CC', shutil.which('gcc') or 'gcc'))
