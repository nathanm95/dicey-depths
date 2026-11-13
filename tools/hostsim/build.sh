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
# The same again with the round display compiled in (gabbro). ui.c decides
# rectangle-or-circle with #if PBL_ROUND, so the build above never runs it.
clang -std=c11 -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -DPBL_ROUND -DPBL_DISPLAY_WIDTH=260 -DPBL_DISPLAY_HEIGHT=260 \
  -I. -I$SRC -include ../../build/gabbro/src/resource_ids.auto.h \
  fuzz_touch.c stubs.c \
  $SRC/items.c $SRC/run.c $SRC/enemies.c $SRC/classes.c $SRC/rules.c \
  $SRC/anim.c $SRC/ui.c $SRC/portraits.c $SRC/achieve.c $SRC/score.c \
  -o fuzz_touch_round 2>&1 | head -40
# The rules regression tests (tools/hostsim/rules_test.c), rectangle and round.
for v in "" "_round"; do
  EXTRA=""; RES=emery
  [ -n "$v" ] && { EXTRA="-DPBL_ROUND -DPBL_DISPLAY_WIDTH=260 -DPBL_DISPLAY_HEIGHT=260"; RES=gabbro; }
  clang -std=c11 -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    ${=EXTRA} -I. -I$SRC -include ../../build/$RES/src/resource_ids.auto.h \
    rules_test.c stubs.c \
    $SRC/items.c $SRC/run.c $SRC/enemies.c $SRC/classes.c $SRC/rules.c \
    $SRC/anim.c $SRC/ui.c $SRC/portraits.c $SRC/achieve.c $SRC/score.c \
    -o rules_test$v 2>&1 | head -40
done
