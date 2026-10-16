"""Bounded first-fault stop and evidence support for independently reviewed trials.

Adapters are explicit: this module never launches a service or adopts a PID.
Terminal supervisors with live guests block; they do not authorize new cleanup.
"""

from collections import deque
import json
import time
import traceback

from journal import IncrementalLog


def clipped(value, limit=2048):
    return str(value)[:limit].encode('utf-8', errors='replace')[:limit].decode('utf-8', errors='ignore')


def exception_record(error):
    chain = []
    seen = set()
    while error is not None and id(error) not in seen and len(chain) < 4:
        seen.add(id(error))
        frames = traceback.extract_tb(error.__traceback__, limit=-16)
        chain.append({'type': type(error).__name__, 'message': clipped(error),
                      'frames': [{'file': clipped(frame.filename, 512), 'line': frame.lineno,
                                  'function': clipped(frame.name, 256),
                                  'source': clipped(frame.line or '', 512)} for frame in frames]})
        error = error.__cause__ or (None if error.__suppress_context__ else error.__context__)
    return chain


class WW56Ownership:
    """Use existing WW56 identity helpers plus positive terminal emptiness proof."""

    def __init__(self, runner, unit, owned, properties, members, deliver, guests=(), *,
                 clock=time.monotonic, yield_for=time.sleep, terminal_wait_seconds=10, poll_seconds=0.1):
        if not (0 < terminal_wait_seconds <= 10 and 0.01 <= poll_seconds <= 1):
            raise ValueError('invalid bounded terminal observation policy')
        self.runner = runner
        self.unit = unit
        self.owned = owned
        self.properties = properties
        self.members = members
        self.deliver = deliver
        self.guests = tuple(guests)
        self.state = {'owned_identity': owned}
        self.snapshot = {}
        self.clock = clock
        self.yield_for = yield_for
        self.terminal_wait_seconds = terminal_wait_seconds
        self.poll_seconds = poll_seconds

    def verify(self):
        result = self._observe()
        return self.wait_terminal() if result is None else result

    def wait_terminal(self):
        started = self.clock()
        deadline = started + self.terminal_wait_seconds
        for poll in range(int(self.terminal_wait_seconds / self.poll_seconds) + 2):
            terminal = self._observe()
            self.snapshot['terminal_wait'] = {'polls': poll + 1, 'elapsed': self.clock() - started}
            if terminal is True:
                return True
            remaining = deadline - self.clock()
            if remaining <= 0:
                break
            self.yield_for(min(self.poll_seconds, remaining))
        raise ValueError('BLOCKED: bounded wait expired without original terminal proof')

    def _observe(self):
        self.snapshot = {'unit': self.unit, 'owned': self.owned._asdict()}
        if self.state.get('owned_identity') != self.owned:
            raise ValueError('BLOCKED: original owned identity or launch nonce changed')
        current = self.properties(self.unit)
        self.snapshot['properties'] = dict(current)
        terminal = self.runner.owned_terminal(self.unit, self.state, current)
        if terminal:
            return self._confirm_terminal()
        if self._stopping(current):
            return None
        try:
            self.snapshot['process'] = self.runner.process_identity(self.owned.pid)
            if self.snapshot['process'] != (self.owned.starttime, self.owned.cgroup):
                raise ValueError('BLOCKED: changed original process identity')
            self.runner.verify_owned_identity(self.unit, self.owned, current)
        except (FileNotFoundError, ProcessLookupError):
            current = self.properties(self.unit)
            self.snapshot['properties_recheck'] = dict(current)
            if self.runner.owned_terminal(self.unit, self.state, current):
                return self._confirm_terminal()
            if self._stopping(current):
                return None
            raise ValueError('BLOCKED: missing fresh original terminal proof')
        return False

    def _stopping(self, current):
        if current.get('ActiveState') != 'deactivating':
            return False
        if (current.get('LoadState') != 'loaded' or current.get('InvocationID') != self.owned.invocation_id or
                current.get('ControlGroup') != self.owned.cgroup or
                current.get('MainPID') not in (str(self.owned.pid), '0')):
            raise ValueError('BLOCKED: changed stopping invocation/process ownership')
        try:
            process = self.runner.process_identity(self.owned.pid)
            self.snapshot['process'] = process
            if process != (self.owned.starttime, self.owned.cgroup):
                raise ValueError('BLOCKED: changed stopping original PID identity')
        except (FileNotFoundError, ProcessLookupError):
            self.snapshot['original_pid_absent'] = True
        return True

    def _confirm_terminal(self):
        gone = self.runner.original_process_gone(self.owned)
        self.snapshot['original_pid_absent'] = gone
        members = self.members(self.owned.cgroup)
        self.snapshot['members'] = members
        if not gone or members:
            raise ValueError('BLOCKED: terminal supervisor lacks empty owned process proof')
        self.snapshot['guests'] = []
        for guest in self.guests:
            try:
                guest_terminal = guest.verify()
            finally:
                self.snapshot['guests'].append(dict(guest.snapshot))
            if not guest_terminal:
                raise ValueError('BLOCKED: terminal supervisor still has an active owned guest')
        return True

    def signal(self):
        self.deliver(self.owned.pid)


class FirstFaultWatchguard:
    def __init__(self, ownership, predicate, *, clock=time.monotonic):
        self.ownership = ownership
        self.predicate = predicate
        self.clock = clock
        self.first_fault = None
        self.disposition = 'pending'
        self.safety = 'OK'
        self.cleanup = 'pending'
        self.errors = []
        self.error_count = 0
        self.attempt = None

    def error(self, error):
        self.safety = 'FAILED'
        self.error_count += 1
        if len(self.errors) < 4:
            self.errors.append({'exception': exception_record(error),
                                'proof': dict(self.ownership.snapshot),
                                'attempt': dict(self.attempt or {})})

    def request_stop(self):
        if self.disposition != 'pending':
            return
        self.attempt = {'requested': self.clock(), 'outcome': 'pending'}
        try:
            if self.ownership.verify():
                self.disposition = 'terminal-confirmed'
                self.cleanup = 'confirmed'
            else:
                self.ownership.signal()
                self.disposition = 'signal-sent'
            self.attempt['outcome'] = self.disposition
        except Exception as error:
            self.disposition = 'blocked'
            self.cleanup = 'blocked'
            self.attempt['outcome'] = 'blocked'
            self.error(error)

    def observe(self, source, line):
        if self.first_fault is not None or not self.predicate(line):
            return False
        self.first_fault = {'monotonic': self.clock(), 'path': clipped(source, 512),
                            'line': clipped(line)}
        self.request_stop()
        return True

    def sample(self, readers, on_line=None):
        """One bounded chunk/source; first recognition stops before the next line."""
        try:
            for source, reader in readers.items():
                lines, _ = reader.read_bounded()
                for line in lines:
                    if self.observe(source, line):
                        return
                    if on_line is not None:
                        on_line(source, line)
        except Exception as error:
            self.error(error)
            self.request_stop()

    def confirm_cleanup(self):
        try:
            observe_terminal = getattr(self.ownership, 'wait_terminal', self.ownership.verify)
            if not observe_terminal():
                raise ValueError('BLOCKED: cleanup has no terminal proof')
            self.cleanup = 'confirmed'
            if self.disposition == 'signal-sent':
                self.disposition = 'terminal-confirmed'
        except Exception as error:
            self.cleanup = 'blocked'
            self.error(error)

    def result(self, native_status, publication=False):
        return {'safety': self.safety, 'native': native_status, 'cleanup': self.cleanup,
                'publication': publication, 'stop_disposition': self.disposition,
                'first_fault': self.first_fault, 'errors': self.errors,
                'error_count': self.error_count, 'attempt': self.attempt,
                'proof': self.ownership.snapshot}


def guardian_ok(result):
    return (result.get('safety') == 'OK' and result.get('native') == 'success' and
            result.get('cleanup') == 'confirmed' and result.get('publication') is True and
            result.get('first_fault') is None)


def summarize_sources(paths, predicate, *, stopped_and_clean, continue_scan=lambda: True):
    """Streaming postflight only; duplicate paths remain separate source records."""
    if not stopped_and_clean:
        raise ValueError('postflight scan requires confirmed stop and cleanup')
    paths = tuple(paths)
    if len(paths) > 8 or any(len(str(path).encode()) > 512 for path in paths):
        raise ValueError('evidence source policy exceeded')
    sources = []
    for path in paths:
        reader = IncrementalLog(path)
        first, tail = [], deque(maxlen=16)
        matches = 0
        seen_lines = 0
        error = None

        def ingest(lines):
            nonlocal matches, seen_lines
            for line in lines:
                seen_lines += 1
                if predicate(line):
                    matches += 1
                    item = {'match': matches, 'line_number': seen_lines, 'line': clipped(line)}
                    if len(first) < 16:
                        first.append(item)
                    else:
                        tail.append(item)

        try:
            while continue_scan():
                lines, _ = reader.read_bounded()
                ingest(lines)
                if reader.complete:
                    ingest(reader.finish_bounded())
                    break
                if reader.identity is None:
                    raise FileNotFoundError(str(path))
        except Exception as caught:
            error = exception_record(caught)
        sources.append({'path': str(path), 'bytes_read': reader.offset,
                        'scanned_bytes': reader.scanned_bytes, 'lines': reader.lines,
                        'matches': matches, 'clipped_bytes': reader.clipped_bytes,
                        'clipped_lines': reader.clipped_lines,
                        'classification_prefix_only': bool(reader.clipped_lines),
                        'complete': reader.complete and error is None,
                        'sha256_read_extent': reader.sha256.hexdigest(),
                        'first': first, 'tail': list(tail), 'error': error})
    result = {'sources': sources, 'counts_are_per_source_not_unique_events': True,
              'sample_budget_clipped_bytes': 0}
    limit = 2048
    while len(json.dumps(result).encode()) >= 256 * 1024 and limit > 0:
        limit //= 2
        for source in sources:
            for sample in source['first'] + source['tail']:
                original = sample['line']
                sample['line'] = clipped(original, limit)
                result['sample_budget_clipped_bytes'] += len(original.encode()) - len(sample['line'].encode())
    if len(json.dumps(result).encode()) >= 256 * 1024:
        raise ValueError('evidence output exceeds 256 KiB budget')
    return result


def write_summary(path, result):
    content = json.dumps(result, ensure_ascii=False).encode('utf-8')
    if len(content) >= 256 * 1024:
        raise ValueError('evidence output exceeds 256 KiB budget')
    with path.open('xb') as stream:
        stream.write(content + b'\n')
