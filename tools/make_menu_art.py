#!/usr/bin/env python3
"""Build the menu backdrops and menu icons from art/raw/.

  python3 tools/make_menu_art.py          # all of them
  python3 tools/make_menu_art.py --list   # what is missing a raw original

Backdrops: art/raw/<name>.png (260x260, from PixelLab) becomes
resources/data/bd_<name>~gabbro.bin (the whole square) and ~emery.bin (the
centre 200x228). Each is dimmed so light menu text reads over it, snapped to the
64 colours the watch has, cut to 16 colours, and packed (see src/c/skin.c for
the format). A missing original gets a drawn placeholder so the code can be
built and tested before the art exists.

Icons: art/raw/ic_<name>.png is cropped to its contents, snapped to the
palette with on/off alpha, and written to resources/images/ic_<name>.png.

package.json's BD_* and IC_* resource entries are rewritten to match.
art/preview/ gets a PNG of what each backdrop will look like on the watch.
"""
import json, os, struct, sys
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
RAW = os.path.join(ROOT, 'art', 'raw')
PREVIEW = os.path.join(ROOT, 'art', 'preview')
DATA = os.path.join(ROOT, 'resources', 'data')
IMAGES = os.path.join(ROOT, 'resources', 'images')

# name: (dim table, placeholder colours). The dim table maps each 2-bit channel
# level to a darker one. Stage art is bright and busy, so it loses a full step;
# the dungeon is dark already and keeps its torchlight.
STEP = {0: 0, 1: 0, 2: 1, 3: 2}
SOFT = {0: 0, 1: 1, 2: 1, 3: 2}
BACKDROPS = {
    'stage_title':     (STEP, ((0, 0, 85), (170, 0, 0))),
    'stage_class':     (STEP, ((0, 0, 85), (170, 0, 0))),
    'stage_hall':      (STEP, ((85, 0, 0), (170, 85, 0))),
    'stage_win':       (STEP, ((85, 0, 85), (255, 170, 0))),
    'ep1_opening':     (STEP, ((0, 0, 85), (170, 0, 0))),
    'ep2_greed':       (STEP, ((0, 85, 0), (170, 170, 0))),
    'ep3_parallel':    (STEP, ((85, 0, 85), (0, 170, 170))),
    'ep4_elimination': (STEP, ((85, 0, 0), (170, 0, 0))),
    'ep5_sudden':      (STEP, ((85, 0, 0), (0, 0, 0))),
    'bonus_hoard':     (STEP, ((85, 85, 0), (255, 170, 0))),
    'bonus_swarm':     (STEP, ((85, 85, 0), (0, 0, 0))),
    'bonus_backstage': (STEP, ((0, 0, 0), (85, 85, 85))),
    'dun_floor1':      (SOFT, ((0, 0, 0), (85, 85, 85))),
    'dun_floor2':      (SOFT, ((0, 0, 85), (0, 85, 85))),
    'dun_floor3':      (SOFT, ((85, 0, 0), (170, 0, 0))),
    'dun_shop':        (SOFT, ((85, 0, 0), (170, 85, 0))),
    'dun_anvil':       (SOFT, ((0, 0, 0), (170, 85, 0))),
    'dun_trade':       (SOFT, ((0, 0, 85), (170, 85, 0))),
    'dun_lose':        (SOFT, ((0, 0, 0), (0, 0, 85))),
}
ICONS = ['key', 'flag', 'stairs', 'trophy', 'medal', 'book', 'lock', 'feather',
         'sword', 'skull', 'coin', 'heart', 'backpack']
ICON_MAX = 28        # a menu row is 44px tall
# Icons that came out too dark to read over a dimmed backdrop: every colour
# except the black outline is lifted one level.
ICON_LIFT = {'lock'}
PLATFORMS = {'gabbro': (260, 260), 'emery': (200, 228)}


def snap(v):
    return min((0, 85, 170, 255), key=lambda L: abs(L - v))


def level(v):
    return snap(v) // 85


def gcolor8(rgb):
    r, g, b = (level(c) for c in rgb)
    return 0xC0 | (r << 4) | (g << 2) | b


def placeholder(cols):
    """A dark stage: a vertical fade with a light pool, so text over it can be judged."""
    top, accent = cols
    im = Image.new('RGB', (260, 260), top)
    d = ImageDraw.Draw(im)
    for y in range(130, 260, 4):
        d.rectangle([0, y, 259, y + 3], fill=tuple(max(0, c - 85) for c in top))
    d.ellipse([70, 150, 190, 250], fill=accent)
    d.rectangle([0, 0, 30, 259], fill=accent)
    d.rectangle([229, 0, 259, 259], fill=accent)
    return im


def dimmed(im, table):
    px = im.load()
    out = Image.new('RGB', im.size)
    po = out.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            po[x, y] = tuple(table[level(c)] * 85 for c in px[x, y][:3])
    return out


def to16(im):
    """Cut to at most 16 colours, every one of them a legal watch colour."""
    cols = im.getcolors(1 << 16)
    if len(cols) > 16:
        q = im.quantize(16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:48]
        legal = sorted({tuple(snap(c) for c in pal[i:i + 3]) for i in range(0, 48, 3)})
        px = im.load()
        out = Image.new('RGB', im.size)
        po = out.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                c = px[x, y]
                po[x, y] = min(legal, key=lambda L: sum((a - b) ** 2 for a, b in zip(L, c)))
        im = out
    palette = sorted({c for _, c in im.getcolors(1 << 16)})
    return im, palette


def lz_compress(data):
    # Same token format as Zuma's tools/artlib.py, which this is lifted from.
    data = bytes(data)
    n = len(data)
    out = bytearray()
    lits = bytearray()
    head = {}
    i = 0

    def flush():
        nonlocal lits
        while lits:
            chunk = lits[:128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            lits = lits[128:]

    while i < n:
        best_len, best_d = 0, 0
        if i + 3 <= n:
            for j in reversed(head.get(data[i:i + 3], [])[-64:]):
                d = i - j
                if d > 65535:
                    continue
                L = 0
                while L < 130 and i + L < n and data[j + L] == data[i + L]:
                    L += 1
                if L > best_len:
                    best_len, best_d = L, d
                    if L == 130:
                        break
        if best_len >= 3:
            flush()
            out.append(0x80 | (best_len - 3))
            out += struct.pack('<H', best_d)
            for k in range(best_len):
                if i + k + 3 <= n:
                    head.setdefault(data[i + k:i + k + 3], []).append(i + k)
            i += best_len
        else:
            if i + 3 <= n:
                head.setdefault(data[i:i + 3], []).append(i)
            lits.append(data[i])
            i += 1
    flush()
    return bytes(out)


def lz_decompress(src, n):
    out = bytearray()
    i = 0
    while len(out) < n:
        t = src[i]; i += 1
        if t < 0x80:
            out += src[i:i + t + 1]; i += t + 1
        else:
            d = src[i] | (src[i + 1] << 8); i += 2
            for _ in range((t & 0x7F) + 3):
                out.append(out[-d])
    return bytes(out)


def pack(im, palette):
    w, h = im.size
    index = {c: k for k, c in enumerate(palette)}
    px = im.load()
    body = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            a = index[px[x, y]]
            b = index[px[x + 1, y]] if x + 1 < w else 0
            body.append((a << 4) | b)
    pal = bytes(gcolor8(c) for c in palette) + bytes([0xC0] * (16 - len(palette)))
    packed = lz_compress(body)
    assert lz_decompress(packed, len(body)) == bytes(body)
    return b'DB' + bytes([1, 1]) + struct.pack('<HH', w, h) + pal + packed


def crop(im, size):
    w, h = size
    x = (im.size[0] - w) // 2
    y = (im.size[1] - h) // 2
    return im.crop((x, y, x + w, y + h))


def build_backdrops():
    os.makedirs(DATA, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    total = {p: 0 for p in PLATFORMS}
    for name, (table, cols) in BACKDROPS.items():
        src = os.path.join(RAW, name + '.png')
        if os.path.exists(src):
            im = Image.open(src).convert('RGB')
            if im.size != (260, 260):
                im = im.resize((260, 260), Image.NEAREST)
            tag = ''
        else:
            im = placeholder(cols)
            tag = '  (placeholder)'
        base = dimmed(im, table)
        for plat, size in PLATFORMS.items():
            out, palette = to16(crop(base, size))
            blob = pack(out, palette)
            with open(os.path.join(DATA, 'bd_%s~%s.bin' % (name, plat)), 'wb') as f:
                f.write(blob)
            out.save(os.path.join(PREVIEW, 'bd_%s~%s.png' % (name, plat)))
            total[plat] += len(blob)
            print('bd_%-18s %-7s %2d colours %6d bytes%s' % (name, plat, len(palette), len(blob), tag))
    for p, n in total.items():
        print('backdrops on %s: %d bytes' % (p, n))


def build_icons():
    made = []
    for name in ICONS:
        src = os.path.join(RAW, 'ic_%s.png' % name)
        if os.path.exists(src):
            im = Image.open(src).convert('RGBA')
        else:
            # A placeholder disc, so the C side can reference every icon now.
            im = Image.new('RGBA', (20, 20), (0, 0, 0, 0))
            ImageDraw.Draw(im).ellipse([0, 0, 19, 19], fill=(255, 170, 0, 255), outline=(0, 0, 0, 255))
            print('ic_%-10s placeholder' % name)
        px = im.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                r, g, b, a = px[x, y]
                px[x, y] = (snap(r), snap(g), snap(b), 255) if a >= 128 else (0, 0, 0, 0)
        if name in ICON_LIFT:
            for y in range(im.size[1]):
                for x in range(im.size[0]):
                    r, g, b, a = px[x, y]
                    if a and (r, g, b) != (0, 0, 0):
                        px[x, y] = (min(255, r + 85), min(255, g + 85), min(255, b + 85), 255)
        bbox = im.getbbox()
        if bbox:
            im = im.crop(bbox)
        if max(im.size) > ICON_MAX:
            k = ICON_MAX / max(im.size)
            im = im.resize((max(1, round(im.size[0] * k)), max(1, round(im.size[1] * k))), Image.NEAREST)
        im.save(os.path.join(IMAGES, 'ic_%s.png' % name))
        made.append(name)
        print('ic_%-10s %dx%d' % (name, im.size[0], im.size[1]))
    return made


def update_package(icons):
    path = os.path.join(ROOT, 'package.json')
    with open(path) as f:
        pkg = json.load(f)
    media = [m for m in pkg['pebble']['resources']['media']
             if not (m['name'].startswith('BD_') or m['name'].startswith('IC_'))]
    for name in BACKDROPS:
        media.append({'type': 'raw', 'name': 'BD_' + name.upper(), 'file': 'data/bd_%s.bin' % name})
    for name in icons:
        media.append({'type': 'bitmap', 'name': 'IC_' + name.upper(), 'file': 'images/ic_%s.png' % name})
    pkg['pebble']['resources']['media'] = media
    with open(path, 'w') as f:
        json.dump(pkg, f, indent=2)
        f.write('\n')


if __name__ == '__main__':
    if '--list' in sys.argv:
        for name in BACKDROPS:
            if not os.path.exists(os.path.join(RAW, name + '.png')):
                print('missing', name)
        for name in ICONS:
            if not os.path.exists(os.path.join(RAW, 'ic_%s.png' % name)):
                print('missing ic_' + name)
        sys.exit(0)
    build_backdrops()
    update_package(build_icons())
