"""First-fault fail-closed watch contract and bounded evidence sampling.

The behaviour is specified by test_watch_first_fault.py. `decide` and
`bounded_sample` are pure; `FirstFaultWatch` adds incremental file observation
without launching an emulator, VPK or run.
"""

from collections import deque
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import stat
from typing import Iterable, Mapping, Optional

GUEST_EXCEPTION_MARKER = "[exception]"
HOST_FAULT_MARKERS = ("|E| [MemoryWrite]: Invalid write",
                      "|E| [MemoryRead]: Invalid read")
DEFAULT_CEILING_BYTES = 64 * 1024 * 1024
HEAD_LINES = 5000
WINDOW_LINES = 300
TAIL_LINES = 300
TAIL_ANCHOR_BYTES = 4096
READ_CHUNK_BYTES = 1 << 20
MAX_PENDING_BYTES = 4 << 20


@dataclass(frozen=True)
class Observation:
    guest_exception_line: Optional[str] = None
    host_fault_line: Optional[str] = None
    log_sizes: Mapping[str, int] = field(default_factory=dict)
    ceiling_bytes: int = DEFAULT_CEILING_BYTES


@dataclass(frozen=True)
class Stop:
    reason: str
    detail: str


def decide(observation: Observation) -> Optional[Stop]:
    """Return the first stop, or None to continue. Precedence: guest [exception],
    then host Invalid write|Invalid read, then the log-size ceiling."""
    if observation.guest_exception_line is not None:
        return Stop("guest-exception", observation.guest_exception_line)
    if observation.host_fault_line is not None:
        return Stop("host-invalid-access", observation.host_fault_line)
    if not isinstance(observation.ceiling_bytes, int) or observation.ceiling_bytes <= 0:
        raise ValueError("ceiling_bytes must be a positive integer")
    for path in sorted(observation.log_sizes, key=str):
        size = observation.log_sizes[path]
        if not isinstance(size, int) or size < 0:
            raise ValueError(f"log size must be a nonnegative integer byte count: {path}")
        if size >= observation.ceiling_bytes:
            return Stop("log-size-ceiling", str(path))
    return None


def _fault_bounds(total: int, fault_index: int, window: int) -> tuple[int, int]:
    if not isinstance(fault_index, int) or not 0 <= fault_index < total:
        raise ValueError("fault_index must name an existing line")
    if window < 0:
        raise ValueError("window must be nonnegative")
    if window == 0:
        return fault_index, fault_index
    length = min(window, total)
    start = max(0, min(fault_index - window // 2, total - length))
    return start, start + length


def bounded_sample(lines: Iterable[str], fault_index: int,
                   head: int = HEAD_LINES, window: int = WINDOW_LINES,
                   tail: int = TAIL_LINES) -> dict:
    """Return {'head': [...], 'fault': [...], 'tail': [...]} with exactly the
    requested bounds (fault window centred on fault_index, clamped at the file
    edges). Never returns the whole flood."""
    if head < 0 or window < 0 or tail < 0:
        raise ValueError("sample bounds must be nonnegative")
    rows = list(lines)
    if not rows:
        return {"head": [], "fault": [], "tail": []}
    start, end = _fault_bounds(len(rows), fault_index, window)
    return {"head": rows[:head], "fault": rows[start:end],
            "tail": rows[-tail:] if tail else []}


class LogTail:
    """Incremental whole-line reader; restarts when a log is replaced or rewritten."""

    def __init__(self, path):
        self.path = Path(path)
        self.offset = 0
        self.pending = b""
        self.identity = None
        self.lines = 0
        self.generation = 0
        self.tail_bytes = b""

    def _reset(self):
        self.offset = 0
        self.pending = b""
        self.lines = 0
        self.generation += 1
        self.tail_bytes = b""

    def read_new_lines(self, ceiling_bytes=None):
        try:
            stream = self.path.open("rb")
        except FileNotFoundError:
            if self.identity is not None:
                raise ValueError("watched log vanished: " + str(self.path))
            return
        with stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise ValueError("watched log must be a regular file: " + str(self.path))
            identity = (info.st_dev, info.st_ino)
            if self.identity is not None:
                if identity != self.identity or info.st_size < self.offset:
                    self._reset()
                elif self.tail_bytes:
                    stream.seek(self.offset - len(self.tail_bytes))
                    if stream.read(len(self.tail_bytes)) != self.tail_bytes:
                        self._reset()
            self.identity = identity
            stream.seek(self.offset)
            while True:
                chunk = stream.read(READ_CHUNK_BYTES)
                if not chunk:
                    break
                self.offset += len(chunk)
                self.tail_bytes = (self.tail_bytes + chunk)[-TAIL_ANCHOR_BYTES:]
                parts = (self.pending + chunk).split(b"\n")
                self.pending = parts.pop()
                for part in parts:
                    index = self.lines
                    self.lines += 1
                    yield index, part.decode("utf-8", errors="replace")
                if len(self.pending) > MAX_PENDING_BYTES:
                    raise ValueError("watched log line exceeds its bound: " + str(self.path))
                if ceiling_bytes is not None and os.fstat(stream.fileno()).st_size >= ceiling_bytes:
                    return

    def size(self):
        try:
            info = self.path.stat(follow_symlinks=False)
        except FileNotFoundError:
            return None
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
            raise ValueError("watched log must be an owned regular file: " + str(self.path))
        return info.st_size

    def anchor(self):
        return {"identity": self.identity, "generation": self.generation,
                "offset": self.offset, "tail_bytes": self.tail_bytes}

    def anchor_verified(self, anchor):
        if anchor is None or anchor.get("generation") != self.generation:
            return False
        try:
            info = self.path.stat(follow_symlinks=False)
        except FileNotFoundError:
            return False
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
            return False
        if anchor.get("identity") is not None and (info.st_dev, info.st_ino) != anchor["identity"]:
            return False
        if info.st_size < anchor.get("offset", 0):
            return False
        tail = anchor.get("tail_bytes", b"")
        if tail:
            with self.path.open("rb") as stream:
                stream.seek(anchor["offset"] - len(tail))
                if stream.read(len(tail)) != tail:
                    return False
        return True


class FirstFaultWatch:
    """Always-on first-fault gate over one guest log and the host logs."""

    def __init__(self, guest_log, host_logs, sample_directory,
                 ceiling_bytes=DEFAULT_CEILING_BYTES):
        self.guest = LogTail(guest_log)
        self.hosts = tuple(LogTail(path) for path in host_logs)
        self.logs = (self.guest, *self.hosts)
        self.sample_directory = Path(sample_directory)
        self.ceiling_bytes = ceiling_bytes
        self.stop = None
        self.fault_index = {}
        self.stop_sizes = {}
        self.stop_anchors = {}
        self._names = {self.guest.path: "guest-" + self.guest.path.name}
        for position, log in enumerate(self.hosts):
            self._names[log.path] = f"host{position}-{log.path.name}"

    def poll(self):
        if self.stop is not None:
            return self.stop
        guest_line = self._first_match(self.guest, (GUEST_EXCEPTION_MARKER,))
        host_line = None
        if guest_line is None:
            for log in self.hosts:
                host_line = self._first_match(log, HOST_FAULT_MARKERS)
                if host_line is not None:
                    break
        sizes = self._sizes()
        stop = decide(Observation(guest_exception_line=guest_line,
                                  host_fault_line=host_line,
                                  log_sizes=sizes,
                                  ceiling_bytes=self.ceiling_bytes))
        if stop is not None:
            self.stop = stop
            self.stop_sizes = sizes
            self.stop_anchors = {str(log.path): log.anchor() for log in self.logs}
        return stop

    def write_samples(self):
        if self.stop is None:
            raise ValueError("bounded samples require a recorded stop")
        self.sample_directory.mkdir(mode=0o700, parents=True, exist_ok=False)
        entries = []
        sources = [("guest", self.guest)]
        sources.extend(("host", log) for log in self.hosts)
        for role, log in sources:
            stop_bytes = self.stop_sizes.get(str(log.path))
            if stop_bytes is None:
                entries.append({"role": role, "path": str(log.path), "present": False,
                                "stop_bytes": 0})
                continue
            fault_index = self.fault_index.get(str(log.path))
            if self.stop.reason == "log-size-ceiling" and str(log.path) == self.stop.detail:
                fault_index = None
            entries.append(self._write_log_sample(
                role, log, fault_index, stop_bytes, self.stop_anchors.get(str(log.path))))
        stats = {"stop": {"reason": self.stop.reason, "detail": self.stop.detail},
                 "deletion": "none",
                 "bounds": {"head": HEAD_LINES, "window": WINDOW_LINES, "tail": TAIL_LINES},
                 "logs": entries}
        stats_path = self.sample_directory / "flood-stats.json"
        _write_text(stats_path, json.dumps(stats, indent=2) + "\n")
        sample_files = sorted(self.sample_directory.glob("*/*.txt")) + [stats_path]
        sums = "".join(f"{_sha256(path)}  {path.relative_to(self.sample_directory)}\n"
                       for path in sample_files)
        _write_text(self.sample_directory / "SHA256SUMS", sums)
        self._verify_sample_files(entries, stats_path)
        return stats

    def _verify_sample_files(self, entries, stats_path):
        sums = {}
        for row in (self.sample_directory / "SHA256SUMS").read_text().splitlines():
            digest, name = row.split("  ", 1)
            sums[name] = digest
        files = []
        for entry in entries:
            if not entry.get("present") or not entry.get("sample"):
                continue
            for kind in ("head", "fault", "tail"):
                relative = f"{entry['sample']}/{kind}.txt"
                path = self.sample_directory / relative
                content = path.read_bytes() if path.is_file() else None
                files.append({
                    "name": relative,
                    "lines": content.count(b"\n") if content is not None else None,
                    "sha256": hashlib.sha256(content).hexdigest() if content is not None else None,
                    "expected_lines": entry[f"{kind}_lines"],
                    "expected_sha256": sums.get(relative),
                })
        stats_relative = "flood-stats.json"
        stats_content = stats_path.read_bytes()
        files.append({
            "name": stats_relative,
            "lines": None,
            "sha256": hashlib.sha256(stats_content).hexdigest(),
            "expected_lines": None,
            "expected_sha256": sums.get(stats_relative),
        })
        checks = {
            "files_present": all(entry["sha256"] is not None for entry in files),
            "line_counts_match": all(entry["expected_lines"] is None or
                                     entry["lines"] == entry["expected_lines"] for entry in files),
            "hashes_match": all(entry["sha256"] == entry["expected_sha256"] for entry in files),
            "bounds": all(entry.get("head_lines", 0) <= HEAD_LINES and
                          entry.get("fault_lines", 0) <= WINDOW_LINES and
                          entry.get("tail_lines", 0) <= TAIL_LINES for entry in entries),
        }
        verification = {"stop": {"reason": self.stop.reason, "detail": self.stop.detail},
                        "files": files, "checks": checks, "deletion": "none",
                        "scope": "sample artifacts and flood-stats.json; this receipt is not self-hashed"}
        _write_text(self.sample_directory / "verification.json",
                    json.dumps(verification, indent=2) + "\n")
        if not all(checks.values()):
            failed = [name for name, value in checks.items() if not value]
            raise ValueError("sample verification failed: " + ", ".join(failed))

    def _first_match(self, log, markers):
        for index, line in log.read_new_lines(self.ceiling_bytes):
            if any(marker in line for marker in markers):
                self.fault_index[str(log.path)] = index
                return line
        return None

    def _sizes(self):
        sizes = {}
        for log in self.logs:
            size = log.size()
            if size is not None:
                sizes[str(log.path)] = size
        return sizes

    def _write_log_sample(self, role, log, fault_index, stop_bytes, anchor):
        name = self._names[log.path]
        directory = self.sample_directory / name
        directory.mkdir(mode=0o700)
        anchor_ok = log.anchor_verified(anchor)
        requested_fault_index = fault_index
        if fault_index is not None and not anchor_ok:
            fault_index = None
        tail_sample = fault_index is None
        head = []
        before = deque(maxlen=WINDOW_LINES)
        after = []
        tail = deque(maxlen=TAIL_LINES)
        digest = hashlib.sha256()
        total = 0
        observed_bytes = 0
        pending = b""
        missing = False
        try:
            stream = log.path.open("rb")
        except FileNotFoundError:
            missing = True
        else:
            with stream:
                remaining = stop_bytes
                while remaining > 0:
                    chunk = stream.read(min(READ_CHUNK_BYTES, remaining))
                    if not chunk:
                        break
                    remaining -= len(chunk)
                    observed_bytes += len(chunk)
                    digest.update(chunk)
                    parts = (pending + chunk).split(b"\n")
                    pending = parts.pop()
                    for part in parts:
                        total = self._collect_sample_line(
                            part, total, fault_index, head, before, after, tail)
                    if len(pending) > MAX_PENDING_BYTES:
                        raise ValueError("watched log line exceeds its bound: " + str(log.path))
                if pending:
                    total = self._collect_sample_line(
                        pending, total, fault_index, head, before, after, tail)
        if total == 0:
            fault_index = 0
        elif tail_sample or fault_index is None or fault_index >= total:
            tail_sample = True
            fault_index = total - 1
        window = []
        if total:
            start, end = _fault_bounds(total, fault_index, WINDOW_LINES)
            before_start = total - len(before) if tail_sample else fault_index - len(before)
            for index in range(start, end):
                if tail_sample or index < fault_index:
                    window.append(before[index - before_start])
                else:
                    window.append(after[index - fault_index])
        _write_lines(directory / "head.txt", head)
        _write_lines(directory / "fault.txt", window)
        _write_lines(directory / "tail.txt", tail)
        return {"role": role, "path": str(log.path), "present": True,
                "sample": name, "stop_bytes": stop_bytes, "bytes": observed_bytes,
                "lines": total, "sha256": digest.hexdigest(), "fault_index": fault_index,
                "fault_anchor": "line" if requested_fault_index is not None and anchor_ok else "tail",
                "anchor_verified": anchor_ok,
                "generation": anchor.get("generation") if anchor else None,
                "missing_at_sample": missing,
                "head_lines": len(head), "fault_lines": len(window),
                "tail_lines": len(tail)}

    @staticmethod
    def _collect_sample_line(part, total, fault_index, head, before, after, tail):
        text = part.rstrip(b"\r").decode("utf-8", errors="replace")
        if total < HEAD_LINES:
            head.append(text)
        if fault_index is None or total < fault_index:
            before.append(text)
        elif len(after) < WINDOW_LINES:
            after.append(text)
        tail.append(text)
        return total + 1


def _write_text(path, text):
    with _private_stream(path) as stream:
        stream.write(text)


def _write_lines(path, lines):
    with _private_stream(path) as stream:
        for line in lines:
            stream.write(line + "\n")


def _private_stream(path):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                         0o600)
    return os.fdopen(descriptor, "w", encoding="utf-8")


def _sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()
