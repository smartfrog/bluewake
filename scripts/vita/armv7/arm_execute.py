#!/usr/bin/env python3
"""Execute only a coordinator-authorized saved ARM08 baseline or ARM09 cpu-opt-off pair, once, without retry."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

from build import ROOT, checked_sources
from fp_campaign import execute_campaign, verify_campaign_reservation, verify_inputs
from fp_launch import reject_symlinks, sandbox_command
from fp_libraries import directory_fd
from fp_profile import config_identity, read_profile_file


def reserve_pair(path, plan):
    with directory_fd(path.parent) as parent:
        descriptor = os.open(path.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    with os.fdopen(descriptor, "w") as stream:
        json.dump({"authorization": plan["authorization"], "source": plan["source"],
                   "reserved_process_numbers": [1, 2], "retry": "FORBIDDEN",
                   "conditional_pair_3_4": "NOT AUTHORIZED; separate result and material-risk review required"}, stream)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def execute(path, authorization, expected_plan_sha256):
    return execute_pair(path, authorization, expected_plan_sha256, cpu_opt_off=False)


def execute_cpu_opt_off(path, authorization, expected_plan_sha256):
    return execute_pair(path, authorization, expected_plan_sha256, cpu_opt_off=True)


def execute_pair(path, authorization, expected_plan_sha256, cpu_opt_off):
    if not isinstance(expected_plan_sha256, str) or re.fullmatch(r"[0-9a-f]{64}", expected_plan_sha256) is None:
        raise ValueError("expected reviewed plan SHA-256 must be 64 lowercase hexadecimal characters")
    path = reject_symlinks(path)
    content = read_profile_file(path, 1024 * 1024, owned=True)
    if hashlib.sha256(content).hexdigest() != expected_plan_sha256:
        raise ValueError("saved plan bytes differ from coordinator-reviewed plan SHA-256")
    plan = json.loads(content)
    if authorization != plan.get("authorization"):
        raise ValueError("exact coordinator baseline pair authorization does not match saved plan")
    root = path.parent.parent
    if cpu_opt_off:
        expected = f"ARM09:{plan['source']}:{plan['run']}:cpu-opt-off:1,2"
    else:
        expected = f"ARM08:{plan['source']}:{plan['run']}:baseline:1,2"
    if authorization != expected or path != root / "evidence/plan.json" or root.name != "vita3k-run-" + plan["run"]:
        raise ValueError("authorization/path is not the named baseline pair")
    if (plan.get("diagnostic"), plan.get("method"), plan.get("title"), plan.get("comparison"),
            plan.get("process_budget"), plan.get("ticket_process_budget"), plan.get("process_budget_used"),
            plan.get("real_execution")) != ("arm", "scalar-vfp-v1", "BWWA00008",
                                            "cpu-opt-off" if cpu_opt_off else "pristine-baseline",
                                            2, 2 if cpu_opt_off else 4, 0, False):
        raise ValueError("not an unexecuted bounded cpu-opt-off ARM09 plan" if cpu_opt_off else
                         "not an unexecuted bounded pristine ARM08 plan")
    source = subprocess.check_output(["git", "-C", ROOT, "rev-parse", "HEAD"], text=True).strip()
    if source != plan["source"] or subprocess.check_output(["git", "-C", ROOT, "status", "--porcelain"], text=True):
        raise ValueError("saved source is not the current clean checkpoint")
    checked_sources()
    libraries = Path(plan["runtime_libraries"]["root"])
    display = Path(plan["wayland_socket"]) if plan.get("wayland_socket") else None
    for name, arguments in (("install", ["--console", "--backend-renderer", "OpenGL", "arm.vpk"]),
                            ("guest", ["--backend-renderer", "OpenGL", "--installed-path", "BWWA00008"])):
        phase = plan["phases"][name]
        timeout = phase["timeout_seconds"]
        if not math.isfinite(timeout) or not 0 < timeout <= 30 or phase["process_number"] != (1 if name == "install" else 2):
            raise ValueError("unexpected phase deadline/process number")
        if phase["evidence_root"] != str(root / "phases" / name):
            raise ValueError("unexpected phase evidence root")
        saved = json.loads(read_profile_file(root / "phases" / name / "evidence/plan.json", 65536, owned=True))
        if saved != phase or phase["command"] != sandbox_command(root, "Vita3K", arguments, libraries, wayland_socket=display):
            raise ValueError("protected phase plan/isolated command changed")
    verify_inputs(root, plan)
    if config_identity(root, alt=cpu_opt_off) != plan["private_config"]:
        raise ValueError("canonical configuration changed before authorization")
    if cpu_opt_off:
        verify_campaign_reservation(ROOT, plan)
        if any((root / "phases" / name / "evidence/process.json").exists() for name in ("install", "guest")):
            raise ValueError("reserved campaign already started; retry forbidden")
    else:
        reserve_pair(ROOT / "build-ww08-baseline-reservation.json", plan)
    return execute_campaign(root, plan)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--authorize-pair", required=True, help="exact coordinator authorization after material-risk review")
    parser.add_argument("--expected-plan-sha256", required=True,
                        help="root plan byte hash supplied independently by the coordinator, not from the plan itself")
    args = parser.parse_args()
    return execute(args.plan, args.authorize_pair, args.expected_plan_sha256)


if __name__ == "__main__":
    raise SystemExit(main())
