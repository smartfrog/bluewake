"""Preserve private linked observation images before launch; no guest execution."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def identity(path):
    with Path(path).open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'bytes': Path(path).stat().st_size, 'sha256': digest}


def verify_receipt(path, source):
    path = Path(path)
    receipt = json.loads(path.read_text())
    if receipt['source'] != source or not receipt['private']:
        raise ValueError('native observation artifact source/private identity mismatch')
    for name in ('ww05_host', 'ww05_host.velf', 'ww05.map'):
        row = receipt['artifacts'][name]
        copy = Path(row['copy'])
        if copy.is_symlink() or not copy.is_file() or identity(copy) != row['identity']:
            raise ValueError('preserved native observation artifact missing or changed: ' + name)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--composite-digest', required=True)
    parser.add_argument('--object-inventory', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=root):
        raise ValueError('preservation requires a clean reviewed observation commit')
    args.out.mkdir(parents=True, exist_ok=False, mode=0o700)
    receipt = {'source': source, 'base': 'bbd79f1208c6ca5668075de08c1a261e3a0e1500', 'private': True,
               'composite_digest': args.composite_digest,
               'object_inventory': {'path': str(args.object_inventory), **identity(args.object_inventory)},
               'compiler': {'path': str(args.compiler), **identity(args.compiler),
                            'version': subprocess.check_output([str(args.compiler), '--version'], text=True).splitlines()[0]},
               'build_options': (args.build / 'CMakeCache.txt').read_text(),
               'native_image_load_addresses': None, 'trial': 'not run', 'artifacts': {}}
    for name in ('ww05_host', 'ww05_host.velf', 'ww05.map', 'eboot.bin', 'ww05.vpk'):
        original, copied = args.build / name, args.out / name
        before = identity(original)
        shutil.copyfile(original, copied)
        if copied.is_symlink() or identity(copied) != before or identity(original) != before:
            raise ValueError('native artifact copy mismatch: ' + name)
        receipt['artifacts'][name] = {'source': str(original), 'copy': str(copied), 'identity': before,
                                      'source_identity': before, 'copy_identity': identity(copied),
                                      'source_equals_copy': True}
    (args.out / 'artifact-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    (args.out / 'SHA256SUMS').write_text(''.join(row['identity']['sha256'] + '  ' + name + '\n'
                                              for name, row in receipt['artifacts'].items()))
    verify_receipt(args.out / 'artifact-receipt.json', source)


if __name__ == '__main__':
    main()
