"""Incremental tail for logs that grow while a phase runs.

Reads only whole lines after the last read and refuses a replaced or
truncated file: the watcher must never re-read or fabricate evidence.
"""

from __future__ import annotations

import os
import hashlib


class IncrementalLog:
    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.pending = b''
        self.identity = None
        self.buffer = b''
        self.complete = False
        self.lines = 0
        self.clipped_bytes = 0
        self.clipped_lines = 0
        self.line_clipped = False
        self.sha256 = hashlib.sha256()
        self.scanned_bytes = 0

    def read_bounded(self, *, max_bytes=65536, max_lines=256, line_bytes=2048):
        """Read at most one chunk; retain bounded prefixes of oversized lines.

        This explicit API must not be mixed with read(). complete describes the
        observed extent, not a promise that a live writer has finished.
        """
        if not (1 <= max_bytes <= 65536 and 1 <= max_lines <= 256 and 1 <= line_bytes <= 2048):
            raise ValueError('invalid bounded journal policy')
        count = 0
        try:
            with self.path.open('rb') as stream:
                info = os.fstat(stream.fileno())
                identity = (info.st_dev, info.st_ino)
                if self.identity is not None and (identity != self.identity or info.st_size < self.offset):
                    raise ValueError('observed log was replaced or truncated: ' + str(self.path))
                self.identity = identity
                if not self.buffer:
                    stream.seek(self.offset)
                    self.buffer = stream.read(max_bytes)
                    count = len(self.buffer)
                    self.offset += count
                    self.sha256.update(self.buffer)
                at_end = self.offset == info.st_size
        except FileNotFoundError:
            if self.identity is not None:
                raise ValueError('observed log vanished: ' + str(self.path))
            self.complete = False
            return [], 0
        parts = self.buffer.split(b'\n', max_lines)
        self.buffer = parts.pop()
        result = []
        for part in parts:
            self._bounded_fragment(part, line_bytes)
            result.append(self.pending.decode('utf-8', errors='replace'))
            self.pending = b''
            self.line_clipped = False
            self.lines += 1
            self.scanned_bytes += len(part) + 1
        if len(result) < max_lines:
            self._bounded_fragment(self.buffer, line_bytes)
            self.scanned_bytes += len(self.buffer)
            self.buffer = b''
        self.complete = at_end and not self.buffer
        return result, count

    def _bounded_fragment(self, part, limit):
        retained = part[:max(0, limit - len(self.pending))]
        self.pending += retained
        clipped = len(part) - len(retained)
        self.clipped_bytes += clipped
        if clipped and not self.line_clipped:
            self.clipped_lines += 1
            self.line_clipped = True

    def finish_bounded(self):
        """Postflight only: count an unterminated final line at observed EOF."""
        if not self.complete:
            raise ValueError('cannot finish a partial journal scan')
        if not self.pending and not self.line_clipped:
            return []
        self.lines += 1
        result = [self.pending.decode('utf-8', errors='replace')]
        self.pending = b''
        self.line_clipped = False
        return result

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


def count_marker_lines(text, marker):
    return sum(1 for line in text.splitlines() if marker in line)
