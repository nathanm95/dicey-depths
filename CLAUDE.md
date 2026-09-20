# Dicey Depths — Pebble C SDK game

A dice roguelike in the spirit of Terry Cavanagh's *Dicey Dungeons*. You are a walking
six-sided die on a game show: roll your dice, then spend them across four pieces of
equipment. The randomness is shown to you **before** you commit, so a bad roll is a puzzle
rather than a punishment — every design decision below exists to protect that.

One class so far: the Warrior.

## Toolchain

- `pebble-tool` via `uv tool install pebble-tool --python 3.12` (3.13 fails building `stpyv8`)
- Pebble SDK 4.33.1, `pebble` binary on PATH via `~/.zshenv`
- `pebble build` / `pebble install --emulator <platform>`

## Targets: emery and gabbro ONLY — this is deliberate

`package.json` → `targetPlatforms` lists exactly two:

| Platform | Watch | Display | Touch |
|---|---|---|---|
| `emery`  | Pebble Time 2 | 200x228 rectangular colour | yes |
| `gabbro` | Pebble Round 2 | **260x260 round** colour | yes |

These are the only two platforms in the SDK with `PBL_TOUCH` (confirmed in
`sdk-core/pebble/common/tools/pebble_sdk_platform.py`). The game is touch-first, so the
older watches are not targets. Do not "helpfully" add aplite/basalt/chalk/diorite/flint —
the layout assumes a large colour screen and the input model assumes a touchscreen.

## File layout

```
src/c/
  dicey.c       main(), title menu, campaign select, best runs, how-to, results screen
  splash.c      the launch splash — Dicey drops in, title lands, then it leaves
  game.h        every shared type and constant
  combat.c      THE GAME: dice, the 2x2 card grid, statuses, enemy AI, touch, animation
  floor.c       the floor map (MenuLayer of nodes)
  backpack.c    equip/unequip between fights
  shop.c        shop and anvil
  run.c         run state, floor generation, levelling, endless scaling, persistence
  score.c       campaign unlocks + the local top-10 endless table
  items.c       equipment catalogue, curated loot tables, status text
  enemies.c     enemy roster including one boss per campaign level
  rules.c       the five campaign rulesets
  portraits.c   procedural enemy art — flat shapes, no PNG assets
  ui.c/.h       round-aware layout, palette, dice, HP bars
  anim.c/.h     integer tween toolkit (no FPU on this hardware)
docs/
  DESIGN-BREAKDOWN.pdf   the source design analysis this was built from
  reference/             screenshots of the original for art direction
  art/pebble64.png       the 64 colours the hardware can show
store/                   listing assets — NOT watch resources, see store/README.md
tools/                   make_palette.py, verify_art.py
```

## Conventions specific to this project

- **Never assume a rectangle.** `gabbro` is a 260x260 *circle*. A full-width band near the
  top of it is barely 150px wide. All layout goes through `ui_band()` / `ui_half_width_at()`
  in `ui.c`, which return the chord of the circle at a given y. `ui_band()` deliberately
  takes the *tighter* of its top and bottom edges, because measuring only the midpoint lets
  a tall band's corners spill off the glass.

- **There is no `vsnprintf` on Pebble** — the firmware `static_assert`s on it. `toast()` in
  `combat.c` is therefore a variadic *macro* wrapping `snprintf`, not a function. Same trap
  applies anywhere else you want printf-style formatting.

- **No floating point.** There is no FPU. `anim.c` carries tween progress as 0..1000 and all
  easing is integer arithmetic; `ui_isqrt()` does the circle maths. Don't introduce floats.

- **Hand-drawn MenuLayer cells must honour `menu_cell_layer_is_highlighted()`.** If you draw
  a cell yourself with `ui_draw_text_c()` and pass a fixed colour, the selected row comes out
  dark-on-dark. `camp_draw()` in `dicey.c` is the worked example.

- **Three input paths, none of them a fallback:**
  - drag a die from the tray onto a card,
  - tap a die then tap a card,
  - buttons (Up/Down move a cursor, Select picks then drops).

  The button path is not optional: `pebble emu-button` and the QEMU monitor cannot inject a
  drag, so it is the only path that can be verified until real hardware exists. The cursor
  skips empty grid slots and still renders when it lands on one.

- **Combat owns its own touch.** `window_set_touch_bridge_disabled(window, true)` in
  `combat.c`'s `window_appear` takes the touch stream off the system gesture bridge, because
  the grid is a drag surface and not a list to be swiped. Every other screen is a MenuLayer
  and relies on the bridge, which is why `main()` calls `app_touch_navigation_enable(true)` —
  third-party apps are opt-out by default and get no touch navigation without it.

- **`TouchEvent.non_navigational` is checked and must stay checked.** It marks the contact
  that merely woke the watch. Without that guard, the touch that lights the screen plays a die.

- **Everything animates, on purpose.** `combat.c` runs a 30fps `app_timer` the whole time the
  window is up: idle bounce with squash-and-stretch on the enemy, dice tumbling through faces
  before they settle with an overshoot, cards kicking when they fire, hit recoil and shake,
  floating damage numbers, and health bars that drain behind a pale ghost bar. The battery
  cost of the constant idle motion is a known, accepted trade — do not "optimise" it away.

- **Portraits are procedural.** `portraits.c` draws each enemy from circles, rounded rects and
  a scanline triangle fill, sized in percent of its box so one definition works at any scale.
  There are no image resources and the resource budget is essentially untouched. Squash and
  stretch works by passing a scaled rect.

- **Loot tables are curated, not weighted.** `item_roll_for_floor()` picks from hand-authored
  per-depth arrays. Two-slot equipment only appears from depth 3, which is the floor where the
  fourth die arrives. Shop stock is *derived* from floor+node via a hash so that walking out
  and back in cannot reroll the shelves.

## Locked design decisions

Agreed up front; don't relitigate without asking.

- A run is **3 floors**. Floors 1-2 hold three enemies and two support nodes; clearing any
  **two** enemies opens the door, so the third fight is an explicit gold-versus-health trade.
  Floor 3 is an apple then the boss.
- **Four equipment cards in a 2x2 grid that never scrolls.** Reading the whole board at once
  *is* the puzzle. Four dice maximum (2 / 3 / 4 on floors 1 / 2 / 3).
- **A card fires once per turn** (`Card.spent`, cleared in `roll_side()`). This is what makes
  a fourth die worthless without a fourth card, and it is why filling the grid is the build.
- **The Reroll card is the one deliberate exception** to once-per-turn, and is always in the
  Warrior's starting kit. Feeding it a die rerolls THAT die and returns it to the tray
  unspent, so its cost is one of its own uses rather than the die. Its allowance is refilled
  every turn to `dice - 1`, which is how "more dice" also means "more second chances". There
  is no reroll button; the action bar is Limit Break and End Turn only (`ACTION_COUNT`).
- **Permadeath.** Lose a fight and the run is over.
- **Campaign: five levels, each a ruleset** (`rules.c`) rather than a difficulty step, each
  with its own boss. Clear one to unlock the next.
- **Endless: no boss.** Enemy HP scales +12%/floor, +1 enemy die every 4 floors, and from
  floor 4 each floor draws a random campaign ruleset. Score is floors cleared, kept in a
  **local top-10 table** — no phone, no backend.
- **All eight statuses ship**, each announcing in plain English what it did the moment it
  lands. On a screen with no room for a tooltip, narration is how the game teaches itself.

## Artwork: generating sprites that the hardware can actually show

Enemy sprites come from PixelLab (MCP). `portraits.c` maps a shape to a resource
and returns 0 when there is no sprite yet, so the procedural portrait stays as the
fallback and art can be added one enemy at a time without the roster ever having a hole.

**The palette is the whole game here.** `GColor8` is two bits per channel, so the
display has exactly 64 colours — each of R/G/B is 0, 85, 170 or 255 — plus 2-bit
alpha. Art that ignores this still builds: the resource compiler silently
approximates it, and at 48 pixels across the result is dithered mush. So force the
palette at generation time rather than fixing it afterwards:

```
python3 tools/make_palette.py          # writes docs/art/pebble64.png, the 64 legal colours
# then pass that file base64-encoded as PixelLab's color_image_base64
```

Settings that have worked, via `create_image_pixflux`:

| Parameter | Value | Why |
|---|---|---|
| `width`/`height` | 48 | the portrait box is capped at 52px; 48 fits both displays |
| `color_image_base64` | `docs/art/pebble64.png` | forces Pebble-legal colours up front |
| `no_background` | `true` | portraits composite over the enemy band |
| `shading` | `flat shading` | survives a 64-colour palette; matches the art direction |
| `outline` | `single color black outline` | the only thing that reads at 48px on a wrist |
| `detail` | `low detail` | fine detail is invisible and just costs colours |

That produced 6-17 colours per sprite, binary alpha, under 1KB each — four enemies
cost 1.9KB of the 256KB resource budget, so the full roster is nothing.

**Always run `python3 tools/verify_art.py` before committing a sprite.** It fails on
off-palette colours, partial alpha and oversized images. Its size limit is role-aware:
`enemy_*.png` must fit a 64px portrait box, anything else must fit the smaller display
(200x228), because a bitmap cannot be scaled down to fit at runtime.

### The splash, and why it is short

`splash.c` drops Dicey in on an `ease_out_bounce`, slides the title up under him, and
leaves after 2.3s. **Any button or any tap skips it immediately.** A splash on a watch
is a tax charged on every single launch, so it must never be the thing standing between
someone and a game they opened on purpose. Keep it skippable and keep it brief.

### Store listing assets are a different problem

`store/` holds what `pebble publish` asks for: `iconLarge` 144x144 and `iconSmall` 80x80.
Those sizes come from the tool's own prompts in `pebble_tool/commands/publish.py`, not
from documentation. The publisher captures its own screenshots per platform.

Store icons are deliberately **not** palette-restricted — they are shown on the web and
on a phone, never on the watch, so forcing them into 64 colours would throw away quality
for nothing. This is why `tools/verify_art.py` only looks in `resources/images/`.

Resources are declared `"type": "bitmap"` in `package.json`, and drawing needs
`graphics_context_set_compositing_mode(ctx, GCompOpSet)` or the transparency is lost.
Only the enemy currently on screen is held in memory (`s_foe_bmp`, created in
`combat_window_push`, destroyed in `window_unload`), so sprite count does not grow the heap.

### Never rotate a bitmap every frame

`graphics_draw_rotated_bitmap` carries an SDK warning about "performance limitations"
and it means it. Using it for a 48x48 idle sway at 30fps **pegged the emulator at
100% CPU**; swapping to a plain `graphics_draw_bitmap_in_rect` at a bobbing y brought
it to 65%, which is *below* the procedural portrait's 68%. Rotation is now spent only
on the death tumble, where it plays once and a dropped frame costs nothing.

A bitmap also cannot be **scaled** — `graphics_draw_bitmap_in_rect` tiles rather than
scales. So a sprite enemy bobs where a procedural one squashes and stretches. If you
want real squash on a sprite, it has to be pre-rendered frames, not a transform.

## Verifying in the emulator

The emulator **cannot inject a drag** — only `Touchdown` arrives, never `PositionUpdate` or
`Liftoff` — so drag-and-drop is unverifiable here and must be checked on real hardware before
any store upload. What does work:

```bash
# Boot straight into a fight, skipping the menus (the emulator cannot send SELECT
# through the QEMU monitor, and emu-button wedges `pebble screenshot` afterwards).
DD_BOOT_COMBAT=1 DD_BOOT_ENEMY=7 pebble build      # DD_BOOT_RULE=2 for a ruleset
pebble kill && pebble install --emulator emery

# Screenshots: go through QEMU's own monitor, NOT `pebble screenshot`, which times out
# for the rest of the session after any emu-button press.
ps aux | grep -o "monitor tcp::[0-9]*"             # find the port
#   then over a raw TCP socket:  screendump /path/out.ppm
#   then:  sips -s format png /path/out.ppm --out /path/out.png
```

`DD_BOOT_*` is read at **configure** time, so `pebble clean` is needed to change or clear it.

Buttons: the QEMU monitor's `sendkey up|down <ms>` works; SELECT and BACK are still unmapped
there and need `pebble emu-button --emulator <platform> click select|back`.

## Credit

Inspired by *Dicey Dungeons* by Terry Cavanagh, with art by Marlowe Dobbe and music by
Chipzel. This is an original implementation — no code or assets came from that game.
