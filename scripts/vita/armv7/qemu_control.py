#!/usr/bin/env python3
"""Local QEMU TCG discriminator for the independent ARMv7 scalar-VFP controls.

Builds the unchanged fixture sources (`arm_vfp.c`, `arm_control.c`) for bare-metal
ARMv7 hard-float, runs the 32 independent control cases on an independent reference
implementation (QEMU TCG, vexpress-a9 / cortex-a9) and compares the observed bit
patterns with the repository oracle table (`arm_vectors.py`). The 71 PPC runtime
cases are not integrated here: see the ticket document. Never launches Vita3K and
never uses the network.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from arm_vectors import PROVENANCE, cases, write_header

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "vita/feasibility/armv7"

MASK = 0x01C00000
MACHINE = "vexpress-a9"
CPU = "cortex-a9"
RAM = "128M"
TEXT_BASE = "0x60000000"
METHOD = "scalar-vfp-v1"
SKIP_RECORD = "RUNTIME skipped reason=not-integrated cases=71"
RUNTIME_SKIP_REASON = ("the 71 runtime cases drive the pinned PowerPC core, generated DolRecomp "
                       "corpus and composite dispatch, not a self-contained ARMv7 VFP sequence")
REQUIRED_INSTRUCTIONS = {
    "arm_vfp_read": ("vmrs",),
    "arm_vfp_write": ("vmsr",),
    "arm_vfp_add": ("vadd.f64",),
    "arm_vfp_mul": ("vmul.f64",),
    "arm_vfp_convert": ("vcvt.f32.f64", "vcvt.f64.f32"),
}

IDENTITY = re.compile(r"QEMU-CONTROL source=([0-9a-f]{40}) machine=(\S+) cpu=(\S+) method=scalar-vfp-v1")
ENVIRONMENT = re.compile(r"ENV available=([01]) initial=([0-9a-f]{8})")
CONTROL = re.compile(r"CONTROL (\S+) op=(add|mul|convert) rn=([0-3]) fz=([01]) a=([0-9a-f]{16}) "
                     r"b=([0-9a-f]{16}) expected=([0-9a-f]{16}) requested=([0-9a-f]{8}) "
                     r"readback=([0-9a-f]{8}) after=([0-9a-f]{8}) actual=([0-9a-f]{16}) "
                     r"restored=([0-9a-f]{8}) (PASS|FAIL)")
END = re.compile(r"END controls=(PASS|FAIL) control_cases=(\d+) control_failures=(\d+) restore=(PASS|FAIL)")
INSTRUCTION = re.compile(r"\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)")


def parse_report(text):
    if not text.endswith("\n"):
        raise ValueError("truncated QEMU report: missing trailing newline")
    lines = text.splitlines()
    if len(lines) != 36:
        raise ValueError(f"unexpected QEMU report line count {len(lines)}, expected 36")
    identity = IDENTITY.fullmatch(lines[0])
    environment = ENVIRONMENT.fullmatch(lines[1])
    if identity is None:
        raise ValueError("QEMU report identity/method does not match")
    if environment is None:
        raise ValueError("malformed QEMU ENV record")
    if environment.group(1) != "1":
        raise ValueError("QEMU run has no scalar VFP (ENV available=0)")
    controls = []
    for line in lines[2:34]:
        match = CONTROL.fullmatch(line)
        if match is None:
            raise ValueError("malformed QEMU CONTROL record")
        controls.append(dict(zip(("id", "op", "rn", "fz", "a", "b", "expected", "requested",
                                  "readback", "after", "actual", "restored", "verdict"),
                                 match.groups(), strict=True),
                             rn=int(match.group(3)), fz=int(match.group(4))))
    if lines[34] != SKIP_RECORD:
        raise ValueError("missing or unexpected QEMU runtime skip record")
    end = END.fullmatch(lines[35])
    if end is None:
        raise ValueError("malformed QEMU END record")
    return {"identity": {"source": identity.group(1), "machine": identity.group(2),
                         "cpu": identity.group(3)},
            "initial": int(environment.group(2), 16),
            "controls": controls,
            "end": {"controls": end.group(1), "control_cases": int(end.group(2)),
                    "control_failures": int(end.group(3)), "restore": end.group(4)}}


def compare_controls(parsed, oracle):
    if len(parsed["controls"]) != len(oracle):
        raise ValueError("control count differs from the independent ARM oracle")
    initial = parsed["initial"]
    results = []
    for observed, case in zip(parsed["controls"], oracle, strict=True):
        if (observed["id"], observed["op"], observed["rn"], observed["fz"], observed["a"],
                observed["b"], observed["expected"]) != (case["id"], case["op"], case["rn"],
                                                         case["fz"], case["a"], case["b"],
                                                         case["expected"]):
            raise ValueError(f"control {observed['id']} differs from the independent ARM oracle")
        requested = int(observed["requested"], 16)
        desired = (initial & ~MASK) | ((0, 3, 1, 2)[case["rn"]] << 22) | (case["fz"] << 24)
        if requested != desired:
            raise ValueError(f"control {case['id']} requested FPSCR does not preserve initial non-mode bits")
        readback = int(observed["readback"], 16)
        after = int(observed["after"], 16)
        restored = int(observed["restored"], 16)
        actual = observed["actual"]
        failure = None
        if readback != requested:
            failure = f"readback={readback:08x} requested={requested:08x}"
        elif (after & MASK) != (requested & MASK):
            failure = f"after={after:08x} requested={requested:08x}"
        elif actual != case["expected"]:
            failure = f"actual={actual} expected={case['expected']}"
        elif restored != initial:
            failure = f"restored={restored:08x} initial={initial:08x}"
        verdict = "FAIL" if failure else "PASS"
        if observed["verdict"] != verdict:
            raise ValueError(f"dishonest QEMU control verdict for {case['id']}")
        results.append(dict(id=case["id"], expected=case["expected"], actual=actual,
                            requested=requested, readback=readback, after=after,
                            restored=restored, verdict=verdict, failure=failure))
    failures = sum(result["verdict"] == "FAIL" for result in results)
    restore_ok = all(result["restored"] == initial for result in results)
    end = parsed["end"]
    if (end["controls"] != ("FAIL" if failures else "PASS") or
            end["control_cases"] != len(oracle) or end["control_failures"] != failures or
            end["restore"] != ("PASS" if restore_ok else "FAIL")):
        raise ValueError("inconsistent terminal QEMU verdict")
    return results


def format_results(results):
    lines = [f"{'case':<28} {'expected':<16} {'observed':<16} verdict"]
    for result in results:
        lines.append(f"{result['id']:<28} {result['expected']:<16} {result['actual']:<16} {result['verdict']}")
    failures = [result for result in results if result["verdict"] == "FAIL"]
    if failures:
        lines.append("")
        lines.append("failures:")
        for result in failures:
            lines.append(f"{result['id']} expected={result['expected']} observed={result['actual']} "
                         f"{result['failure']}")
    lines.append("")
    lines.append(f"control_cases={len(results)} control_failures={len(failures)} "
                 f"controls={'FAIL' if failures else 'PASS'}")
    return "\n".join(lines) + "\n"


def instruction_proof(objdump_text):
    functions = {}
    current = None
    for line in objdump_text.splitlines():
        header = re.fullmatch(r"[0-9a-f]{8} <(\S+)>:", line.strip())
        if header:
            current = header.group(1)
            functions.setdefault(current, [])
            continue
        if current is None:
            continue
        instruction = INSTRUCTION.match(line)
        if instruction:
            functions[current].append(instruction.group(1))
    missing = []
    for function, required in REQUIRED_INSTRUCTIONS.items():
        observed = functions.get(function, [])
        for mnemonic in required:
            if mnemonic not in observed:
                missing.append((function, mnemonic))
    return {"functions": functions, "missing": missing}


def run_checked(command, log, **options):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, **options)
    Path(log).write_text("$ " + " ".join(str(argument) for argument in command) + "\n" + result.stdout)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); see {log}")
    return result.stdout


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_identity():
    source = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True))
    return source, dirty


def build(build_dir, cc):
    compile_flags = ["-std=c11", "-O2", "-ffp-contract=off", "-fno-fast-math", "-ffreestanding",
                     "-mcpu=cortex-a9", "-mfpu=neon", "-mfloat-abi=hard", "-DNDEBUG", "-D__vita__=1",
                     "-Wall", "-Wextra", "-Werror", f"-I{build_dir}"]
    objects = []
    for name in ("qemu_startup.S", "qemu_control.c", "arm_control.c", "arm_vfp.c"):
        object_path = build_dir / (Path(name).stem + ".o")
        run_checked([cc, *compile_flags, "-c", str(FIXTURE / name), "-o", str(object_path)],
                    build_dir / f"compile-{Path(name).stem}.log")
        objects.append(str(object_path))
    link_command = [cc, "-nostdlib", "-nostartfiles", "-static", f"-Wl,-Ttext={TEXT_BASE}",
                    "-e", "_start", *objects, "-o", str(build_dir / "qemu_control.elf")]
    run_checked(link_command, build_dir / "link.log")
    return compile_flags, link_command


def evidence_manifest(build_dir, names):
    lines = [f"{sha256(build_dir / name)}  {name}\n" for name in sorted(names)]
    (build_dir / "manifest.sha256").write_text("".join(lines))
    return {name: sha256(build_dir / name) for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build-ww11")
    parser.add_argument("--cc", default=os.environ.get("ARM_CC", "arm-none-eabi-gcc"))
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--readelf", default="arm-none-eabi-readelf")
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 60:
        parser.error("--timeout must be within (0, 60] seconds")
    build_dir = args.out.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    source, dirty = source_identity()
    write_header(build_dir / "arm_vectors.h")
    (build_dir / "qemu_identity.h").write_text(f'#define QEMU_SOURCE "{source}"\n'
                                               f'#define QEMU_MACHINE "{MACHINE}"\n'
                                               f'#define QEMU_CPU "{CPU}"\n')
    compile_flags, link_command = build(build_dir, args.cc)
    cc_version = subprocess.check_output([args.cc, "--version"], text=True).splitlines()[0]
    qemu_version = subprocess.check_output([args.qemu, "--version"], text=True).splitlines()[0]
    objdump_text = run_checked([args.objdump, "-d", str(build_dir / "qemu_control.elf")],
                               build_dir / "objdump.log")
    (build_dir / "objdump.txt").write_text(objdump_text)
    run_checked([args.readelf, "-A", str(build_dir / "qemu_control.elf")], build_dir / "attributes.log")
    proof = instruction_proof(objdump_text)
    if proof["missing"]:
        raise RuntimeError(f"built ELF does not contain the required scalar VFP instructions: {proof['missing']}")
    qemu_command = [args.qemu, "-M", MACHINE, "-cpu", CPU, "-m", RAM, "-display", "none",
                    "-serial", "none", "-monitor", "none", "-semihosting-config",
                    "enable=on,target=native", "-kernel", str(build_dir / "qemu_control.elf")]
    try:
        run = subprocess.run(qemu_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired as expired:
        (build_dir / "qemu-stdout.txt").write_text(expired.stdout or "")
        (build_dir / "qemu-stderr.txt").write_text(expired.stderr or "")
        raise RuntimeError(f"QEMU run exceeded {args.timeout} s")
    (build_dir / "qemu-stdout.txt").write_text(run.stdout)
    (build_dir / "qemu-stderr.txt").write_text(run.stderr)
    if run.returncode:
        raise RuntimeError(f"QEMU exited with status {run.returncode}; see qemu-stderr.txt")
    candidates = [stream for stream in (run.stdout, run.stderr) if stream.startswith("QEMU-CONTROL ")]
    if len(candidates) != 1:
        raise RuntimeError("QEMU did not emit exactly one QEMU-CONTROL report on stdout/stderr")
    parsed = parse_report(candidates[0])
    if parsed["identity"]["source"] != source:
        raise RuntimeError("QEMU report source identity differs from the worktree HEAD")
    results = compare_controls(parsed, cases())
    report = format_results(results)
    (build_dir / "qemu-control-report.txt").write_text(report)
    failures = [result for result in results if result["verdict"] == "FAIL"]
    artifacts = ["arm_vectors.h", "qemu_identity.h", "qemu_control.elf", "objdump.txt",
                 "objdump.log", "attributes.log", "qemu-stdout.txt", "qemu-stderr.txt",
                 "qemu-control-report.txt", "compile-qemu_startup.log", "compile-qemu_control.log",
                 "compile-arm_control.log", "compile-arm_vfp.log", "link.log"]
    evidence = {"ticket": "VITA-WW-11", "source": source, "source_dirty": dirty,
                "method": METHOD, "control_oracle": PROVENANCE,
                "machine": MACHINE, "cpu": CPU, "ram": RAM, "text_base": TEXT_BASE,
                "qemu_version": qemu_version, "cc_version": cc_version,
                "compile_flags": compile_flags, "link_command": link_command,
                "qemu_command": qemu_command,
                "qemu_exit": run.returncode, "initial": parsed["initial"],
                "controls": results, "control_failures": len(failures),
                "runtime": {"status": "SKIP", "cases": 71, "reason": RUNTIME_SKIP_REASON},
                "instruction_proof": proof["missing"],
                "artifacts": evidence_manifest(build_dir, artifacts)}
    (build_dir / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(report, end="")
    print(f"source={source}{' DIRTY' if dirty else ''} machine={MACHINE} cpu={CPU}")
    print(f"evidence: {build_dir / 'evidence.json'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
