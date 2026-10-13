"""Explicit v1 history or v2 byte completion; neither proves producer success."""

from pathlib import Path
import hashlib
import os
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'armv7'))
from fp_libraries import directory_fd
from fp_profile import read_profile_file


class UnsealedArtifact(ValueError):
    def __init__(self, logical):
        super().__init__('v2 artifact completion seal is absent: ' + str(logical))
        self.logical = logical


def receipt_publication(receipt):
    version = receipt.get('publication_version', 1)
    if type(version) is not int or version not in (1, 2):
        raise ValueError('unknown build publication capability')
    return version


def _seal(logical, token, limit):
    if not isinstance(token, str) or re.fullmatch(r'[0-9a-f]{32}', token) is None:
        raise ValueError('v2 seal requires the exact run token')
    with directory_fd(logical.parent) as parent:
        names = [name for name in os.listdir(parent) if name.startswith(logical.name + '.v')]
    if not names:
        raise UnsealedArtifact(logical)
    if len(names) != 1:
        raise ValueError('v2 artifact requires exactly one seal; multiple seals')
    match = re.fullmatch(re.escape(logical.name) + r'\.v2\.([0-9a-f]{32})\.([0-9a-f]{64})\.(0|[1-9][0-9]{0,19})\.seal', names[0])
    if match is None or match[1] != token or int(match[3]) > limit:
        raise ValueError('v2 seal version, token, hash or extent is malformed')
    if read_profile_file(logical.parent / names[0], 0, owned=True) != b'':
        raise ValueError('v2 seal must be empty')
    return names[0], match[2], int(match[3])


def completed_names(logicals, *, token, publication, limit):
    if type(publication) is not int or publication not in (1, 2):
        raise ValueError('publication version must explicitly be 1 or 2')
    names = set()
    for logical in logicals:
        names.add(logical.name if publication == 1 else logical.name + '.partial')
        if publication == 2:
            names.add(_seal(logical, token, limit)[0])
    return names


def read_artifact(logical, limit, *, token, publication, candidate=None):
    logical = Path(logical)
    if type(publication) is not int or publication not in (1, 2):
        raise ValueError('publication version must explicitly be 1 or 2')
    if publication == 1:
        return read_profile_file(logical, limit, owned=True)
    _, digest, extent = _seal(logical, token, limit)
    candidate = Path(candidate) if candidate is not None else logical.with_name(logical.name + '.partial')
    content = read_profile_file(candidate, limit, owned=True)
    if len(content) != extent or hashlib.sha256(content).hexdigest() != digest:
        raise ValueError('v2 candidate extent/hash differs from the producer seal')
    return content
