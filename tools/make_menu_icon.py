#!/usr/bin/env python3
"""Hand-pixel the 25x25 launcher icon: Dicey's face, two eyes and all.

A downscale of the store icon turns his eyes to mush at this size, so this is
drawn pixel by pixel from a map instead. Writes resources/images/menu_icon.png.
"""
import os
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
C = {
    '.': (0, 0, 0, 0),
    'K': (0, 0, 0, 255),          # outline, eyes
    'T': (170, 255, 255, 255),    # top face, lit
    'F': (85, 255, 255, 255),     # front face
    'S': (0, 170, 255, 255),      # side face, shaded
    'D': (0, 85, 170, 255),       # pips on the shaded side
    'W': (255, 255, 255, 255),    # eye highlight
    'P': (255, 170, 170, 255),    # cheeks
    'R': (170, 0, 0, 255),        # mouth
}
MAP = """
.........................
.....KKKKKKKKKKKKKK......
....KTTTTTTDDTTTTTTK.....
...KTTTTTTTTTTTTTTSSK....
..KKKKKKKKKKKKKKKKSSSK...
..KFFFFFFFFFFFFFFKSSSSK..
..KFFFFFFFFFFFFFFKSSDSK..
..KFFFFFFFFFFFFFFKSDDSK..
..KFFFKKFFFFKKFFFKSSSSK..
..KFFFKWFFFFKWFFFKSSSSK..
..KFFFKKFFFFKKFFFKSSSSK..
..KFFFKKFFFFKKFFFKSSSSK..
..KFPPFFFFFFFFPPFKSSSSK..
..KFFFFKFFFFKFFFFKSDDSK..
..KFFFFFKRRKFFFFFKSDDSK..
..KFFFFFFKKFFFFFFKSSSSK..
..KFFFFFFFFFFFFFFKSSSK...
..KFFFFFFFFFFFFFFKSSK....
..KFFFFFFFFFFFFFFKSK.....
..KKKKKKKKKKKKKKKKK......
.....KK.......KK.........
.....KK.......KK.........
....KKK......KKK.........
.........................
.........................
"""
rows = [r for r in MAP.strip('\n').split('\n')]
im = Image.new('RGBA', (25, 25), (0, 0, 0, 0))
for y, row in enumerate(rows[:25]):
    for x, ch in enumerate(row[:25]):
        im.putpixel((x, y), C[ch])
im.save(os.path.join(ROOT, 'resources', 'images', 'menu_icon.png'))
print('menu_icon.png 25x25')
