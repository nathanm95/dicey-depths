#!/usr/bin/env python3
"""Compose the store banner: store/banner-720x320.png.

  art/raw/banner_stage.png   360x160 PixelLab game-show stage, doubled
  art/dicey/dicey_cut.png    Dicey as the store icon draws him, doubled
  title + tagline            drawn here, so the words are real type

Both pictures are pixel art and are only ever scaled by whole numbers with
nearest-neighbour, so the pixels stay square.
"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageEnhance

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FONTS = '/System/Library/Fonts/Supplemental/'
W, H = 720, 320

stage = Image.open(os.path.join(ROOT, 'art', 'raw', 'banner_stage.png')).convert('RGBA')
stage = stage.resize((stage.width * 2, stage.height * 2), Image.NEAREST)
# Knock the stage back a little so the title owns the picture.
stage = ImageEnhance.Brightness(stage).enhance(0.8)
banner = Image.new('RGBA', (W, H), (0, 0, 0, 255))
banner.paste(stage, ((W - stage.width) // 2, (H - stage.height) // 2))

dicey = Image.open(os.path.join(ROOT, 'art', 'dicey', 'dicey_cut.png')).convert('RGBA')
dicey = dicey.resize((dicey.width * 2, dicey.height * 2), Image.NEAREST)
# A soft floor shadow so he stands on the stage rather than floating over it.
shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
dx, dy = 44, H - dicey.height - 16
ImageDraw.Draw(shadow).ellipse([dx + 40, dy + dicey.height - 18, dx + dicey.width - 10, dy + dicey.height + 10],
                               fill=(0, 0, 0, 120))
banner = Image.alpha_composite(banner, shadow)
banner.alpha_composite(dicey, (dx, dy))

d = ImageDraw.Draw(banner)
title = ImageFont.truetype(FONTS + 'Arial Black.ttf', 74)
tag = ImageFont.truetype(FONTS + 'Arial Rounded Bold.ttf', 23)


def marquee(text, x, y, font, fill, outline, shadow_col, stroke, drop):
    d.text((x + drop, y + drop), text, font=font, fill=shadow_col, stroke_width=stroke, stroke_fill=shadow_col)
    d.text((x, y), text, font=font, fill=fill, stroke_width=stroke, stroke_fill=outline)


tx = 300
marquee('DICEY', tx, 34, title, (255, 221, 0), (0, 0, 0), (200, 0, 40), 6, 6)
marquee('DEPTHS', tx, 122, title, (255, 221, 0), (0, 0, 0), (200, 0, 40), 6, 6)
for i, line in enumerate(('Roll your dice face up,', 'then decide how to spend them.')):
    marquee(line, tx + 4, 222 + i * 30, tag, (255, 255, 255), (0, 0, 0), (0, 0, 0), 3, 0)

out = os.path.join(ROOT, 'store', 'banner-720x320.png')
banner.convert('RGB').save(out)
print(out, banner.size)
