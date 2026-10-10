#!/usr/bin/env python3
"""Opt-in, game-free VITA-WW-01 campaign. Never boots a game or installs a VPK."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

from prepare import prepare

ROOT = Path(__file__).resolve().parents[3]
PINS = {
    "recompcore": "ddd031b2a7f45fcbda37a49daac9b308b8d95da1",
    "dolrecomp": "b8b534591cba8ca7cd43943a655ee6e2591cf5de",
}


def verify_source(path, sha):
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True)
    if actual != sha or dirty:
        raise ValueError(f"source must be pristine at {sha}: {path}")


def checked_sources():
    locked = {entry["id"]: entry for entry in
              json.loads((ROOT / "config/dependencies.lock.json").read_text())["dependencies"]}
    profile = (ROOT / "scripts/builder/profiles/bluewake.sh").read_text()
    sources = {name: ROOT / "ref/vita-ww-01" / directory
               for name, directory in (("recompcore", "RecompCore"), ("dolrecomp", "DolRecomp"))}
    for name, path in sources.items():
        if locked[name]["sha"] != PINS[name] or PINS[name] not in profile:
            raise ValueError("product pin changed; review required")
        verify_source(path, PINS[name])
    return sources


def build_directory(path):
    path = path.resolve()
    if path.parent != ROOT or not path.name.startswith("build-"):
        raise ValueError("output must be a root build-* directory (already ignored)")
    return path


def collect_stack_usage(build):
    stacks = {}
    for file in build.rglob("*.su"):
        for line in file.read_text().splitlines():
            location, size, kind = line.split("\t")
            name = location.rsplit(":", 1)[-1]
            size = int(size)
            if name not in stacks or size > stacks[name]["bytes"]:
                stacks[name] = {"bytes": size, "kind": kind}
    return stacks


class Campaign:
    def __init__(self, output):
        self.output = output
        self.logs = output / "logs"
        self.logs.mkdir(parents=True, exist_ok=True)

    def run(self, name, command, env=None):
        command = [str(argument) for argument in command]
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, env=env)
        (self.logs / f"{name}.log").write_text(shlex.join(command) + "\n" + result.stdout)
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {self.logs / (name + '.log')}")
        print(f"PASS {name}", flush=True)
        return result.stdout

    def configure(self, name, source, args, options):
        build = self.output / name
        self.run(name + "-configure", [args.cmake, "-S", source, "-B", build, "-G", "Ninja",
                                      "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_MAKE_PROGRAM={args.ninja}",
                                      *options])
        return build

    def compile(self, name, build, args, targets=()):
        self.run(name + "-build", [args.cmake, "--build", build, "-j", args.jobs,
                                  *(["--target", *targets] if targets else [])])

    def evidence(self, name, build, compiler):
        binary = build / "ww_fixture"
        tools = {}
        for tool in ("size", "readelf", "objdump", "nm"):
            tools[tool] = subprocess.check_output([compiler, "-print-prog-name=" + tool], text=True).strip()
        sizes = self.run(name + "-sections", [tools["size"], "-A", binary])
        sections = {section: int(size) for section, size in
                    re.findall(r"^(\.\S+)\s+(\d+)\s+\d+", sizes, re.M)}
        self.run(name + "-elf", [tools["readelf"], "-W", "-h", "-l", "-A", binary])
        self.run(name + "-assembly", [tools["objdump"], "-d", binary])
        self.run(name + "-symbols", [tools["nm"], "-S", "--size-sort", binary])
        abi = self.run(name + "-abi", [tools["objdump"], "-s", "-j", ".fixture_abi", binary])
        return {"elf_bytes": binary.stat().st_size, "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                "sections": sections, "stack_usage": collect_stack_usage(build), "abi_section_dump": abi}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="fetch missing pinned source checkouts")
    parser.add_argument("--vita", action="store_true", help="cross-compile and package both diagnostic VPKs")
    parser.add_argument("--vitasdk", type=Path, default=Path(os.environ.get("VITASDK", "~/vitasdk")).expanduser())
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"), help="native HOST GCC; never the Vita compiler")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--out", type=Path, default=ROOT / "build-vita-ww-01")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.out = build_directory(args.out)
    args.ninja = shutil.which(args.ninja)
    if args.ninja is None:
        parser.error("Ninja not found")
    campaign = Campaign(args.out)
    lock = json.loads((ROOT / "config/dependencies.lock.json").read_text())
    dependencies = {entry["id"]: entry for entry in lock["dependencies"]}
    profile = (ROOT / "scripts/builder/profiles/bluewake.sh").read_text()
    sources = {}
    for name, sha in PINS.items():
        if dependencies[name]["sha"] != sha or sha not in profile:
            raise ValueError(f"product pin changed: {name}; this bounded campaign needs review")
        path = ROOT / "ref/vita-ww-01" / ("RecompCore" if name == "recompcore" else "DolRecomp")
        if not path.exists():
            if not args.fetch:
                parser.error(f"missing {path}; rerun with --fetch")
            path.mkdir(parents=True)
            campaign.run(name + "-init", ["git", "-C", path, "init"])
            campaign.run(name + "-fetch", ["git", "-C", path, "fetch", "--depth", "1",
                                           dependencies[name]["url"], sha])
            campaign.run(name + "-checkout", ["git", "-C", path, "checkout", "--detach", sha])
        verify_source(path, sha)
        sources[name] = path
    campaign.run("host-compiler", [args.cc, "--version"])
    campaign.run("fixture-python-tests", [sys.executable, "-m", "unittest", "discover",
                                          "-s", ROOT / "vita/feasibility/armv7", "-p", "test_*.py"],
                 env=dict(os.environ, CC=args.cc, VITASDK=str(args.vitasdk)))
    generator = campaign.configure("host-generator", ROOT / "vita/feasibility/armv7/generator", args,
                                   [f"-DCMAKE_C_COMPILER={args.cc}", f"-DDOLRECOMP_DIR={sources['dolrecomp']}"])
    campaign.compile("host-generator", generator, args, ("test_codegen_emit", "ww_stack_emit"))
    fixture = args.out / "fixture"
    fixture.mkdir(exist_ok=True)
    corpus = fixture / "corpus.c"
    campaign.run("emit-corpus", [generator / "dolrecomp/test_codegen_emit", corpus])
    extra = fixture / "stack.c"
    extra.write_text(campaign.run("emit-stack", [generator / "ww_stack_emit"]))
    prepare(ROOT, sources["dolrecomp"], corpus, extra, fixture)
    runtime = campaign.configure("host-runtime", sources["recompcore"] / "GXRuntime", args,
                                 [f"-DCMAKE_C_COMPILER={args.cc}", "-DGXRUNTIME_ENABLE_AURORA=OFF",
                                  "-DGXRUNTIME_ENABLE_AURORA_RECOMP=OFF", "-DBUILD_TESTING=ON"])
    campaign.compile("host-runtime", runtime, args)
    ctest = str(Path(shutil.which(args.cmake) or args.cmake).with_name("ctest"))
    campaign.run("host-runtime-tests", [ctest, "--test-dir", runtime, "--output-on-failure"])
    evidence = {"pins": PINS, "bluewake_sha": campaign.run("bluewake-sha", ["git", "rev-parse", "HEAD"]).strip()}
    for platform in (["host", "vita"] if args.vita else ["host"]):
        compiler = args.cc
        options = [f"-DCMAKE_C_COMPILER={args.cc}"]
        if platform == "vita":
            os.environ["VITASDK"] = str(args.vitasdk.resolve())
            compiler = str(args.vitasdk / "bin/arm-vita-eabi-gcc")
            campaign.run("vita-compiler", [compiler, "--version"])
            options = [f"-DCMAKE_TOOLCHAIN_FILE={args.vitasdk}/share/vita.toolchain.cmake"]
        for mode in ("corpus", "contract"):
            name = platform + "-" + mode
            build = campaign.configure(name, ROOT / "vita/feasibility/armv7", args,
                                       [*options, f"-DRECOMPCORE_DIR={sources['recompcore']}",
                                        f"-DFIXTURE_DIR={fixture}", f"-DWW_MODE={mode}"])
            campaign.compile(name, build, args)
            if platform == "host":
                campaign.run(name + "-tests", [ctest, "--test-dir", build, "--output-on-failure"])
                campaign.run(name + "-run", [build / "ww_fixture"])
            evidence[name] = campaign.evidence(name, build, compiler)
    for name, path in sources.items():
        verify_source(path, PINS[name])
    (args.out / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Evidence: {args.out / 'evidence.json'}; hardware execution and overall game budget remain PENDING")


if __name__ == "__main__":
    main()
