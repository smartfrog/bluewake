"""Authorize only the exact external vector observed in the private guest trial."""

import hashlib
import subprocess

from dol_range import identified_dol_range

TEMPLATE_SOURCE = 0x80301D8C
VECTOR_BYTES = 152
TEMPLATE_SHA256 = '9fd8348984aa801eafe743d52f99e803c6bf5bd5cfc620102f9fa832a1dd95a0'
OBSERVED_SHA256 = '959cdcd88e3425871362441b5b3a335accea0263466a3316d38be5d09a5a6f22'


def extract_external_vector(dol, observed):
    if len(observed) != VECTOR_BYTES or hashlib.sha256(observed).hexdigest() != OBSERVED_SHA256:
        raise ValueError('external vector identity differs from the observed program')
    source = identified_dol_range(dol, TEMPLATE_SOURCE, VECTOR_BYTES, TEMPLATE_SHA256)
    if (source[:104] != observed[:104] or source[108:] != observed[108:] or
            source[104:108] != bytes.fromhex('38600000') or observed[104:108] != bytes.fromhex('38600004')):
        raise ValueError('external vector lineage requires only the observed exception-number change')
    return bytes(observed)


def emit_external_vector(dol, observed, emitter, output):
    code = extract_external_vector(dol, observed)
    binary = output.with_suffix('.bin')
    binary.write_bytes(code)
    result = subprocess.run([str(emitter), str(binary)], check=True, capture_output=True)
    output.write_bytes(result.stdout)
