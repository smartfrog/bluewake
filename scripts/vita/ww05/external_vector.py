"""Authorize only the exact exception vectors observed in private guest trials."""

import hashlib
import subprocess

from dol_range import identified_dol_range

TEMPLATE_SOURCE = 0x80301D8C
VECTOR_BYTES = 152
TEMPLATE_SHA256 = '9fd8348984aa801eafe743d52f99e803c6bf5bd5cfc620102f9fa832a1dd95a0'
OBSERVED_SHA256 = '959cdcd88e3425871362441b5b3a335accea0263466a3316d38be5d09a5a6f22'
FP_OBSERVED_SHA256 = 'a7537905d3a5cdeffa646f393bea89c8bcd48df2340ef1bd2a32c9da04904ae5'
DECREMENTER_OBSERVED_SHA256 = 'bc97b5f351867639d7c79721c1ed87d8670e4ac1ae741d89ef1782906f00e352'


def identified_exception_vector(dol, observed, identity, exception_number):
    if len(observed) != VECTOR_BYTES or hashlib.sha256(observed).hexdigest() != identity:
        raise ValueError('exception vector identity differs from the observed program')
    source = identified_dol_range(dol, TEMPLATE_SOURCE, VECTOR_BYTES, TEMPLATE_SHA256)
    if (source[:104] != observed[:104] or source[108:] != observed[108:] or
            source[104:108] != bytes.fromhex('38600000') or
            observed[104:108] != (0x38600000 | exception_number).to_bytes(4, 'big')):
        raise ValueError('exception vector lineage requires only the observed exception-number change')
    return bytes(observed)


def extract_external_vector(dol, observed):
    return identified_exception_vector(dol, observed, OBSERVED_SHA256, 4)


def extract_fp_vector(dol, observed):
    return identified_exception_vector(dol, observed, FP_OBSERVED_SHA256, 7)


def extract_decrementer_vector(dol, observed):
    return identified_exception_vector(dol, observed, DECREMENTER_OBSERVED_SHA256, 8)


def emit_vector(code, emitter, output, vector):
    binary = output.with_suffix('.bin')
    binary.write_bytes(code)
    command = [str(emitter), str(binary)]
    if vector != 0x500:
        command.append(f'{vector:08X}')
    result = subprocess.run(command, check=True, capture_output=True)
    output.write_bytes(result.stdout)


def emit_external_vector(dol, observed, emitter, output):
    emit_vector(extract_external_vector(dol, observed), emitter, output, 0x500)


def emit_fp_vector(dol, observed, emitter, output):
    emit_vector(extract_fp_vector(dol, observed), emitter, output, 0x800)


def emit_decrementer_vector(dol, observed, emitter, output):
    emit_vector(extract_decrementer_vector(dol, observed), emitter, output, 0x900)
