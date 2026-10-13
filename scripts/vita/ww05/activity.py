"""CPU or output-growth activity for the owned sandbox and its descendants."""

import math
import os
from pathlib import Path
import stat
import time

MIN_EXTENDED_AVAILABLE_MEMORY_BYTES = 5 * 1024 ** 3 // 2


def available_memory_bytes():
    records = [line.split() for line in Path('/proc/meminfo').read_text().splitlines()]
    available = [fields for fields in records if fields and fields[0] == 'MemAvailable:']
    if (len(available) != 1 or len(available[0]) != 3 or available[0][2] != 'kB' or
            not available[0][1].isdecimal()):
        raise ValueError('available memory requires one actual MemAvailable value in kB')
    return int(available[0][1]) * 1024


class CpuIdleWindow:
    def __init__(self, seconds, threshold):
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError('idle timeout must be finite and positive')
        if not math.isfinite(threshold) or not 0 < threshold <= 1:
            raise ValueError('CPU threshold must be finite and in (0, 1]')
        self.seconds = seconds
        self.threshold = threshold
        self.previous = None
        self.idle_since = None
        self.cpu_fraction = 0

    def sample(self, now, cpu_seconds, output_active=False):
        if not math.isfinite(now) or not math.isfinite(cpu_seconds) or cpu_seconds < 0:
            raise ValueError('invalid CPU activity sample')
        previous = self.previous
        self.previous = (now, cpu_seconds)
        if previous is None:
            return False
        elapsed = now - previous[0]
        if elapsed <= 0 or cpu_seconds < previous[1]:
            raise ValueError('CPU activity clock or accounting moved backwards')
        self.cpu_fraction = (cpu_seconds - previous[1]) / elapsed
        if self.cpu_fraction >= self.threshold or output_active:
            self.idle_since = None
            return False
        if self.idle_since is None:
            self.idle_since = previous[0]
        return now - self.idle_since >= self.seconds


def process_cpu_ticks(pid):
    pending = [pid]
    counters = {}
    while pending:
        current = pending.pop()
        proc = Path('/proc') / str(current)
        try:
            fields = (proc / 'stat').read_text().rsplit(') ', 1)[1].split()
            children = (proc / 'task' / str(current) / 'children').read_text().split()
        except FileNotFoundError:
            continue
        counters[(current, int(fields[19]))] = int(fields[11]) + int(fields[12])
        pending.extend(int(child) for child in children)
    return counters


class ProcessIdleObserver:
    def __init__(self, seconds=30, threshold=0.05, output_paths=(), minimum_available_memory_bytes=0):
        if type(minimum_available_memory_bytes) is not int or minimum_available_memory_bytes < 0:
            raise ValueError('minimum available memory must be a nonnegative integer byte count')
        self.window = CpuIdleWindow(seconds, threshold)
        self.ticks_per_second = os.sysconf('SC_CLK_TCK')
        self.counters = {}
        self.next_sample = 0
        self.started = None
        self.interval = min(1, seconds / 3)
        self.output_paths = tuple(Path(path) for path in output_paths)
        self.output_sizes = {}
        self.output_growth_bytes = 0
        self.minimum_available_memory_bytes = minimum_available_memory_bytes

    def __call__(self, pid):
        now = time.monotonic()
        if now < self.next_sample:
            return None
        self.next_sample = now + self.interval
        current = process_cpu_ticks(pid)
        if not current:
            return None
        self.counters.update(current)
        cpu_seconds = sum(self.counters.values()) / self.ticks_per_second
        if self.started is None:
            self.started = now
        current_sizes = output_file_sizes(self.output_paths)
        growth = 0
        for path, identity in current_sizes.items():
            previous = self.output_sizes.get(path)
            previous_size = previous[2] if previous is not None and previous[:2] == identity[:2] else 0
            growth += max(0, identity[2] - previous_size)
        self.output_sizes = current_sizes
        self.output_growth_bytes += growth
        inactive = self.window.sample(now, cpu_seconds, output_active=growth > 0)
        if self.minimum_available_memory_bytes:
            available = available_memory_bytes()
            if available < self.minimum_available_memory_bytes:
                return {'stop_reason': 'memory-pressure', 'available_memory_bytes': available,
                        'minimum_available_memory_bytes': self.minimum_available_memory_bytes,
                        'memory_source': '/proc/meminfo:MemAvailable in kB',
                        'monitored_pids': sorted({identity[0] for identity in self.counters})}
        if not inactive:
            return None
        return {'stop_reason': 'inactive', 'idle_seconds': now - self.window.idle_since,
                'idle_timeout_seconds': self.window.seconds, 'cpu_threshold': self.window.threshold,
                'cpu_fraction': self.window.cpu_fraction, 'cpu_seconds': cpu_seconds,
                 'activity_elapsed_seconds': now - self.started,
                 'activity_policy': 'cpu-or-output-growth', 'output_growth_bytes': self.output_growth_bytes,
                 'monitored_outputs': [str(path) for path in self.output_paths],
                 'monitored_pids': sorted({identity[0] for identity in self.counters})}


def output_file_sizes(paths):
    from fp_libraries import directory_fd

    sizes = {}
    for path in paths:
        try:
            with directory_fd(path.parent) as parent:
                info = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
        except FileNotFoundError:
            continue
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
            raise ValueError(f'activity output must be an owned regular single-link file: {path}')
        sizes[path] = (info.st_dev, info.st_ino, info.st_size)
    return sizes
