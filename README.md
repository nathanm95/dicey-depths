# Dicey Depths

A dice roguelike for the **Pebble Time 2** and **Pebble Round 2**.

You are a walking six-sided die on a game show. Every turn you roll your dice face-up, and
then decide how to spend them across the equipment you brought in. You always see the
randomness before you commit, which turns a bad roll into a puzzle rather than a punishment.

Inspired by [*Dicey Dungeons*](https://terrycavanagh.itch.io/dicey-dungeons) by Terry
Cavanagh. This is an original implementation for Pebble — no code or assets came from that
game.

## Playing

**Drag** a die onto a card. Or **tap** a die and then tap a card. Or use the **buttons** —
Up/Down to move, Select to pick a die up and Select again to drop it. Tap the enemy, or hold
Select, to see exactly what it is carrying; nothing it does should ever be a surprise.

Each card takes a die that matches its slot — *any*, *odd*, *even*, *4+*, *3-*, or an exact
number. Rare cards take two dice and use the total. **A card fires once per turn**, so a
spare die is worth nothing without a card to put it in — filling all four slots is most of
the build.

**The Warrior** always carries a **Reroll** card: feed it a die and that die is rerolled and
handed straight back to you, unspent. It is the one card that works more than once a turn —
its uses refill each turn, one for every die you carry beyond the first, so a bigger handful
of dice also buys you more second chances.

Taking damage charges a Limit Break: **Fury** makes your next card fire twice. Levelling up
raises your maximum health, which also raises the damage you have to soak to earn that Limit
Break — getting tougher makes your best move rarer.

### Statuses

Statuses attack your *rolls*, not your health, so every debuff is a puzzle rather than a
subtraction.

| | | |
|---|---|---|
| **Burn** | a die catches fire | 2 HP to put out |
| **Frozen** | your best die becomes a 1 | |
| **Shocked** | a card goes offline | drop any die on it to clear |
| **Weakened** | one card does half | |
| **Blind** | dice hidden until spent | |
| **Poison** | 3 HP a turn | straight through shields |
| **Fury** | your next card fires twice | |
| **Silence** | no Limit Break | |

### Modes

**Campaign** — five levels, each three floors. Each is a *ruleset* that rewrites the game
rather than a difficulty step, and each ends in its own boss. Clear one to unlock the next.

1. **Opening Round** — standard rules
2. **Curse of Greed** — start with upgraded weapons, take a curse every turn
3. **Parallel Universe** — Frozen drops every die by 1; Burn is usable but jams the card; Poison infects a card
4. **Elimination Round** — lose one piece of equipment after every fight
5. **Sudden Death** — half the health, double the gold

**Endless** — no boss. Enemies get tougher with depth and from floor 4 each floor draws one
of the campaign rulesets at random. Your ten best runs are kept on the watch.

Lose a fight and the run is over.

## Building

```bash
pebble build
pebble install --emulator emery     # or gabbro
```

Targets `emery` and `gabbro` only — the two Pebble platforms with a touchscreen.

See [CLAUDE.md](CLAUDE.md) for architecture, conventions and the emulator verification
recipe.
