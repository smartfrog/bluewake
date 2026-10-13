"""Verify and retain real live frame PNGs in the same private run's evidence."""

import hashlib
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gx.image_metadata import png_metadata, verify_display_rgb

MAX_FRAMES = 128
MAX_PNG_BYTES = 4 * 1024 * 1024


def frame_outputs(directory):
    return tuple(directory / f'frame-{frame}{suffix}{partial}' for frame in range(1, MAX_FRAMES + 1)
                 for suffix in ('-rgba.png', '.png', '.json') for partial in ('', '.partial'))


def collect_frames(directory, output, *, enabled, presented, token, source, package, audio='strict'):
    from fp_libraries import directory_fd
    from fp_profile import read_profile_file

    if type(presented) is not int or presented < 0 or (enabled and presented > MAX_FRAMES):
        raise ValueError('live capture presentation count exceeds its diagnostic bound')
    if audio not in ('strict', 'deferred'):
        raise ValueError('invalid declared audio policy')
    expected = {f'frame-{frame}{suffix}' for frame in range(1, presented + 1)
                for suffix in ('-rgba.png', '.png', '.json')} if enabled else set()
    observed = set()
    if directory.exists():
        with directory_fd(directory) as parent:
            observed = set(os.listdir(parent))
    if observed != expected:
        raise ValueError('live frame PNG evidence is missing, partial, unexpected or not requested')
    frames = []
    if enabled and output != directory:
        output.mkdir(mode=0o700, exist_ok=False)
    previous_us = -1
    for index in range(1, presented + 1) if enabled else ():
        metadata_path = directory / f'frame-{index}.json'
        content = read_profile_file(metadata_path, 8192, owned=True)
        record = json.loads(content)
        if not isinstance(record, dict):
            raise ValueError('live frame metadata must be an object')
        for key, value in dict(kind='ww05-live-frame-v1', token=token, source=source, vpk_sha256=package,
                               frame=index, raw=f'frame-{index}-rgba.png', display=f'frame-{index}.png',
                               phase='after-gx-present-before-next-frame', row_order='top-first',
                               display_transform='alpha=255; RGB unchanged from raw front-buffer readback').items():
            if record.get(key) != value:
                raise ValueError('live frame identity, presentation or boundary mismatch')
        if record.get('audio', 'strict') != audio:
            raise ValueError('live frame audio policy differs from the declared run profile')
        for key in ('frame', 'command', 'draws', 'width', 'height', 'us', 'cycles', 'mem1_snapshots'):
            if type(record.get(key)) is not int or not 0 <= record[key] <= (1 << 64) - 1:
                raise ValueError('live frame numeric metadata mismatch')
        if record['us'] <= previous_us:
            raise ValueError('live capture clock did not advance')
        if not 1 <= record['width'] <= 4096 or not 1 <= record['height'] <= 4096 or record['width'] * record['height'] > 960 * 544:
            raise ValueError('live frame dimensions exceed the actual Vita extent')
        previous_us = record['us']
        for name in (record['raw'], record['display'], metadata_path.name):
            data = read_profile_file(directory / name, MAX_PNG_BYTES, owned=True)
            if output != directory:
                with directory_fd(output) as parent:
                    descriptor = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                                         0o600, dir_fd=parent)
                with os.fdopen(descriptor, 'wb') as stream:
                    stream.write(data)
        raw, display = output / record['raw'], output / record['display']
        record.update(raw_image=png_metadata(raw), image=png_metadata(display),
                      metadata_sha256=hashlib.sha256(content).hexdigest())
        for image in (record['raw_image'], record['image']):
            if (image['width'], image['height']) != (record['width'], record['height']):
                raise ValueError('live frame PNG dimensions differ from metadata')
        verify_display_rgb(raw, display)
        frames.append(record)
    return dict(kind='ww05-live-images-v1', enabled=enabled, audio=audio, captures=len(frames), frames=frames,
                parity_verified=False, gameplay_verified=False, fps_baseline=not enabled,
                capture_point='after-gx-present-before-next-frame',
                policy='actual front-buffer readback only; preserve raw alpha, display alpha=255 with identical RGB; uniform pixels are not a game scene')
