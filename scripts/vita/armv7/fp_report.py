"""Strict VITA-WW-03 report validation; semantic failure remains exit status 1."""

import re


def validate(text, token, source, package, cases):
    lines = text.splitlines()
    if not lines or lines[0] != f"RUN {token} {source} {package}":
        raise ValueError("report identity does not match this run/source/package")
    if not text.endswith("\n") or len(lines) != len(cases) + 2:
        raise ValueError("missing, truncated or excess report lines")
    failures = 0
    for line, case in zip(lines[1:-1], cases):
        match = re.fullmatch(r"CASE (\S+) mode=([0-7]) expected=([0-9a-f]{16}) "
                             r"actual=([0-9a-f]{16}) path=([01]) (PASS|FAIL)", line)
        if match is None:
            raise ValueError("malformed case")
        name, mode, expected, actual, path, verdict = match.groups()
        if (name, int(mode), expected) != (case["id"], case["mode"], case["expected"]):
            raise ValueError("case does not match independent golden vector")
        failure = actual != expected or path != "1"
        if verdict != ("FAIL" if failure else "PASS"):
            raise ValueError("dishonest case verdict")
        failures += failure
    result = "FAIL" if failures else "PASS"
    if lines[-1] != f"END {result} cases={len(cases)} failures={failures}":
        raise ValueError("missing or inconsistent terminal verdict")
    return int(failures != 0)
