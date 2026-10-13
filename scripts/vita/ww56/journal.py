"""Incremental tail for logs that grow while a phase runs.

Reads only whole lines after the last read and refuses a replaced or
truncated file: the watcher must never re-read or fabricate evidence.
"""

from __future__ import annotations

import os


class IncrementalLog:
    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.pending = b''
        self.identity = None

    def read(self):
        try:
            with self.path.open('rb') as stream:
                info = os.fstat(stream.fileno())
                identity = (info.st_dev, info.st_ino)
                if self.identity is not None and (identity != self.identity or info.st_size < self.offset):
                    raise ValueError('observed log was replaced or truncated: ' + str(self.path))
                self.identity = identity
                stream.seek(self.offset)
                content = stream.read()
                self.offset += len(content)
        except FileNotFoundError:
            return [], 0
        parts = (self.pending + content).split(b'\n')
        self.pending = parts.pop()
        return [part.decode('utf-8', errors='replace') for part in parts], len(content)
