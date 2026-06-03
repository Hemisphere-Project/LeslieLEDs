#!/usr/bin/env python3
"""Generate the LeslieLEDs macOS app icon assets.

Creates a simple pixel-art icon: black square background with a white,
8-bit-style "L" and writes both PNG previews and a macOS .icns bundle.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent
ASSET_DIR = ROOT / "icon-assets"
ICONSET_DIR = ASSET_DIR / "LeslieLEDs.iconset"
PREVIEW_PATH = ASSET_DIR / "LeslieLEDs-preview-512.png"
ICNS_PATH = ROOT / "LeslieLEDs.app" / "Contents" / "Resources" / "LeslieLEDs.icns"

BASE_ICON = (
    "................",
    "................",
    "....###.........",
    "....###.........",
    "....###.........",
    "....###.........",
    "....###.........",
    "....###.........",
    "....###.........",
    "....###.........",
    "....###.........",
    "....########....",
    "....########....",
    "................",
    "................",
    "................",
)

ICONSET_SIZES = {
    "icon_16x16.png": 16,
    "icon_16x16@2x.png": 32,
    "icon_32x32.png": 32,
    "icon_32x32@2x.png": 64,
    "icon_128x128.png": 128,
    "icon_128x128@2x.png": 256,
    "icon_256x256.png": 256,
    "icon_256x256@2x.png": 512,
    "icon_512x512.png": 512,
    "icon_512x512@2x.png": 1024,
}

ICNS_CHUNKS = (
    (16, b"icp4"),
    (32, b"icp5"),
    (64, b"icp6"),
    (128, b"ic07"),
    (256, b"ic08"),
    (512, b"ic09"),
    (1024, b"ic10"),
)

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def _render_rgba(size: int) -> bytes:
    src_h = len(BASE_ICON)
    src_w = len(BASE_ICON[0])
    pixels = bytearray()
    for y in range(size):
        sy = y * src_h // size
        row = BASE_ICON[sy]
        for x in range(size):
            sx = x * src_w // size
            if row[sx] == "#":
                pixels.extend((255, 255, 255, 255))
            else:
                pixels.extend((0, 0, 0, 255))
    return bytes(pixels)


def _encode_png(size: int) -> bytes:
    rgba = _render_rgba(size)
    scanlines = bytearray()
    row_bytes = size * 4
    for y in range(size):
        start = y * row_bytes
        scanlines.append(0)
        scanlines.extend(rgba[start:start + row_bytes])

    return b"".join(
        (
            PNG_SIGNATURE,
            _chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)),
            _chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9)),
            _chunk(b"IEND", b""),
        )
    )


def _write_iconset(pngs: dict[int, bytes]):
    ICONSET_DIR.mkdir(parents=True, exist_ok=True)
    for name, size in ICONSET_SIZES.items():
        (ICONSET_DIR / name).write_bytes(pngs[size])


def _write_preview(pngs: dict[int, bytes]):
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    PREVIEW_PATH.write_bytes(pngs[512])


def _write_icns(pngs: dict[int, bytes]):
    ICNS_PATH.parent.mkdir(parents=True, exist_ok=True)
    elements = []
    for size, icon_type in ICNS_CHUNKS:
        payload = pngs[size]
        elements.append(icon_type + struct.pack(">I", len(payload) + 8) + payload)
    body = b"".join(elements)
    ICNS_PATH.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)


def main() -> int:
    pngs = {size: _encode_png(size) for size, _ in ICNS_CHUNKS}
    _write_iconset(pngs)
    _write_preview(pngs)
    _write_icns(pngs)
    print(f"Wrote preview: {PREVIEW_PATH}")
    print(f"Wrote iconset: {ICONSET_DIR}")
    print(f"Wrote app icon: {ICNS_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())