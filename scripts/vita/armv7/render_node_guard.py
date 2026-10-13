"""Validate the mounted object before any guest or graphics-library execution."""

GUARD_METHOD = 'mounted-render-node-stat-v1'
GUARD_PREFIX = 'WW24_RENDER_NODE_GUARD '
GUARD_FIELDS = ('device', 'inode', 'major', 'minor')
GUARD_CODE = '''import os, stat, sys
path, device, inode, major, minor, libraries = sys.argv[1:7]
info = os.lstat(path)
actual = (info.st_dev, info.st_ino, os.major(info.st_rdev), os.minor(info.st_rdev))
expected = tuple(map(int, (device, inode, major, minor)))
evidence = ' '.join(f'{key}={value}' for key, value in zip(('device', 'inode', 'major', 'minor'), actual))
if not stat.S_ISCHR(info.st_mode) or actual != expected:
    os.write(2, ('WW24_RENDER_NODE_GUARD REFUSED ' + evidence + '\\n').encode())
    raise SystemExit(126)
os.write(1, ('WW24_RENDER_NODE_GUARD VERIFIED ' + evidence + '\\n').encode())
if libraries:
    os.environ['LD_LIBRARY_PATH'] = libraries
os.execv(sys.argv[7], sys.argv[7:])
'''


def renderer_guard_command(application, renderer, libraries=''):
    node = renderer['render_node']
    return ['/usr/bin/python3', '-I', '-S', '-c', GUARD_CODE, node['path'],
            *(str(node[field]) for field in GUARD_FIELDS), libraries, *application]


def renderer_guard_plan(renderer):
    if renderer['render_node'] is None:
        return None
    return {'method': GUARD_METHOD, 'interpreter': '/usr/bin/python3 -I -S -c',
            'identity': renderer['render_node'],
            'guarantee': 'no guest/graphics execution on a mismatched mount; transient mismatched mounts possible',
            'library_policy': 'LD_LIBRARY_PATH restored only after mounted identity validation'}


def renderer_guard_status(line, renderer):
    node = renderer['render_node']
    evidence = ' '.join(f'{field}={node[field]}' for field in GUARD_FIELDS)
    if line == (GUARD_PREFIX + 'VERIFIED ' + evidence + '\n').encode():
        return 'VERIFIED'
    if line.startswith((GUARD_PREFIX + 'REFUSED ').encode()):
        return 'REFUSED'
    return 'UNOBSERVED'
