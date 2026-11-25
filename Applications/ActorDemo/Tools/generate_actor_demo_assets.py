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
"""

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
