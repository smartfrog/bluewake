#!/usr/bin/env python3
"""Stage the protected VITA-WW-09 cpu-opt-off comparison; execution is a separate exact-plan gate."""

from fp_launch import main as launch_main


def main():
    return launch_main(control=True, cpu_opt_off=True)


if __name__ == "__main__":
    raise SystemExit(main())
