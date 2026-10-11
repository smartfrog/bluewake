"""Identify the documented init program without publishing its private bytes."""

from dol_range import identified_dol_range

INIT_UCODE_SOURCE = 0x8039F5B0
INIT_UCODE_BYTES = 128
INIT_UCODE_SHA256 = '5b873ee91f33b0884574b814d7f46d81e283394de91b8b2ea9073ac218dee350'


def extract_init_ucode(dol):
    return identified_dol_range(dol, INIT_UCODE_SOURCE, INIT_UCODE_BYTES, INIT_UCODE_SHA256)
