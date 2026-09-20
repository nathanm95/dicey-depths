#!/usr/bin/env python3
"""Check every sprite in resources/images against what the hardware can show.

Run this on anything before committing it. A PNG that fails here still
"works" — the resource compiler will quietly approximate it — but the result
on a 64-colour display is dithered mush at 48 pixels across.
"""
from PIL import Image
import glob, os, sys

LEVELS = {0, 85, 170, 255}
root = os.path.join(os.path.dirname(__file__), "..", "resources", "images")
bad_files = 0

for path in sorted(glob.glob(os.path.join(root, "*.png"))):
    im = Image.open(path).convert("RGBA")
    px = list(im.get_flattened_data())
    opaque = [p for p in px if p[3] > 0]
    colours = {(p[0], p[1], p[2]) for p in opaque}
    illegal = {c for c in colours if not all(v in LEVELS for v in c)}
    alphas = sorted({p[3] for p in px})
    partial = [a for a in alphas if a not in (0, 255)]

    # Size limits depend on what the asset is for: an enemy portrait has to fit a
    # ~52px box in the combat band, while a splash sprite legitimately fills most
    # of the screen. The smaller display (emery, 200x228) is the ceiling for
    # anything, since a bitmap cannot be scaled down to fit.
    name = os.path.basename(path)
    if name.startswith("enemy_"):
        limit, what = (64, 64), "an enemy portrait box"
    else:
        limit, what = (200, 228), "the smaller display (emery)"

    problems = []
    if illegal:
        problems.append("%d colours off-palette %s" % (len(illegal), sorted(illegal)[:3]))
    if partial:
        problems.append("partial alpha %s (use 0 or 255)" % partial[:3])
    if im.size[0] > limit[0] or im.size[1] > limit[1]:
        problems.append("%dx%d does not fit %s (%dx%d)" % (im.size + (what,) + limit))

    status = "FAIL" if problems else "ok"
    if problems:
        bad_files += 1
    print("%-24s %-9s %2d colours  %s%s" % (
        os.path.basename(path), "%dx%d" % im.size, len(colours), status,
        "  - " + "; ".join(problems) if problems else ""))

sys.exit(1 if bad_files else 0)
