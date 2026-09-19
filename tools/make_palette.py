#!/usr/bin/env python3
"""Write docs/art/pebble64.png — every colour a Pebble can actually show.

GColor8 gives two bits per channel, so each of R/G/B is one of four levels and
the whole display has 64 colours. Feeding this image to PixelLab as the forced
palette makes it generate in those colours in the first place, rather than
generating freely and being snapped (and dithered) by the resource compiler.
"""
from PIL import Image
import os

LEVELS = [0, 85, 170, 255]          # 2 bits per channel
COLOURS = [(r, g, b) for r in LEVELS for g in LEVELS for b in LEVELS]
assert len(COLOURS) == 64

out = os.path.join(os.path.dirname(__file__), "..", "docs", "art", "pebble64.png")
im = Image.new("RGB", (8, 8))
im.putdata(COLOURS)
im.save(out)
print("wrote", os.path.normpath(out), "-", len(set(COLOURS)), "colours")
