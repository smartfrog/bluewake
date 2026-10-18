#!/usr/bin/env python3
"""Compatibility entry point for the canonical guest watcher.

Run scripts call `watch.py RUNDIR`; this forwards to `watch_guest.py`, which
scans both the guest log and the emulator console, stops the guest on the
first fault, and writes live-end.txt.
"""

import runpy
import sys
from pathlib import Path

target = Path(__file__).with_name('watch_guest.py')
sys.argv[0] = str(target)
runpy.run_path(str(target), run_name='__main__')
