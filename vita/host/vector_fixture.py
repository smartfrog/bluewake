import struct


def external_vector_fixture(exception_number=4):
    words = [
        0x7C9043A6, 0x808000C0, 0x9064000C, 0x7C7042A6, 0x90640010,
        0x7CBA02A6, 0x90A40198, 0x7CBB02A6, 0x90A4019C, 0x38600000 | exception_number,
        0x80A03010, 0x7CBA03A6, 0x7CA000A6, 0x60A50030, 0x7CBB03A6, 0x4C000064,
    ]
    words.extend([0x60000000] * (38 - len(words)))
    return struct.pack('>38I', *words)
