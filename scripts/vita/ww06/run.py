#!/usr/bin/env python3
"""Run the VITA-WW-06 acceptance check on one or more session logs.

Each --run is one session log in order: the save session first, the reload
session after it. The runner parses the acceptance markers, evaluates the
five-minute Outset scenario requirements and writes baseline.json and
summary.txt under --out (the ignored build-f6 tree by default). A log with no
acceptance events records the not-measured verdict; it never invents one.

Exit status: 0 when the evidence was read and the baseline written (even when
the verdict is not measured or not met - that is the honest result), 1 when a
stream is unusable, 2 on bad arguments. No game code is run or modified here.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from acceptance_events import parse_acceptance_events  # noqa: E402
from baseline import build_baseline, write_baseline  # noqa: E402
from scenario import DEFAULT_DEADZONE, DEFAULT_MIN_FRAMES  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f6/ww06')


def git_source() -> str:
    return subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                                   text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT,
                        help='evidence directory (default: the ignored build-f6 tree)')
    parser.add_argument('--label', default='unlabelled')
    parser.add_argument('--run', type=Path, action='append', default=[],
                        dest='runs', help='session log with acceptance markers, '
                        'in session order (repeatable; the reload session after '
                        'the save session)')
    parser.add_argument('--scene', default=None,
                        help='game phase the sessions cover (boot/title/outset)')
    parser.add_argument('--deadzone', type=int, default=DEFAULT_DEADZONE,
                        help='stick deflection deadzone in signed-byte units')
    parser.add_argument('--min-frames', type=int, default=DEFAULT_MIN_FRAMES,
                        dest='min_frames', help='consecutive deflected frames '
                        'a movement/camera run needs')
    parser.add_argument('--source', default=None, help='build source (default: git HEAD)')
    args = parser.parse_args()

    sessions = []
    for index, path in enumerate(args.runs):
        events = parse_acceptance_events(path.read_text(errors='replace'))
        sessions.append((f"session-{index}", str(path), events))
    notes = [
        f"runs: {len(sessions)}",
        f"deadzone: {args.deadzone}, min-frames: {args.min_frames}",
    ]
    if not sessions:
        notes.append("no session log given: nothing was measured")
    for name, _, events in sessions:
        if events.is_empty():
            notes.append(f"{name}: no acceptance events in the log")
    baseline = build_baseline(
        label=args.label, source=args.source or git_source(), scene=args.scene,
        sessions=sessions, deadzone=args.deadzone, min_frames=args.min_frames,
        notes=notes)
    json_path, summary_path = write_baseline(args.out, baseline)
    print(summary_path.read_text(), end='')
    print(f"written           {json_path}")
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except ValueError as error:
        print(f"run: {error}", file=sys.stderr)
        raise SystemExit(1)
