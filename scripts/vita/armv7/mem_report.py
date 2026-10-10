"""Strict, versioned memory ledger. Unknown measurements never become zero observations."""

import re

REAL_SIZES = dict(mem1=25165824, vm=33554432, cache=16384, alias=64)
SDK_SIZES = dict(user=4096, cdram=262144, phycont=1048576)
SDK_TYPES = dict(user=0x0C20D060, cdram=0x09408060, phycont=0x0C80D060)
UNKNOWN = "UNKNOWN native_highwater largest_block physical_limits residency game_peak"
VITA_ABI = (4, 3496, 6, 3428, 3436)
MEMBLOCK_INFO_SIZE = 0x18


def touched_bytes(size):
    return 64 if size == 64 else ((size + 4095) // 4096 + 1) * 64


def validate(text, token, source, package, expected_platform=None):
    lines = iter(text.splitlines())

    def match(pattern):
        found = re.fullmatch(pattern, next(lines, ""))
        if found is None:
            raise ValueError("malformed, truncated or out-of-order memory report")
        return found.groups()

    if not text.endswith("\n"):
        raise ValueError("truncated memory report")
    platform, = match(re.escape(f"MEM {token} {source} {package} method=bounded-v2 phase=guest platform=") + r"(HOST|VITA)")
    if expected_platform and platform != expected_platform:
        raise ValueError("memory report platform mismatch")
    pointer, cpu, version, ram, external = map(int, match(
        r"ABI pointer=([48]) cpu=(\d+) version=(\d+) ram_offset=(\d+) external_offset=(\d+)"))
    if (platform == "VITA" and (pointer, cpu, version, ram, external) != VITA_ABI) or not 0 < cpu < 262144 or version != 6 or max(ram, external) + pointer > cpu:
        raise ValueError("invalid CPU ABI")
    match(re.escape(f"RESERVED heap=100663296 worker=262144 cpu_subset_worker={cpu}"))
    match(re.escape(UNKNOWN))

    def owner(name, size):
        requested, mapped, touched, views = map(int, match(
            rf"OWNER {name} requested=(\d+) mapped=(\d+) touched=(\d+) views=(\d+)"))
        if requested != size or mapped not in (0, size) or touched > min(mapped, touched_bytes(size)) or touched % 64 or views > (2 if name == "alias" else 1) or (mapped == 0 and views != 0):
            raise ValueError("invalid backing ledger")
        return mapped == size and touched == touched_bytes(size) and views == (2 if name == "alias" else 1), mapped

    owners = [owner(name, size) for name, size in REAL_SIZES.items()]

    def snapshot(name):
        available, result, size, user, cdram, phycont, valid = match(
            rf"SNAP {name} available=([01]) result=(UNKNOWN|-?\d+) size=(UNKNOWN|-?\d+) "
            r"user=(UNKNOWN|-?\d+) cdram=(UNKNOWN|-?\d+) phycont=(UNKNOWN|-?\d+) valid=(UNKNOWN|[01])")
        values = (result, size, user, cdram, phycont, valid)
        if available == "0":
            if any(value != "UNKNOWN" for value in values):
                raise ValueError("unavailable pool observation is not UNKNOWN")
            return platform == "HOST"
        if platform == "HOST" or any(value == "UNKNOWN" for value in values):
            raise ValueError("false host/partial target snapshot")
        result, size, user, cdram, phycont, valid = map(int, values)
        correct = result >= 0 and size == 16 and min(user, cdram, phycont) >= 0
        if valid != int(correct):
            raise ValueError("dishonest raw snapshot validity")
        return correct

    snapshots = [snapshot("real-" + phase) for phase in ("before", "live", "after")]
    peak, live, pattern, alias, bounds, real_verdict = match(
        r"REAL peak=(\d+) live=(\d+) pattern=([01]) alias=([01]) bounds=([01]) (PASS|FAIL)")
    peak, live = int(peak), int(live)
    if peak != sum(mapped for _, mapped in owners) or live != 0:
        raise ValueError("inconsistent real backing peak/cleanup")
    real_ok = all(ok for ok, _ in owners) and all(snapshots) and (pattern, alias, bounds) == ("1", "1", "1")
    if real_verdict != ("PASS" if real_ok else "FAIL"):
        raise ValueError("dishonest real scenario verdict")
    blocks = []
    for name, expected in SDK_SIZES.items():
        size, mapped, touched, views, available, uid, base, info, info_size, info_required, freed, access, block_type, pattern, verdict = match(
            rf"BLOCK {name} requested=(\d+) mapped=(\d+) touched=(\d+) views=([01]) available=([01]) "
            r"uid=(UNKNOWN|-?\d+) base=(UNKNOWN|-?\d+) info=(UNKNOWN|-?\d+) info_size=(UNKNOWN|\d+) info_required=(UNKNOWN|\d+) free=(UNKNOWN|-?\d+) access=(UNKNOWN|\d+) type=(UNKNOWN|\d+) pattern=([01]) (PASS|FAIL|SKIP)")
        size, mapped, touched = int(size), int(mapped), int(touched)
        if size != expected or mapped > 0xffffffff or touched > touched_bytes(expected) or touched % 64:
            raise ValueError("invalid SDK ledger")
        observations = [snapshot(name + "-" + phase) for phase in ("before", "live", "after")]
        if available == "0":
            if (mapped, touched, views, uid, base, info, info_size, info_required, freed, access, block_type, pattern, verdict) != (0, 0, "0", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "UNKNOWN", "0", "SKIP") or platform != "HOST":
                raise ValueError("dishonest SDK skip")
            blocks.append((True, 0, 0))
            continue
        if platform != "VITA" or "UNKNOWN" in (uid, base, info, info_size, info_required, freed, access, block_type):
            raise ValueError("invalid SDK target identity")
        if int(info_required) != MEMBLOCK_INFO_SIZE or int(info_size) > 0xffffffff:
            raise ValueError("invalid SDK block-info structure size")
        uid, base, info, freed = map(int, (uid, base, info, freed))
        ok = uid >= 0 and min(base, info, freed) >= 0 and int(info_size) == MEMBLOCK_INFO_SIZE and mapped == expected and touched == touched_bytes(expected) and views == pattern == "1" and int(access) & 6 == 6 and int(block_type) == SDK_TYPES[name] and all(observations)
        if verdict != ("PASS" if ok else "FAIL"):
            raise ValueError("dishonest SDK block verdict")
        blocks.append((ok, expected if uid >= 0 else 0, expected if uid >= 0 and freed < 0 else 0))
    peak, max_blocks, live, sdk_verdict = match(r"SDK peak=(\d+) max_blocks=([01]) live=(\d+) (PASS|FAIL|SKIP)")
    expected_peak = max(allocated for _, allocated, _ in blocks)
    if (int(peak), int(max_blocks), int(live)) != (expected_peak, int(expected_peak > 0), sum(live for _, _, live in blocks)):
        raise ValueError("inconsistent SDK peak/cleanup")
    expected_sdk = "SKIP" if platform == "HOST" else ("PASS" if all(ok for ok, _, _ in blocks) else "FAIL")
    if sdk_verdict != expected_sdk:
        raise ValueError("dishonest SDK scenario verdict")
    failures = int(not real_ok) + int(expected_sdk == "FAIL")
    match(re.escape(f"END real={real_verdict} sdk={sdk_verdict} failures={failures}"))
    if next(lines, None) is not None:
        raise ValueError("excess memory report lines")
    return int(failures != 0)
