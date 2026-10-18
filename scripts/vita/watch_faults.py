"""Fault-line recognition shared by the Vita run watchers.

A watcher stops the guest at the first line that says the guest has already
failed, instead of waiting for the run ceiling while the guest floods the log:

    SIGSEGV                       any segmentation fault report, including
                                  "Unhandled SIGSEGV"
    PC is 0xNN                    a stopped program counter, any value (0x0, 0x8)
    [MemoryRead]: Invalid read    a guest read outside mapped memory
    [MemoryWrite]: Invalid write  a guest write outside mapped memory
    assert: / assertion /         an assertion failure ("assertion failed",
      Assertion failed            "Assertion `frame' failed.")
    panic                         a panic
    std::terminate                an uncaught C++ exception
    <same line> x FAULT_REPEATS   a flood of one fault line, even when that line
                                  carries none of the signatures above

A healthy guest logs `PC: 0x80000000` (a colon, a running program counter),
`[frame-present]`, the boot audio event `event=assert-to-no-op` and the counter
`exception=00000000`; none of those matches.
"""

import re

FAULT_REPEATS = 20

_SIGNATURE = re.compile('|'.join((
    r'sigsegv',
    r'pc is 0x[0-9a-f]+',
    r'\[memoryread\]: invalid read',
    r'\[memorywrite\]: invalid write',
    r'assert:',
    r'assertion',
    r'\bpanic',
    r'std::terminate',
)), re.IGNORECASE)

_FLOOD = re.compile('|'.join((
    r'invalid read',
    r'invalid write',
    r'sigsegv',
    r'segfault',
    r'\bfault',
    r'assertion',
    r'\bpanic',
    r'terminate',
    r'abort',
    r'crash',
)), re.IGNORECASE)


def fault_line(line):
    """True when one log line already reports a guest failure."""
    return _SIGNATURE.search(line) is not None


def fault_match(lines, repeats=FAULT_REPEATS):
    """Return the first fault line, or a fault line repeated `repeats` times.

    `lines` is an iterable of log lines or one log chunk.
    """
    if isinstance(lines, str):
        # One regex pass over the chunk before splitting it: a chunk carrying no
        # fault word at all cannot hold a fault or a flood, and logs reach 545 MB.
        if _SIGNATURE.search(lines) is None and _FLOOD.search(lines) is None:
            return None
        lines = lines.splitlines()
    counts = {}
    for line in lines:
        if fault_line(line):
            return line
        text = line.strip()
        if not text or not _FLOOD.search(text):
            continue
        counts[text] = counts.get(text, 0) + 1
        if counts[text] >= repeats:
            return line
    return None
