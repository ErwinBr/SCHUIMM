#!/usr/bin/env python3
"""Generates the ActorDemo's art (Python standard library only).

    python3 Applications/ActorDemo/Tools/generate_actor_demo_assets.py

Writes the PNG images into Applications/ActorDemo/Assets/Actors. Everything is drawn
procedurally, so the files are reproduced exactly. The JSON resources and scripts next to them
are written by hand and are not touched.

Walker (Actors/Walker/walker.png, 192 x 96): a small figure in 32 x 48 cells.
  row 0: idle_south (24x40, 24x42), idle_north (24x40, 24x42), idle_west (24x40, 24x42)
  row 1: wave_south (32x40, 32x44, 32x44): the arm makes the frames wider and taller.
The second idle frame of each direction is two pixels taller (breathing), so its pivot differs:
the feet stay on the ground point.

Lamp (Actors/Lamp/lamp_on.png, lamp_dim.png, 16 x 48 each): whole-image frames.

Mover (Actors/Mover/mover.png, 160 x 192): a walking figure in 32 x 64 cells; rows north, south,
east (west is east mirrored by the actor resource), columns idle, then four walking frames.

Mover actions (Actors/Mover/mover_actions.png, 160 x 64): the mover facing east pressing a
button: reaching forward over three frames, then back (columns 0..4). A separate image and
animation resource, so a local replacement of mover.png/mover.anim.json leaves it alone.

Courtyard (Rooms/Courtyard/*.png): a 960 x 540 background with a raised balcony, and props
(column, fountain, crate) and a chest object.

Fan (Rooms/Courtyard/fan.png, 240 x 56): a standing fan in 48 x 56 cells: column 0 at rest,
columns 1..4 the blades turning by 45 degrees per frame.
"""

import math
import os
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "Actors")

TRANSPARENT = (0, 0, 0, 0)
OUTLINE = (30, 24, 40, 255)
SKIN = (240, 200, 160, 255)
HAIR = (90, 60, 40, 255)
SHIRT = (60, 120, 200, 255)
TROUSERS = (50, 50, 70, 255)
SHOE = (35, 30, 30, 255)
EYE = (20, 20, 30, 255)


class Canvas:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.pixels = [[TRANSPARENT] * width for _ in range(height)]

    def rect(self, x, y, w, h, colour):
        for yy in range(max(0, y), min(self.height, y + h)):
            for xx in range(max(0, x), min(self.width, x + w)):
                self.pixels[yy][xx] = colour

    def outlined(self, x, y, w, h, colour):
        self.rect(x, y, w, h, OUTLINE)
        self.rect(x + 1, y + 1, w - 2, h - 2, colour)

    def png(self):
        raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in self.pixels)

        def chunk(kind, data):
            body = kind + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def figure(canvas, ox, oy, height, facing, arm=None):
    """A figure whose feet are at the bottom of a 24-pixel-wide frame of `height` at (ox, oy)."""
    bottom = oy + height
    stretch = height - 40  # 0 or 2: the torso gets longer.
    # Legs and shoes.
    canvas.outlined(ox + 6, bottom - 12, 5, 12, TROUSERS)
    canvas.outlined(ox + 13, bottom - 12, 5, 12, TROUSERS)
    canvas.rect(ox + 6, bottom - 3, 5, 3, SHOE)
    canvas.rect(ox + 13, bottom - 3, 5, 3, SHOE)
    # Torso.
    torso_top = bottom - 27 - stretch
    canvas.outlined(ox + 4, torso_top, 16, 16 + stretch, SHIRT)
    # Head.
    head_top = torso_top - 13
    canvas.outlined(ox + 5, head_top, 14, 14, SKIN)
    if facing == "south":
        canvas.rect(ox + 6, head_top + 1, 12, 3, HAIR)
        canvas.rect(ox + 8, head_top + 6, 2, 2, EYE)
        canvas.rect(ox + 14, head_top + 6, 2, 2, EYE)
    elif facing == "north":
        canvas.rect(ox + 6, head_top + 1, 12, 11, HAIR)
    elif facing == "west":
        canvas.rect(ox + 9, head_top + 1, 9, 6, HAIR)
        canvas.rect(ox + 7, head_top + 6, 2, 2, EYE)
        canvas.rect(ox + 3, head_top + 8, 3, 2, SKIN)  # Nose: points west.
    # Arms.
    if arm is None:
        canvas.outlined(ox + 1, torso_top + 2, 4, 12, SHIRT)
        canvas.outlined(ox + 19, torso_top + 2, 4, 12, SHIRT)
    else:
        canvas.outlined(ox + 1, torso_top + 2, 4, 12, SHIRT)
        # Raised arm on the figure's left (screen right), reaching up to `arm` pixels above the torso.
        canvas.outlined(ox + 19, torso_top - arm, 5, arm + 6, SHIRT)
        canvas.outlined(ox + 23, torso_top - arm - 2, 7, 6, SKIN)


def walker():
    canvas = Canvas(192, 96)
    for column, facing in enumerate(["south", "north", "west"]):
        figure(canvas, column * 64, 48 - 40, 40, facing)
        figure(canvas, column * 64 + 32, 48 - 42, 42, facing)
    # Wave: 32 wide; the frame is 40, 44 and 44 high, the figure always 40 high at its bottom.
    for index, (height, arm) in enumerate([(40, 2), (44, 6), (44, 9)]):
        frame_top = 48 + 48 - height
        figure(canvas, index * 32, frame_top + height - 40, 40, "south", arm=arm)
    return canvas


def lamp(bright):
    canvas = Canvas(16, 48)
    glow = (255, 230, 120, 255) if bright else (150, 120, 60, 255)
    canvas.outlined(6, 14, 4, 30, (80, 80, 90, 255))  # Pole.
    canvas.outlined(3, 44, 10, 4, (60, 60, 70, 255))  # Foot.
    canvas.outlined(1, 0, 14, 15, glow)  # Shade.
    if bright:
        canvas.rect(4, 3, 8, 8, (255, 250, 210, 255))
    return canvas


COAT = (170, 70, 60, 255)
COAT_DARK = (130, 50, 45, 255)
BOOT = (60, 40, 30, 255)


def mover_figure(canvas, ox, oy, facing, phase):
    """A 32 x 64 cell; the feet touch the cell's bottom edge. phase: None (standing) or 0..3."""
    bottom = oy + 64
    swing = [0, 3, 0, -3][phase] if phase is not None else 0
    lift = [0, 2, 0, 2][phase] if phase is not None else 0
    if facing == "east":
        # Side view: legs swing forward and back.
        canvas.outlined(ox + 12 + swing, bottom - 20, 6, 20 - (lift if swing > 0 else 0), TROUSERS)
        canvas.outlined(ox + 14 - swing, bottom - 20, 6, 20 - (lift if swing < 0 else 0), TROUSERS)
        canvas.outlined(ox + 9, bottom - 44, 15, 26, COAT)
        canvas.outlined(ox + 10, bottom - 58, 13, 14, SKIN)
        canvas.rect(ox + 10, bottom - 57, 9, 5, HAIR)
        canvas.rect(ox + 19, bottom - 52, 2, 2, EYE)
        canvas.rect(ox + 23, bottom - 49, 3, 2, SKIN)  # Nose: east.
        canvas.outlined(ox + 14 - swing, bottom - 40, 5, 16, COAT_DARK)  # Arm.
    else:
        left = lift if phase in (1,) else 0
        right = lift if phase in (3,) else 0
        canvas.outlined(ox + 9, bottom - 20, 6, 20 - left, TROUSERS)
        canvas.outlined(ox + 17, bottom - 20, 6, 20 - right, TROUSERS)
        canvas.outlined(ox + 6, bottom - 44, 20, 26, COAT)
        canvas.outlined(ox + 9, bottom - 58, 14, 14, SKIN)
        if facing == "south":
            canvas.rect(ox + 10, bottom - 57, 12, 4, HAIR)
            canvas.rect(ox + 12, bottom - 52, 2, 2, EYE)
            canvas.rect(ox + 18, bottom - 52, 2, 2, EYE)
            canvas.rect(ox + 15, bottom - 38, 2, 16, COAT_DARK)  # Buttons line.
        else:
            canvas.rect(ox + 10, bottom - 57, 12, 12, HAIR)
        canvas.outlined(ox + 3, bottom - 42 + (swing if facing == "south" else -swing), 4, 15, COAT_DARK)
        canvas.outlined(ox + 25, bottom - 42 - (swing if facing == "south" else -swing), 4, 15, COAT_DARK)


def mover():
    canvas = Canvas(160, 192)
    for row, facing in enumerate(["north", "south", "east"]):
        mover_figure(canvas, 0, row * 64, facing, None)
        for phase in range(4):
            mover_figure(canvas, (phase + 1) * 32, row * 64, facing, phase)
    return canvas


def mover_actions():
    canvas = Canvas(160, 64)
    for column, reach in enumerate([0, 4, 9, 4, 0]):
        ox = column * 32
        mover_figure(canvas, ox, 0, "east", None)
        # The arm reaches forward (east) at shoulder height, then back.
        canvas.outlined(ox + 16, 64 - 40, 6 + reach, 5, COAT_DARK)
        canvas.outlined(ox + 20 + reach, 64 - 41, 5, 6, SKIN)
    return canvas


FAN_METAL = (150, 155, 165, 255)
FAN_BLADE = (200, 215, 230, 255)


def fan_frame(canvas, ox, angle):
    """A standing fan in a 48 x 56 cell; its foot touches the cell's bottom edge."""
    canvas.outlined(ox + 14, 52, 20, 4, FAN_METAL)  # Foot.
    canvas.outlined(ox + 22, 26, 4, 27, FAN_METAL)  # Pole.
    centre_x, centre_y = ox + 24, 18
    # Four blades: dots along two perpendicular lines at `angle` (multiples of 45 degrees).
    directions = {0: [(1, 0), (0, 1)], 45: [(1, 1), (1, -1)]}[angle % 90]
    for dx, dy in directions:
        for sign in (1, -1):
            for step in range(3, 15):
                x = centre_x + sign * dx * step * (10 if dx and dy else 14) // 14
                y = centre_y + sign * dy * step * (10 if dx and dy else 14) // 14
                canvas.rect(x - 2, y - 2, 4, 4, FAN_BLADE)
    canvas.outlined(centre_x - 3, centre_y - 3, 7, 7, (90, 95, 110, 255))  # Hub.
    # Cage ring.
    for i in range(48):
        a = i * 2 * math.pi / 48
        canvas.rect(int(centre_x + 17 * math.cos(a)) - 1, int(centre_y + 17 * math.sin(a)) - 1, 2, 2, (110, 115, 125, 255))


def fan():
    canvas = Canvas(240, 56)
    for column, angle in enumerate([0, 0, 45, 90, 135]):
        fan_frame(canvas, column * 48, angle)
    return canvas


class Random:
    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next(self):
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x

    def range(self, low, high):
        return low + self.next() % (high - low + 1)


def shade(colour, amount):
    r, g, b, a = colour
    return (max(0, min(255, r + amount)), max(0, min(255, g + amount)), max(0, min(255, b + amount)), a)


def courtyard_background():
    random = Random(4242)
    canvas = Canvas(960, 540)
    wall = (150, 120, 100, 255)
    canvas.rect(0, 0, 960, 230, wall)
    for y in range(0, 230, 16):  # Brick courses.
        offset = 0 if (y // 16) % 2 == 0 else 16
        for x in range(-offset, 960, 32):
            canvas.rect(x + 1, y + 1, 30, 14, shade(wall, random.range(-12, 12)))
    for x in (90, 330, 570):  # Windows.
        canvas.outlined(x, 60, 70, 90, (60, 80, 110, 255))
        canvas.rect(x + 34, 62, 2, 86, (40, 50, 70, 255))
    # Balcony (its own walkable area, x 700..900, y 120..190): a platform with a railing.
    canvas.outlined(690, 110, 220, 90, (120, 100, 90, 255))
    canvas.rect(692, 112, 216, 6, (180, 160, 140, 255))
    for x in range(700, 905, 20):
        canvas.rect(x, 190, 4, 40, (70, 60, 55, 255))
    canvas.rect(690, 226, 220, 6, (70, 60, 55, 255))
    floor = (175, 165, 140, 255)
    canvas.rect(0, 230, 960, 310, floor)
    for y in range(230, 540, 30):
        for x in range(0, 960, 30):
            canvas.rect(x + 1, y + 1, 28, 28, shade(floor, random.range(-14, 10)))
    # Unwalkable flower beds (outside the walkable areas): right of the corridor, along the bottom.
    for (x, y, w, h) in [(210, 232, 380, 64), (0, 524, 960, 16)]:
        canvas.rect(x, y, w, h, (70, 110, 50, 255))
        for _ in range(w * h // 60):
            canvas.rect(x + random.range(0, w - 3), y + random.range(0, h - 3), 3, 3, (200, random.range(60, 200), 80, 255))
    return canvas


def column():
    canvas = Canvas(28, 130)
    canvas.outlined(0, 0, 28, 12, (200, 195, 185, 255))
    canvas.outlined(4, 10, 20, 108, (220, 215, 205, 255))
    for x in (8, 13, 18):
        canvas.rect(x, 14, 2, 100, (190, 185, 175, 255))
    canvas.outlined(0, 116, 28, 14, (200, 195, 185, 255))
    return canvas


def fountain():
    canvas = Canvas(150, 100)
    canvas.outlined(0, 50, 150, 50, (140, 140, 150, 255))
    canvas.rect(6, 54, 138, 20, (90, 140, 200, 255))
    canvas.outlined(62, 10, 26, 46, (160, 160, 170, 255))
    canvas.outlined(52, 0, 46, 14, (150, 150, 160, 255))
    for x in (60, 75, 88):
        canvas.rect(x, 14, 2, 36, (160, 200, 240, 255))
    return canvas


def crate():
    canvas = Canvas(40, 38)
    canvas.outlined(0, 0, 40, 38, (150, 110, 60, 255))
    canvas.rect(3, 3, 34, 4, (120, 85, 45, 255))
    canvas.rect(3, 31, 34, 4, (120, 85, 45, 255))
    for i in range(32):
        canvas.rect(4 + i, 4 + i * 30 // 32, 2, 2, (110, 80, 40, 255))
    return canvas


def chest():
    canvas = Canvas(36, 28)
    canvas.outlined(0, 8, 36, 20, (120, 70, 30, 255))
    canvas.outlined(0, 0, 36, 10, (140, 85, 40, 255))
    canvas.rect(15, 8, 6, 8, (230, 200, 60, 255))
    return canvas


def write(relative, canvas):
    path = os.path.join(ROOT, relative)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(canvas.png())
    print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    write("Walker/walker.png", walker())
    write("Lamp/lamp_on.png", lamp(True))
    write("Lamp/lamp_dim.png", lamp(False))
    write("Mover/mover.png", mover())
    write("../Rooms/Courtyard/background.png", courtyard_background())
    write("../Rooms/Courtyard/column.png", column())
    write("../Rooms/Courtyard/fountain.png", fountain())
    write("../Rooms/Courtyard/crate.png", crate())
    write("../Rooms/Courtyard/chest.png", chest())
    write("Mover/mover_actions.png", mover_actions())
    write("../Rooms/Courtyard/fan.png", fan())
