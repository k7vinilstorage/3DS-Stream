#!/usr/bin/env python3
"""Turns the probe's MVD dumps (sdmc:/3ds/3ds-stream/mvd_dump_N.bin) into PNGs.

    python3 tools/dump2png.py mvd_dump_0.bin [...]

Each dump is the raw MVD output buffer after a render: RGB565, little-endian.
The probe reads it as 400x240 rows; if the image looks wrong, the other
layouts written next to it (240x400, BGR) show what MVD really wrote.
Needs Pillow.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

W, H = 400, 240


def rgb565(data: bytes, w: int, h: int, bgr: bool) -> Image.Image:
    px = struct.unpack_from(f"<{w * h}H", data)
    out = bytearray()
    for p in px:
        r, g, b = (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F
        if bgr:
            r, b = b, r
        out += bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    return Image.frombytes("RGB", (w, h), bytes(out))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for name in sys.argv[1:]:
        path = Path(name)
        data = path.read_bytes()
        if len(data) < W * H * 2:
            print(f"{path}: {len(data)} bytes, expected {W * H * 2}")
            continue
        variants = {"": (W, H, False), "_bgr": (W, H, True), "_240x400": (H, W, False)}
        for suffix, (w, h, bgr) in variants.items():
            out = path.with_name(path.stem + suffix + ".png")
            rgb565(data, w, h, bgr).save(out)
            print(out)


if __name__ == "__main__":
    main()
