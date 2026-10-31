#!/usr/bin/env python3 -I
"""Checks picture-to-sound timing of a run (ares or the port) against a video of the game (pure Python + ffmpeg).

  tools/av_sync.py <video> --ares <ares.pcm> --pictures <dir> [--frames A:B:STEP ...] [--offset S]
  tools/av_sync.py <video> --port <port.pcm> --pictures <dir> [--frames A:B:STEP ...] [--offset S]

<video>: any file ffmpeg reads, e.g. a capture of a real console from `yt-dlp -f "bv*[height<=480]+ba"`.
--ares: tools/ares_ref ARES_REF_WAV output (48 kHz) with its <file>.frames (u32 sample count after each VI frame).
--port: PORT_AUDIO_DUMP output with its <file>.frames ("<VI frame> <samples>" per buffer); --port-rate (default
32006, the "[audio] rate" line). --pictures: the run's pictures, one per VI frame, named with the frame number
(ares_ref `every 1 <dir>`: f_000123.ppm; the port: PORT_SHOT_AT=1,2,... PORT_SCREENSHOT_DIR=<dir>: stop_00123.ppm).

Both runs must start from boot without input (deterministic). The sound is aligned first: coarse over the whole
file (50 ms loudness blocks), then locally every 2 s (4 s windows, 10 ms blocks), which prints how the offset moves
(a scene that comes earlier or later than in the video shows as a step). Then, for each frame given by --frames
(default: every 10th picture), the video frame that looks most like the run's picture (normalized 32x24
thumbnails, within 3 s of where the sound puts it) is mapped back to a run frame through the local sound offset.
The difference is how many VI frames later the video shows that picture relative to the music (with the match
quality, the thumbnails' correlation; below 0.3 the frame is left out, and so are still pictures, which match many
video frames and time nothing). It is constant
(capture delay, a few frames) when picture and sound are timed as in the video, and drifts where they are not:
the boot diorama without the N64 load waits went +1, +13, +35, +42, +46 against a console capture, with them
-4, -6, -4, -6, -7. --offset S: start the coarse search around "run time = video time + S" (default: search
+-10 s).
"""
import array, bisect, glob, math, os, re, struct, subprocess, sys

FPS_N64 = 48681812 / (3094 * 263)  # VI frames per second (NTSC)
BLOCK = 480                        # 10 ms at 48 kHz
TW, TH = 32, 24                    # thumbnail size


def pcm48(path, rate):
    """s16 stereo PCM at 48 kHz as an array (ffmpeg resamples other rates and decodes videos)."""
    if rate is None:
        cmd = ["ffmpeg", "-v", "error", "-i", path, "-vn", "-ac", "2", "-ar", "48000", "-f", "s16le", "-"]
    elif rate != 48000:
        cmd = ["ffmpeg", "-v", "error", "-f", "s16le", "-ar", str(rate), "-ac", "2", "-i", path,
               "-ar", "48000", "-f", "s16le", "-"]
    else:
        cmd = None
    data = subprocess.run(cmd, check=True, capture_output=True).stdout if cmd else open(path, "rb").read()
    a = array.array("h")
    a.frombytes(data[: len(data) // 4 * 4])
    if sys.byteorder != "little":
        a.byteswap()
    return a


def envelope(a, block):
    """log RMS per block (both channels, every third sample)."""
    out = []
    for i in range(0, len(a) // 2 - block, block):
        s = 0
        for j in range(2 * i, 2 * (i + block), 6):
            s += a[j] * a[j]
        out.append(math.log(math.sqrt(s / (block / 3)) + 1))
    return out


def corr(a, b):
    n = len(a)
    ma, mb = sum(a) / n, sum(b) / n
    num = da = db = 0.0
    for x, y in zip(a, b):
        x -= ma
        y -= mb
        num += x * y
        da += x * x
        db += y * y
    return num / (math.sqrt(da * db) + 1e-9)


def best_lag(v, r, lo, hi, start=0, length=None):
    """Lag k (blocks, r = v shifted by k) in [lo, hi] with the best correlation of v[start:start+length]."""
    length = length or len(v)
    w = v[start:start + length]
    best = (-2.0, 0)
    for k in range(lo, hi + 1):
        if start + k < 0 or start + length + k > len(r):
            continue
        best = max(best, (corr(w, r[start + k:start + length + k]), k))
    return best


def sample_counts(args):
    """48 kHz sample count of the run's sound after each VI frame."""
    if args["ares"]:
        d = open(args["ares"] + ".frames", "rb").read()
        return list(struct.unpack("<%dI" % (len(d) // 4), d))
    per = {}
    for line in open(args["port"] + ".frames"):
        f, n = map(int, line.split())
        per[f] = per.get(f, 0) + n
    out, c = [], 0.0
    for f in range(max(per) + 1):
        c += per.get(f, 0) * 48000 / args["port_rate"]
        out.append(int(c))
    return out


def norm(t):
    m = sum(t) / len(t)
    sd = math.sqrt(sum((x - m) ** 2 for x in t) / len(t))
    return [(x - m) / (sd + 1e-6) for x in t], sd


def ppm_thumb(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    return [px[((int((y + .5) * h / TH)) * w + int((x + .5) * w / TW)) * 3 + 1] for y in range(TH) for x in range(TW)]


def video_thumbs(path):
    """Normalized green-ish (gray) thumbnails of every video frame, and the frame rate."""
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=r_frame_rate",
                            "-of", "csv=p=0", path], check=True, capture_output=True, text=True).stdout.strip()
    num, den = (probe.split("/") + ["1"])[:2]
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-an", "-vf", f"scale={TW}:{TH},format=gray",
                          "-f", "rawvideo", "-"], check=True, capture_output=True).stdout
    n = TW * TH
    return [norm(list(raw[i * n:(i + 1) * n]))[0] for i in range(len(raw) // n)], float(num) / float(den)


def main(argv):
    args = {"video": None, "ares": None, "port": None, "pictures": None, "frames": [], "offset": None,
            "port_rate": 32006}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--ares", "--port", "--pictures", "--offset", "--port-rate", "--frames"):
            v = argv[i + 1]
            i += 2
            if a == "--frames":
                lo, hi, step = (int(x) for x in v.split(":"))
                args["frames"].append(range(lo, hi + 1, step))
            elif a == "--offset":
                args["offset"] = float(v)
            elif a == "--port-rate":
                args["port_rate"] = int(v)
            else:
                args[a[2:]] = v
        elif a.startswith("-"):
            sys.exit(__doc__)
        else:
            args["video"] = a
            i += 1
    if not args["video"] or not args["pictures"] or bool(args["ares"]) == bool(args["port"]):
        sys.exit(__doc__)

    run = pcm48(args["ares"] or args["port"], 48000 if args["ares"] else args["port_rate"])
    vid = pcm48(args["video"], None)
    counts = sample_counts(args)

    # sound: coarse offset over the whole file, then local offsets
    cv, cr = envelope(vid, BLOCK * 5), envelope(run, BLOCK * 5)
    if args["offset"] is None:
        lo, hi = -200, 200
    else:
        lo = hi = int(round(args["offset"] * 20))
        lo, hi = lo - 30, hi + 30
    margin = max(abs(lo), abs(hi))
    c, k = best_lag(cv, cr, lo, hi, margin, min(len(cv), len(cr)) - 2 * margin)
    print(f"sound: run = video {k * 0.05:+.2f} s (envelope correlation {c:.2f})")
    ev, er = envelope(vid, BLOCK), envelope(run, BLOCK)
    local = {}
    for t in range(2, int(min(len(ev), len(er) - k * 5) / 100) - 4, 2):  # (from 2 s: the boot is silent)
        cc, kk = best_lag(ev, er, k * 5 - 150, k * 5 + 150, t * 100, 400)
        if cc > 0.5:
            local[t] = kk / 100
    # median of each window and its neighbours: a window on a quiet or ambiguous stretch can lock onto a wrong lag
    # (at the ends, the one neighbour counts twice)
    local = {t: sorted((local.get(t - 2, local.get(t + 2, local[t])), local[t],
                        local.get(t + 2, local.get(t - 2, local[t]))))[1] for t in local}
    print("local sound offset (video s: run = video + offset):",
          " ".join(f"{t}:{o:+.2f}" for t, o in local.items()))
    if not local:
        sys.exit("no usable sound alignment")

    def run_frame_of_video_time(tv):
        t = min(local, key=lambda x: abs(x + 2 - tv))
        return bisect.bisect_left(counts, (tv + local[t]) * 48000)

    # pictures
    pics = {}
    for p in glob.glob(os.path.join(args["pictures"], "*.ppm")):
        m = re.search(r"(\d+)\.ppm$", p)
        if m:
            pics[int(m.group(1))] = p
    frames = sorted(pics)
    wanted = [f for r in args["frames"] for f in r] if args["frames"] else frames[::10]
    vt, fps = video_thumbs(args["video"])
    print("run frame: video shows it N frames later relative to the music (match quality)")
    line = []
    for f in wanted:
        j = bisect.bisect_left(frames, f)
        if j == len(frames):
            break
        f = frames[j]
        if f >= len(counts):
            break
        raw = ppm_thumb(pics[f])
        a, sd = norm(raw)
        if sd < 3:
            continue  # black or flat: nothing to match
        k = bisect.bisect_left(frames, f + 10)
        if k < len(frames) and sum(abs(x - y) for x, y in zip(raw, ppm_thumb(pics[frames[k]]))) / len(raw) < 1:
            continue  # a still picture (the same 10 frames later): it matches many video frames, which times nothing
        tr = counts[f] / 48000
        t = min(local, key=lambda x: abs(x + 2 - (tr - local[x])))
        tv0 = tr - local[t]
        lo, hi = max(0, int((tv0 - 3) * fps)), min(len(vt), int((tv0 + 3) * fps))
        if lo >= hi:
            continue
        q, best = max((sum(x * y for x, y in zip(a, vt[i])) / len(a), i) for i in range(lo, hi))
        if q < 0.3:
            continue  # no picture in the video looks like it (a still picture with other text, a different scene)
        line.append(f"{f}:{run_frame_of_video_time(best / fps) - f:+d}({q:.2f})")
        if len(line) == 8:
            print("  " + " ".join(line))
            line = []
    if line:
        print("  " + " ".join(line))


if __name__ == "__main__":
    main(sys.argv[1:])
