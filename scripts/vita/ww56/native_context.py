"""Trace-only native-context window; never changes the default core policy."""

import re

MARKER = re.compile(r'\[native-ctx\] event=(ready|armed|failure|tlogoe-open|tlogoe-header|tlogoe-body) '
                    r'us=(\d+) fd=(-?\d+) flags=(\d+)')


class NativeContextWindow:
    def __init__(self):
        self.events = {}
        self.error = None

    def feed(self, lines, *, now, wall):
        for line in lines:
            if '[native-ctx]' not in line:
                continue
            match = MARKER.fullmatch(line.strip())
            if match is None:
                self.error = 'malformed native-context marker'
                continue
            event, us, fd, flags = match.groups()
            age = wall - int(us) / 1000000
            if age < 0:
                self.error = 'native-context clock is ahead of supervisor'
            if int(fd) < 0 or int(flags):
                self.error = 'native-context observer reported incomplete coverage'
            if event.startswith('tlogoe-') and 'armed' not in self.events:
                self.error = 'TlogoE marker arrived without an armed window'
            self.events.setdefault(event, {'monotonic': now - max(0, age), 'us': int(us),
                                           'fd': int(fd), 'flags': int(flags)})

    def stop(self, now):
        if self.error:
            return {'stop_reason': 'native-context-coverage-failure', 'classification': 'inconclusive',
                    'detail': self.error}
        for event, seconds, reason in (('tlogoe-body', 5, 'body'), ('armed', 10, 'arm')):
            if event in self.events and now >= self.events[event]['monotonic'] + seconds:
                return {'stop_reason': 'native-context-' + reason + '-deadline',
                        'classification': 'bounded-observation-not-native-crash', 'events': self.events}
        return None

    def receipt(self):
        complete = all(event in self.events for event in ('armed', 'tlogoe-open', 'tlogoe-header', 'tlogoe-body'))
        return {'variant': 'trace-only', 'events': self.events, 'coverage_error': self.error,
                'markers_complete': complete,
                'window_evidence': 'available' if complete and not self.error else 'inconclusive',
                'window_armed': 'armed' in self.events,
                'native_fault_classification': 'unresolved: core policy deferred pending user approval',
                'core_policy': 'unchanged LimitCORE=0'}
