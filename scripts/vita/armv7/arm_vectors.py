"""Independent ARM scalar oracle; no runtime or emulator-derived expectations."""

from pathlib import Path

PROVENANCE = "ARM DDI 0408F table 2-5; ARM DDI 0406C.d A2.7.5 and A8.8.310; IEEE binary rounding"
METHOD = "scalar-vfp-v1"
TITLE = "BWWA00008"


def cases():
    result = []

    def add(name, op, a, b, modes, expected):
        for (rn, fz), bits in zip(modes, expected, strict=True):
            result.append(dict(id=f"{name}-rn{rn}-fz{fz}", op=op, a=f"{a:016x}", b=f"{b:016x}",
                               rn=rn, fz=fz, expected=f"{bits:016x}"))

    rn = ((0, 0), (1, 0), (2, 0), (3, 0))
    add("vadd-positive", "add", 0x3ff0000000000000, 0x3c90000000000000, rn,
        (0x3ff0000000000000, 0x3ff0000000000000, 0x3ff0000000000001, 0x3ff0000000000000))
    add("vadd-negative", "add", 0xbff0000000000000, 0xbc90000000000000, rn,
        (0xbff0000000000000, 0xbff0000000000000, 0xbff0000000000000, 0xbff0000000000001))
    for name, a, expected in (
            ("vcvt-positive-even", 0x3ff0000010000000,
             (0x3ff0000000000000, 0x3ff0000000000000, 0x3ff0000020000000, 0x3ff0000000000000)),
            ("vcvt-positive-odd", 0x3ff0000030000000,
             (0x3ff0000040000000, 0x3ff0000020000000, 0x3ff0000040000000, 0x3ff0000020000000)),
            ("vcvt-negative-even", 0xbff0000010000000,
             (0xbff0000000000000, 0xbff0000000000000, 0xbff0000000000000, 0xbff0000020000000)),
            ("vcvt-negative-odd", 0xbff0000030000000,
             (0xbff0000040000000, 0xbff0000020000000, 0xbff0000020000000, 0xbff0000040000000))):
        add(name, "convert", a, 0, rn, expected)
    for name, a, b, expected in (
            ("vmul-input-positive", 0x1, 0x4000000000000000, (0x2, 0x0)),
            ("vmul-output-positive", 0x0010000000000000, 0x3fe0000000000000, (0x0008000000000000, 0x0)),
            ("vmul-input-negative", 0x8000000000000001, 0x4000000000000000,
             (0x8000000000000002, 0x8000000000000000)),
            ("vmul-output-negative", 0x8010000000000000, 0x3fe0000000000000,
             (0x8008000000000000, 0x8000000000000000))):
        add(name, "mul", a, b, ((0, 0), (0, 1)), expected)
    return result


def write_header(path):
    operations = {"add": 0, "mul": 1, "convert": 2}
    rows = ['    {"%s", "%s", %du, %du, %du, 0x%sull, 0x%sull, 0x%sull},' %
            (case["id"], case["op"], operations[case["op"]], case["rn"], case["fz"],
             case["a"], case["b"], case["expected"]) for case in cases()]
    Path(path).write_text("static const ArmCase arm_cases[] = {\n" + "\n".join(rows) + "\n};\n")
