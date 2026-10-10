#!/usr/bin/env python3
"""Build VITA-WW-08 stage 1 against pristine pins; does not execute Vita3K."""

from fp_build import main

if __name__ == "__main__":
    raise SystemExit(main(control=True))
