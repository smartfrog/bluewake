#!/usr/bin/env python3
"""Stage the protected memory install/guest pair. Execution needs coordinator review."""

from fp_launch import main


if __name__ == "__main__":
    raise SystemExit(main(memory=True))
