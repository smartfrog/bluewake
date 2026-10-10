#!/usr/bin/env python3
"""Build the bounded FP diagnostic. Never executes Vita3K or accesses a console."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

from build import Campaign, PINS, ROOT, build_directory, verify_source
from fp_report import validate
from fp_vectors import cases, write_header
from prepare import prepare


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vita", action="store_true")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--vitasdk", type=Path, default=Path("~/vitasdk").expanduser())
    parser.add_argument("--out", type=Path, default=ROOT / "build-vita-ww-03")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.out = build_directory(args.out)
    args.ninja = shutil.which(args.ninja)
    if not args.ninja:
        parser.error("Ninja not found")
    campaign = Campaign(args.out)
    sources = {name: ROOT / "ref/vita-ww-01" / directory
               for name, directory in (("recompcore", "RecompCore"), ("dolrecomp", "DolRecomp"))}
    lock = json.loads((ROOT / "config/dependencies.lock.json").read_text())
    locked = {entry["id"]: entry for entry in lock["dependencies"]}
    profile = (ROOT / "scripts/builder/profiles/bluewake.sh").read_text()
    for name, path in sources.items():
        if locked[name]["sha"] != PINS[name] or PINS[name] not in profile:
            raise ValueError("product pin changed; review required")
        verify_source(path, PINS[name])
    source = campaign.run("source-sha", ["git", "rev-parse", "HEAD"]).strip()
    generator = campaign.configure("fp-generator", ROOT / "vita/feasibility/armv7/generator", args,
                                   [f"-DCMAKE_C_COMPILER={args.cc}", f"-DDOLRECOMP_DIR={sources['dolrecomp']}"])
    campaign.compile("fp-generator", generator, args, ("test_codegen_emit", "ww_stack_emit", "ww_fp_emit"))
    fixture = args.out / "fp-fixture"
    fixture.mkdir(exist_ok=True)
    campaign.run("fp-corpus", [generator / "dolrecomp/test_codegen_emit", fixture / "corpus.c"])
    for name in ("stack", "fp"):
        (fixture / f"{name}.c").write_text(campaign.run(f"emit-{name}", [generator / f"ww_{name}_emit"]))
    prepare(ROOT, sources["dolrecomp"], fixture / "corpus.c", fixture / "stack.c", fixture, fixture / "fp.c")
    write_header(fixture / "fp_vectors.h")
    (fixture / "fp_identity.h").write_text(f'#define WW_FP_SOURCE "{source}"\n')
    evidence = {"source": source, "pins": PINS, "cases": cases(), "artifacts": {}, "vita3k": "NOT RUN"}
    semantic_status = 0
    for platform in (("host", "vita") if args.vita else ("host",)):
        compiler = args.cc
        options = [f"-DCMAKE_C_COMPILER={args.cc}"]
        if platform == "vita":
            os.environ["VITASDK"] = str(args.vitasdk.resolve())
            compiler = str(args.vitasdk / "bin/arm-vita-eabi-gcc")
            options = [f"-DCMAKE_TOOLCHAIN_FILE={args.vitasdk}/share/vita.toolchain.cmake"]
        build = campaign.configure(f"{platform}-fp", ROOT / "vita/feasibility/armv7", args,
                                   [*options, f"-DRECOMPCORE_DIR={sources['recompcore']}",
                                    f"-DFIXTURE_DIR={fixture}", "-DWW_MODE=fp"])
        campaign.compile(f"{platform}-fp", build, args)
        evidence[platform] = campaign.evidence(f"{platform}-fp", build, compiler)
        for name in (("ww_fixture", "eboot.bin", "ww_fp.vpk") if platform == "vita" else ("ww_fixture",)):
            artifact = build / name
            evidence["artifacts"][str(artifact)] = {"sha256": sha256(artifact), "bytes": artifact.stat().st_size}
        if platform == "host":
            token = uuid.uuid4().hex
            run = args.out / ("host-run-" + token)
            run.mkdir(mode=0o700)
            package = sha256(build / "ww_fixture")
            (run / "fp.request").write_text(f"{token} {source} {package}\n")
            result = subprocess.run([build / "ww_fixture"], cwd=run, capture_output=True, text=True)
            (run / "process.log").write_text(result.stdout + result.stderr)
            status = validate((run / "fp.report").read_text(), token, source, package, cases())
            if result.returncode != status:
                raise RuntimeError(f"host report/process status disagreement: {run}")
            semantic_status = status
            evidence["host_run"] = {"directory": str(run), "exit": result.returncode,
                                    "semantic": "FAIL" if status else "PASS"}
            print(f"HOST SEMANTIC {'FAIL' if status else 'PASS'}: {run}", flush=True)
    for name, path in sources.items():
        verify_source(path, PINS[name])
    (args.out / "fp-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return semantic_status


if __name__ == "__main__":
    raise SystemExit(main())
