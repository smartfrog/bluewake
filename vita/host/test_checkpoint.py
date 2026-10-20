import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build-f5/host-tests'
RUNTIME = ROOT / 'ref/vita-ww-01/RecompCore/GXRuntime'


class CheckpointTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        sources = ['src/core/cpu.c', 'src/core/cpu_exception.c', 'src/core/cpu_interpreter.c',
                   'src/core/cpu_interpreter_table.c', 'src/core/cpu_interpreter_float.c',
                   'src/core/cpu_interpreter_integer.c', 'src/guest_memory.c',
                   'src/guest_memory_dirty.c', 'src/gx_recomp.c', 'src/gx_recomp_parser.c']
        command = [os.environ['CC'], '-O1', '-ffp-contract=off', '-I', str(RUNTIME / 'include'),
                   '-I', str(ROOT / 'vita/host'), str(ROOT / 'vita/host/checkpoint_probe.c'),
                   str(ROOT / 'vita/host/gx_checkpoint.c'),
                   *[str(RUNTIME / source) for source in sources], '-lm', '-o', str(OUT / 'checkpoint')]
        subprocess.run(command, check=True)

    def test_fragment_is_not_replayed_until_complete(self):
        subprocess.run([str(OUT / 'checkpoint'), 'fragment'], check=True)

    def test_unknown_opcode_fails_without_padding(self):
        subprocess.run([str(OUT / 'checkpoint'), 'unknown'], check=True)

    def test_first_command_latches_and_counts_unprocessed_suffix(self):
        subprocess.run([str(OUT / 'checkpoint'), 'suffix'], check=True)

    def test_oversized_xf_is_rejected_before_parser(self):
        subprocess.run([str(OUT / 'checkpoint'), 'xf-count'], check=True)

    def test_cp_frame_uses_big_endian_full_command(self):
        subprocess.run([str(OUT / 'checkpoint'), 'cp'], check=True)

    def test_parser_failure_does_not_claim_frontier(self):
        subprocess.run([str(OUT / 'checkpoint'), 'parser-error'], check=True)


if __name__ == '__main__':
    unittest.main()
