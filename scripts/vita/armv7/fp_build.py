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
import sys

from build import Campaign, PINS, ROOT, build_directory, checked_sources, verify_source
from fp_report import validate
from fp_vectors import cases, write_header
from prepare import prepare
sys.path.insert(0, str(ROOT / 'scripts/vita/ww05'))
from publication import read_artifact


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main(control=False):
    diagnostic = "arm" if control else "fp"
    if control:
        from arm_report import validate as validate_control
        from arm_vectors import METHOD, PROVENANCE, TITLE, cases as control_cases, write_header as write_control_header
    parser = argparse.ArgumentParser(description="Build independent scalar VFP/runtime comparison; never launches Vita3K." if control else __doc__)
    parser.add_argument("--vita", action="store_true")
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--vitasdk", type=Path, default=Path("~/vitasdk").expanduser())
    parser.add_argument("--out", type=Path, default=ROOT / ("build-ww08" if control else "build-vita-ww-03"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.out = build_directory(args.out)
    args.ninja = shutil.which(args.ninja)
    if not args.ninja:
        parser.error("Ninja not found")
    campaign = Campaign(args.out)
    sources = checked_sources()
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
    evidence = {"source": source, "pins": PINS, "publication_version": 2,
                "cases": cases(), "artifacts": {}, "vita3k": "NOT RUN"}
    if control:
        write_control_header(fixture / "arm_vectors.h")
        (fixture / "arm_identity.h").write_text(f'#define WW_ARM_SOURCE "{source}"\n')
        evidence.update(method=METHOD, title=TITLE, control_cases=control_cases(),
                        control_oracle=PROVENANCE, runtime_oracle="unchanged VITA-WW-03 Gekko literals")
    semantic_status = 0
    for platform in (("host", "vita") if args.vita else ("host",)):
        compiler = args.cc
        options = [f"-DCMAKE_C_COMPILER={args.cc}"]
        if platform == "vita":
            os.environ["VITASDK"] = str(args.vitasdk.resolve())
            compiler = str(args.vitasdk / "bin/arm-vita-eabi-gcc")
            options = [f"-DCMAKE_TOOLCHAIN_FILE={args.vitasdk}/share/vita.toolchain.cmake"]
        build = campaign.configure(f"{platform}-{diagnostic}", ROOT / "vita/feasibility/armv7", args,
                                   [*options, f"-DRECOMPCORE_DIR={sources['recompcore']}",
                                     f"-DFIXTURE_DIR={fixture}", f"-DWW_MODE={diagnostic}"])
        campaign.compile(f"{platform}-{diagnostic}", build, args)
        evidence[platform] = campaign.evidence(f"{platform}-{diagnostic}", build, compiler)
        for name in (("ww_fixture", "eboot.bin", f"ww_{diagnostic}.vpk") if platform == "vita" else ("ww_fixture",)):
            artifact = build / name
            evidence["artifacts"][str(artifact)] = {"sha256": sha256(artifact), "bytes": artifact.stat().st_size}
        if platform == "host":
            if control:
                ctest = str(Path(shutil.which(args.cmake) or args.cmake).with_name("ctest"))
                campaign.run("host-arm-tests", [ctest, "--test-dir", build, "--output-on-failure"])
            token = uuid.uuid4().hex
            run = args.out / ("host-run-" + token)
            run.mkdir(mode=0o700)
            package = sha256(build / "ww_fixture")
            (run / f"{diagnostic}.request").write_text(f"{token} {source} {package}\n")
            result = subprocess.run([build / "ww_fixture"], cwd=run, capture_output=True, text=True)
            (run / "process.log").write_text(result.stdout + result.stderr)
            text = read_artifact(run / f'{diagnostic}.report', 1024 * 1024, token=token,
                                  candidate=run / f'{diagnostic}.partial', publication=2).decode()
            status = validate_control(text, token, source, package, expected_platform="HOST") if control else validate(text, token, source, package, cases())
            if result.returncode != status:
                raise RuntimeError(f"host report/process status disagreement: {run}")
            semantic_status = status
            evidence["host_run"] = {"directory": str(run), "exit": result.returncode,
                                    "semantic": "FAIL" if status else "PASS"}
            if control:
                evidence["host_run"].update(control_measurement="SKIP", runtime_cases=len(cases()))
            print(f"HOST SEMANTIC {'FAIL' if status else 'PASS'}: {run}", flush=True)
    for name, path in sources.items():
        verify_source(path, PINS[name])
    (args.out / f"{diagnostic}-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return semantic_status


if __name__ == "__main__":
    raise SystemExit(main())
