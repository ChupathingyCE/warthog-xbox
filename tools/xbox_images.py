#!/usr/bin/env python3
"""The dashboard's pictures of the game: port/xbox/art/title-image.png (128x128)
and save-image.png (64x64) as the XPR0 files the dashboard reads
(TitleImage.xbx and SaveImage.xbx, DXT1), which cxbe embeds as the XBE's
$$XTIMAGE and $$XSIMAGE sections (port/xbox/build.py). Neither section is
loaded with the game, so they cost the console no memory.

    python3 tools/xbox_images.py            writes port/xbox/art/*.xbx

The .xbx files are kept in the repository, so a build needs no image
tools; run this after changing a PNG. Python only: the PNG reader below
takes the 8-bit RGB and RGBA PNGs the art is saved as.

The art is ChupathingyCE's own icon (the helmet in red, with tusks), the
same as the other ports' app icons."""

import struct
import sys
import zlib
from pathlib import Path
from typing import List, Tuple

ART = Path(__file__).resolve().parent.parent / "port" / "xbox" / "art"
IMAGES = (("title-image.png", "TitleImage.xbx", 128), ("save-image.png", "SaveImage.xbx", 64))

# XPR0: the header (magic, total size, header size), one texture resource,
# the end marker, padding to the header size, then the texture's bytes
XPR_HEADER_SIZE = 2048
D3DCOMMON_TYPE_TEXTURE = 0x00040000
# the texture's format word: DMA channel A, border from the texture, a 2D
# texture (dimension 2), D3DFMT_DXT1 (0x0C), one mip level, and log2 of
# the width and height
DXT1 = 0x0C

Pixel = Tuple[int, int, int]


def read_png(path: Path) -> Tuple[int, int, List[Pixel]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    offset, idat = 8, b""
    width = height = depth = color = interlace = 0
    while offset < len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        chunk = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
    if depth != 8 or color not in (2, 6) or interlace:
        raise ValueError(f"{path}: only 8-bit RGB or RGBA PNGs without interlacing")
    channels = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * channels
    rows: List[bytearray] = []
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        kind = raw[position]
        line = bytearray(raw[position + 1:position + 1 + stride])
        position += 1 + stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                guess = left + up - corner
                pa, pb, pc = abs(guess - left), abs(guess - up), abs(guess - corner)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else corner
                line[i] = (line[i] + predictor) & 0xFF
        rows.append(line)
        previous = line
    pixels = [(row[x * channels], row[x * channels + 1], row[x * channels + 2]) for row in rows for x in range(width)]
    return width, height, pixels


def rgb565(pixel: Pixel) -> int:
    r, g, b = pixel
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def unpack565(value: int) -> Pixel:
    r, g, b = value >> 11 & 31, value >> 5 & 63, value & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def dxt1_block(block: List[Pixel]) -> bytes:
    """One opaque 4x4 block: the two ends of the colors' widest axis, and
    each pixel's nearest of the four colors between them."""
    axis = max(range(3), key=lambda c: max(p[c] for p in block) - min(p[c] for p in block))
    low = min(block, key=lambda p: (p[axis], sum(p)))
    high = max(block, key=lambda p: (p[axis], sum(p)))
    color0, color1 = rgb565(high), rgb565(low)
    if color0 == color1:
        return struct.pack("<HHI", color0, color1, 0)
    if color0 < color1:
        color0, color1 = color1, color0
    c0, c1 = unpack565(color0), unpack565(color1)
    palette = [c0, c1, tuple((2 * a + b) // 3 for a, b in zip(c0, c1)),
               tuple((a + 2 * b) // 3 for a, b in zip(c0, c1))]
    indices = 0
    for i, pixel in enumerate(block):
        best = min(range(4), key=lambda k: sum((pixel[c] - palette[k][c]) ** 2 for c in range(3)))
        indices |= best << (2 * i)
    return struct.pack("<HHI", color0, color1, indices)


def xpr_dxt1(width: int, height: int, pixels: List[Pixel]) -> bytes:
    if width != height or width & (width - 1) or width < 4:
        raise ValueError("a square power-of-two picture")
    texture = bytearray()
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = [pixels[(by + y) * width + bx + x] for y in range(4) for x in range(4)]
            texture += dxt1_block(block)
    log2 = width.bit_length() - 1
    format_word = 0x1 | 0x8 | 2 << 4 | DXT1 << 8 | 1 << 16 | log2 << 20 | log2 << 24
    header = struct.pack("<4sII", b"XPR0", XPR_HEADER_SIZE + len(texture), XPR_HEADER_SIZE)
    # the resource: common (one reference, a texture), data (offset 0 in the
    # texture bytes), lock, format, size (0: taken from the format word)
    header += struct.pack("<IIIII", D3DCOMMON_TYPE_TEXTURE | 1, 0, 0, format_word, 0)
    header += struct.pack("<I", 0xFFFFFFFF)
    header += b"\xad" * (XPR_HEADER_SIZE - len(header))
    return header + bytes(texture)


def main() -> int:
    for png, xbx, size in IMAGES:
        width, height, pixels = read_png(ART / png)
        if (width, height) != (size, size):
            print(f"{png}: {width}x{height}, the dashboard wants {size}x{size}", file=sys.stderr)
            return 1
        (ART / xbx).write_bytes(xpr_dxt1(width, height, pixels))
        print(f"{xbx}: {size}x{size} DXT1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
