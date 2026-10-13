"""Separate new framebuffer candidates from the verified red boot-logo envelope."""

from pathlib import Path

import image


def validate_pixels(width, height, pixels):
    if (type(width) is not int or type(height) is not int or width <= 0 or height <= 0 or
            len(pixels) != width * height * 4):
        raise ValueError('visual comparison requires complete RGBA8 pixels')


def logo_reference(width, height, pixels):
    validate_pixels(width, height, pixels)
    if any(pixels[1::4]) or any(pixels[2::4]) or not any(pixels[0::4]):
        raise ValueError('reference must contain a visible red logo on black')
    return dict(width=width, height=height, mask=bytes(bool(red) for red in pixels[0::4]),
                red_max=max(pixels[0::4]))


def read_pixels(path):
    width, height, depth, color, pixels = image.parse_png(path)
    if (depth, color) != (8, 6):
        raise ValueError('visual comparison requires complete RGBA8 pixels')
    validate_pixels(width, height, pixels)
    return width, height, pixels


def load_logo(path):
    path = Path(path).resolve()
    reference = logo_reference(*read_pixels(path))
    reference.update(path=str(path), sha256=image.launch.sha256(path))
    return reference


def classify_pixels(width, height, pixels, reference):
    validate_pixels(width, height, pixels)
    nonblack = sum(bool(pixels[n] or pixels[n + 1] or pixels[n + 2])
                   for n in range(0, len(pixels), 4))
    result = dict(classification='black', nonblack_RGB_pixels=nonblack)
    if not nonblack:
        return result
    if (width, height) != (reference['width'], reference['height']):
        return dict(result, classification='new-content-candidate', reason='dimensions-changed')
    outside = non_red = brighter = 0
    for n, known in enumerate(reference['mask']):
        red, green, blue = pixels[n * 4:n * 4 + 3]
        outside += bool((red or green or blue) and not known)
        non_red += bool(green or blue)
        brighter += red > reference['red_max']
    result.update(outside_logo_pixels=outside, non_red_pixels=non_red,
                  brighter_red_pixels=brighter,
                  classification='new-content-candidate' if outside or non_red or brighter else 'logo-compatible')
    return result


def classify_frame(path, expected_sha256, reference):
    if image.launch.sha256(path) != expected_sha256:
        raise ValueError('visual comparison PNG SHA-256 mismatch')
    result = classify_pixels(*read_pixels(path), reference)
    result['reference_sha256'] = reference['sha256']
    return result
