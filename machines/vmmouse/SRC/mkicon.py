#!/usr/bin/env python3
# mkicon.py - make vmmouse.ico, a 32x32 16-color mouse icon.
#
# A host-side tool (does not run on NT). It writes the real build input,
# vmmouse.ico, which vmmsetup.rc embeds. Run:  python3 mkicon.py
#
# The icon is a top view of a two-button mouse with a scroll wheel.

import struct, sys

W = H = 32

# palette: index -> (R, G, B).  0 reads as transparent via the AND mask.
PAL = [
    (0, 0, 0),          # 0 black (outline)  -- also used for transparent XOR
    (255, 255, 255),    # 1 white (button highlight)
    (205, 205, 205),    # 2 light grey (body)
    (96, 96, 96),       # 3 dark grey (shadow)
    (150, 150, 150),    # 4 mid grey (wheel)
]
while len(PAL) < 16:
    PAL.append((0, 0, 0))

TRANSP = -1
grid = [[TRANSP] * W for _ in range(H)]

cx, cy = 15.5, 15.5
rx, ry, p = 7.4, 13.8, 2.5          # superellipse: tall rounded blob


def inside(x, y):
    return (abs((x - cx) / rx) ** p + abs((y - cy) / ry) ** p) <= 1.0


for y in range(H):
    for x in range(W):
        if not inside(x, y):
            continue
        # outline where a 4-neighbour is outside the body
        edge = not (inside(x - 1, y) and inside(x + 1, y)
                    and inside(x, y - 1) and inside(x, y + 1))
        if edge:
            grid[y][x] = 0
        elif y <= 11:
            grid[y][x] = 1          # the two buttons (light top)
        else:
            grid[y][x] = 2          # the body

# divider between the two buttons
for y in range(4, 11):
    if grid[y][15] != TRANSP:
        grid[y][15] = 0

# scroll wheel: a small grey bar with a black outline, centred
for y in range(6, 11):
    for x in range(14, 17):
        if grid[y][x] == TRANSP:
            continue
        grid[y][x] = 4 if 14 < x < 16 and 6 < y < 10 else 0

# shading along the lower-right edge for a bit of depth
for y in range(13, 29):
    for x in range(W):
        if grid[y][x] == 2 and (not inside(x + 2, y) or not inside(x, y + 2)):
            grid[y][x] = 3


def nibbles(row_vals):
    out = bytearray()
    for i in range(0, W, 2):
        hi = (row_vals[i] if row_vals[i] != TRANSP else 0) & 0xF
        lo = (row_vals[i + 1] if row_vals[i + 1] != TRANSP else 0) & 0xF
        out.append((hi << 4) | lo)
    return bytes(out)


def and_bits(row_vals):
    out = bytearray()
    for i in range(0, W, 8):
        b = 0
        for j in range(8):
            if row_vals[i + j] == TRANSP:      # 1 = transparent
                b |= (0x80 >> j)
        out.append(b)
    return bytes(out)


xor = b"".join(nibbles(grid[y]) for y in range(H - 1, -1, -1))   # bottom-up
andm = b"".join(and_bits(grid[y]) for y in range(H - 1, -1, -1))

bih = struct.pack("<IiiHHIIiiII", 40, W, H * 2, 1, 4, 0, 0, 0, 0, 0, 0)
pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in PAL)
image = bih + pal + xor + andm

icondir = struct.pack("<HHH", 0, 1, 1)
entry = struct.pack("<BBBBHHII", W, H, 16, 0, 1, 4, len(image), 6 + 16)

out = "vmmouse.ico" if len(sys.argv) < 2 else sys.argv[1]
with open(out, "wb") as f:
    f.write(icondir + entry + image)
print("wrote %s (%d bytes, image %d)" % (out, 6 + 16 + len(image), len(image)))
