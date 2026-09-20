# Store listing assets

What `pebble publish` asks for, taken from the tool's own prompts
(`pebble_tool/commands/publish.py`) rather than from documentation:

| Asset | Size | File |
|---|---|---|
| `iconLarge` | 144x144 | `icon-large-144.png` |
| `iconSmall` | 80x80 | `icon-small-80.png` |
| Screenshots | native per platform | captured by the publisher from a running emulator |

Screenshots are collected by `pebble publish` itself, one set per target platform
(200x228 for emery, 260x260 for gabbro) — they are not kept in here.

These two icons are **not** restricted to the watch's 64-colour palette, unlike
everything in `resources/images/`: they are shown on the web and on a phone, never
on the watch, so palette-crushing them would only throw away quality. That is why
`tools/verify_art.py` does not look in this directory.

`icon-small-80.png` is a LANCZOS reduction of the 144 so both read as the same
artwork; 144 to 80 is a 1.8x step, which nearest-neighbour cannot do without
tearing the pixel grid.
