#!/usr/bin/env python3
"""Run the VITA-WW-14 active-set measurement on one run's evidence.

Reads an optional REL-event log ([rel-load] / [rel-unload] markers) and the
static catalog build logs, then writes baseline.json and summary.txt under
--out. A run with no REL events records the static picture and says the
active set is not measured; it never invents a number.

Exit status: 0 when the evidence was read and the baseline written (even when
the verdict is not measured - that is the honest result), 1 when the evidence
is unusable, 2 on bad arguments. No game code is run or modified here.

Command (static picture only, until the host lane emits the markers):

    python3 scripts/vita/relset/run.py --out build-f14/relset/<label> \
      --label <label> --composite-log <generate-composite.log> \
      --translate-log <translate-rels.log>

and with events, the moment a run log has them:

    ... --events <run.log> --scene boot
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from composite_evidence import (  # noqa: E402
    CompositeEvidenceError,
    parse_composite_log,
    parse_rel_module_lines,
)
from rel_events import parse_rel_events  # noqa: E402
from relset_baseline import build_baseline, write_baseline  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUT = Path('/home/fred/projects/zeldaWWHDRecomp/build-f14/relset')


def git_source() -> str:
    return subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                                   text=True).strip()


def read_static(composite_log: Path | None, translate_log: Path | None) -> dict | None:
    if composite_log is None and translate_log is None:
        return None
    static = {}
    if composite_log is not None:
        summary = parse_composite_log(composite_log.read_text(errors='replace'))
        static.update({
            "dol_chunks": summary.dol_chunks,
            "rel_modules": summary.rel_modules,
            "composite_chunks": summary.composite_chunks,
            "code_ranges": summary.code_ranges,
            "rel_chunks": summary.rel_chunks,
        })
    if translate_log is not None:
        modules = parse_rel_module_lines(translate_log.read_text(errors='replace'))
        static["module_ids"] = sorted({module.module_id for module in modules})
    if "rel_modules" in static and "module_ids" in static:
        if static["rel_modules"] != len(static["module_ids"]):
            raise CompositeEvidenceError(
                "the composite log says " + str(static["rel_modules"])
                + " REL modules but the translator log lists "
                + str(len(static["module_ids"])))
    return static


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT,
                        help='evidence directory (default: the ignored build-f14 tree)')
    parser.add_argument('--label', default='unlabelled')
    parser.add_argument('--scene', default=None,
                        help='game phase the window covers (boot/title/outset)')
    parser.add_argument('--events', type=Path, action='append', default=[],
                        dest='event_logs', help='log with REL-event markers (repeatable)')
    parser.add_argument('--composite-log', type=Path, default=None,
                        help='generate-composite.log with the chunk/module totals')
    parser.add_argument('--translate-log', type=Path, default=None,
                        help='translate-rels.log with the per-module lines')
    parser.add_argument('--source', default=None, help='build source (default: git HEAD)')
    args = parser.parse_args()

    events = None
    if args.event_logs:
        lines = []
        for path in args.event_logs:
            lines.extend(path.read_text(errors='replace').splitlines())
        events = parse_rel_events(lines)
    static = read_static(args.composite_log, args.translate_log)
    notes = [
        ("event logs: " + str(len(args.event_logs))) if args.event_logs
        else "no REL-event log: the active set is not measured",
        "static evidence: " + ("given" if static is not None else "none"),
    ]
    baseline = build_baseline(
        label=args.label, source=args.source or git_source(), scene=args.scene,
        events=events, static=static, notes=notes)
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
