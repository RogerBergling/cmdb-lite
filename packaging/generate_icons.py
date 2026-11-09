#!/usr/bin/env python3
"""Generate the cmdb-lite application icon without external dependencies."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WINDOWS_ICON = ROOT / "windows" / "cmdb-lite.ico"
LINUX_ICON = ROOT / "icons" / "cmdb-lite.png"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def rounded_mask(x: int, y: int, size: int, radius: int) -> bool:
    if x < 0 or y < 0 or x >= size or y >= size:
        return False
    if x < radius and y < radius:
        return (x - radius) ** 2 + (y - radius) ** 2 <= radius ** 2
    if x >= size - radius and y < radius:
        return (x - (size - radius - 1)) ** 2 + (y - radius) ** 2 <= radius ** 2
    if x < radius and y >= size - radius:
        return (x - radius) ** 2 + (y - (size - radius - 1)) ** 2 <= radius ** 2
    if x >= size - radius and y >= size - radius:
        return ((x - (size - radius - 1)) ** 2 +
                (y - (size - radius - 1)) ** 2 <= radius ** 2)
    return True


def inside_bar(x: int, y: int, x0: int, y0: int, x1: int, y1: int) -> bool:
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    radius = max(1, (y1 - y0) // 2)
    if x < x0 + radius and y < y0 + radius:
        return (x - (x0 + radius)) ** 2 + (y - (y0 + radius)) ** 2 <= radius ** 2
    if x > x1 - radius and y < y0 + radius:
        return (x - (x1 - radius)) ** 2 + (y - (y0 + radius)) ** 2 <= radius ** 2
    if x < x0 + radius and y > y1 - radius:
        return (x - (x0 + radius)) ** 2 + (y - (y1 - radius)) ** 2 <= radius ** 2
    if x > x1 - radius and y > y1 - radius:
        return (x - (x1 - radius)) ** 2 + (y - (y1 - radius)) ** 2 <= radius ** 2
    return True


def render(size: int) -> bytes:
    margin = max(1, round(size * 0.035))
    radius = max(3, round(size * 0.2))
    bar_left = round(size * 0.25)
    bar_right = round(size * 0.75)
    bar_height = max(2, round(size * 0.12))
    bar_radius = max(1, bar_height // 2)
    bar_ys = (
        round(size * 0.27),
        round(size * 0.44),
        round(size * 0.61),
    )

    rows = []
    for y in range(size):
        row = bytearray([0])  # PNG filter type 0
        for x in range(size):
            # gradient teal -> blue
            t = (x + y) / max(1, 2 * (size - 1))
            r = round(45 * (1 - t) + 37 * t)
            g = round(212 * (1 - t) + 99 * t)
            b = round(191 * (1 - t) + 235 * t)
            a = 255
            if not rounded_mask(x, y, size, radius):
                a = 0
            else:
                for bar_y in bar_ys:
                    if inside_bar(x, y, bar_left, bar_y,
                                  bar_right, bar_y + bar_height):
                        r = g = b = 255
                        break
            row.extend((r, g, b, a))
        rows.append(bytes(row))
    return b"".join(rows)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def png_bytes(size: int) -> bytes:
    raw = render(size)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" +
            png_chunk(b"IHDR", ihdr) +
            png_chunk(b"IDAT", zlib.compress(raw, 9)) +
            png_chunk(b"IEND", b""))


def write_ico(path: Path) -> None:
    images = [(size, png_bytes(size)) for size in SIZES]
    offset = 6 + 16 * len(images)
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, data in images:
        width = 0 if size >= 256 else size
        directory.extend(struct.pack(
            "<BBBBHHII", width, width, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(directory) + b"".join(data for _, data in images))


def main() -> None:
    write_ico(WINDOWS_ICON)
    LINUX_ICON.parent.mkdir(parents=True, exist_ok=True)
    LINUX_ICON.write_bytes(png_bytes(256))
    print(f"wrote {WINDOWS_ICON}")
    print(f"wrote {LINUX_ICON}")


if __name__ == "__main__":
    main()
