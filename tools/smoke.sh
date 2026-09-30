#!/bin/zsh
# End-of-fight smoke test: the five ways a fight can end, each of which leaves
# combat differently. Buttons only -- the emulator's touch device reports
# contact with no coordinates, so touch is covered by tools/hostsim instead.
#
#   smoke.sh <class 0|1> <1=win 2=lose> <tag> [platform] [0=campaign 1=endless]
cd "$(dirname "$0")/.."
SP=${SP:-/tmp}
CLS=${1:-0}; MODE=${2:-1}; TAG=${3:-run}; PLAT=${4:-emery}; GMODE=${5:-0}
pebble kill >/dev/null 2>&1
rm -f ~/Library/Application\ Support/Pebble\ SDK/4.33.1/$PLAT/qemu_spi_flash.bin
export DD_BOOT_FLOOR=3 DD_BOOT_CLASS=$CLS DD_ONESHOT=$MODE DD_BOOT_MODE=$GMODE
pebble clean >/dev/null 2>&1
pebble build 2>&1 | grep -E "error" | head -3
pebble install --emulator $PLAT >/dev/null 2>&1
sleep 9
PORT=$(ps aux | grep -o "monitor tcp::[0-9]*" | head -1 | grep -o "[0-9]*$")
QPID=$(pgrep -f qemu-pebble | head -1)
B() { pebble emu-button --emulator $PLAT click "$1" >/dev/null 2>&1; sleep 0.25; }
B down; B select          # into the boss fight; DD_ONESHOT ends it on a timer
sleep 8
python3 "$SP/shot.py" $PORT "$SP/$TAG-end.png" >/dev/null 2>&1
NOW=$(pgrep -f qemu-pebble | head -1)
[ "$QPID" != "$NOW" ] && { echo "$TAG: FAILED - died at the end screen"; exit 1; }
B select                  # dismiss -> floor appear -> leave -> title
sleep 3
python3 "$SP/shot.py" $PORT "$SP/$TAG-after.png" >/dev/null 2>&1
NOW=$(pgrep -f qemu-pebble | head -1)
if [ "$QPID" = "$NOW" ]; then echo "$TAG: PASS"; else echo "$TAG: FAILED - emulator restarted"; fi
