#!/usr/bin/env python3 -I
"""Runs the same scripted input on the port and on ares and puts the pictures side by side.

Usage: tools/tour.py <name> "<script>" [--port-only|--ares-only] [--keep-frames] [--port-env K=V ...]
Script tokens (as tools/rec_script.py, from boot):
  A B Z START L R UP DOWN LEFT RIGHT CU CD CL CR, A+B ...   tap (held 6 frames, then 1 idle)
  hold:A:30                                                hold 30 frames
  <number>                                                 wait that many frames
  shot / shot:<label>                                      capture this frame on both sides
Output: build/tour/<name>/<k>_<label>_port.png, _ares.png and a side-by-side <k>_<label>.png (port left, ares right).
The port's raw frames (build/tour/<name>/port_shots) are deleted afterwards; --keep-frames keeps them.
--audio also records the game's sound on both sides (build/tour/<name>/port.pcm: PORT_AUDIO_DUMP, the AI rate;
ares.pcm: ARES_REF_WAV, 48 kHz) for tools/audio_compare.py. The port runs with its sound engine on (the recording
carries "# game audio"; see Port_GameAudio in src/port/os_devices.cpp).
ares runs start from the title-screen state when the script begins with a wait of at least 1500 frames
(ARES_TITLE_STATE, saved 1500 frames after boot), otherwise from boot.
Needs ARES_REF_BUILD / ARES_REF_SRC (see tools/ares_ref/ref.sh).
"""
import os, shutil, subprocess, sys, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUTTONS = {"A": 0x8000, "B": 0x4000, "Z": 0x2000, "START": 0x1000, "UP": 0x0800, "DOWN": 0x0400, "LEFT": 0x0200,
           "RIGHT": 0x0100, "L": 0x0020, "R": 0x0010, "CU": 0x0008, "CD": 0x0004, "CL": 0x0002, "CR": 0x0001}


def parse(script):
    """-> (presses [(frame, names, hold)], shots [(frame, label)], end)"""
    frame, presses, shots = 0, [], []
    for tok in script.split():
        if tok.isdigit():
            frame += int(tok)
        elif tok.lower().startswith("shot"):
            label = tok.split(":", 1)[1] if ":" in tok else "s%d" % len(shots)
            shots.append((frame, label))
        elif tok.lower().startswith("hold:"):
            _, spec, n = tok.split(":")
            presses.append((frame, spec.upper(), int(n)))
            frame += int(n) + 1
        else:
            presses.append((frame, tok.upper(), 6))
            frame += 7
    return presses, shots, frame + 2


def mask(spec):
    m = 0
    for n in spec.split("+"):
        m |= BUTTONS[n]
    return m


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def scale_to(w, h, rgb, tw, th):
    out = bytearray(tw * th * 3)
    for y in range(th):
        sy = y * h // th
        for x in range(tw):
            sx = x * w // tw
            o = (y * tw + x) * 3
            i = (sy * w + sx) * 3
            out[o:o + 3] = rgb[i:i + 3]
    return bytes(out)


def main():
    # defaults: build/ares/{build,src} (links to the ares build made with tools/ares_ref/build.sh), build/ares_title.state
    os.environ.setdefault("ARES_REF_BUILD", os.path.join(ROOT, "build", "ares", "build"))
    os.environ.setdefault("ARES_REF_SRC", os.path.join(ROOT, "build", "ares", "src"))
    if os.path.exists(os.path.join(ROOT, "build", "ares_title.state")):
        os.environ.setdefault("ARES_TITLE_STATE", os.path.join(ROOT, "build", "ares_title.state"))
    name, script = sys.argv[1], sys.argv[2]
    flags = sys.argv[3:]
    out = os.path.join(ROOT, "build", "tour", name)
    os.makedirs(out, exist_ok=True)
    presses, shots, end = parse(script)
    port_shots, ares_shots = {}, {}

    if "--ares-only" not in flags:
        rec = os.path.join(out, "port.rec")
        lines = ["# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4\n", "# game audio\n"]
        for f, spec, hold in presses:
            lines.append("%d %d 0 0 0 0 0 0 0 0 0 0 0\n" % (f, mask(spec)))
            lines.append("%d 0 0 0 0 0 0 0 0 0 0 0 0\n" % (f + hold))
        lines.append("# end %d\n" % end)
        open(rec, "w").writelines(lines)
        shotdir = os.path.join(out, "port_shots")
        os.makedirs(shotdir, exist_ok=True)
        for f in os.listdir(shotdir):
            os.remove(os.path.join(shotdir, f))
        env = dict(os.environ, PORT_REPLAY=rec, PORT_STOP_AT=str(end), PORT_SCREENSHOT_DIR=shotdir,
                   PORT_SHOT_AT=",".join(str(f) for f, _ in shots), PORT_SPEED="max", PORT_EXIT_ON_FREEZE="1",
                   PORT_RECORD="last_tour.rec")
        if "--audio" in flags:
            env["PORT_AUDIO_DUMP"] = os.path.join(out, "port.pcm")
        for kv in flags:
            if "=" in kv and not kv.startswith("--"):
                k, v = kv.split("=", 1)
                env[k] = v
        rom = os.environ.get("ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
        log = open(os.path.join(out, "port.log"), "w")
        subprocess.run([os.path.join(ROOT, "build", "pokestadium"), rom], cwd=os.path.join(ROOT, "build"), env=env,
                       stdout=log, stderr=subprocess.STDOUT)
        got = sorted(int(f[5:10]) for f in os.listdir(shotdir) if f.startswith("stop_") and f.endswith(".ppm"))
        for f, label in shots:
            cand = [g for g in got if g >= f]
            if cand:
                port_shots[label] = os.path.join(shotdir, "stop_%05d.ppm" % cand[0])
        crash = [l for l in open(os.path.join(out, "port.log")) if l.startswith("*** crash") or l.startswith("[freeze]")]
        if crash:
            print("port:", crash[0].strip())

    if "--port-only" not in flags:
        build = os.environ["ARES_REF_BUILD"]
        state = os.environ.get("ARES_TITLE_STATE")
        offset = 0
        if state and presses and presses[0][0] >= 1500 and (not shots or shots[0][0] >= 1500):
            offset = 1500
        lines = []
        for f, spec, hold in presses:
            lines.append("hold %d %d %s\n" % (f - offset + 1, f - offset + hold, spec))
        for f, label in shots:
            lines.append("shot %d %s\n" % (f - offset + 1, os.path.join(out, "ares_%s.ppm" % label)))
        lines.append("end %d\n" % (end - offset + 1))
        scr = os.path.join(out, "ares.txt")
        open(scr, "w").writelines(lines)
        src = os.environ["ARES_REF_SRC"]
        env = dict(os.environ, ARES_REF_FIRMWARE=os.path.join(src, "mia", "Firmware", "Nintendo 64"),
                   DYLD_LIBRARY_PATH="/Applications/ares.app/Contents/Frameworks")
        if "--audio" in flags:
            env["ARES_REF_WAV"] = os.path.join(out, "ares.pcm")
        rom = os.environ.get("ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
        link = "/tmp/ares_ref_rom.z64"  # nall rewrites path arguments with spaces
        if os.path.islink(link):
            os.remove(link)
        os.symlink(rom, link)
        args = [os.path.join(build, "ares_ref"), link, scr] + (["--load", state] if offset else [])
        r = subprocess.run(args, capture_output=True, text=True, env=env)
        open(os.path.join(out, "ares.log"), "w").write(r.stdout + r.stderr)
        for f, label in shots:
            p = os.path.join(out, "ares_%s.ppm" % label)
            if os.path.exists(p):
                ares_shots[label] = p

    for f, label in shots:  # reuse earlier ares pictures (--port-only)
        p = os.path.join(out, "ares_%s.ppm" % label)
        if label not in ares_shots and os.path.exists(p):
            ares_shots[label] = p
    # side by side: port (640x480) | ares (640x240 field doubled to 640x480)
    for k, (f, label) in enumerate(shots):
        tiles = []
        if label in port_shots:
            w, h, rgb = read_ppm(port_shots[label])
            tiles.append(scale_to(w, h, rgb, 640, 480))
        if label in ares_shots:
            w, h, rgb = read_ppm(ares_shots[label])
            tiles.append(scale_to(w, h, rgb, 640, 480))
        if not tiles:
            continue
        W = 640 * len(tiles)
        canvas = bytearray(W * 480 * 3)
        for i, t in enumerate(tiles):
            for y in range(480):
                canvas[(y * W + i * 640) * 3:(y * W + i * 640 + 640) * 3] = t[y * 640 * 3:(y + 1) * 640 * 3]
        write_png(os.path.join(out, "%02d_%s.png" % (k, label)), W, 480, bytes(canvas))
        print("%02d_%s: port %s, ares %s" % (k, label, "yes" if label in port_shots else "-",
                                           "yes" if label in ares_shots else "-"))
    # the port's raw frames (uncompressed, several MB each) are only needed for the pictures above: drop them unless
    # --keep-frames, so runs do not pile up gigabytes in build/tour (the ares_*.ppm stay for --port-only reruns)
    shotdir = os.path.join(out, "port_shots")
    if "--keep-frames" not in flags and os.path.isdir(shotdir):
        shutil.rmtree(shotdir)


if __name__ == "__main__":
    main()
