#!/usr/bin/env python3
"""Execute only a coordinator-authorized saved ARM09 cpu-opt-off pair, once, without retry."""

import argparse
from pathlib import Path

from arm_execute import execute_cpu_opt_off


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--authorize-pair", required=True, help="exact coordinator authorization after material-risk review")
    parser.add_argument("--expected-plan-sha256", required=True,
                        help="root plan byte hash supplied independently by the coordinator, not from the plan itself")
    args = parser.parse_args()
    return execute_cpu_opt_off(args.plan, args.authorize_pair, args.expected_plan_sha256)


if __name__ == "__main__":
    raise SystemExit(main())
