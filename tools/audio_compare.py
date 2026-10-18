#!/usr/bin/env python3 -I
"""Compares the port's game sound with an ares recording (pure Python, no numpy).

  tools/audio_compare.py <port.pcm> <ares.pcm> [--port-rate 32006] [--ares-rate 48000] [--wav <dir>] [--from S] [--to S]
                         [--align-from S --align-to S]

<port.pcm>: PORT_AUDIO_DUMP output (s16 stereo at the AI rate, host byte order = little-endian on the supported
hosts). <ares.pcm>: tools/ares_ref ARES_REF_WAV output (s16le stereo, 48 kHz). The two runs start at different
moments (ares boots slower), so the port's sound is first aligned to ares: coarse by the loudness envelope (10 ms
blocks), then to the sample by cross-correlation around the loudest part. Prints the offset, then per second the
level of both (dBFS RMS, left+right), the envelope correlation and, after the fine alignment, the waveform
correlation of the port's sound resampled to the ares rate. --align-from/--align-to (port seconds) align on that
part only (runs reach later scenes at different times, so each scene needs its own alignment). --wav writes both as
.wav files (port resampled and shifted to line up with ares) for listening.
"""
import array, math, os, struct, sys


def load(path):
    a = array.array("h")
    with open(path, "rb") as f:
        a.frombytes(f.read())
    if sys.byteorder != "little":
        a.byteswap()
    return a


def mono(a):
    return [(a[i] + a[i + 1]) * 0.5 for i in range(0, len(a) - 1, 2)]


def resample(x, src, dst):
    """Linear interpolation from rate src to rate dst."""
    n = int(len(x) * dst / src)
    out = [0.0] * n
    step = src / dst
    for i in range(n):
        p = i * step
        j = int(p)
        f = p - j
        a = x[j] if j < len(x) else 0.0
        b = x[j + 1] if j + 1 < len(x) else a
        out[i] = a + (b - a) * f
    return out


def envelope(x, block):
    env = []
    for i in range(0, len(x) - block + 1, block):
        s = 0.0
        for v in x[i:i + block]:
            s += v * v
        env.append(math.sqrt(s / block))
    return env


def db(v):
    return 20 * math.log10(v / 32768.0) if v > 1e-9 else -120.0


def corr(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    ma = sum(a[:n]) / n
    mb = sum(b[:n]) / n
    sab = saa = sbb = 0.0
    for i in range(n):
        da = a[i] - ma
        dbb = b[i] - mb
        sab += da * dbb
        saa += da * da
        sbb += dbb * dbb
    return sab / math.sqrt(saa * sbb) if saa > 0 and sbb > 0 else 0.0


def write_wav(path, samples, rate):
    with open(path, "wb") as f:
        data = array.array("h", (max(-32768, min(32767, int(round(v)))) for v in samples))
        if sys.byteorder != "little":
            data.byteswap()
        raw = data.tobytes()
        f.write(b"RIFF" + struct.pack("<I", 36 + len(raw)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + b"data" + struct.pack("<I", len(raw)) + raw)


def main():
    args = sys.argv[1:]
    opts = {"--port-rate": 32006.0, "--ares-rate": 48000.0, "--wav": None, "--from": 0.0, "--to": 1e9,
            "--align-from": 0.0, "--align-to": 1e9}
    pos = []
    i = 0
    while i < len(args):
        if args[i] in opts:
            opts[args[i]] = args[i + 1] if args[i] == "--wav" else float(args[i + 1])
            i += 2
        else:
            pos.append(args[i])
            i += 1
    if len(pos) != 2:
        print(__doc__)
        sys.exit(1)
    pr, ar = opts["--port-rate"], opts["--ares-rate"]
    port = resample(mono(load(pos[0])), pr, ar)
    ares = mono(load(pos[1]))
    block = int(ar / 100)
    ep, ea = envelope(port, block), envelope(ares, block)
    lp = [db(v) for v in ep]
    la = [db(v) for v in ea]
    # coarse: envelope (in dB, floor -60) lag that best matches, port block k <-> ares block k + lag
    cp = [max(v, -60.0) for v in lp]
    ca = [max(v, -60.0) for v in la]
    best = (-2.0, 0)
    w0 = int(opts["--align-from"] * 100)
    w1 = min(len(cp), int(opts["--align-to"] * 100))
    for lag in range(-len(cp) + 200, len(ca) - 200):
        lo = max(0, -lag, w0)
        hi = min(len(cp), len(ca) - lag, w1)
        if hi - lo < 200:
            continue
        c = corr(cp[lo:hi:2], ca[lo + lag:hi + lag:2])
        if c > best[0]:
            best = (c, lag)
    c, lag = best
    print(f"coarse offset: ares = port + {lag * block / ar:.3f} s (envelope correlation {c:.3f})")
    # fine: sample lag around the loudest second of the overlap
    lo = max(0, -lag, w0)
    hi = min(len(ep), len(ea) - lag, w1)
    loud = max(range(lo, max(lo + 1, hi - 100)), key=lambda k: sum(ep[k:k + 100]))
    s0 = loud * block
    win = port[s0:s0 + int(ar * 0.5)]
    bestf = (-2.0, 0)
    for d in range(-block * 2, block * 2 + 1):
        a0 = s0 + lag * block + d
        if a0 < 0 or a0 + len(win) > len(ares):
            continue
        cc = corr(win[::3], ares[a0:a0 + len(win):3])
        if cc > bestf[0]:
            bestf = (cc, d)
    shift = lag * block + bestf[1]
    print(f"fine offset: ares = port + {shift / ar:.4f} s ({shift} samples at {ar:.0f} Hz), waveform correlation "
          f"{bestf[0]:.3f} at {s0 / ar:.1f} s (port time)")
    print(" port s   port dB  ares dB   env corr  wave corr  local offset ms (corr)")
    sec = int(ar)
    t = max(0, int(-shift / ar) + 1, int(opts["--from"]))
    while True:
        p0 = t * sec
        a0 = p0 + shift
        if p0 + sec > len(port) or a0 + sec > len(ares) or t > opts["--to"]:
            break
        if a0 >= 0:
            pp, aa = port[p0:p0 + sec], ares[a0:a0 + sec]
            rp = math.sqrt(sum(v * v for v in pp) / sec)
            ra = math.sqrt(sum(v * v for v in aa) / sec)
            k0 = p0 // block
            ec = corr(lp[k0:k0 + 100], la[k0 + lag:k0 + lag + 100]) if rp > 30 and ra > 30 else float("nan")
            wc = corr(pp[::2], aa[::2]) if rp > 30 and ra > 30 else float("nan")
            local = ""
            if rp > 30 and ra > 30:  # best waveform match within +-20 ms (drift between the two runs)
                seg = pp[::4][:2000]
                bl = (-2.0, 0)
                for d in range(-int(ar * 0.02), int(ar * 0.02) + 1, 2):
                    b0 = a0 + d
                    if b0 < 0 or b0 + 4 * len(seg) > len(ares):
                        continue
                    cc = corr(seg, ares[b0:b0 + 4 * len(seg):4])
                    if cc > bl[0]:
                        bl = (cc, d)
                local = f"{bl[1] * 1000 / ar:+7.2f} ({bl[0]:.3f})"
            print(f"{t:6d}   {db(rp):7.1f}  {db(ra):7.1f}   {ec:7.3f}   {wc:7.3f}   {local}")
        t += 1
    if opts["--wav"]:
        os.makedirs(opts["--wav"], exist_ok=True)
        aligned = ([0.0] * shift + port) if shift >= 0 else port[-shift:]
        write_wav(os.path.join(opts["--wav"], "port.wav"), aligned, int(ar))
        write_wav(os.path.join(opts["--wav"], "ares.wav"), ares, int(ar))
        print("wrote", os.path.join(opts["--wav"], "port.wav"), "and ares.wav")


main()
