#!/usr/bin/env python3
"""Cut Dicey out of the store icon (store/icon-large-144.png) onto transparency.

The icon is the version of the character the player knows -- two big eyes,
pink cheeks, sword up -- so every other Dicey (banner, splash, class portrait)
is made from this cut-out rather than redrawn. Writes art/dicey/dicey_cut.png.

Background = the purple backdrop, its glow and the floor shadow (red and blue
both above green; Dicey's blues have green above red), plus the near-white
rounded corners, flood-filled in from the edge so nothing inside the outline
-- the pink cheeks, say -- is touched.
"""
import os
from collections import deque
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
im = Image.open(os.path.join(ROOT, 'store', 'icon-large-144.png')).convert('RGBA')
px = im.load()
W, H = im.size


def is_bg(x, y):
    r, g, b, a = px[x, y]
    if a < 128:
        return True
    edge = min(x, y, W - 1 - x, H - 1 - y) < 10
    if edge and r > 235 and g > 235 and b > 235:
        return True                      # the icon's rounded corners
    return r >= g - 6 and b > g + 12     # purple


seen = set()
q = deque([(x, y) for x in range(W) for y in (0, H - 1)] + [(x, y) for y in range(H) for x in (0, W - 1)])
while q:
    x, y = q.popleft()
    if (x, y) in seen or not (0 <= x < W and 0 <= y < H) or not is_bg(x, y):
        continue
    seen.add((x, y))
    q.extend([(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)])

out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
po = out.load()
for y in range(H):
    for x in range(W):
        if (x, y) not in seen:
            po[x, y] = px[x, y]
# The backdrop has sparkles that are not purple; drop any island of pixels
# too small to be part of Dicey.
po = out.load()
done = set()
for y in range(H):
    for x in range(W):
        if po[x, y][3] == 0 or (x, y) in done:
            continue
        comp, q2 = [], deque([(x, y)])
        while q2:
            cx, cy = q2.popleft()
            if (cx, cy) in done or not (0 <= cx < W and 0 <= cy < H) or po[cx, cy][3] == 0:
                continue
            done.add((cx, cy))
            comp.append((cx, cy))
            q2.extend([(cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)])
        if len(comp) < 40:
            for cx, cy in comp:
                po[cx, cy] = (0, 0, 0, 0)
out = out.crop(out.getbbox())
os.makedirs(os.path.join(ROOT, 'art', 'dicey'), exist_ok=True)
out.save(os.path.join(ROOT, 'art', 'dicey', 'dicey_cut.png'))
print('dicey_cut.png', out.size)

# --- the watch's copies -----------------------------------------------------
# Snapped to the 64 colours (see tools/verify_art.py). The full cut-out is the
# splash; a LANCZOS reduction to 47x48 is the Warrior's class portrait -- at
# that size the eyes survive, which a nearest-neighbour step of 2.4x does not.
LEVELS = (0, 85, 170, 255)


def snap(img):
    img = img.convert('RGBA')
    p = img.load()
    for yy in range(img.height):
        for xx in range(img.width):
            r, g, b, a = p[xx, yy]
            p[xx, yy] = (tuple(min(LEVELS, key=lambda v: abs(v - c)) for c in (r, g, b)) + (255,)
                         if a >= 128 else (0, 0, 0, 0))
    return img


images = os.path.join(ROOT, 'resources', 'images')
snap(out).save(os.path.join(images, 'dicey.png'))
snap(out.resize((47, 48), Image.LANCZOS)).save(os.path.join(images, 'class_warrior.png'))
print('dicey.png', out.size, ' class_warrior.png (47, 48)')
