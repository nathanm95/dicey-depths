#!/usr/bin/env python3
"""Record a store preview GIF of the menus and a fight.

  python3 tools/record_preview.py <emery|gabbro> <out.gif>

Assumes a DD_SHOWCASE build is installed and the app is on its title screen.
It presses BACK (out to the launcher), starts dumping frames through QEMU's
monitor, presses SELECT (the launcher still has Dicey Depths highlighted, so it
relaunches with the splash), then walks the menus into a fight with buttons.

Frames are dumped as fast as the monitor allows, identical neighbours are
merged, and each kept frame is shown for as long as it was really on screen.
Every frame is already in the watch's 64 colours, so the GIF uses exactly
those as one fixed palette (see the pebble-dev notes: 2.2 MB -> 0.5 MB).
"""
import os, socket, subprocess, sys, tempfile, threading, time
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shot  # noqa: E402  (monitor_port, rescale)

PLAT, OUT = sys.argv[1], sys.argv[2]
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

# (button, seconds to wait after it). None = just wait.
SCRIPT = [
    (None, 3.0),                                   # splash -> title
    ('down', 0.6), ('down', 0.8), ('up', 0.4), ('up', 0.6),
    ('select', 1.2),                               # campaign: each episode its own set
    ('down', 1.0), ('down', 1.0), ('down', 1.0), ('up', 0.5), ('up', 0.5), ('up', 0.8),
    ('select', 1.4),                               # class select
    ('select', 1.8),                               # floor map, in the dungeon
    ('select', 2.2),                               # into the fight
    ('select', 0.7), ('select', 1.4),              # Sword: take a die, drop it
    ('down', 0.4), ('select', 0.7), ('select', 1.4),   # Shield
    ('down', 0.3), ('down', 0.3), ('down', 0.3), ('down', 0.5),
    ('select', 6.0),                               # END TURN: the enemy's go, then yours
]


def press(button):
    subprocess.run(['pebble', 'emu-button', '--emulator', PLAT, 'click', button],
                   cwd=ROOT, capture_output=True)


def main():
    tmp = tempfile.mkdtemp(prefix='dd_rec_')
    # BACK leaves to the watchface; SELECT there opens the launcher, which still
    # has Dicey Depths highlighted; the SELECT in driver() launches it.
    press('back')
    time.sleep(1.2)
    press('select')
    time.sleep(1.2)
    sock = socket.create_connection(('127.0.0.1', shot.monitor_port(PLAT)), timeout=5)
    time.sleep(0.3)
    sock.recv(65536)
    frames, stop = [], threading.Event()

    def driver():
        press('select')
        for button, wait in SCRIPT:
            if button:
                press(button)
            time.sleep(wait)
        stop.set()

    t = threading.Thread(target=driver)
    t.start()
    n = 0
    while not stop.is_set():
        path = os.path.join(tmp, 'f%05d.ppm' % n)
        sock.sendall(('screendump %s\n' % path).encode())
        t0 = time.time()
        while time.time() - t0 < 1.0:
            try:
                if os.path.getsize(path) > 1000:
                    time.sleep(0.01)
                    break
            except OSError:
                pass
            time.sleep(0.005)
        try:
            sock.recv(65536)
        except socket.timeout:
            pass
        frames.append((time.time(), path))
        n += 1
        time.sleep(0.03)
    t.join()
    sock.close()

    # Merge runs of identical frames; each kept frame lasts until the next.
    kept = []
    last = None
    for when, path in frames:
        shot.rescale(path, PLAT)
        try:
            im = Image.open(path).convert('RGB')
        except Exception:
            continue
        data = im.tobytes()
        if data == last:
            continue
        kept.append((when, im))
        last = data
    # Drop the launcher (light) frames before the splash (dark) comes up.
    def dark(img):
        small = img.resize((32, 32))
        return sum(sum(p) for p in small.getdata()) / (32 * 32 * 3) < 110
    while len(kept) > 1 and not dark(kept[0][1]):
        kept.pop(0)
    durations = []
    for i, (when, _) in enumerate(kept):
        nxt = kept[i + 1][0] if i + 1 < len(kept) else when + 1.5
        durations.append(max(40, min(1200, int((nxt - when) * 1000))))

    pal_img = Image.new('P', (1, 1))
    pal = []
    for r in (0, 85, 170, 255):
        for g in (0, 85, 170, 255):
            for b in (0, 85, 170, 255):
                pal += [r, g, b]
    pal_img.putpalette(pal + [0] * (768 - len(pal)))
    seq = [im.quantize(palette=pal_img, dither=Image.Dither.NONE) for _, im in kept]
    seq[0].save(OUT, save_all=True, append_images=seq[1:], duration=durations, loop=0, optimize=True)
    total = sum(durations) / 1000
    print('%s: %d dumps, %d distinct frames, %.1fs, %d KB' % (OUT, len(frames), len(seq), total,
                                                               os.path.getsize(OUT) // 1024))


if __name__ == '__main__':
    main()
