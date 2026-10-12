"""VITA-WW-15: parse what the WW05 boot host captures from the GX gather pipe.

Three capture shapes are accepted, each with well-formed refusal (ValueError):

- the report's GX_FRONTIER record (the first complete GX command as hex);
- gather-pipe batch log lines (`[gather-pipe] bytes=<n> hex=<HEX>`), the
  documented trace shape for the host's BwGatherPipeBytes callback;
- a raw binary FIFO dump (exactly the callback's bytes).

An empty capture (no GX command yet) and a partial capture (the host stopped
mid-command) are honest states, not errors. Only malformed evidence is
refused. The framing table mirrors the host's gx_checkpoint.c frame().
"""

import re
from dataclasses import dataclass

FRONTIER_RE = re.compile(
    r'GX_FRONTIER complete=([01]) opcode=([0-9A-F]{2}) bytes=(\d+) '
    r'pc=([0-9A-F]{8}) completion_pc=([0-9A-F]{8}) sp=([0-9A-F]{8}) '
    r'trailing=(\d+) edge_stop=([01]) hex=([0-9A-F]*)')
GATHER_RE = re.compile(r'\[gather-pipe\] bytes=(\d+) hex=([0-9A-F]*)')

FIXED_FRAME = {0x00: 1, 0x48: 1, 0x08: 6, 0x61: 5, 0x20: 5, 0x28: 5,
               0x30: 5, 0x38: 5, 0x40: 9}


@dataclass(frozen=True)
class Capture:
    """The host's GX frontier: one command, a prefix of one, or nothing yet."""

    complete: bool
    opcode: int
    size: int
    pc: int
    completion_pc: int
    sp: int
    trailing: int
    edge_stop: bool
    data: bytes
    source: str = ""

    @property
    def partial(self):
        return not self.complete and bool(self.data)


def required_length(data):
    """The framed length of the command starting at data[0].

    None: unsupported first opcode (the host refuses it too). 'overflow': an
    XF block whose count exceeds the pinned parser's u8 capacity.
    """
    if not data:
        return 0
    opcode = data[0]
    if opcode in FIXED_FRAME:
        return FIXED_FRAME[opcode]
    if opcode == 0x10:
        if len(data) < 5:
            return None
        count = (int.from_bytes(data[1:5], 'big') >> 16) & 0xFFFF
        if count > 255:
            return 'overflow'
        return 5 + (count + 1) * 4
    return None


def parse_report(text, source=""):
    """Parse the GX_FRONTIER record of a WW05 report (or a log carrying it)."""
    matches = [match for line in text.splitlines()
               if (match := FRONTIER_RE.fullmatch(line.strip()))]
    if not matches:
        raise ValueError('report has no GX_FRONTIER record')
    if len(matches) > 1:
        raise ValueError('report has more than one GX_FRONTIER record')
    complete, opcode, size, pc, completion_pc, sp, trailing, edge_stop, raw = matches[0].groups()
    if len(raw) % 2:
        raise ValueError('GX_FRONTIER hex has an odd digit count')
    data = bytes.fromhex(raw)
    if len(data) != int(size):
        raise ValueError('GX_FRONTIER byte count does not match its hex')
    if data and data[0] != int(opcode, 16):
        raise ValueError('GX_FRONTIER opcode is not the first captured byte')
    framed = required_length(data)
    if complete == '1':
        if not data:
            raise ValueError('complete GX_FRONTIER without bytes')
        if framed == 'overflow':
            raise ValueError('complete GX_FRONTIER exceeds the pinned XF capacity')
        if framed is None:
            raise ValueError('complete GX_FRONTIER has an unsupported first opcode')
        if len(data) != framed:
            raise ValueError('complete GX_FRONTIER is not one framed command')
    else:
        if not data and int(opcode, 16) != 0:
            raise ValueError('empty GX_FRONTIER with a nonzero opcode')
        if data and isinstance(framed, int) and len(data) >= framed:
            raise ValueError('incomplete GX_FRONTIER already carries a full frame')
    return Capture(complete=complete == '1', opcode=int(opcode, 16),
                   size=len(data), pc=int(pc, 16), completion_pc=int(completion_pc, 16),
                   sp=int(sp, 16), trailing=int(trailing), edge_stop=edge_stop == '1',
                   data=data, source=source)


def parse_gather_log(text, source=""):
    """Concatenate every gather-pipe batch in a host log, in stream order."""
    stream = bytearray()
    for line in text.splitlines():
        line = line.strip()
        match = GATHER_RE.fullmatch(line)
        if match is None:
            if line.startswith('[gather-pipe]'):
                raise ValueError('malformed gather-pipe record: ' + line)
            continue
        size, raw = match.groups()
        if int(size) == 0:
            raise ValueError('empty gather-pipe batch cannot exist')
        if len(raw) != 2 * int(size):
            raise ValueError('gather-pipe byte count does not match its hex')
        stream += bytes.fromhex(raw)
    return bytes(stream)


def classify_capture(data, source=""):
    """Classify one capture file's bytes.

    Returns (kind, payload): 'report' carries a Capture, 'gather' and
    'binary' the FIFO bytes. Text with no capture markers is refused; bytes
    that do not decode as text are taken as a raw FIFO dump.
    """
    if b'GX_FRONTIER ' in data:
        return 'report', parse_report(data.decode(), source=source)
    if b'[gather-pipe]' in data:
        return 'gather', parse_gather_log(data.decode(), source=source)
    try:
        text = data.decode()
    except UnicodeDecodeError:
        return 'binary', data
    if all(character.isprintable() or character.isspace() for character in text):
        raise ValueError('capture has no GX_FRONTIER record and no gather-pipe batches')
    return 'binary', data


def read_capture(path):
    """Read a capture file: a WW05 report, a host log, or a raw FIFO dump."""
    return classify_capture(path.read_bytes(), source=str(path))
