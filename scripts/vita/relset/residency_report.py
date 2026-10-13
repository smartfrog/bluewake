#!/usr/bin/env python3
"""Join one boot run's REL evidence with the build catalog.

Reads a fixture directory holding the run's `ww05.log` and `ww05.report` (and,
when present, the `ww05.mem1-N.bin` snapshots), plus the build evidence
(translator log, link map, verified raw `.rel` sizes), then writes
`residency.json` and `summary.txt` under --out.

What the report can and cannot say is bounded by the markers the host emits.
Today that is [rel-frontier] / [rel-materialized] / [rel-link]; the ticket-14
[rel-load] / [rel-unload] pair is absent, so the resident set is "materialized
and never reported released" and the report records the missing contract
markers instead of inventing loads and unloads.

Exit status: 0 when the evidence was read and the report written, 1 when the
evidence is unusable, 2 on bad arguments. No game code is run or modified.

    python3 scripts/vita/relset/residency_report.py --run <fixture-dir> \
      --evidence build-f5/ww05 --map build-f5/ww05/artifacts-*/ww05.map \
      --out build-f19/relresidency/<label> --label <label>
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from boot_residency import summarize  # noqa: E402
from mem1_regions import compare_snapshots, measure_snapshot  # noqa: E402
from rel_footprint import build_catalog  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
GUEST_BASE = 0x80000000


def git_source() -> str:
    return subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                                   text=True).strip()


def read_catalog(evidence: Path, map_path: Path) -> dict:
    translate = (evidence / "logs/translate-rels.log").read_text(errors="replace")
    rel_dirs = sorted(evidence.glob("rel-importer-check-*/private/rels"))
    if not rel_dirs:
        raise ValueError("no verified raw REL directory under " + str(evidence))
    raw_sizes = {path.stem: path.stat().st_size for path in rel_dirs[0].glob("*.rel")}
    catalog = build_catalog(translate, map_path.read_text(errors="replace"), raw_sizes)
    return {
        "modules": len(catalog.modules),
        "compiled_bytes": catalog.compiled_bytes,
        "raw_bytes": catalog.raw_bytes,
        "largest": [
            {"module_id": module.module_id, "name": module.name,
             "compiled_bytes": module.compiled_bytes, "raw_bytes": module.raw_bytes}
            for module in catalog.largest(10)],
    }


def read_mem1(run_dir: Path) -> dict | None:
    first = run_dir / "ww05.mem1-1.bin"
    second = run_dir / "ww05.mem1-2.bin"
    if not first.is_file() or not second.is_file():
        return None
    snapshots = [first, second]
    stats = [measure_snapshot(path.read_bytes(), GUEST_BASE) for path in snapshots]
    growth = compare_snapshots(snapshots[0].read_bytes(), snapshots[1].read_bytes())
    return {
        "snapshots": len(snapshots),
        "bytes_total": stats[0].bytes_total,
        "nonzero": [entry.nonzero for entry in stats],
        "first_nonzero": stats[0].first_nonzero,
        "last_nonzero": stats[0].last_nonzero,
        "growth": {"newly_written": growth.newly_written,
                   "cleared": growth.cleared, "changed": growth.changed},
    }


def render_summary(report: dict) -> str:
    lines = [f"VITA-WW-19 residency: {report['label']}",
             f"source            {report['source']}",
             f"boot frontier     cycles={report['report']['cycles']} "
             f"decoded={report['report']['gx_stream']['decoded']} "
             f"presented={report['report']['render']['presented']}",
             f"resident RELs     {len(report['resident'])}"]
    for entry in report["resident"]:
        lines.append(f"  module {entry['module_id']} {entry['name']} "
                     f"bytes={entry['bytes']} capacity={entry['capacity']} "
                     f"guest={entry['guest']:#010x} "
                     f"cycles={entry['loaded_between_cycles']}")
    lines.append(f"marker contract   rel-load={report['contract']['rel_load_marker']} "
                 f"rel-unload={report['contract']['rel_unload_marker']}")
    catalog = report["catalog"]
    lines.append(f"catalog            {catalog['modules']} modules, "
                 f"{catalog['compiled_bytes']} compiled bytes, "
                 f"{catalog['raw_bytes']} raw bytes")
    for entry in catalog["largest"][:3]:
        lines.append(f"  largest {entry['name']} module={entry['module_id']} "
                     f"compiled={entry['compiled_bytes']}")
    if report["mem1"] is not None:
        mem1 = report["mem1"]
        lines.append(f"MEM1 snapshots     {mem1['bytes_total']} bytes each, "
                     f"nonzero={mem1['nonzero']} "
                     f"extent={mem1['first_nonzero']:#010x}..{mem1['last_nonzero']:#010x}")
        lines.append(f"MEM1 growth        newly_written={mem1['growth']['newly_written']} "
                     f"cleared={mem1['growth']['cleared']} "
                     f"changed={mem1['growth']['changed']}")
    lines.append(f"stop              {report['report']['stop']}")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True,
                        help='fixture dir with ww05.log, ww05.report, ww05.mem1-N.bin')
    parser.add_argument('--evidence', type=Path, required=True,
                        help='build evidence root (translator log, raw RELs)')
    parser.add_argument('--map', type=Path, required=True, help='the link map')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='unlabelled')
    parser.add_argument('--source', default=None,
                        help='build source (default: git HEAD)')
    args = parser.parse_args()

    report = summarize((args.run / "ww05.log").read_text(errors="replace"),
                       (args.run / "ww05.report").read_text(errors="replace"))
    report["label"] = args.label
    report["source"] = args.source or git_source()
    report["catalog"] = read_catalog(args.evidence, args.map)
    report["mem1"] = read_mem1(args.run)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "residency.json").write_text(json.dumps(report, indent=2) + "\n")
    (args.out / "summary.txt").write_text(render_summary(report))
    print(render_summary(report), end='')
    print(f"written           {args.out / 'residency.json'}")
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except ValueError as error:
        print(f"residency_report: {error}", file=sys.stderr)
        raise SystemExit(1)
