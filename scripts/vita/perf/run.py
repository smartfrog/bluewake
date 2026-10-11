#!/usr/bin/env python3
"""Run the VITA-WW-12 measurement on one run's evidence.

Reads an optional frame-event log ([frame-timing] / [frame-present] markers)
and any number of WW05 boot reports, then writes baseline.json and summary.txt
under --out. A run with no presented frames records the boot evidence and says
the frame rate is not measured; it never invents one.

Exit status: 0 when the evidence was read and the baseline written (even when
the verdict is below the floor - the number is the number), 1 when the evidence
is unusable, 2 on bad arguments. No optimization is applied or measured here.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from baseline import FLOOR_FPS, TARGET_FPS, build_baseline, write_baseline  # noqa: E402
from frame_events import parse_frame_events  # noqa: E402
from ww05_report import boot_series, parse_boot_report  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f12/perf')


def git_source() -> str:
    return subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                                   text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT,
                        help='evidence directory (default: the ignored build-f12 tree)')
    parser.add_argument('--label', default='unlabelled')
    parser.add_argument('--events', type=Path, help='log with frame-event markers')
    parser.add_argument('--ww05-report', type=Path, action='append', default=[],
                        dest='reports', help='WW05 boot report (repeatable)')
    parser.add_argument('--window', type=float, default=10.0,
                        help='windowed-metrics window in seconds')
    parser.add_argument('--target', type=int, default=TARGET_FPS)
    parser.add_argument('--floor', type=int, default=FLOOR_FPS)
    parser.add_argument('--source', default=None, help='build source (default: git HEAD)')
    args = parser.parse_args()

    events = None
    if args.events is not None:
        events = parse_frame_events(args.events.read_text(errors='replace'))
    boot = None
    if args.reports:
        boot = boot_series([parse_boot_report(path.read_text())
                            for path in args.reports])
    baseline = build_baseline(
        label=args.label, source=args.source or git_source(), window_s=args.window,
        events=events, boot=boot, target_fps=args.target, floor_fps=args.floor,
        notes=[f"events: {args.events}" if args.events else "no frame-event log",
               f"boot reports: {len(args.reports)}"])
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
