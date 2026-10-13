#!/usr/bin/env python3
"""Windowed frame-rate metrics for the VITA-WW-12 performance harness.

The input is presented-frame wall stamps in microseconds, in order. Windows are
non-overlapping and anchored at the first stamp; a window's FPS is the frames
inside it divided by its length. Only complete windows are reported, so the
last, short tail of a run never drags the series down. An optional observation
end extends the window count to it: windows after the last stamp are reported
as 0 FPS instead of being skipped, so a stream that stalls while the process
stays alive is measured rather than refused. The summary carries the median,
p95 and minimum window FPS plus the p95 frame time, so one slow second cannot
hide in an average. The percentile convention matches the desktop bench
(scripts/bench_report.py): linear interpolation between neighbours.
"""

from __future__ import annotations

from dataclasses import dataclass
import math


class FpsError(ValueError):
    """The stamps cannot produce a frame-rate measurement."""


@dataclass(frozen=True)
class WindowFps:
    start_s: float
    fps: float


def intervals_us(times_us: list) -> list:
    if len(times_us) < 2:
        raise FpsError("fewer than two frames: no interval to measure")
    intervals = [later - earlier for earlier, later in zip(times_us, times_us[1:])]
    if any(interval <= 0 for interval in intervals):
        raise FpsError("frame stamps do not advance: cannot measure intervals")
    return intervals


def percentile(values: list, fraction: float) -> float:
    if not values:
        raise FpsError("no samples")
    ordered = sorted(values)
    if len(ordered) == 1:
        return float(ordered[0])
    index = fraction * (len(ordered) - 1)
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return float(ordered[int(index)])
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (index - lower)


def windowed_fps(times_us: list, window_s: float, end_us: int | None = None) -> list:
    if window_s <= 0:
        raise FpsError("window must be positive")
    intervals_us(times_us)
    first = times_us[0]
    if end_us is not None and end_us <= times_us[-1]:
        raise FpsError("observation end must follow the last frame stamp")
    last = end_us if end_us is not None else times_us[-1]
    windows = int((last - first) / 1e6 / window_s)
    if windows < 1:
        raise FpsError("the stream is shorter than one window: nothing to report")
    series = []
    for index in range(windows):
        start_us = first + int(index * window_s * 1e6)
        window_end_us = first + int((index + 1) * window_s * 1e6)
        frames = sum(1 for stamp in times_us if start_us <= stamp < window_end_us)
        series.append(WindowFps(index * window_s, frames / window_s))
    return series


def frame_rate_summary(times_us: list, window_s: float, end_us: int | None = None) -> dict:
    intervals = intervals_us(times_us)
    series = windowed_fps(times_us, window_s, end_us)
    span_s = (times_us[-1] - times_us[0]) / 1e6
    frame_ms = [interval / 1000.0 for interval in intervals]
    window_fps = [window.fps for window in series]
    return {
        "frames": len(times_us),
        "window_seconds": window_s,
        "windows": len(series),
        "span_seconds": span_s,
        "mean_fps": len(intervals) / span_s,
        "median_window_fps": percentile(window_fps, 0.50),
        "p95_window_fps": percentile(window_fps, 0.95),
        "min_window_fps": min(window_fps),
        "max_window_fps": max(window_fps),
        "median_frame_ms": percentile(frame_ms, 0.50),
        "p95_frame_ms": percentile(frame_ms, 0.95),
    }
