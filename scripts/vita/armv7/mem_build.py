#!/usr/bin/env python3
"""Build a synthetic memory diagnostic; never starts Vita3K or transfers a package."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

from build import Campaign, PINS, ROOT, build_directory, checked_sources, verify_source
from fp_build import sha256
from mem_report import validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vita", action="store_true")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--vitasdk", type=Path, default=Path("~/vitasdk").expanduser())
    parser.add_argument("--out", type=Path, default=ROOT / "build-vita-ww-04")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.out = build_directory(args.out)
    args.ninja = shutil.which(args.ninja)
    if not args.ninja:
        parser.error("Ninja not found")
    sources = checked_sources()
    campaign = Campaign(args.out)
    source = campaign.run("source-sha", ["git", "rev-parse", "HEAD"]).strip()
    fixture = args.out / "mem-fixture"
    fixture.mkdir(exist_ok=True)
    (fixture / "mem_identity.h").write_text(f'#define WW_MEM_SOURCE "{source}"\n')
    evidence = {"source": source, "pins": PINS, "method": "bounded-v2", "title": "BWWM00004",
                "artifacts": {}, "vita3k": "NOT RUN", "physical_vita": "NOT RUN"}
    semantic_status = 0
    for platform in (("host", "vita") if args.vita else ("host",)):
        compiler = args.cc
        options = [f"-DCMAKE_C_COMPILER={args.cc}"]
        if platform == "vita":
            os.environ["VITASDK"] = str(args.vitasdk.resolve())
            compiler = str(args.vitasdk / "bin/arm-vita-eabi-gcc")
            options = [f"-DCMAKE_TOOLCHAIN_FILE={args.vitasdk}/share/vita.toolchain.cmake"]
        build = campaign.configure(f"{platform}-mem", ROOT / "vita/feasibility/armv7", args,
                                   [*options, f"-DRECOMPCORE_DIR={sources['recompcore']}",
                                    f"-DFIXTURE_DIR={fixture}", "-DWW_MODE=mem"])
        campaign.compile(f"{platform}-mem", build, args)
        evidence[platform] = campaign.evidence(f"{platform}-mem", build, compiler)
        for name in (("ww_fixture", "eboot.bin", "ww_mem.vpk") if platform == "vita" else ("ww_fixture",)):
            artifact = build / name
            evidence["artifacts"][str(artifact)] = {"sha256": sha256(artifact), "bytes": artifact.stat().st_size}
        if platform == "host":
            ctest = str(Path(shutil.which(args.cmake) or args.cmake).with_name("ctest"))
            campaign.run("host-mem-tests", [ctest, "--test-dir", build, "--output-on-failure"])
            token = uuid.uuid4().hex
            run = args.out / ("host-run-" + token)
            run.mkdir(mode=0o700)
            package = sha256(build / "ww_fixture")
            (run / "mem.request").write_text(f"{token} {source} {package}\n")
            result = subprocess.run([build / "ww_fixture"], cwd=run, capture_output=True, text=True)
            (run / "process.log").write_text(result.stdout + result.stderr)
            status = validate((run / "mem.report").read_text(), token, source, package, expected_platform="HOST")
            if result.returncode != status:
                raise RuntimeError(f"host report/process disagreement: {run}")
            semantic_status = status
            evidence["host_run"] = {"directory": str(run), "exit": result.returncode,
                                    "semantic": "FAIL" if status else "PASS", "sdk": "SKIP"}
    for name, path in sources.items():
        verify_source(path, PINS[name])
    (args.out / "mem-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return semantic_status


if __name__ == "__main__":
    raise SystemExit(main())
