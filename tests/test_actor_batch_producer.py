from pathlib import Path
import io
import json
import os
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ActorBatchProducer(unittest.TestCase):
    def test_bounded_selection_and_exclusive_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'probe.c'
            patch = (ROOT / 'patches/dolrecomp/0020-c-emit-ship-canonical-code-input.patch').read_text()
            added = '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+') and not line.startswith('+++'))
            start = added.index('static int publish_ship_code_input(')
            publish = added[start:added.index('\nstatic int emit_rel_split_impl(', start)]
            source.write_text('''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint32_t u32;
typedef unsigned char u8;
typedef struct {u8* data; size_t size; u32 module_id; const char* magic;} ShipCodeInput;
static const char* failure;
static FILE* capture_stream;
static FILE* manifest_stream;
static FILE* probe_fopen(const char* path, const char* mode) {
    FILE* stream = fopen(path, mode);
    if (strstr(path, "complete.json")) manifest_stream = stream;
    else capture_stream = stream;
    return stream;
}
static size_t probe_fwrite(const void* data, size_t size, size_t count, FILE* stream) {
    if (failure && strcmp(failure, "capture-write") == 0 && stream == capture_stream) return 0;
    return fwrite(data, size, count, stream);
}
static int probe_fclose(FILE* stream) {
    int fail = failure && ((strcmp(failure, "capture-close") == 0 && stream == capture_stream) ||
                          (strcmp(failure, "manifest-close") == 0 && stream == manifest_stream));
    int result = fclose(stream);
    return fail ? EOF : result;
}
static int probe_fprintf(FILE* stream, const char* format, ...) {
    if (failure && strcmp(failure, "manifest-write") == 0 && stream == manifest_stream) return -1;
    va_list args; va_start(args, format);
    int result = vfprintf(stream, format, args);
    va_end(args); return result;
}
#define fopen probe_fopen
#define fwrite probe_fwrite
#define fclose probe_fclose
#define fprintf probe_fprintf
PUBLISH_FUNCTION
#include "actor_batch_capture.h"
int main(int argc, char** argv) {
    assert(argc == 3);
    assert(!actor_batch_separate(argv[1], argv[1]));
    assert(actor_batch_separate(argv[1], argv[0]));
    ActorCapture entries[] = {
        {.capture={.module_id=84u,.magic="WOOD84\\0"},.basename="wood84.ship-code-input"},
        {.capture={.module_id=85u,.magic="TEST85\\0\\0"},.basename="test85.ship-code-input"}};
    if (strcmp(argv[2], "collision") == 0) {
        entries[1].basename = entries[0].basename;
        assert(!actor_batch_paths(entries, 2u, argv[1]));
        return 0;
    }
    assert(actor_batch_paths(entries, 2u, argv[1]));
    assert(actor_batch_match(entries, 2u, 90u) == NULL);
    assert(!actor_batch_complete(entries, 2u));
    assert(actor_batch_match(entries, 2u, 84u) == &entries[0].capture);
    assert(!actor_batch_complete(entries, 2u));
    assert(actor_batch_match(entries, 2u, 85u) == &entries[1].capture);
    assert(actor_batch_complete(entries, 2u));
    if (strcmp(argv[2], "duplicate") == 0) {
        actor_batch_match(entries, 2u, 84u);
        assert(!actor_batch_complete(entries, 2u));
        return 0;
    }
    for (u32 i=0; i<2u; ++i) {
        entries[i].capture.data = (u8*)entries[i].capture.magic;
        entries[i].capture.size = 8u;
    }
    if (strcmp(argv[2], "publish") != 0) {
        failure = argv[2];
        assert(!actor_batch_publish(entries, 2u, argv[1]));
        assert(access(argv[1], F_OK) != 0);
        return 0;
    }
    assert(actor_batch_publish(entries, 2u, argv[1]));
    assert(!actor_batch_publish(entries, 2u, argv[1]));
    return 0;
}
'''.replace('PUBLISH_FUNCTION', publish))
            result = subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'patches/dolrecomp/diagnostic'), str(source), '-o', str(root / 'probe')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for scenario in ('duplicate', 'collision', 'capture-write', 'capture-close',
                             'manifest-write', 'manifest-close', 'publish'):
                with self.subTest(scenario=scenario):
                    output = root / scenario
                    subprocess.run([str(root / 'probe'), str(output), scenario], check=True)
                    if scenario != 'publish':
                        self.assertFalse(output.exists())
                    else:
                        self.assertEqual((output / 'wood84.ship-code-input').read_bytes(), b'WOOD84\0\0')
                        self.assertEqual((output / 'test85.ship-code-input').read_bytes(), b'TEST85\0\0')
                        self.assertTrue((output / 'complete.json').is_file())


class ActorBatchEmitter(unittest.TestCase):
    @classmethod
    def build_producer(cls, source, name):
        build = cls.root / name
        subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DDOLRECOMP_ENABLE_LLVM=OFF', '-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON'],
            check=True, capture_output=True)
        subprocess.run(['cmake', '--build', str(build), '--target', 'dolrecomp', '-j1'], check=True, capture_output=True)
        return build / 'dolrecomp'

    @classmethod
    def setUpClass(cls):
        (ROOT / 'build-f5').mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(dir=ROOT / 'build-f5', prefix='actor-batch-producer-')
        cls.root = Path(cls.temporary.name)
        source = cls.root / 'source'
        source.mkdir()
        pin = ROOT / 'ref/vita-ww-01/DolRecomp'
        archive = subprocess.check_output(['git', 'archive', 'b8b534591cba8ca7cd43943a655ee6e2591cf5de'], cwd=pin)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(source, filter='data')
        for patch in ('0020-c-emit-ship-canonical-code-input.patch', 'diagnostic/actor-batch.patch'):
            subprocess.run(['git', 'apply', str(ROOT / 'patches/dolrecomp' / patch)], cwd=source,
                env={**os.environ, 'GIT_CEILING_DIRECTORIES': str(source.parent)}, check=True, capture_output=True)
        shutil.copy2(ROOT / 'patches/dolrecomp/diagnostic/actor_batch_capture.h', source / 'src/app')
        cls.production_executable = cls.build_producer(source, 'production-build')
        pipeline = source / 'src/app/pipeline.c'
        text = pipeline.read_text()
        row = '{.capture={.module_id=19u, .magic="GRASS19"}, .basename="grass19.ship-code-input"}'
        if text.count(row) != 1:
            raise AssertionError('reviewed production table changed')
        pipeline.write_text(text.replace(row, '{.capture={.module_id=84u, .magic="WOOD84\\0"}, .basename="wood84.ship-code-input"},\n'
            '        {.capture={.module_id=85u, .magic="TEST85\\0"}, .basename="test85.ship-code-input"}'))
        cls.executable = cls.build_producer(source, 'build')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=self.root)
        self.case = Path(self.temporary.name)
        self.inputs = self.case / 'inputs'
        self.inputs.mkdir()
        for mid in (84, 85, 90):
            raw = bytearray(512)
            for at, value in ((0, mid), (0xC, 4), (0x10, 0x4C), (0x1C, 3), (0x40, 4), (0x44, 1), (0x48, 0x84)):
                struct.pack_into('>I', raw, at, value)
            struct.pack_into('>6I', raw, 0x54, 0x6D, 16, 0x7C, 4, 0x80, 4)
            struct.pack_into('>4I', raw, 0x6C, 0x60000000, 0x60000000, 0x4E800020, 0x60000000)
            (self.inputs / f'{mid:04d}.rel').write_bytes(raw)

    def tearDown(self):
        self.temporary.cleanup()

    def emit(self, label, controls, executable=None):
        return subprocess.run([str(executable or self.executable), '--gamecube', '--backend', 'c', '--cpu', 'gekko',
            '--rel-base', '0xC0400000', str(self.inputs), str(self.case / label), '-j', '1'],
            env={**os.environ, **controls}, capture_output=True, text=True)

    def test_production_wave_captures_only_grass_without_changing_ordinary_outputs(self):
        raw = bytearray((self.inputs / '0084.rel').read_bytes())
        struct.pack_into('>I', raw, 0, 19)
        (self.inputs / '0019.rel').write_bytes(raw)
        captures = self.case / 'captures'
        result = self.emit('grass', {'DOLRECOMP_ACTOR_BATCH_DIR': str(captures)}, self.production_executable)
        self.assertEqual(result.returncode, 0, result.stderr)
        ordinary = self.emit('ordinary', {}, self.production_executable)
        self.assertEqual(ordinary.returncode, 0, ordinary.stderr)
        maps = [{str(p.relative_to(self.case / name)): p.read_bytes() for p in (self.case / name).rglob('*') if p.is_file()}
                for name in ('grass', 'ordinary')]
        self.assertEqual(*maps)
        self.assertEqual({p.name for p in captures.iterdir()}, {'grass19.ship-code-input', 'complete.json'})
        self.assertEqual((captures / 'grass19.ship-code-input').read_bytes()[:8], b'GRASS19\0')
        self.assertEqual(json.loads((captures / 'complete.json').read_text()), {'version': 1, 'batch': 'opening-b',
            'modules': [{'id': 19, 'capture': 'grass19.ship-code-input', 'size': 44}]})

    def test_production_missing_or_duplicate_grass_cannot_publish_a_subset(self):
        for scenario in ('missing', 'duplicate'):
            with self.subTest(scenario=scenario):
                if scenario == 'duplicate':
                    raw = bytearray((self.inputs / '0084.rel').read_bytes())
                    struct.pack_into('>I', raw, 0, 19)
                    (self.inputs / '0019.rel').write_bytes(raw)
                    (self.inputs / 'copy.rel').write_bytes(raw)
                captures = self.case / ('captures-' + scenario)
                result = self.emit(scenario, {'DOLRECOMP_ACTOR_BATCH_DIR': str(captures)}, self.production_executable)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(captures.exists())

    def test_two_selected_modules_share_one_full_directory_pass_without_changing_outputs(self):
        captures = self.case / 'captures'
        result = self.emit('batch', {'DOLRECOMP_ACTOR_BATCH_DIR': str(captures)})
        self.assertEqual(result.returncode, 0, result.stderr)
        ordinary = self.emit('ordinary', {})
        self.assertEqual(ordinary.returncode, 0, ordinary.stderr)
        maps = [{str(p.relative_to(self.case / name)): p.read_bytes() for p in (self.case / name).rglob('*') if p.is_file()}
                for name in ('batch', 'ordinary')]
        self.assertEqual(*maps)
        self.assertEqual(set(p.name for p in captures.iterdir()),
                         {'wood84.ship-code-input', 'test85.ship-code-input', 'complete.json'})
        for mid, magic, basename in ((84, b'WOOD84\0\0', 'wood84'), (85, b'TEST85\0\0', 'test85')):
            with self.subTest(mid=mid):
                capture = (captures / (basename + '.ship-code-input')).read_bytes()
                self.assertEqual(capture[:8], magic)
                self.assertEqual(len(capture), 44)
                self.assertEqual(struct.unpack_from('>III', capture, 8), (1, 1, 0x6C))
                self.assertEqual(struct.unpack_from('>I', capture, 24)[0], 16)
                self.assertEqual(capture[28:], (self.inputs / f'{mid:04d}.rel').read_bytes()[0x6C:0x7C])

    def test_missing_or_duplicate_selected_module_cannot_publish_a_subset(self):
        for scenario in ('missing', 'duplicate'):
            with self.subTest(scenario=scenario):
                member = self.inputs / '0085.rel'
                original = member.read_bytes()
                if scenario == 'missing': member.unlink()
                else: (self.inputs / 'copy.rel').write_bytes(original)
                captures = self.case / ('captures-' + scenario)
                result = self.emit(scenario, {'DOLRECOMP_ACTOR_BATCH_DIR': str(captures)})
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(captures.exists())
                member.write_bytes(original)

    def test_conflicting_legacy_control_and_output_collision_refuse_before_emission(self):
        for scenario in ('conflict', 'collision', 'empty'):
            with self.subTest(scenario=scenario):
                output = self.case / scenario
                captures = output if scenario == 'collision' else self.case / ('captures-' + scenario)
                controls = {'DOLRECOMP_ACTOR_BATCH_DIR': '' if scenario == 'empty' else str(captures)}
                if scenario == 'conflict': controls['DOLRECOMP_SHIP_CODE_INPUT'] = str(self.case / 'legacy.ship-code-input')
                result = self.emit(scenario, controls)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertFalse(captures.exists())

    def test_generated_output_write_failure_never_publishes_captures(self):
        result = self.emit('baseline', {})
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = self.case / 'baseline'
        for kind, source in (('chunk', next(baseline.rglob('chunk_*.c'))),
                             ('header', next(baseline.rglob('generated.h'))),
                             ('manifest', next(baseline.rglob('generated.c')))):
            with self.subTest(kind=kind):
                output = self.case / kind
                sink = output / source.relative_to(baseline)
                sink.parent.mkdir(parents=True)
                sink.symlink_to('/dev/full')
                captures = self.case / ('captures-' + kind)
                failed = self.emit(kind, {'DOLRECOMP_ACTOR_BATCH_DIR': str(captures)})
                self.assertNotEqual(failed.returncode, 0)
                self.assertFalse(captures.exists())


if __name__ == '__main__':
    unittest.main()
