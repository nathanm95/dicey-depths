#!/bin/zsh
# Build the host fuzzer. -fsanitize catches the out-of-bounds access that a
# watch would turn into a reboot and an emulator cannot show us at all.
cd "$(dirname "$0")"
SRC=../../src/c
clang -std=c11 -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -I. -I$SRC -include ../../build/emery/src/resource_ids.auto.h \
  fuzz_touch.c stubs.c \
  $SRC/items.c $SRC/run.c $SRC/enemies.c $SRC/classes.c $SRC/rules.c \
  $SRC/anim.c $SRC/ui.c $SRC/portraits.c $SRC/achieve.c $SRC/score.c \
  -o fuzz_touch 2>&1 | head -40
