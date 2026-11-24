#!/usr/bin/env python3
"""Generates the small WAV files used by Tests/Resource/WavDecoderTests.cpp.

Uses only the Python standard library (struct), so the files can be reproduced exactly:

    python3 Tests/Assets/generate_test_sounds.py

The sample values written here are the expected values in WavDecoderTests.cpp; change both
together. See Tests/Assets/README.md for what each file covers.
"""

import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Audio")


def chunk(kind, data):
    pad = b"\0" if len(data) % 2 else b""
    return kind + struct.pack("<I", len(data)) + data + pad


def wav(fmt_chunk, data, extra_chunks=b""):
    body = b"WAVE" + fmt_chunk + extra_chunks + chunk(b"data", data)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def fmt(encoding, channels, rate, bits):
    block = channels * bits // 8
    return chunk(b"fmt ", struct.pack("<HHIIHH", encoding, channels, rate, rate * block, block, bits))


def write(name, content):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(content)


def main():
    os.makedirs(OUT, exist_ok=True)
    # 16-bit mono, 8000 Hz: 0, max, min, -half, then a 400-frame square wave (for loading).
    samples = [0, 32767, -32768, -16384] + [8192 if (i // 20) % 2 == 0 else -8192 for i in range(400)]
    write("mono16_8k.wav", wav(fmt(1, 1, 8000, 16), struct.pack("<%dh" % len(samples), *samples),
                               extra_chunks=chunk(b"LIST", b"INFOISFT\x05\0\0\0test\0")))
    # 8-bit unsigned stereo, 22050 Hz: (128, 255), (0, 192)
    write("stereo8_22k.wav", wav(fmt(1, 2, 22050, 8), bytes([128, 255, 0, 192])))
    # 24-bit mono: 0x400000 (0.5), 0xC00000 (-0.5)
    write("mono24_44k.wav", wav(fmt(1, 1, 44100, 24), bytes([0x00, 0x00, 0x40, 0x00, 0x00, 0xC0])))
    # 32-bit float stereo: (0.25, -0.75)
    write("stereo_float_48k.wav", wav(fmt(3, 2, 48000, 32), struct.pack("<ff", 0.25, -0.75)))
    # WAVE_FORMAT_EXTENSIBLE, 16-bit stereo PCM: (16384, -16384)
    subformat_pcm = struct.pack("<HHI", 1, 0, 0x00100000) + bytes([0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71])
    ext = chunk(b"fmt ", struct.pack("<HHIIHHHHI", 0xFFFE, 2, 44100, 44100 * 4, 4, 16, 22, 16, 3) + subformat_pcm)
    write("extensible16.wav", wav(ext, struct.pack("<hh", 16384, -16384)))
    # Unsupported: IMA ADPCM (0x0011)
    write("adpcm.wav", wav(chunk(b"fmt ", struct.pack("<HHIIHH", 0x11, 1, 8000, 4055, 256, 4)), bytes(256)))
    # Unsupported: three channels
    write("three_channels.wav", wav(fmt(1, 3, 8000, 16), bytes(6)))
    # Truncated: the data chunk claims more bytes than the file has.
    full = wav(fmt(1, 1, 8000, 16), bytes(100))
    write("truncated.wav", full[:-40])
    # Not a WAV file at all.
    write("not_a_sound.wav", b"This is a text file with a .wav name.\n")


if __name__ == "__main__":
    main()
