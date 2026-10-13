"""Native WW launch with an explicit private profile and approved dependencies."""

import os
import stat

from fp_launch import reject_symlinks

RENDERER = {'mode': 'native-default', 'observation': 'UNOBSERVED'}


def direct_environment(root, libraries=None, wayland_socket=None):
    root = reject_symlinks(root)
    environment = {'PATH': '/usr/bin', 'LANG': 'C.UTF-8', 'HOME': str(root / 'home'),
                   'XDG_CONFIG_HOME': str(root / 'config'), 'XDG_CACHE_HOME': str(root / 'cache'),
                   'XDG_DATA_HOME': str(root / 'data'), 'XDG_STATE_HOME': str(root / 'state'),
                   'XDG_RUNTIME_DIR': str(root / 'xdg-runtime'),
                   'XDG_CONFIG_DIRS': str(root / 'config-system'),
                   'XDG_DATA_DIRS': str(root / 'data-system'),
                   'QT_QPA_PLATFORM': 'offscreen', 'SDL_VIDEODRIVER': 'offscreen',
                   'SDL_AUDIODRIVER': 'dummy', 'FONTCONFIG_FILE': '/etc/fonts/fonts.conf'}
    if libraries is not None:
        libraries = reject_symlinks(libraries)
        environment['LD_LIBRARY_PATH'] = str(libraries / 'lib')
        environment['QT_PLUGIN_PATH'] = str(libraries / 'lib/qt6/plugins')
    if wayland_socket is not None:
        socket = reject_symlinks(wayland_socket)
        info = socket.stat()
        if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
            raise ValueError('display must be an existing Unix socket owned by the current user')
        environment['QT_QPA_PLATFORM'] = 'wayland'
        environment['WAYLAND_DISPLAY'] = str(socket)
        del environment['SDL_VIDEODRIVER']
    return environment


def direct_phases(root, timeout, libraries=None, wayland_socket=None):
    root = reject_symlinks(root)
    environment = direct_environment(root, libraries, wayland_socket)
    commands = {'install': ['--console', '--backend-renderer', 'OpenGL', 'ww05.vpk'],
                'guest': ['--backend-renderer', 'OpenGL', '--installed-path', 'BWWG00005']}
    return {name: {'command': [str(root / 'runtime/Vita3K'), *arguments],
                   'environment': dict(environment), 'cwd': str(root / 'runtime'),
                   'renderer': dict(RENDERER),
                   'timeout_seconds': min(timeout, 600) if name == 'install' else timeout,
                   'evidence_root': str(root / 'phases' / name)}
            for name, arguments in commands.items()}
