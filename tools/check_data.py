#!/usr/bin/env python3
"""Validate the hand-written tables against the constants that index them.

These tables are plain C arrays indexed by #define'd constants and by enum
values in other files. Nothing in the compiler checks that they agree, and when
they disagree the game does not crash - it quietly does the wrong thing. That is
exactly how every campaign boss silently became the Drake. Run this after
touching enemies.c, items.c, classes.c or rules.c.
"""
import re, sys, os

ROOT = os.path.join(os.path.dirname(__file__), "..", "src", "c")
def src(name): return open(os.path.join(ROOT, name)).read()

fails, warns = [], []
def fail(m): fails.append(m)
def warn(m): warns.append(m)

def rows(text, array):
    """Entries of a `static const X arr[] = { ... };` table, in order."""
    body = text.split(array, 1)[1].split("\n};", 1)[0]
    return [l for l in body.split("\n") if '{"' in l]

def name_of(line): return re.search(r'\{"([^"]+)"', line).group(1)

items   = rows(src("items.c"),   "static const ItemDef s_items[] = {")
enemies = rows(src("enemies.c"), "static const EnemyDef s_enemies[] = {")
gameh   = src("game.h")

# --- 1. constants that point into items.c --------------------------------
ITEM_REROLL = int(re.search(r"#define ITEM_REROLL\s+(\d+)", gameh).group(1))
reroll_idx = [i for i, l in enumerate(items) if name_of(l) == "Reroll"]
if not reroll_idx:
    fail("items.c has no entry named 'Reroll'")
elif reroll_idx[0] != ITEM_REROLL:
    fail("ITEM_REROLL is %d but 'Reroll' is at index %d" % (ITEM_REROLL, reroll_idx[0]))

# --- 2. constants that point into enemies.c ------------------------------
BOSS_FIRST = int(re.search(r"#define BOSS_FIRST (\d+)", src("enemies.c")).group(1))
boss_idx = [i for i, l in enumerate(enemies) if l.rstrip().endswith("true},")]
if not boss_idx:
    fail("no enemy is flagged boss:true")
else:
    if boss_idx[0] != BOSS_FIRST:
        fail("BOSS_FIRST is %d but the first boss is at index %d" % (BOSS_FIRST, boss_idx[0]))
    if boss_idx != list(range(boss_idx[0], boss_idx[0] + len(boss_idx))):
        fail("bosses are not contiguous: %s" % boss_idx)
    rule_count = len([l for l in src("rules.c").split("\n") if re.match(r'\s*"', l)])
    campaign_n = int(re.search(r"#define RULE_CAMPAIGN_COUNT (\d+)", gameh).group(1))
    if len(boss_idx) < campaign_n:
        fail("%d campaign levels but only %d bosses" % (campaign_n, len(boss_idx)))
    for lvl in range(campaign_n):
        idx = BOSS_FIRST + lvl
        if idx >= len(enemies):
            fail("campaign level %d boss index %d is past the end of the table" % (lvl + 1, idx))

# --- 3. every /*nn*/ comment matches its real index ----------------------
for label, table in (("items.c", items), ("enemies.c", enemies)):
    for i, l in enumerate(table):
        m = re.search(r"/\*\s*(\d+)\s*\*/", l)
        if m and int(m.group(1)) != i:
            fail("%s: %s is commented /*%s*/ but sits at index %d"
                 % (label, name_of(l), m.group(1), i))

# --- 4. every item id referenced anywhere actually exists ----------------
n_items = len(items)
for i, l in enumerate(enemies):
    equip = re.search(r"\{([0-9,\s]+)\}\s*,\s*\d+\s*,", l)
    if equip:
        for tok in equip.group(1).split(","):
            tok = tok.strip()
            if tok and int(tok) >= n_items:
                fail("enemy %s carries item id %s but only %d items exist"
                     % (name_of(l), tok, n_items))

# --- 5. loot tables: in range, and no enemy-only kit ---------------------
enemy_only = set()
for i, l in enumerate(items):
    price = re.search(r",\s*\d+\s*,\s*(\d+)\s*,\s*C_", l)
    if price and price.group(1) == "0":
        enemy_only.add(i)
for arr in ("s_early", "s_mid", "s_late"):
    m = re.search(arr + r"\[\]\s*=\s*\{([^}]*)\}", src("items.c"), re.S)
    ids = [int(x) for x in re.findall(r"\d+", m.group(1))]
    if not ids:
        fail("loot table %s is empty" % arr)
    for i in ids:
        if i >= n_items:
            fail("loot table %s references item %d, out of range" % (arr, i))
        elif i in enemy_only:
            fail("loot table %s offers %s, which is enemy-only kit (price 0)"
                 % (arr, name_of(items[i])))
    if len(set(ids)) != len(ids):
        warn("loot table %s has duplicate entries" % arr)

# --- 6. class starting kits ---------------------------------------------
for m in re.finditer(r"\{(\d+|ITEM_REROLL)\s*,\s*(\d+|ITEM_REROLL)\s*,\s*(\d+|ITEM_REROLL)\}\s*,\s*(\d+)",
                     src("classes.c")):
    kit = [ITEM_REROLL if g == "ITEM_REROLL" else int(g) for g in m.groups()[:3]]
    for k in kit:
        if k >= n_items:
            fail("a class starting kit references item %d, out of range" % k)

# --- 7. parallel string tables are the right length ---------------------
def count_strings(text, marker):
    body = text.split(marker, 1)[1].split("};", 1)[0]
    return len(re.findall(r'"', body)) // 2

checks = [
    ("rules.c", "s_names[RULE_COUNT] = {",  "RULE_COUNT",  gameh),
    ("rules.c", "s_blurbs[RULE_COUNT] = {", "RULE_COUNT",  gameh),
    ("achieve.c", "s_names[ACH_COUNT] = {",  "ACH_COUNT",  src("achieve.h")),
    ("achieve.c", "s_blurbs[ACH_COUNT] = {", "ACH_COUNT",  src("achieve.h")),
]
def enum_len(text, name):
    body = text.split("typedef enum", 1)
    for chunk in text.split("typedef enum")[1:]:
        head = chunk.split("}", 1)
        if name in head[1][:40]:
            entries = [e.strip() for e in head[0].split("{",1)[1].split(",")]
            entries = [e for e in entries if e and not e.startswith("//")]
            return len([e for e in entries if not e.startswith(name)])
    return None

for fname, marker, enum_name, enum_src in checks:
    try:
        n = count_strings(src(fname), marker)
    except IndexError:
        warn("could not find %s in %s" % (marker, fname)); continue
    want = enum_len(enum_src, enum_name)
    if want is not None and n != want:
        fail("%s: %s has %d strings but %s is %d" % (fname, marker, n, enum_name, want))

# --- 8. shapes all resolve to a sprite or fall through ------------------
shapes = re.search(r"typedef enum \{([^}]*)\} ShapeId;", gameh, re.S).group(1)
shape_names = [s.strip().split("=")[0].strip()
               for s in shapes.replace("\n", " ").split(",") if s.strip()]
mapped = set(re.findall(r"case (SHAPE_\w+):\s*return RESOURCE_ID", src("portraits.c")))
missing = [s for s in shape_names if s not in mapped]
if missing:
    warn("shapes with no sprite (procedural fallback): %s" % ", ".join(missing))

# --- 9. depth ranges are sane -------------------------------------------
for l in enemies:
    nums = re.findall(r",\s*(\d+)\s*,\s*(\d+)\s*,\s*SHAPE_", l)
    if nums:
        lo, hi = int(nums[0][0]), int(nums[0][1])
        if lo > hi:
            fail("enemy %s has depth_min %d > depth_max %d" % (name_of(l), lo, hi))

# --- 10. duplicate names ------------------------------------------------
for label, table in (("items.c", items), ("enemies.c", enemies)):
    seen = {}
    for i, l in enumerate(table):
        n = name_of(l)
        if n in seen:
            warn("%s: duplicate name %r at %d and %d" % (label, n, seen[n], i))
        seen[n] = i

print("items: %d   enemies: %d   bosses: %d" % (n_items, len(enemies), len(boss_idx)))
for w in warns: print("WARN  " + w)
for f in fails: print("FAIL  " + f)
print("\n%d failures, %d warnings" % (len(fails), len(warns)))
sys.exit(1 if fails else 0)
