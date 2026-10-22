#!/usr/bin/env python3 -I
"""Captures a run as a series of small pictures, to look over a whole stretch of the game (e.g. the intro and the
attract demos) at a glance, without filling the disk.

Usage: tools/frame_sweep.py <out dir> <last frame> [--every N] [--aspect 4:3|16:9] [--rec recording] [--diff D] [--from F]
       tools/frame_sweep.py <out dir> <last frame> --rec replays/<name>.rec   (a stored recording, with its save)

Runs build/pokestadium hidden at full speed (lockstep) from boot, with no input unless a recording is given, and asks
for a picture every N game frames (default 12: 5 per second), from frame F on (default N). Each picture is shrunk to a 320-pixel-wide thumbnail as
soon as it is written and the full-size frame is deleted; a thumbnail is kept only when it differs from the last kept
one by more than D (mean absolute difference per channel, 0-255, default 3), so still scenes leave one picture.
A recording runs from a copy of its save, replays/<name>.flash or else replays/default.flash, as tools/run_replays.py.
The kept thumbnails become contact sheets <out dir>/sheet_<n>.png (5 x 4 pictures each, in order) and
<out dir>/frames.txt lists which game frame each picture shows. The ROM comes from $ROM.
"""
import os, shutil, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tour  # read_ppm, scale_to, write_png

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TW, TH = 320, 240
COLS, ROWS = 5, 4


def arg(name, default):
    if name in sys.argv:
        return sys.argv[sys.argv.index(name) + 1]
    return default


def mean_diff(a, b):
    step = 7  # a sparse sample is enough to tell still frames from changing ones
    total = sum(abs(a[i] - b[i]) for i in range(0, len(a), step))
    return total / (len(a) / step)


def sheets(out, kept):
    for n in range(0, len(kept), COLS * ROWS):
        chunk = kept[n:n + COLS * ROWS]
        rows = (len(chunk) + COLS - 1) // COLS
        W, H = COLS * TW, rows * TH
        canvas = bytearray(W * H * 3)
        for k, (_, thumb) in enumerate(chunk):
            ox, oy = (k % COLS) * TW, (k // COLS) * TH
            for y in range(TH):
                canvas[((oy + y) * W + ox) * 3:((oy + y) * W + ox + TW) * 3] = thumb[y * TW * 3:(y + 1) * TW * 3]
        tour.write_png(os.path.join(out, "sheet_%d.png" % (n // (COLS * ROWS))), W, H, bytes(canvas))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, last = os.path.abspath(sys.argv[1]), int(sys.argv[2])
    every, aspect, threshold = int(arg("--every", 12)), arg("--aspect", "16:9"), float(arg("--diff", 3))
    shots = os.path.join(out, "frames_tmp")
    os.makedirs(shots, exist_ok=True)
    for f in os.listdir(out):
        if f.startswith("sheet_") and f.endswith(".png"):
            os.remove(os.path.join(out, f))
    rec = arg("--rec", None)
    if rec is None:
        rec = os.path.join(out, "empty.rec")
        with open(rec, "w") as f:
            f.write("# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4\n"
                    "# game audio\n# end %d\n" % last)
    flash = os.path.splitext(os.path.abspath(rec))[0] + ".flash"
    if not os.path.exists(flash):
        flash = os.path.join(ROOT, "replays", "default.flash")
    flash_copy = os.path.join(out, "sweep.flash")
    if os.path.exists(flash):
        shutil.copyfile(flash, flash_copy)
    env = dict(os.environ, PORT_FLASH_FILE=flash_copy, PORT_HIDDEN_WINDOW="1", PORT_ASPECT=aspect, PORT_REPLAY=os.path.abspath(rec),
               PORT_STOP_AT=str(last), PORT_SHOT_AT=",".join(str(f) for f in range(int(arg("--from", every)), last + 1, every)),
               PORT_SCREENSHOT_DIR=shots, PORT_SCREENSHOT_MAX_MB="1", PORT_SPEED="max",
               PORT_RECORD=os.path.join(out, "last_sweep.rec"))
    rom = os.environ.get("ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
    log = open(os.path.join(out, "run.log"), "w")
    game = subprocess.Popen([os.path.join(ROOT, "build", "pokestadium"), rom], cwd=os.path.join(ROOT, "build"), env=env,
                            stdout=log, stderr=subprocess.STDOUT)
    kept, last_thumb, seen = [], None, 0

    def drain(final):
        nonlocal last_thumb, seen
        files = sorted(f for f in os.listdir(shots) if f.endswith(".ppm"))
        if not final:
            files = files[:-1]  # the newest may still be being written
        for f in files:
            path = os.path.join(shots, f)
            try:
                w, h, rgb = tour.read_ppm(path)
            except Exception:
                continue
            os.remove(path)
            seen += 1
            thumb = tour.scale_to(w, h, rgb, TW, TH)
            if last_thumb is None or mean_diff(thumb, last_thumb) > threshold:
                kept.append((int(f[5:10]), thumb))
                last_thumb = thumb

    while game.poll() is None:
        drain(False)
        time.sleep(0.2)
    drain(True)
    for f in os.listdir(shots):  # anything left over (e.g. change-detection frames)
        os.remove(os.path.join(shots, f))
    os.rmdir(shots)
    sheets(out, kept)
    with open(os.path.join(out, "frames.txt"), "w") as f:
        for i, (frame, _) in enumerate(kept):
            f.write("sheet %d, picture %d: frame %d\n" % (i // (COLS * ROWS), i % (COLS * ROWS) + 1, frame))
    print("%d pictures, %d kept, %d sheets in %s" % (seen, len(kept), (len(kept) + COLS * ROWS - 1) // (COLS * ROWS), out))


if __name__ == "__main__":
    main()
