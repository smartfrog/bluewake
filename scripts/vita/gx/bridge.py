#!/usr/bin/env python3
"""VITA-WW-15: one capture file through the whole GX chain.

Reads a WW05 boot-host capture — the report's GX_FRONTIER hex= field, a
gather-pipe batch log, or a raw FIFO dump — and feeds the captured bytes
through the pinned frontend, the DrawPlan build, the gx_vita translation and
bounded Cg generation (the gx_bridge binary), printing one machine-readable
line per fact. An empty capture reports honestly that no GX bytes exist yet; a
partial one (the host stopped mid-command) says so; a malformed or rejected
one is refused with its reason.

Usage: bridge.py <capture-file> [--mem1 <dump>]
Exit:  0 report printed (including no-bytes-yet and partial captures),
       1 refusal, 2 usage or missing tooling.

Environment:
  WW02_BUILD  build directory (default <main checkout>/build-f2/ww02)
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import capture

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BUILD = Path("/home/fred/projects/zeldaWWHDRecomp/build-f2/ww02")


def find_binary():
    binary = Path(os.environ.get("WW02_BUILD", DEFAULT_BUILD)) / "gx-host" / "gx_bridge"
    if not binary.is_file():
        raise FileNotFoundError(f"{binary} not built; run scripts/vita/gx/build.py")
    return binary


def run_stream(binary, fifo, mem1_path):
    """Feed the captured bytes to the bridge binary; return (report, refusal)."""
    with tempfile.NamedTemporaryFile(prefix="ww15-fifo-", suffix=".bin",
                                     dir=os.environ.get("TMPDIR", "/tmp/opencode")) as stream:
        stream.write(fifo)
        stream.flush()
        command = [str(binary), "--fifo", stream.name]
        if mem1_path is not None:
            command += ["--mem1", str(mem1_path)]
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
    if result.returncode == 0:
        return result.stdout, None
    return None, result.stderr.strip() or "gx_bridge rejected the stream"


def run_report(parsed, binary, mem1_path):
    if not parsed.complete and not parsed.data:
        print("verdict=no-bytes-yet")
        return 0
    if not parsed.complete:
        print("verdict=partial")
        return 0
    output, refusal = run_stream(binary, parsed.data, mem1_path)
    if refusal is not None:
        print(f"bridge ok=0 reason={refusal}")
        print("verdict=rejected")
        return 1
    print(output, end="")
    print("verdict=ran")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", type=Path,
                        help="WW05 report, gather-pipe log, or raw FIFO dump")
    parser.add_argument("--mem1", type=Path, default=None,
                        help="guest MEM1 dump the captured addresses resolve against")
    arguments = parser.parse_args(argv)

    try:
        binary = find_binary()
    except FileNotFoundError as error:
        print(f"refusal reason={error}", file=sys.stderr)
        return 2

    try:
        kind, payload = capture.read_capture(arguments.capture)
    except (ValueError, OSError) as error:
        print(f"refusal reason={error}")
        print("verdict=rejected")
        return 1

    if kind == "report":
        parsed = payload
        print(f"capture kind=report complete={int(parsed.complete)} "
              f"bytes={parsed.size} opcode={parsed.opcode:02X} pc={parsed.pc:08X} "
              f"completion_pc={parsed.completion_pc:08X} trailing={parsed.trailing} "
              f"edge_stop={int(parsed.edge_stop)} source={arguments.capture}")
        return run_report(parsed, binary, arguments.mem1)

    print(f"capture kind={kind} bytes={len(payload)} source={arguments.capture}")
    if kind == "journal":
        print("coverage=logged-command-prefix-only")
    output, refusal = run_stream(binary, payload, arguments.mem1)
    if refusal is not None:
        print(f"bridge ok=0 reason={refusal}")
        print("verdict=rejected")
        return 1
    print(output, end="")
    print("verdict=ran")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
