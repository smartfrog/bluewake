"""Early-stop decision rules for the WW56 bounded trial.

The trial stops at the first useful frontier instead of waiting out the guest
ceiling: an unsupported/fatal marker stops the run immediately, the first
requested REL settles briefly and then stops, and a first presentation that
never reaches REL stops after its grace. Every early stop keeps the
INCOMPLETE disposition honestly.

Failure markers are the established set from the WW35/WW50 watchers; the run
records them but never edits or retries the guest.
"""

from __future__ import annotations

from dataclasses import dataclass
import math

FAILURE_MARKERS = ("Config file can't be loaded:", '[gx-unsupported]', '[renderer-error]',
                   '[renderer] init failed:', '[renderer-after-present] error=', '[fatal]',
                   'glGetError:', '[exception]')
STOP_WORDS = ('unsupported', 'failed', 'unhandled', 'rejected')


def is_failure(line):
    if any(marker in line for marker in FAILURE_MARKERS):
        return True
    return 'STOP reason=' in line and any(word in line.lower() for word in STOP_WORDS)


def _optional_grace(value, name):
    if value is None:
        return None
    if not math.isfinite(value) or value <= 0:
        raise ValueError(name + ' must be finite and positive, or None to disable')
    return value


@dataclass
class StopState:
    ceiling_seconds: float | None
    present_grace_seconds: float | None
    rel_settle_seconds: float | None
    started: float
    first_present_at: float | None = None
    first_rel_at: float | None = None

    def __post_init__(self):
        self.ceiling_seconds = _optional_grace(self.ceiling_seconds, 'ceiling')
        if not math.isfinite(self.started):
            raise ValueError('started must be finite')
        self.present_grace_seconds = _optional_grace(self.present_grace_seconds, 'present grace')
        self.rel_settle_seconds = _optional_grace(self.rel_settle_seconds, 'rel settle')

    def note_present(self, now):
        if self.first_present_at is None:
            self.first_present_at = now

    def note_rel(self, now):
        if self.first_rel_at is None:
            self.first_rel_at = now


def evaluate_stop(state, *, now, failure_lines):
    if failure_lines:
        return {'stop_reason': 'unsupported-or-context-error', 'failure_lines': list(failure_lines)}
    if (state.first_rel_at is not None and state.rel_settle_seconds is not None and
            now - state.first_rel_at >= state.rel_settle_seconds):
        return {'stop_reason': 'first-requested-rel-settle', 'first_rel_at': state.first_rel_at}
    if (state.first_present_at is not None and state.first_rel_at is None and
            state.present_grace_seconds is not None and
            now - state.first_present_at >= state.present_grace_seconds):
        return {'stop_reason': 'first-present-grace', 'first_present_at': state.first_present_at}
    if state.ceiling_seconds is not None and now - state.started >= state.ceiling_seconds:
        return {'stop_reason': 'bounded-phase-planned-time-stop',
                'elapsed_seconds': now - state.started}
    return None
