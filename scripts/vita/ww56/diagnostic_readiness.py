"""Read-only diagnostic inputs for supervised review, never launch authorization."""

from pathlib import Path

from fp_libraries import file_identity
from fp_launch import REPORT_DIRECTORY, reject_symlinks
from fp_profile import FIRMWARE_DIRECTORY, config_identity, read_profile_file
from launch import request_content
from stage_plan import is_full_hex_identity


def diagnostic_readiness(args):
    if not args.check:
        raise ValueError('diagnostic readiness is check-only; --check is required')
    if args.continue_gx != 64 or args.audio != 'deferred':
        raise ValueError('diagnostic readiness requires continuation 64 and deferred audio')
    for label, value, length in (('request token', args.request_token, 32),
                                 ('candidate source identity', args.guest_source, 40),
                                 ('candidate package identity', args.vpk_sha256, 64)):
        if not is_full_hex_identity(value, length):
            raise ValueError('incomplete readiness: ' + label + ' must be explicit lowercase hex')
    profile = Path(args.diagnostic_profile)
    if not profile.is_absolute():
        raise ValueError('diagnostic profile must be an absolute private profile path')
    reject_symlinks(profile)
    package = file_identity(reject_symlinks(Path(args.vpk)))
    if package['sha256'] != args.vpk_sha256:
        raise ValueError('candidate package does not match its supplied SHA-256')
    directory = profile / REPORT_DIRECTORY
    requests = {}
    for name, count in (('ww05.request', None), ('ww05.continue', 64), ('ww05.audio', 1)):
        expected = request_content(args.request_token, args.guest_source, args.vpk_sha256, count).encode()
        try:
            actual = read_profile_file(directory / name, len(expected), owned=True)
        except (OSError, ValueError) as error:
            raise ValueError(name + ': required owned regular request is missing or invalid: ' + str(error)) from error
        if actual != expected:
            raise ValueError(name + ': expected exact token/source/package' +
                             ('' if count is None else ' and count ' + str(count)) +
                             '; review the staged request, do not reuse a previous trial')
        requests[name] = {'path': str(directory / name), 'count': count}
    for path in directory.iterdir():
        if (path.name in {'frames', 'ww05.mem1', 'ww05.partial', 'ww05.report',
                          'ww05.log', 'ww05.dispatch'} or path.name.startswith('ww05.mem1-')):
            raise ValueError(path.name + ': diagnostic readiness forbids captures or previous trial outputs')
    config = config_identity(profile, alt=True, surface_sync=False,
                             pref_path=profile / FIRMWARE_DIRECTORY)
    return {'kind': 'ww56-diagnostic-readiness-v1', 'launch_authorized': False,
            'profile': str(profile), 'private_config': config,
            'token': args.request_token, 'guest_source': args.guest_source,
            'vpk': str(args.vpk), 'vpk_sha256': package['sha256'],
            'requests': requests, 'continue_gx': 64, 'audio': 'deferred',
            'observation': {'snapshots': False, 'memwatcher': False, 'ram_poll': False},
            'limits': ['Readiness only; explicit live operator approval and supervision remain required.',
                       'Source identity is supplied context, not proof of package build provenance.',
                       'No observer runs here; external watcher settings are not inspected.',
                       'The legacy live launcher is not this diagnostic profile; do not drop --check.']}
