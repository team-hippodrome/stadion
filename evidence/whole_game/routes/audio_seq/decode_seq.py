#!/usr/bin/env python3 -I
"""Decode every libmus song and sound-effect bank the game ships and count the per-voice command opcodes they use.

The sequence player (game/src/libnumus/player.c, Audio_DecodeVoiceCommandStream) reads a voice's command stream:
bytes >= 0x80 are commands (command_func_jumptable[byte & 0x7F]), a byte < 0x80 is a note followed by an optional
velocity (velocity_on) and a 1-2 byte duration (left out while fixed_length is set, unless `ignore` was set for this
one note).  The song and FX files are Yay0 blobs in the ROM's audio region (gAudioData, ROM 0x15C0000, size in its
first word); this tool decompresses every Yay0 blob there, recognises song_t and fx_t headers strictly, and walks
every reachable path of every stream (both sides of each for/next loop, goto targets), tracking the state that
changes the note layout.  A stream that does not decode cleanly (unknown opcode, running off the blob) is reported.

usage: decode_seq.py ROM [--json out.json]
"""
import json, re, struct, sys

def yay0(d, o):
    be = lambda x: struct.unpack(">I", d[x:x+4])[0]
    size, lo, co = be(o+4), be(o+8), be(o+12)
    out = bytearray(); mp = o + 16; bits = 0; nb = 0
    while len(out) < size:
        if nb == 0:
            bits = be(mp); mp += 4; nb = 32
        if bits & 0x80000000:
            out.append(d[o + co]); co += 1
        else:
            v = struct.unpack(">H", d[o+lo:o+lo+2])[0]; lo += 2
            dist = (v & 0xFFF) + 1; n = v >> 12
            if n == 0:
                n = d[o + co] + 18; co += 1
            else:
                n += 2
            for _ in range(n):
                out.append(out[-dist])
        bits = (bits << 1) & 0xFFFFFFFF; nb -= 1
    return bytes(out[:size])

# opcode -> (player.c handler, fixed argument bytes, or "v" for a 1-2 byte variable-length number)
OPS = {
    0x80: ("Audio_ClearVoicePlaybackState", 0), 0x81: ("Audio_SetVoiceSampleIndex", "v"),
    0x82: ("Audio_EnableVoicePitchSlide", 1), 0x83: ("Audio_DisableVoicePitchSlide", 0),
    0x84: ("Audio_SetVoiceVolumeEnvelope", 7), 0x85: ("Audio_SetVoiceSequenceTempo", 1),
    0x86: ("Audio_SetVoiceFixedDuration", 2), 0x87: ("Audio_SetVoiceReleaseDuration", 1),
    0x88: ("Audio_EnableVoiceVibrato", 3), 0x89: ("Audio_EnableVoiceVibratoDown", 3),
    0x8A: ("Audio_DisableVoiceVibrato", 0), 0x8B: ("Audio_SetVoiceDurationOverride", "v"),
    0x8C: ("Audio_EnableVoiceVariableDuration", 0), 0x8D: ("Audio_SetVoiceDetune", 1),
    0x8E: ("Audio_ApplyVoiceDetuneOnce", 0), 0x8F: ("Audio_SetVoiceFinePitch", 1),
    0x90: ("Audio_SetVoiceEnvelopeFromTable", "v"), 0x91: ("Audio_SetVoiceFlagED", 0),
    0x92: ("Audio_ClearVoiceFlagED", 0), 0x93: ("Audio_SetVoiceFlagEE", 0), 0x94: ("Audio_ClearVoiceFlagEE", 0),
    0x95: ("Audio_PushVoiceLoopState", 1), 0x96: ("Audio_PopVoiceLoopState", 0),
    0x97: ("Audio_SetVoicePeriodicPitchModulation", 3), 0x98: ("Audio_DisableVoicePeriodicPitchModulation", 0),
    0x99: ("Audio_EnableVoiceVelocityFromStream", 0), 0x9A: ("Audio_DisableVoiceVelocityFromStream", 0),
    0x9B: ("Audio_SetVoiceDefaultVelocity", 1), 0x9C: ("Audio_SetVoicePan", 1),
    0x9D: ("Audio_SkipVoiceCommandWord", 2), 0x9E: ("Audio_SetVoiceSubEnvelopePointer", 1),
    0x9F: ("Audio_ClearVoiceSubEnvelopePointer", 0), 0xA0: ("Audio_SkipVoiceCommandByte", 1),
    0xA1: ("Audio_SetVoiceEnvelopePointers", 6), 0xA2: ("Audio_SetVoiceFxMix", 1),
    0xA3: ("Audio_SetRandomVoiceDetune", 2), 0xA4: ("Audio_SetRandomVoiceVolume", 2),
    0xA5: ("Audio_SetRandomVoicePan", 2), 0xA6: ("Audio_SetVoiceVolume", 1),
    0xA7: ("Audio_AllocateSubVoiceFromCommand", "v"), 0xA8: ("Audio_SetVoiceVolumeScale", 1),
    0xA9: ("Audio_SetVoicePanModulation", 1), 0xB0: ("Audio_AllocateVoiceFromCommand", "v"),
}

class Bad(Exception):
    pass

def walk(b, start, counts, sites):
    """All paths of the stream at `start`; state = (pc, velocity_on, fixed_length != 0, ignore, loop stack)."""
    seen = set(); work = [(start, 0, 0, 0, ())]; steps = 0
    while work:
        st = work.pop()
        if st in seen:
            continue
        seen.add(st)
        pc, vel, fixed, ign, stack = st
        while True:
            steps += 1
            if steps > 2_000_000:
                raise Bad("path explosion")
            if pc >= len(b):
                raise Bad("ran off the blob at 0x%x" % pc)
            op = b[pc]
            if op < 0x80:  # a note
                pc += 1 + (1 if vel else 0)
                if not fixed or ign:
                    pc += 2 if b[pc] >= 0x80 else 1
                ign = 0
                if (pc, vel, fixed, ign, stack) in seen:
                    break
                seen.add((pc, vel, fixed, ign, stack))
                continue
            if op not in OPS:
                raise Bad("unknown opcode 0x%02x at 0x%x" % (op, pc))
            counts[op] = counts.get(op, 0) + 1
            sites.setdefault(op, pc)
            arg = OPS[op][1]
            a = pc + 1
            n = (2 if b[a] >= 0x80 else 1) if arg == "v" else arg
            nxt = a + n
            if op == 0x80:
                break
            if op == 0x8B:  # fixed_length
                v = b[a] if b[a] < 0x80 else ((b[a] & 0x7F) << 8) + b[a+1]
                fixed = 1 if v else 0
            elif op == 0x8C:
                ign = 1
            elif op == 0x99:
                vel = 1
            elif op in (0x9A, 0x9B):
                vel = 0
            elif op == 0x95:
                if len(stack) > 8:
                    raise Bad("loop stack overflow")
                stack = stack + ((nxt, b[a] == 0xFF),)
            elif op == 0x96:
                if not stack:
                    raise Bad("next without for at 0x%x" % pc)
                # loop again, or the count runs out and the stack pops (a count of 0xFF loops forever)
                body, forever = stack[-1]
                if forever:
                    nxt = body
                else:
                    work.append((body, vel, fixed, ign, stack))
                    stack = stack[:-1]
            elif op == 0xA1:
                nxt = start + ((b[a] << 8) | b[a+1])
            pc = nxt
            if (pc, vel, fixed, ign, stack) in seen:
                break
            seen.add((pc, vel, fixed, ign, stack))
    return len(seen)

def u32(b, o):
    return struct.unpack(">I", b[o:o+4])[0]

def as_song(b):
    if len(b) < 0x18:
        return None
    n, ch, vo, pb, env, drum = (u32(b, 4*i) for i in range(6))
    if not (1 <= n <= 32) or not (0x18 <= ch < len(b)) or ch + 4*n > len(b):
        return None
    if not all(0 <= x < len(b) for x in (vo, pb, env, drum)):
        return None
    offs = [u32(b, ch + 4*i) for i in range(n)]
    if not any(offs) or not all(o == 0 or 0x18 <= o < len(b) for o in offs):
        return None
    return offs

def as_fx(b):
    if len(b) < 0x10:
        return None
    n, flags, meta, data = (u32(b, 4*i) for i in range(4))
    if not (1 <= n <= 4096) or flags & ~3 or not (0x10 <= meta < len(b)) or not (0x10 <= data < len(b)):
        return None
    if data + 4*n > len(b):
        return None
    offs = [u32(b, data + 4*i) for i in range(n)]
    if not all(0x10 <= o < len(b) for o in offs):
        return None
    return offs

def main():
    rom = open(sys.argv[1], "rb").read()
    base = 0x15C0000
    end = base + u32(rom, base)
    blobs = [m.start() for m in re.finditer(b"Yay0", rom) if base <= m.start() < end]
    total = {}; per_blob = []; bad = []; kinds = {"song": 0, "fx": 0, "other": 0}; streams = 0
    for off in blobs:
        try:
            b = yay0(rom, off)
        except Exception:
            kinds["other"] += 1
            continue
        for kind, offs in (("song", as_song(b)), ("fx", as_fx(b))):
            if offs is None:
                continue
            counts = {}; sites = {}; ok = True
            for i, s in enumerate(offs):
                if s == 0:
                    continue
                try:
                    walk(b, s, counts, sites)
                    streams += 1
                except Bad as e:
                    ok = False; bad.append("%s 0x%x stream %d: %s" % (kind, off, i, e))
            if ok:
                kinds[kind] += 1
                for k, v in counts.items():
                    total[k] = total.get(k, 0) + v
                per_blob.append({"rom": "0x%x" % off, "kind": kind, "streams": sum(1 for s in offs if s),
                                 "size": len(b), "ops": {"0x%02x" % k: v for k, v in sorted(counts.items())}})
            break
        else:
            kinds["other"] += 1
    used = {"0x%02x %s" % (k, OPS[k][0]): total[k] for k in sorted(total)}
    unused = ["0x%02x %s" % (k, OPS[k][0]) for k in sorted(OPS) if k not in total]
    print("Yay0 blobs in the audio region 0x%x-0x%x: %d (songs %d, fx banks %d, other %d), streams %d, bad %d"
          % (base, end, len(blobs), kinds["song"], kinds["fx"], kinds["other"], streams, len(bad)))
    for x in bad:
        print("  BAD", x)
    print("used opcodes (command count over all decoded paths):")
    for k, v in used.items():
        print("  %-48s %d" % (k, v))
    print("opcodes no stream uses:")
    for k in unused:
        print("  " + k)
    if "--json" in sys.argv:
        json.dump({"region": ["0x%x" % base, "0x%x" % end], "blobs": len(blobs), "kinds": kinds, "streams": streams,
                   "bad": bad, "used": used, "unused": unused, "per_blob": per_blob},
                  open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)

if __name__ == "__main__":
    main()
