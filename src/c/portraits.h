#pragma once
#include <pebble.h>
#include "game.h"

// Enemies are drawn in code from flat shapes and colour. No PNGs, no art
// pipeline, nothing eating the resource budget — and every one of them can be
// squashed and stretched by passing a scaled rect, which is what gives the
// idle bounce its life.
void portrait_draw(GContext *ctx, uint8_t shape, GRect box, bool flash);
