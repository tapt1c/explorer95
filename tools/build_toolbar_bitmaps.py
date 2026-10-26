#!/usr/bin/env python3
"""Pack original Windows 98 palette tiles as Win95 BI_RGB/4bpp toolbars."""
from pathlib import Path
import struct
import sys

PALETTE = ((0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
           (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
           (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
           (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255))
TILES = ((0, 0), (0, 1), (0, 28), (0, 17), (0, 12), (1, 3), (0, 20), (1, 12),
         (0, 6), (0, 5), (0, 7), (0, 10), (0, 15), (1, 10))


def read_bitmap(path):
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b'BM':
        raise ValueError(f'{path}: missing bitmap header')
    offset, = struct.unpack_from('<I', data, 10)
    header, width, height, planes, bits, compression = struct.unpack_from('<IiiHHI', data, 14)
    used, = struct.unpack_from('<I', data, 46)
    count = used or 1 << bits
    if header != 40 or not 0 < width <= 4096 or not 0 < height <= 256 or planes != 1 or bits not in (4, 8):
        raise ValueError(f'{path}: unsupported palette bitmap')
    if count > 1 << bits or offset < 54 + count * 4 or offset > len(data):
        raise ValueError(f'{path}: invalid palette bounds')
    palette = [tuple(data[54 + i * 4:57 + i * 4][::-1]) for i in range(count)]
    pixels = [[0] * width for _ in range(height)]  # bottom-up DIB rows
    if compression == 0:
        stride = (width * bits + 31) // 32 * 4
        # These original exports count a 16-byte file header in bfOffBits,
        # but store pixels immediately after the actual 14-byte header/DIB.
        # The extra two bytes are at EOF, not before the scanlines. Respect
        # their physical DIB layout instead of shifting/wrapping every row.
        if path.name in ('262.bmp', '264.bmp'):
            start = 54 + count * 4
            if offset != start + 2 or len(data) != offset + stride * height or data[-2:] != b'\0\0':
                raise ValueError(f'{path}: unexpected original export layout')
            offset = start
        if offset + stride * height > len(data):
            raise ValueError(f'{path}: truncated bitmap rows')
        for y in range(height):
            for x in range(width):
                value = data[offset + y * stride + (x if bits == 8 else x // 2)]
                pixels[y][x] = value if bits == 8 else (value >> (0 if x & 1 else 4)) & 15
    elif compression == 2 and bits == 4:
        position, x, y, ended = offset, 0, 0, False

        def emit(value):
            nonlocal x
            if not (0 <= x < width and 0 <= y < height):
                raise ValueError(f'{path}: RLE run leaves bitmap')
            pixels[y][x] = value
            x += 1

        while position + 2 <= len(data):
            run, value = data[position:position + 2]
            position += 2
            if run:
                for i in range(run):
                    emit((value >> (0 if i & 1 else 4)) & 15)
            elif value == 0:
                x, y = 0, y + 1
            elif value == 1:
                ended = True
                break
            elif value == 2:
                if position + 2 > len(data):
                    raise ValueError(f'{path}: truncated RLE delta')
                x += data[position]
                y += data[position + 1]
                position += 2
                if x > width or y >= height:
                    raise ValueError(f'{path}: RLE delta leaves bitmap')
            else:
                size = (value + 1) // 2
                padded = (size + 1) & ~1
                if position + padded > len(data):
                    raise ValueError(f'{path}: truncated RLE absolute run')
                for i in range(value):
                    emit((data[position + i // 2] >> (0 if i & 1 else 4)) & 15)
                position += padded
        if not ended:
            raise ValueError(f'{path}: missing RLE end marker')
    else:
        raise ValueError(f'{path}: unsupported bitmap compression')
    return [[palette[value] for value in row] for row in reversed(pixels)]


def write_strip(path, size, sources):
    cell = 16 if size == 16 else 20
    pad, width = (size - cell) // 2, size * len(TILES)
    pixels = [[13] * width for _ in range(size)]
    images = [read_bitmap(path) for path in sources]
    for tile, (source, index) in enumerate(TILES):
        image = images[source]
        if len(image) != cell or len(image[0]) != cell * (46 if source == 0 else 16):
            raise ValueError('incorrect source tile pitch')
        for y in range(cell):
            for x in range(cell):
                pixels[y + pad][tile * size + pad + x] = PALETTE.index(image[y][index * cell + x])
    stride = (width * 4 + 31) // 32 * 4
    rows = bytearray(stride * size)
    for y, row in enumerate(reversed(pixels)):
        for x, value in enumerate(row):
            rows[y * stride + x // 2] |= value << (0 if x & 1 else 4)
    colors = b''.join(bytes((b, g, r, 0)) for r, g, b in PALETTE)
    offset = 14 + 40 + len(colors)
    header = struct.pack('<2sIHHI', b'BM', offset + len(rows), 0, 0, offset)
    header += struct.pack('<IiiHHIIiiII', 40, width, size, 1, 4, 0, len(rows), 2834, 2834, 16, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + colors + rows)


def main():
    root = Path(__file__).resolve().parents[1]
    source = root / 'resources/toolbar98'
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build'
    write_strip(output / 'toolbar98-small.bmp', 16, (source / '275.bmp', source / '264.bmp'))
    write_strip(output / 'toolbar98-large.bmp', 24, (source / '273.bmp', source / '262.bmp'))


if __name__ == '__main__':
    main()
