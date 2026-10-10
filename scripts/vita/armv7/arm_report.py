"""Typed VITA-WW-08 report: raw control failure blocks runtime attribution."""

import re

from arm_vectors import METHOD, cases
from fp_vectors import cases as runtime_cases


def fields(line, pattern):
    match = re.fullmatch(pattern, line)
    if match is None:
        raise ValueError("malformed ARM08 record")
    return match.groups()


def analyze(text, token, source, package, expected_platform="VITA"):
    if expected_platform not in ("HOST", "VITA"):
        raise ValueError("unsupported report platform")
    lines = text.splitlines()
    controls, runtime = cases(), runtime_cases()
    if not lines or lines[0] != f"ARM08 {token} {source} {package} method={METHOD} phase=guest platform={expected_platform}":
        raise ValueError("report identity/method/platform does not match")
    if not text.endswith("\n") or len(lines) != len(controls) + len(runtime) + 4:
        raise ValueError("truncated or excess ARM08 records")
    available, initial = fields(lines[1], r"ENV available=([01]) initial=([0-9a-f]{8})")
    if int(available) != int(expected_platform == "VITA"):
        raise ValueError("unavailable target control or fabricated host control")
    initial = int(initial, 16)
    if not int(available) and initial:
        raise ValueError("host FPSCR must be unavailable")
    control_failures = runtime_failures = 0
    restore_ok = True
    for line, case in zip(lines[2:2 + len(controls)], controls, strict=True):
        values = fields(line, r"CONTROL (\S+) op=(add|mul|convert) rn=([0-3]) fz=([01]) "
                        r"a=([0-9a-f]{16}) b=([0-9a-f]{16}) expected=([0-9a-f]{16}) "
                        r"requested=([0-9a-f]{8}) readback=([0-9a-f]{8}) after=([0-9a-f]{8}) "
                        r"actual=([0-9a-f]{16}) restored=([0-9a-f]{8}) (PASS|FAIL|SKIP)")
        name, op, rn, fz, a, b, expected, requested, readback, after, actual, restored, verdict = values
        if (name, op, int(rn), int(fz), a, b, expected) != tuple(case[key] for key in
                ("id", "op", "rn", "fz", "a", "b", "expected")):
            raise ValueError("control differs from independent ARM literal oracle")
        raw = [int(value, 16) for value in (requested, readback, after, restored)]
        if not int(available):
            if raw != [0, 0, 0, 0] or int(actual, 16) or verdict != "SKIP":
                raise ValueError("HOST must not fabricate ARM measurements")
            continue
        desired = (initial & ~0x01c00000) | ((0, 3, 1, 2)[int(rn)] << 22) | (int(fz) << 24)
        if raw[0] != desired:
            raise ValueError("requested FPSCR does not preserve initial non-mode bits")
        restored_ok = raw[3] == initial
        restore_ok &= restored_ok
        failure = raw[1] != desired or (raw[2] & 0x01c00000) != (desired & 0x01c00000) or actual != expected or not restored_ok
        if verdict != ("FAIL" if failure else "PASS"):
            raise ValueError("dishonest independent control verdict")
        control_failures += failure
    observations = []
    for line, case in zip(lines[2 + len(controls):-2], runtime, strict=True):
        values = fields(line, r"RUNTIME (\S+) mode=([0-7]) expected=([0-9a-f]{16}) actual=([0-9a-f]{16}) "
                        r"path=([01]) before=([0-9a-f]{8}) updated=([0-9a-f]{8}) operated=([0-9a-f]{8}) "
                        r"restored=([0-9a-f]{8}) restore=([01]) (PASS|FAIL)")
        name, mode, expected, actual, path, before, updated, operated, restored, restoration, verdict = values
        if (name, int(mode), expected) != (case["id"], case["mode"], case["expected"]):
            raise ValueError("runtime differs from original Gekko oracle/order")
        if not int(available) and any(int(value, 16) for value in (before, updated, operated, restored)):
            raise ValueError("HOST runtime FPSCR unavailable")
        restored_ok = int(before, 16) == initial and int(restored, 16) == initial
        if restoration != str(int(restored_ok)):
            raise ValueError("dishonest runtime restoration")
        restore_ok &= restored_ok
        failure = actual != expected or path != "1" or not restored_ok
        if verdict != ("FAIL" if failure else "PASS"):
            raise ValueError("dishonest runtime verdict")
        runtime_failures += failure
        observations.append(dict(id=name, mode=int(mode), before=before, updated=updated, operated=operated,
                                 restored=restored, path=int(path), actual=actual, expected=expected))
    final, verdict = fields(lines[-2], r"RESTORE final=([0-9a-f]{8}) (PASS|FAIL)")
    final_ok = int(final, 16) == initial
    if verdict != ("PASS" if final_ok else "FAIL"):
        raise ValueError("dishonest final restoration")
    restore_ok &= final_ok
    control_status = "SKIP" if not int(available) else "FAIL" if control_failures else "PASS"
    runtime_status = "FAIL" if runtime_failures else "PASS"
    restore_status = "PASS" if restore_ok else "FAIL"
    if lines[-1] != (f"END controls={control_status} runtime={runtime_status} restore={restore_status} "
                     f"control_cases={len(controls)} runtime_cases={len(runtime)} "
                     f"control_failures={control_failures} runtime_failures={runtime_failures}"):
        raise ValueError("inconsistent terminal ARM08 verdict")
    return dict(controls=control_status, runtime=runtime_status, restore=restore_status,
                status=int(bool(control_failures or runtime_failures or not restore_ok)),
                attribution="STOP_CONTROL" if control_status == "FAIL" or not restore_ok else
                            "UNAVAILABLE_HOST" if control_status == "SKIP" else
                            "REVIEW_RUNTIME_OBSERVATIONS" if runtime_failures else "NO_REPRODUCTION",
                observations=observations)


def validate(text, token, source, package, expected_platform="VITA"):
    return analyze(text, token, source, package, expected_platform)["status"]
