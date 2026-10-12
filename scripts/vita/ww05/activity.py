"""CPU inactivity accounting for the owned sandbox and its descendants."""

import math
import os
from pathlib import Path
import time


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

    def sample(self, now, cpu_seconds):
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
        if self.cpu_fraction >= self.threshold:
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
    def __init__(self, seconds=30, threshold=0.05):
        self.window = CpuIdleWindow(seconds, threshold)
        self.ticks_per_second = os.sysconf('SC_CLK_TCK')
        self.counters = {}
        self.next_sample = 0
        self.started = None
        self.interval = min(1, seconds / 3)

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
        if not self.window.sample(now, cpu_seconds):
            return None
        return {'stop_reason': 'inactive', 'idle_seconds': now - self.window.idle_since,
                'idle_timeout_seconds': self.window.seconds, 'cpu_threshold': self.window.threshold,
                'cpu_fraction': self.window.cpu_fraction, 'cpu_seconds': cpu_seconds,
                'activity_elapsed_seconds': now - self.started,
                'monitored_pids': sorted({identity[0] for identity in self.counters})}
