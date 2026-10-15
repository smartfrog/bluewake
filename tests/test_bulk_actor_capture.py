from pathlib import Path
import importlib.util
import json
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DIAGNOSTIC = ROOT / 'patches/dolrecomp/diagnostic'


class BulkActorCapture(unittest.TestCase):
    def test_frozen_table_has_exact_membership_and_unique_identity_fields(self):
        spec = importlib.util.spec_from_file_location('bulk_capture', DIAGNOSTIC / 'generate_bulk_capture.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        header = (DIAGNOSTIC / 'actor_bulk_entries.h').read_text()
        rows = [{'id': int(mid), 'name': basename.rsplit('-', 1)[0], 'capture_magic_hex': magic.encode().hex(),
                 'capture_basename': basename, 'executable_size': int(size)}
                for mid, magic, basename, size in re.findall(
                    r'module_id=(\d+)u, \.magic="([A-Z0-9]+)".*basename="([^"]+)".*executable_size=(\d+)u', header)]
        self.assertEqual(len(rows), 399)
        self.assertEqual({row['id'] for row in rows}, set(range(1, 416)) - module.EXCLUDED - module.INHERITED)
        for field in ('name', 'capture_basename', 'capture_magic_hex'):
            with self.subTest(field=field):
                self.assertEqual(len({row[field] for row in rows}), 399)
        self.assertEqual(module.render(rows), (DIAGNOSTIC / 'actor_bulk_entries.h').read_text())

    def test_modified_specification_is_refused_before_output_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'spec.json'
            source.write_text('{"modules":[]}\n')
            output = root / 'entries.h'
            result = subprocess.run(['python3', '-B', str(DIAGNOSTIC / 'generate_bulk_capture.py'),
                str(source), str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('requires the frozen WW147A specification SHA-256', result.stderr)
            self.assertFalse(output.exists())

    def test_bulk_table_can_publish_all_399_private_captures(self):
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint32_t u32;
typedef unsigned char u8;
typedef struct {u8* data; size_t size; u32 module_id; const char* magic;} ShipCodeInput;
PUBLISH_FUNCTION
#define ACTOR_CAPTURE_MAX 399u
#define ACTOR_CAPTURE_BATCH "bulk-ww149a"
#include "actor_batch_capture.h"
int main(int argc, char** argv) {
    assert(argc == 2);
    assert(actor_batch_separate(argv[1], argv[0]));
    ActorCapture entries[] = {
#include "actor_bulk_entries.h"
    };
    const u32 count = sizeof(entries) / sizeof(entries[0]);
    assert(count == 399u);
    assert(!actor_batch_paths(entries, 400u, argv[1]));
    assert(actor_batch_paths(entries, count, argv[1]));
    const u32 omitted[] = {1,9,19,26,46,60,84,129,168,188,189,214,224,276,379,400};
    for (u32 i=0; i<sizeof(omitted)/sizeof(omitted[0]); ++i)
        assert(actor_batch_match(entries, count, omitted[i]) == NULL);
    for (u32 i=0; i<count; ++i) {
        assert(actor_batch_match(entries, count, entries[i].capture.module_id) == &entries[i].capture);
        entries[i].capture.size = 28u + entries[i].executable_size;
        entries[i].capture.data = calloc(1, entries[i].capture.size);
        assert(entries[i].capture.data);
        memcpy(entries[i].capture.data, entries[i].capture.magic, 8u);
    }
    assert(actor_batch_complete(entries, count));
    --entries[200].capture.size;
    assert(!actor_batch_publish(entries, count, argv[1]));
    assert(access(argv[1], F_OK) != 0);
    ++entries[200].capture.size;
    assert(actor_batch_publish(entries, count, argv[1]));
    assert(!actor_batch_publish(entries, count, argv[1]));
    for (u32 i=0; i<count; ++i) free(entries[i].capture.data);
    return 0;
}
'''.replace('PUBLISH_FUNCTION', publish))
            compiled = subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                '-I', str(DIAGNOSTIC), str(source), '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            output = root / 'captures'
            subprocess.run([str(root / 'probe'), str(output)], check=True)
            completion = json.loads((output / 'complete.json').read_text())
            self.assertEqual(completion['batch'], 'bulk-ww149a')
            self.assertEqual(len(completion['modules']), 399)
            self.assertEqual(len(list(output.iterdir())), 400)
            for row in completion['modules']:
                with self.subTest(module_id=row['id']):
                    data = (output / row['capture']).read_bytes()
                    self.assertEqual(data[:8], f"REL{row['id']:05d}".encode())
                    self.assertEqual(len(data), row['size'])


if __name__ == '__main__':
    unittest.main()
