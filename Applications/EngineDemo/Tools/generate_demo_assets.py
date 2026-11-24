#!/usr/bin/env python3
"""Generates the EngineDemo's placeholder art and sounds (Python standard library only).

    python3 Applications/EngineDemo/Tools/generate_demo_assets.py

Writes into Applications/EngineDemo/Assets/{Images,Audio}. Everything is drawn procedurally with
a fixed random seed, so the files are reproduced exactly. The font in Assets/Fonts is not
generated (Noto Sans, see Assets/Fonts/OFL.txt).
"""

import math
import os
import struct
import wave
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets")


# --- Deterministic random numbers --------------------------------------------------------------

class Random:
    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next(self):
        # xorshift32
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x / 0xFFFFFFFF

    def range(self, lo, hi):
        return lo + (hi - lo) * self.next()


# --- Images --------------------------------------------------------------------------------------

class Canvas:
    """RGBA image with straight alpha. Colours are (r, g, b) or (r, g, b, a) tuples of 0..255."""

    def __init__(self, width, height, fill=(0, 0, 0, 0)):
        self.width = width
        self.height = height
        self.pixels = [list(fill) for _ in range(width * height)]

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            c = list(color) + [255] * (4 - len(color))
            self.pixels[y * self.width + x] = c

    def get(self, x, y):
        return self.pixels[y * self.width + x]

    def blend(self, x, y, color):
        """Source-over with straight alpha."""
        if not (0 <= x < self.width and 0 <= y < self.height):
            return
        r, g, b = color[:3]
        a = (color[3] if len(color) > 3 else 255) / 255.0
        d = self.pixels[y * self.width + x]
        da = d[3] / 255.0
        oa = a + da * (1 - a)
        if oa <= 0:
            self.pixels[y * self.width + x] = [0, 0, 0, 0]
            return
        out = [round((c * a + dc * da * (1 - a)) / oa) for c, dc in zip((r, g, b), d[:3])]
        self.pixels[y * self.width + x] = out + [round(oa * 255)]

    def rect(self, x0, y0, w, h, color):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                self.blend(x, y, color)

    def ellipse(self, cx, cy, rx, ry, color):
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                dx = (x + 0.5 - cx) / rx
                dy = (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.blend(x, y, color)

    def outline(self, color):
        """Adds a 1-pixel outline around opaque pixels (pixel-art style)."""
        opaque = [[self.get(x, y)[3] > 128 for x in range(self.width)] for y in range(self.height)]
        for y in range(self.height):
            for x in range(self.width):
                if opaque[y][x]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < self.width and 0 <= ny < self.height and opaque[ny][nx]:
                        self.set(x, y, color)
                        break

    def write_png(self, path):
        raw = bytearray()
        for y in range(self.height):
            raw.append(0)
            for x in range(self.width):
                raw.extend(max(0, min(255, int(v))) for v in self.get(x, y))

        def chunk(kind, data):
            body = kind + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
        with open(path, "wb") as f:
            f.write(data)


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def background():
    """400x225 landscape, drawn at 4x in the 1600x900 world."""
    rng = Random(1)
    w, h = 400, 225
    c = Canvas(w, h)
    horizon = 70
    for y in range(h):
        for x in range(w):
            if y < horizon:
                c.set(x, y, mix((110, 170, 230), (190, 225, 245), y / horizon))
            else:
                t = (y - horizon) / (h - horizon)
                base = mix((96, 160, 72), (62, 120, 52), t)
                # Sparse dithered grass texture.
                n = rng.next()
                if n < 0.08:
                    base = mix(base, (130, 190, 90), 0.6)
                elif n < 0.14:
                    base = mix(base, (40, 90, 40), 0.5)
                c.set(x, y, base)
    # Distant hills.
    for x in range(w):
        top = horizon - 14 - int(10 * math.sin(x / 37.0) + 6 * math.sin(x / 13.0 + 1.0))
        for y in range(top, horizon + 4):
            c.set(x, y, mix((70, 130, 90), (96, 160, 72), (y - top) / max(1, horizon + 4 - top)))
    # Clouds.
    for cx, cy, s in ((60, 22, 1.0), (180, 14, 0.8), (300, 28, 1.2), (370, 12, 0.7)):
        for dx, dy, r in ((0, 0, 10), (9, 2, 8), (-9, 3, 7), (4, -4, 7)):
            c.ellipse(cx + dx * s, cy + dy * s, r * s * 1.4, r * s * 0.8, (255, 255, 255, 220))
    # A pond.
    c.ellipse(320, 175, 34, 13, (60, 110, 170))
    c.ellipse(316, 172, 24, 7, (110, 160, 210))
    return c


def path_tile():
    """32x32 tileable cobblestones (drawn with TextureWrap::Repeat)."""
    rng = Random(2)
    c = Canvas(32, 32, (150, 130, 100, 255))
    for row in range(4):
        offset = 4 if row % 2 else 0
        for col in range(-1, 4):
            x0 = col * 8 + offset
            y0 = row * 8
            shade = rng.range(0.85, 1.1)
            stone = tuple(min(255, round(v * shade)) for v in (185, 170, 140))
            for y in range(y0 + 1, y0 + 7):
                for x in range(x0 + 1, x0 + 7):
                    c.set(x % 32, y % 32, stone)
    return c


def player():
    """4 walk frames of 16x24 in one sheet, facing right."""
    sheet = Canvas(64, 24)
    for frame in range(4):
        c = Canvas(16, 24)
        bob = 1 if frame % 2 else 0
        swing = (-2, 0, 2, 0)[frame]
        # Legs.
        c.rect(5 + swing, 17, 2, 6, (60, 60, 110))
        c.rect(9 - swing, 17, 2, 6, (50, 50, 95))
        # Body and arm.
        c.rect(4, 9 - bob, 8, 9, (200, 70, 60))
        c.rect(10 - swing // 2, 11 - bob, 2, 5, (230, 190, 150))
        # Head with hair and an eye (facing right).
        c.rect(5, 2 - bob, 7, 7, (240, 200, 160))
        c.rect(4, 1 - bob, 8, 3, (110, 70, 40))
        c.set(10, 5 - bob, (30, 30, 40))
        c.outline((30, 25, 35, 255))
        for y in range(24):
            for x in range(16):
                sheet.set(frame * 16 + x, y, c.get(x, y))
    return sheet


def tree():
    """48x64: canopy, trunk and a half-transparent shadow."""
    rng = Random(3)
    c = Canvas(48, 64)
    c.ellipse(24, 59, 16, 4, (0, 0, 0, 90))  # soft shadow (alpha 90)
    c.rect(21, 34, 6, 25, (110, 75, 45))
    for _ in range(40):
        x = rng.range(10, 38)
        y = rng.range(8, 36)
        r = rng.range(5, 9)
        shade = rng.range(0.75, 1.1)
        c.ellipse(x, y, r, r, tuple(min(255, round(v * shade)) for v in (60, 140, 60)) + (255,))
    c.outline((25, 50, 25, 255))
    return c


def house():
    """96x80 cottage with a translucent window."""
    c = Canvas(96, 80)
    c.ellipse(48, 76, 44, 4, (0, 0, 0, 80))
    c.rect(10, 34, 76, 40, (225, 205, 170))
    for y in range(0, 30):
        half = 46 - (30 - y) * 46 // 30 + 2
        c.rect(48 - half, 6 + y, 2 * half, 1, (170, 60, 50))
    c.rect(40, 50, 16, 24, (120, 80, 50))
    c.set(52, 62, (220, 200, 80))
    c.rect(18, 44, 14, 12, (170, 210, 240, 170))  # glass: partly transparent
    c.rect(64, 44, 14, 12, (170, 210, 240, 170))
    c.rect(24, 44, 2, 12, (120, 80, 50))
    c.rect(70, 44, 2, 12, (120, 80, 50))
    c.outline((50, 35, 30, 255))
    return c


def rock():
    c = Canvas(24, 16)
    c.ellipse(12, 13, 11, 3, (0, 0, 0, 80))
    c.ellipse(12, 9, 10, 6, (130, 130, 135))
    c.ellipse(10, 7, 6, 3, (165, 165, 170))
    c.outline((60, 60, 70, 255))
    return c


def flowers():
    c = Canvas(16, 16)
    for x, y, color in ((4, 6, (240, 90, 120)), (11, 4, (250, 220, 80)), (8, 11, (200, 120, 240))):
        c.rect(x, y + 1, 1, 4, (50, 120, 50))
        c.ellipse(x + 0.5, y, 2.2, 2.2, color)
        c.set(x, y, (255, 250, 210))
    return c


def panel():
    """32x32 nine-slice UI panel: 8-pixel corners, semi-transparent fill."""
    c = Canvas(32, 32)
    for y in range(32):
        for x in range(32):
            dx = max(0, max(6 - x, x - 25))
            dy = max(0, max(6 - y, y - 25))
            d = math.hypot(dx, dy)
            if d <= 6:
                edge = d > 4.5 or x in (1, 30) and 6 <= y <= 25 or y in (1, 30) and 6 <= x <= 25
                c.set(x, y, (235, 225, 190, 255) if edge else (25, 30, 45, 200))
    return c


def glow():
    """64x64 radial glow, white with alpha falling off (drawn tinted)."""
    c = Canvas(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x + 0.5 - 32, y + 0.5 - 32) / 32
            a = max(0.0, 1.0 - d) ** 2
            c.set(x, y, (255, 255, 255, round(a * 255)))
    return c


# --- Sounds --------------------------------------------------------------------------------------

def write_wav(path, rate, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, round(s * 32767)))) for s in samples))


def chime():
    rate = 44100
    n = int(rate * 0.8)
    out = []
    for i in range(n):
        t = i / rate
        env = math.exp(-5.0 * t) * min(1.0, t / 0.005)
        s = 0.5 * math.sin(2 * math.pi * 880 * t) + 0.3 * math.sin(2 * math.pi * 1320 * t) + 0.15 * math.sin(2 * math.pi * 2640 * t)
        out.append(0.6 * env * s)
    return rate, out


def step():
    """Short soft thump at 22050 Hz (the device resamples it)."""
    rate = 22050
    rng = Random(4)
    n = int(rate * 0.09)
    out = []
    low = 0.0
    for i in range(n):
        t = i / rate
        low += (rng.range(-1, 1) - low) * 0.15
        env = math.exp(-40 * t)
        out.append(0.9 * env * (low + 0.5 * math.sin(2 * math.pi * 90 * t)))
    return rate, out


def ambience():
    """4-second seamless loop at 22050 Hz: soft chord with slow swell (periods divide the length)."""
    rate = 22050
    seconds = 4
    n = rate * seconds
    out = []
    for i in range(n):
        t = i / rate
        swell = 0.6 + 0.4 * math.sin(2 * math.pi * t / seconds)
        s = (math.sin(2 * math.pi * 110 * t) + 0.6 * math.sin(2 * math.pi * 165 * t) + 0.4 * math.sin(2 * math.pi * 220.25 * t))
        out.append(0.12 * swell * s)
    return rate, out


def main():
    images = os.path.join(ROOT, "Images")
    audio = os.path.join(ROOT, "Audio")
    os.makedirs(images, exist_ok=True)
    os.makedirs(audio, exist_ok=True)
    for name, make in (("background", background), ("path", path_tile), ("player", player), ("tree", tree),
                       ("house", house), ("rock", rock), ("flowers", flowers), ("panel", panel), ("glow", glow)):
        make().write_png(os.path.join(images, name + ".png"))
    for name, make in (("chime", chime), ("step", step), ("ambience", ambience)):
        rate, samples = make()
        write_wav(os.path.join(audio, name + ".wav"), rate, samples)


if __name__ == "__main__":
    main()
