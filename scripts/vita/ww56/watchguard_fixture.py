"""Public synthetic driver, optionally replaying a legacy helper as AST data."""

import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile
import time
import unittest
from types import SimpleNamespace

from journal import IncrementalLog
from watchguard import (FirstFaultWatchguard, exception_record, guardian_ok,
                        summarize_sources, write_summary)


class FakeOwnership:
    def __init__(self):
        self.snapshot = {'fixture': 'synthetic-only'}
        self.signals = 0
        self.terminal = False

    def verify(self):
        return self.terminal

    def signal(self):
        self.signals += 1


class InfiniteBacklog:
    def __init__(self, owner):
        self.owner = owner
        self.reads = 0

    def read_bounded(self):
        self.reads += 1
        if self.reads > 1 and self.owner.signals == 0:
            raise AssertionError('read again before authenticated STOP')
        return ['synthetic FAULT', 'synthetic later line'], 65536


def replay_legacy(path):
    tree = ast.parse(path.read_text(), filename=str(path))
    function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                    and node.name == 'stop_supervisor')
    identity = dict(pid=12345, starttime=987, cgroup='/synthetic', invocation_id='a' * 32)
    terminal = dict(LoadState='loaded', MainPID='0', ControlGroup='', InvocationID='a' * 32,
                    ActiveState='failed', SubState='failed', Result='oom-kill',
                    ExecMainCode='2', ExecMainStatus='9')
    signals = []
    namespace = dict(ROOT=Path('/synthetic'), PREFIX='synthetic', KEYS=tuple(terminal),
                     read=lambda unused: identity, now=lambda: 'synthetic',
                     write=lambda *unused: None,
                     runner=SimpleNamespace(properties=lambda *unused: terminal,
                                            process_identity=lambda unused: (987, '/synthetic')),
                     os=SimpleNamespace(kill=lambda *args: signals.append(args)),
                     signal=SimpleNamespace(SIGINT=2))
    exec(compile(ast.Module(body=[function], type_ignores=[]), str(path), 'exec'), namespace)
    try:
        namespace['stop_supervisor']('synthetic recognized fault; same invocation now terminal')
    except Exception as error:
        print(json.dumps({'legacy_terminal_acceptance': 'RED', 'signals': signals,
                          'exception': exception_record(error),
                          'synthetic_reconstruction_not_recovered_stack': True}, indent=2))
        return 1
    print(json.dumps({'legacy_terminal_acceptance': 'unexpected pass', 'signals': signals}))
    return 0


def replay_legacy_logs(path):
    tree = ast.parse(path.read_text(), filename=str(path))
    watch = next(node for node in tree.body if isinstance(node, ast.ClassDef) and node.name == 'ExternalWatch')
    logs = next(node for node in watch.body if isinstance(node, ast.FunctionDef) and node.name == 'logs')
    with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
        root = Path(directory)
        state = SimpleNamespace(runroot=root / 'run', receipts=root / 'receipts',
                                identities={'guest': 'synthetic'}, supervisor=None,
                                readers={}, last_growth={}, first_fault=None, record=lambda event: None)
        first = state.runroot / 'phases/guest/evidence/process.log'
        second = state.runroot / 'cache/Vita3K/vita3k.log'
        for source in (first, second):
            source.parent.mkdir(parents=True)
            source.write_bytes(b'synthetic only\n')
        reads = []
        signals = []

        class LegacyReader:
            def __init__(self, source):
                self.source = source
                self.offset = 0

            def read(self):
                reads.append(str(self.source))
                if state.first_fault is not None and not signals:
                    raise AssertionError('legacy source read after fault recognition, before STOP')
                return ['unhandled sigsegv synthetic fault'], 65536

        namespace = dict(Path=Path, ROOT=root, os=os, time=time,
                         now=lambda: 'synthetic', json=json,
                         stop_supervisor=lambda reason: signals.append(reason),
                         runner=SimpleNamespace(REPORT_DIRECTORY='report', IncrementalLog=LegacyReader,
                                                is_failure=lambda line: False))
        exec(compile(ast.Module(body=[logs], type_ignores=[]), str(path), 'exec'), namespace)
        try:
            namespace['logs'](state)
        except Exception as error:
            if state.first_fault is None or len(reads) != 2 or signals:
                raise AssertionError('legacy fixture did not reach the recognized-fault drain defect') from error
            print(json.dumps({'legacy_first_fault_before_second_read': 'RED', 'signals': signals,
                              'reads': len(reads), 'exception': exception_record(error),
                              'synthetic_reconstruction_not_recovered_stack': True}, indent=2))
            return 1
    raise AssertionError('legacy fixture unexpectedly passed first-fault stop ordering')


def replay_template(path):
    """Execute only actual generated log methods against synthetic files/adapters."""
    tree = ast.parse(path.read_text(), filename=str(path))
    imports = [node for node in tree.body if isinstance(node, ast.ImportFrom)
               and node.module == 'watchguard']
    if not imports or 'FirstFaultWatchguard' not in {alias.name for node in imports for alias in node.names}:
        raise AssertionError('actual template does not import public guard')
    owner = FakeOwnership()
    with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
        root = Path(directory)
        namespace = dict(Path=Path, ROOT=root, os=os, stat=stat, time=time,
                         runner=SimpleNamespace(REPORT_DIRECTORY='report', IncrementalLog=IncrementalLog))
        watch_class = next(node for node in tree.body if isinstance(node, ast.ClassDef)
                           and node.name == 'ExternalWatch')
        methods = [node for node in watch_class.body if isinstance(node, ast.FunctionDef)
                   and node.name in ('logs', 'on_line')]
        isolated = ast.ClassDef(name='ActualTemplateWatch', bases=[], keywords=[], body=methods,
                                decorator_list=[])
        ast.fix_missing_locations(isolated)
        exec(compile(ast.Module(body=[isolated], type_ignores=[]), str(path), 'exec'), namespace)
        watch = namespace['ActualTemplateWatch']()
        watch.runroot = root / 'run'
        watch.receipts = root / 'receipts'
        watch.identities = {'guest': 'synthetic original identity'}
        watch.readers = {}
        watch.guard = FirstFaultWatchguard(owner, lambda line: 'FAULT' in line)
        watch.first_fault = None
        watch.last_growth = {}
        watch.marker_count = 0
        watch.first_rel = None
        watch.lifecycle = []
        first = watch.runroot / 'phases/guest/evidence/process.log'
        first.parent.mkdir(parents=True)
        with first.open('wb') as stream:
            stream.write(b'FAULT synthetic only\n')
            for _ in range(1024):
                stream.write(b'FAULT later\n' * 1024)
        watch.logs()
        if owner.signals != 1 or watch.first_fault is None:
            raise AssertionError('actual template did not use authenticated first-fault stop')
        for source, reader in watch.readers.items():
            if source != str(first) and reader.offset:
                raise AssertionError('actual template read another source after first fault')
        if watch.readers[str(first)].offset > 65536:
            raise AssertionError('actual template read an unbounded chunk')
        first.unlink()
        watch.logs()
        if owner.signals != 1 or watch.guard.safety != 'FAILED':
            raise AssertionError('later vanished source duplicated stop or hid error')
        print(json.dumps({'actual_template': str(path), 'signals': owner.signals,
                          'later_error_visible': True, 'synthetic_only': True}))
    return 0


def replay_guardian(path):
    tree = ast.parse(path.read_text(), filename=str(path))
    main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
    with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
        root = Path(directory)
        (root / 'trial.py').write_text('synthetic trial; never executed\n')
        approval = dict(approved=True, reviewer='synthetic reviewer',
                        trial_sha256=hashlib.sha256((root / 'trial.py').read_bytes()).hexdigest(),
                        launch_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        failure = dict(safety='FAILED', native='failure', cleanup='confirmed', publication=True)
        namespace = dict(ROOT=root, WORK=root, Path=Path, __file__=str(path), json=json,
                         hashlib=hashlib, guardian_ok=guardian_ok,
                         preflight=lambda: None,
                         sys=SimpleNamespace(argv=['synthetic', 'reviewed-live']),
                         subprocess=SimpleNamespace(run=lambda *args, **kwargs: SimpleNamespace(returncode=0)),
                         read_bounded=lambda name: approval if name.name == 'independent-review-approval.json' else failure)
        exec(compile(ast.Module(body=[main], type_ignores=[]), str(path), 'exec'), namespace)
        if namespace['main']() != 1:
            raise AssertionError('actual guardian accepted driver zero despite safety/native failure')
        print(json.dumps({'actual_guardian': str(path), 'driver_zero_rejected': True,
                          'synthetic_only': True}))
    return 0


def replay_postflight(path):
    """Run the actual template collector against bounded-live/postflight races."""
    tree = ast.parse(path.read_text(), filename=str(path))
    collector = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                     and node.name == 'final_evidence')

    class PostflightVerdictTests(unittest.TestCase):
        def test_actual_template_postflight_verdict(self):
            for scenario in ('trailing-fault', 'nonfault-success', 'previous-live-fault'):
                with self.subTest(scenario=scenario), tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
                    root = Path(directory)
                    source = root / 'synthetic.log'
                    fault = scenario != 'nonfault-success'
                    with source.open('wb') as stream:
                        for _ in range(4096):
                            stream.write(b'ordinary synthetic log record ' * 4 + b'\n')
                        if fault:
                            stream.write(b'FAULT synthetic trailing record\n')
                    owner = FakeOwnership()
                    guard = FirstFaultWatchguard(owner, lambda line: 'FAULT' in line)
                    guard.sample({str(source): IncrementalLog(source)})
                    self.assertIsNone(guard.first_fault)
                    if scenario == 'previous-live-fault':
                        guard.observe(str(source), 'FAULT synthetic trailing record')
                    signals_before_cleanup = owner.signals
                    owner.terminal = True
                    guard.confirm_cleanup()
                    native = dict(Result='success', ExecMainCode='1', ExecMainStatus='0')
                    native_path = root / 'native-result.json'
                    native_path.write_text(json.dumps(dict(status='success', properties=native)))
                    native_before = native_path.read_bytes()
                    watch = SimpleNamespace(readers={str(source): None}, guard=guard,
                                            first_fault=guard.first_fault)
                    namespace = dict(ROOT=root, Path=Path, HEAD='synthetic-launcher',
                                     GUEST_SOURCE='synthetic-guest', raw_fault=guard.predicate,
                                     summarize_sources=summarize_sources, write_summary=write_summary,
                                     capsule_evidence=lambda unused: {'absence_is_inconclusive': True})
                    exec(compile(ast.Module(body=[collector], type_ignores=[]), str(path), 'exec'), namespace)
                    namespace['final_evidence'](watch)
                    summary = json.loads((root / 'FINAL-DIAGNOSIS.json').read_text())
                    self.assertTrue(summary['sources'][0]['complete'])
                    self.assertEqual(summary['sources'][0]['matches'], int(fault))
                    self.assertLessEqual(len(summary['sources'][0]['first']), 16)
                    self.assertLessEqual(len(summary['sources'][0]['tail']), 16)
                    if fault:
                        self.assertEqual(summary['sources'][0]['first'][0]['line_number'], 4097)
                        self.assertEqual(summary['sources'][0]['first'][0]['line'], 'FAULT synthetic trailing record')
                    result = guard.result('success', publication=True)
                    self.assertEqual(result['native'], 'success')
                    self.assertEqual(native_path.read_bytes(), native_before)
                    self.assertEqual(result['cleanup'], 'confirmed')
                    self.assertEqual(owner.signals, signals_before_cleanup)
                    self.assertEqual(owner.signals, int(scenario == 'previous-live-fault'))
                    self.assertEqual(guardian_ok(result), not fault)

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PostflightVerdictTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy-source', type=Path)
    parser.add_argument('--template-source', type=Path)
    parser.add_argument('--guardian-source', type=Path)
    parser.add_argument('--postflight-template-source', type=Path)
    args = parser.parse_args()
    if args.legacy_source:
        terminal = replay_legacy(args.legacy_source)
        backlog = replay_legacy_logs(args.legacy_source)
        return max(terminal, backlog)
    if args.template_source:
        return replay_template(args.template_source)
    if args.guardian_source:
        return replay_guardian(args.guardian_source)
    if args.postflight_template_source:
        return replay_postflight(args.postflight_template_source)
    owner = FakeOwnership()
    guard = FirstFaultWatchguard(owner, lambda line: 'FAULT' in line)
    reader = InfiniteBacklog(owner)
    guard.sample({'synthetic': reader})
    if owner.signals != 1 or reader.reads != 1:
        raise AssertionError('first fault did not stop before second read')
    owner.terminal = True
    guard.confirm_cleanup()
    result = guard.result('synthetic-native-failure', publication=True)
    print(json.dumps({'guard': result, 'guardian_accepts_trial': guardian_ok(result)}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
