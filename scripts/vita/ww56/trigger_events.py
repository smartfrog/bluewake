"""One-shot journal triggers for the WW56 snapshot protocol.

WW50 missed the first-present memory snapshot because presents and the kill
happened between one-second log observations. WW56 snapshots are event-driven:
the journal tail fires exactly once on `[create]: GPU =` and exactly once on
the first `[frame-present]` stamp, then those kinds never fire again. The
scanner is pure: lines in, trigger events out, no I/O, no polling.

Present stamps are parsed by the canonical perf harness parser
(`scripts/vita/perf/frame_events.py`), so a malformed stamp is an error here
just as it is there: a broken stream is not a measurement.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'perf'))

from frame_events import FrameEventError, parse_frame_events  # noqa: E402

GPU_MARKER = '[create]: GPU ='
PRESENT_MARKER = '[frame-present]'
GPU_TRIGGER = 'gpu-context-created'
PRESENT_TRIGGER = 'first-actual-frame-present'
KINDS = (GPU_TRIGGER, PRESENT_TRIGGER)


class TriggerError(ValueError):
    """A journal line carries a marker that is not a usable trigger."""


@dataclass(frozen=True)
class TriggerEvent:
    kind: str
    line: str
    frame: int | None = None
    us: int | None = None


class OneShotTriggerScanner:
    def __init__(self, kinds=KINDS):
        unknown = [kind for kind in kinds if kind not in KINDS]
        if unknown:
            raise ValueError('unknown trigger kind: ' + ', '.join(unknown))
        self.kinds = tuple(kinds)
        self.fired = set()

    def feed(self, lines):
        events = []
        for line in lines:
            if PRESENT_TRIGGER in self.kinds and PRESENT_TRIGGER not in self.fired and PRESENT_MARKER in line:
                try:
                    present = parse_frame_events([line]).presents[0]
                except (FrameEventError, IndexError) as error:
                    raise TriggerError('malformed [frame-present] stamp: ' + line) from error
                self.fired.add(PRESENT_TRIGGER)
                events.append(TriggerEvent(PRESENT_TRIGGER, line, present.index, present.us))
            if GPU_TRIGGER in self.kinds and GPU_TRIGGER not in self.fired and GPU_MARKER in line:
                self.fired.add(GPU_TRIGGER)
                events.append(TriggerEvent(GPU_TRIGGER, line))
        return events
