#!/usr/bin/env python3
"""VITA-WW-15: one capture file through the whole GX chain.

Reads a WW05 boot-host capture — the report's GX_FRONTIER hex= field, its
bounded command journal, a
gather-pipe batch log, or a raw FIFO dump — and feeds the captured bytes
through the pinned frontend, the DrawPlan build, the gx_vita translation and
bounded Cg generation (the gx_bridge binary), printing one machine-readable
line per fact. An empty capture reports honestly that no GX bytes exist yet; a
partial one (the host stopped mid-command) says so; a malformed or rejected
one is refused with its reason.

Usage: bridge.py <capture-file> [--mem1 <dump> | --snapshot <manifest>]
Exit:  0 replay without gaps (including no-bytes-yet and partial captures),
        1 refusal or coverage gaps, 2 usage or missing tooling.

Environment:
  WW02_BUILD  build directory (default <main checkout>/build-f2/ww02)
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import capture
import memory_snapshot

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = ROOT / 'build-f2/ww02'


def find_binary():
    binary = Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD)) / "gx-host" / "gx_bridge"
    if not binary.is_file():
        raise FileNotFoundError(f"{binary} not built; run scripts/vita/gx/build.py")
    return binary


def run_stream(binary, fifo, mem1_path, *, ranges_path=None):
    """Feed the captured bytes to the bridge binary; return (report, refusal)."""
    with tempfile.NamedTemporaryFile(prefix="ww15-fifo-", suffix=".bin",
                                     dir=os.environ.get("TMPDIR", "/tmp/opencode")) as stream:
        stream.write(fifo)
        stream.flush()
        command = [str(binary), "--fifo", stream.name]
        if mem1_path is not None:
            command += ["--mem1", str(mem1_path)]
        environment = os.environ.copy()
        environment.pop('GX_BRIDGE_MEM1_RANGES', None)
        if ranges_path is not None:
            if mem1_path is None:
                raise ValueError('snapshot coverage requires MEM1 backing')
            environment['GX_BRIDGE_MEM1_RANGES'] = str(ranges_path.resolve())
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
    if result.returncode == 0:
        if ranges_path is not None and not any(line.startswith('memory coverage=GX_MEM1_RANGES_V1 ')
                                               for line in result.stdout.splitlines()):
            return None, 'gx_bridge lacks guarded snapshot support; rebuild the WW18 bridge'
        return result.stdout, None
    return None, result.stderr.strip() or "gx_bridge rejected the stream"


def replay_result(output, *, captured_draws=None):
    summary = re.search(r'^bridge .*$', output, re.MULTILINE)
    if summary is None:
        raise ValueError('bridge did not report counters')
    counters = dict(item.split('=', 1) for item in summary[0].split()[1:])
    gaps = any(int(counters[name]) for name in ('plans_skipped', 'programs_refused', 'copies_refused'))
    if captured_draws is not None:
        gaps |= int(counters['programs']) < captured_draws
    gaps |= re.search(r'^gap .*=[1-9]\d*$', output, re.MULTILINE) is not None
    return output + ('verdict=ran-with-coverage-gaps\n' if gaps else 'verdict=ran\n'), int(gaps)


def run_report(parsed, binary, mem1_path, *, ranges_path=None):
    if not parsed.complete and not parsed.data:
        print("verdict=no-bytes-yet")
        return 0
    if not parsed.complete:
        print("verdict=partial")
        return 0
    output, refusal = run_stream(binary, parsed.data, mem1_path, ranges_path=ranges_path)
    if refusal is not None:
        print(f"bridge ok=0 reason={refusal}")
        print("verdict=rejected")
        return 1
    text, status = replay_result(output)
    print(text, end="")
    return status


def run_capture(kind, payload, source, binary, memory, ranges=None):
    if kind == "report":
        parsed = payload
        print(f"capture kind=report complete={int(parsed.complete)} "
              f"bytes={parsed.size} opcode={parsed.opcode:02X} pc={parsed.pc:08X} "
              f"completion_pc={parsed.completion_pc:08X} trailing={parsed.trailing} "
              f"edge_stop={int(parsed.edge_stop)} source={source}")
        return run_report(parsed, binary, memory, ranges_path=ranges)
    print(f"capture kind={kind} bytes={len(payload)} source={source}")
    if kind == "journal":
        print("coverage=logged-command-prefix-only")
    output, refusal = run_stream(binary, payload, memory, ranges_path=ranges)
    if refusal is not None:
        print(f"bridge ok=0 reason={refusal}")
        print("verdict=rejected")
        return 1
    text, status = replay_result(output)
    print(text, end="")
    return status


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", type=Path,
                        help="WW05 report, gather-pipe log, or raw FIFO dump")
    memory_args = parser.add_mutually_exclusive_group()
    memory_args.add_argument("--mem1", type=Path, default=None,
                         help="guest MEM1 dump the captured addresses resolve against")
    memory_args.add_argument("--snapshot", type=Path, help="versioned MEM1 binary/region manifest")
    arguments = parser.parse_args(argv)

    try:
        binary = find_binary()
    except FileNotFoundError as error:
        print(f"refusal reason={error}", file=sys.stderr)
        return 2

    try:
        kind, payload = capture.read_capture(arguments.capture)
        if arguments.snapshot is not None:
            fifo = payload.data if kind == 'report' else payload
            snapshot = memory_snapshot.load(arguments.snapshot, memory_snapshot.sha256(fifo))
            with tempfile.TemporaryDirectory(prefix='ww18-snapshot-',
                                              dir=os.environ.get('TMPDIR', '/tmp/opencode')) as directory:
                directory = Path(directory)
                snapshot.stage(directory)
                return run_capture(kind, payload, arguments.capture, binary,
                                    directory / 'mem1.bin', directory / 'mem1.ranges')
    except (ValueError, OSError) as error:
        print(f"refusal reason={error}")
        print("verdict=rejected")
        return 1

    return run_capture(kind, payload, arguments.capture, binary, arguments.mem1)


if __name__ == "__main__":
    raise SystemExit(main())
