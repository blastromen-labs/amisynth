#!/usr/bin/env python3
"""Write a classic Workbench tool icon for amisynth.exe."""

import math
import struct
import sys
import zlib

WIDTH = 48
HEIGHT = 32
DEPTH = 2
# Workbench four-color pens: grey, black, white, blue.
GREY, BLACK, WHITE, BLUE = 0, 1, 2, 3
WB_MAGIC = 0xE310
WB_TOOL = 3
NO_ICON_POSITION = 0x80000000
STACK_SIZE = 16384
# Switches the program reads from its icon. In brackets they are off; delete the
# brackets in the icon's Information window to turn one on.
TOOL_TYPES = (
    "(SCREEN)",
    "(TAKEOVER)",
    "(NOAUDIO)",
    "(LOG=RAM:amisynth.log)",
)


def put(image, x, y, color):
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        image[y][x] = color


def fill(image, x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(image, x, y, color)


def disc(image, cx, cy, radius, color):
    limit = radius * radius + radius // 2
    for y in range(cy - radius, cy + radius + 1):
        for x in range(cx - radius, cx + radius + 1):
            if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= limit:
                put(image, x, y, color)


def draw(pressed):
    image = [[GREY for _ in range(WIDTH)] for _ in range(HEIGHT)]
    hi = BLACK if pressed else WHITE
    lo = WHITE if pressed else BLACK

    fill(image, 2, 1, 45, 30, GREY)
    for x in range(2, 45):
        put(image, x, 1, hi)
    for y in range(1, 30):
        put(image, 2, y, hi)
    for x in range(3, 46):
        put(image, x, 30, lo)
    for y in range(2, 31):
        put(image, 45, y, lo)

    fill(image, 5, 4, 42, 14, BLACK)
    fill(image, 6, 5, 41, 13, BLUE)
    for index in range(6, 42):
        phase = (index - 6) / 35 * math.tau * 2
        y = 9 + int(round(math.sin(phase) * 3))
        put(image, index, y, WHITE)
        put(image, index, y + 1, WHITE)

    for cx in (9, 24, 39):
        disc(image, cx, 18, 2, BLACK)
        put(image, cx - 1, 17, WHITE)

    fill(image, 6, 22, 41, 29, WHITE)
    for boundary in range(11, 42, 5):
        for y in range(22, 30):
            put(image, boundary, y, BLACK)
    for boundary in (11, 16, 26, 31, 36):
        fill(image, boundary - 1, 22, boundary + 1, 25, BLACK)
    for x in range(6, 42):
        put(image, x, 29, BLACK)
    return image


def planar(image):
    row_bytes = ((WIDTH + 15) // 16) * 2
    planes = bytearray(row_bytes * HEIGHT * DEPTH)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            color = image[y][x]
            for plane in range(DEPTH):
                if color & (1 << plane):
                    offset = plane * row_bytes * HEIGHT + y * row_bytes + (x >> 3)
                    planes[offset] |= 0x80 >> (x & 7)
    return bytes(planes)


def image_block(image, data_offset):
    header = struct.pack(">hhhhh", 0, 0, WIDTH, HEIGHT, DEPTH)
    header += struct.pack(">I", data_offset)
    header += bytes(((1 << DEPTH) - 1, 0))
    header += struct.pack(">I", 0)
    return header + planar(image)


def disk_object(normal_at, selected_at):
    gadget = struct.pack(
        ">IhhhhHHHIIIiIHI",
        0,
        0,
        0,
        WIDTH,
        HEIGHT,
        0x0006,
        0x0003,
        0x0001,
        normal_at,
        selected_at,
        0,
        0,
        0,
        0,
        1,
    )
    header = struct.pack(">HH", WB_MAGIC, 1)
    header += gadget
    header += bytes((WB_TOOL, 0))
    has_tool_types = 1 if TOOL_TYPES else 0
    header += struct.pack(">IIIIIII", 0, has_tool_types, NO_ICON_POSITION, NO_ICON_POSITION, 0, 0, STACK_SIZE)
    return header


def tool_types_block():
    """The tooltype array that follows the images: its size in bytes with the
    closing null entry, then each string's length and text with its null."""
    if not TOOL_TYPES:
        return b""
    block = struct.pack(">I", (len(TOOL_TYPES) + 1) * 4)
    for entry in TOOL_TYPES:
        text = entry.encode("latin-1") + b"\x00"
        block += struct.pack(">I", len(text)) + text
    return block


def build():
    normal = draw(False)
    selected = draw(True)
    bits = len(planar(normal))
    normal_at = 78
    normal_bits = normal_at + 20
    selected_at = normal_bits + bits
    selected_bits = selected_at + 20
    blob = disk_object(normal_at, selected_at)
    if len(blob) != 78:
        raise RuntimeError("DiskObject must be 78 bytes")
    blob += image_block(normal, normal_bits)
    blob += image_block(selected, selected_bits)
    blob += tool_types_block()
    return blob, normal, selected


def write_png(path, images):
    palette = {
        GREY: (170, 170, 170),
        BLACK: (0, 0, 0),
        WHITE: (255, 255, 255),
        BLUE: (102, 136, 187),
    }
    scale = 8
    gap = 8
    width = len(images) * WIDTH * scale + (len(images) - 1) * gap
    height = HEIGHT * scale
    rows = []
    for y in range(height):
        row = bytearray()
        src_y = y // scale
        for index, image in enumerate(images):
            if index:
                row += bytes((96, 96, 96)) * gap
            for x in range(WIDTH):
                color = palette[image[src_y][x]]
                row += bytes(color) * scale
        rows.append(b"\x00" + bytes(row))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def main():
    if len(sys.argv) not in (2, 3):
        print("usage: makeicon.py out/amisynth.exe.info [preview.png]", file=sys.stderr)
        return 1
    blob, normal, selected = build()
    if blob[:2] != b"\xe3\x10" or len(blob) < 78 + 20:
        print("icon header is invalid", file=sys.stderr)
        return 1
    with open(sys.argv[1], "wb") as handle:
        handle.write(blob)
    if len(sys.argv) == 3:
        write_png(sys.argv[2], (normal, selected))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
