import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/vita/ww05'))
from publication import read_artifact

TOKEN = 'c' * 32


def read_completed(path, limit=32 * 1024 * 1024):
    return read_artifact(path, limit, token=TOKEN, publication=2)


def replace_sealed_fixture(path, content):
    candidate = path.with_name(path.name + '.partial')
    candidate.write_bytes(content)
    for seal in path.parent.glob(path.name + '.v2.*.seal'):
        seal.unlink()
    digest = hashlib.sha256(content).hexdigest()
    (path.parent / f'{path.name}.v2.{TOKEN}.{digest}.{len(content)}.seal').write_bytes(b'')


def collect_v2(*args, **kwargs):
    from frame_capture import collect_frames
    kwargs.setdefault('publication', 2)
    return collect_frames(*args, **kwargs)
