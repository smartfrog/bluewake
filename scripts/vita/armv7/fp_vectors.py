"""Literal Gekko bit patterns. See docs/vita/VITA-WW-03.md for derivations."""

from pathlib import Path


def cases():
    result = []

    def add(name, pc, a, b, modes, expected, gqr=0, width=0, boundary=0):
        for mode, bits in zip(modes, expected, strict=True):
            result.append(dict(id=f"{name}-rn{mode & 3}-ni{mode >> 2}", pc=pc,
                               a=a, b=b, mode=mode, expected=f"{int(bits, 16):016x}",
                               gqr=gqr, width=width, boundary=boundary))

    rn = (0, 1, 2, 3)
    add("add-positive", 0x80007000, "3ff0000000000000", "3c90000000000000", rn,
        ("3ff0000000000000", "3ff0000000000000", "3ff0000000000001", "3ff0000000000000"))
    add("add-negative", 0x80007000, "bff0000000000000", "bc90000000000000", rn,
        ("bff0000000000000", "bff0000000000000", "bff0000000000000", "bff0000000000001"))
    for sign, prefix in (("positive", 0), ("negative", 0x8000000000000000)):
        for parity, source, rounded in (
                ("even", 0x3ff0000010000000,
                 (0x3ff0000000000000, 0x3ff0000000000000, 0x3ff0000020000000, 0x3ff0000000000000)),
                ("odd", 0x3ff0000030000000,
                 (0x3ff0000040000000, 0x3ff0000020000000, 0x3ff0000040000000, 0x3ff0000020000000))):
            ordered = rounded if not prefix else (rounded[0], rounded[1], rounded[3], rounded[2])
            add(f"frsp-{sign}-{parity}", 0x80007020, f"{source | prefix:016x}", "0", rn,
                tuple(f"{bits | prefix:016x}" for bits in ordered))
        for name, pc, source, multiplier, gradual in (
                ("double-input", 0x80007040, 0x0000000000000001, "4000000000000000", 0x2),
                ("double-underflow", 0x80007040, 0x0010000000000000, "3fe0000000000000", 0x0008000000000000),
                ("single-underflow", 0x80007060, 0x3810000000000000, "3fe0000000000000", 0x3800000000000000)):
            add(f"{name}-{sign}", pc, f"{source | prefix:016x}", multiplier, (0, 4),
                (f"{gradual | prefix:016x}", f"{prefix:016x}"))

    for name, operand, gqr, width, expected in (
            ("quant-s8-fraction-positive", "3ffc000000000000", 6, 1, "01"),
            ("quant-s8-fraction-negative", "bffc000000000000", 6, 1, "ff"),
            ("quant-u8-upper", "406ff00000000000", 4, 1, "ff"),
            ("quant-u8-negative", "bfe0000000000000", 4, 1, "00"),
            ("quant-s8-upper", "4060000000000000", 6, 1, "7f"),
            ("quant-s8-lower", "c060200000000000", 6, 1, "80"),
            ("quant-s16-scale-upper", "40c0000000000000", 0x0207, 2, "7fff"),
            ("quant-u16-scale-interior", "40ffffd000000000", 0x3f05, 2, "fffe")):
        add(name, 0x80007080, operand, "0", rn, (expected,) * 4, gqr, width)

    for name, pc, boundary in (("direct", 0x80007100, 0), ("indirect", 0x80007120, 0),
                               ("host-boundary", 0x80007100, 1)):
        add(name, pc, "3ff0000000000000", "3c90000000000000", (2,),
            ("3ff0000000000001",), boundary=boundary)
    return result


def write_header(path):
    rows = []
    for case in cases():
        rows.append('    {"%s", 0x%xu, 0x%sull, 0x%sull, 0x%sull, %du, %du, %du, %du},' %
                    (case["id"], case["pc"], case["a"], case["b"], case["expected"],
                     case["mode"], case["gqr"], case["width"], case["boundary"]))
    Path(path).write_text("static const FpCase fp_cases[] = {\n" + "\n".join(rows) + "\n};\n")
