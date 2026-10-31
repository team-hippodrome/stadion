#!/usr/bin/env python3 -I
"""Write the call files of the audio sweep (run with the sound engine on, PORT_GAME_AUDIO=1): each line calls one of
the game's own play functions at entry of the n-th Audio_CreateFrameTask (once per frame), spaced so each sound
starts on its own.  Entry counts come from audio_banks.json (audio_banks.py).  10 frames before each play the game's
scene-change fade (Audio_FadeCategoriesAndStreams(1)) ends what still sounds: the category, direct and
Audio_PlayCommand banks share one buffer (D_800FC6A8) and the move / species banks rotate through three, so a bank
loaded under a sound that still plays would hand that voice other data (the game fades before it changes scenes).

  music1-4.calls Audio_StartMusicTrack(0..0x4B): music file n has id n (0x5A and 0x5B replay files 2 and 1), each
                 given the frames of one pass through its longest channel (from the decoded tempo and note lengths)
                 plus 15% and 240 frames, in four lanes of about equal length
  system.calls   Audio_PlaySoundEffectById 0..0x60 (system FX, D_800FC688 / D_800FC6A8), 0x01100000|id (category 11),
                 0x01200000|id (category 12); Audio_PlayDirectSoundCommand 0x01000000|id; Audio_PlayCommand(id, 1 / 2);
                 Audio_DispatchSoundBankCommand(cat << 16 | id) for categories 2-A; Audio_GetActivityScore (with
                 PORT_TRACE_ACTIVITY=1: the port's channel dump)
  moves1/2.calls func_800479C0(move, 25, mode): moves 1-165, modes 0-5 (the battle's play modes), and the last bank
                 (move 0xA6: arg2 picks the entry directly), 120 frames each, in two lanes
  species.calls  func_80048060(0, entry, species, 16): mode 16 plays entry `entry` of the species bank directly
usage: gen_calls.py ROM audio_banks.json out_dir
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode_seq as d  # noqa: E402

rom = open(sys.argv[1], "rb").read()
banks = json.load(open(sys.argv[2]))
out = sys.argv[3]
by = lambda pfx: [b for b in banks if b["loader"].startswith(pfx)]
HOOK = "Audio_CreateFrameTask"


def write(name, calls, start, step):
    """step: frames between calls, or a list with each call's own slot"""
    t = start
    with open(os.path.join(out, name), "w") as f:
        for i, (target, ret, args) in enumerate(calls):
            f.write("%s#%d:Audio_FadeCategoriesAndStreams:v:i1\n" % (HOOK, t - 10))
            f.write("%s#%d:%s:%s:%s\n" % (HOOK, t, target, ret, ",".join("i%d" % a for a in args)))
            last = t
            t += step[i] if isinstance(step, list) else step
    print(name, len(calls), "calls, last at", last, "ends at", t)


def pass_frames(b, start):
    """frames of one pass through a song channel: until its endless loop jumps back or the stream ends"""
    pc, vel, fixed, ign, fr, tempo, stack = start, 0, 0, 0, 0.0, 120, []
    while True:
        op = b[pc]
        if op < 0x80:
            pc += 1 + vel
            if not fixed or ign:
                v = b[pc]
                dur = v if v < 0x80 else ((v & 0x7F) << 8) + b[pc + 1]
                pc += 2 if v >= 0x80 else 1
            else:
                dur = fixed
            ign = 0
            fr += dur * 256 / (tempo * 0x6000 / 120 / 60)  # Audio_SetVoiceSequenceTempo, 60 vsyncs a second
            continue
        if op == 0x80:
            return fr
        arg = d.OPS[op][1]
        a = pc + 1
        nxt = a + ((2 if b[a] >= 0x80 else 1) if arg == "v" else arg)
        if op == 0x85:
            tempo = b[a]
        elif op == 0x8B:
            fixed = b[a] if b[a] < 0x80 else ((b[a] & 0x7F) << 8) + b[a + 1]
        elif op == 0x8C:
            ign = 1
        elif op == 0x99:
            vel = 1
        elif op in (0x9A, 0x9B):
            vel = 0
        elif op == 0x95:
            stack.append([nxt, b[a]])
        elif op == 0x96:
            if stack[-1][1] == 0xFF:
                return fr
            stack[-1][1] -= 1
            if stack[-1][1]:
                nxt = stack[-1][0]
            else:
                stack.pop()
        pc = nxt


songs = [b for b in by("music file")]
slots = []
for b in songs:
    blob = d.yay0(rom, int(b["rom"], 16))
    slots.append(int(max(pass_frames(blob, o) for o in d.as_song(blob) if o) * 1.15) + 240)
lanes = [[] for _ in range(4)]
for i in sorted(range(len(songs)), key=lambda i: -slots[i]):  # longest first, each to the shortest lane
    min(lanes, key=lambda l: sum(slots[j] for j in l)).append(i)
for k, lane in enumerate(lanes):
    lane.sort()
    write("music%d.calls" % (k + 1), [("Audio_StartMusicTrack", "i", [i]) for i in lane], 2400, [slots[i] for i in lane])

sysc = [("Audio_GetActivityScore", "i", [])]
sysc += [("Audio_PlaySoundEffectById", "i", [i]) for i in range(0x61)]
n11 = by("category 11")[0]["entries"]
n12 = by("category 12")[0]["entries"]
sysc += [("Audio_PlaySoundEffectById", "i", [0x01100000 | i]) for i in range(n11)]
sysc += [("Audio_PlaySoundEffectById", "i", [0x01200000 | i]) for i in range(n12)]
sysc += [("Audio_PlayDirectSoundCommand", "i", [0x01000000 | i, 0, 0]) for i in range(by("direct")[0]["entries"])]
for a1 in (1, 2):
    sysc += [("Audio_PlayCommand", "i", [i, a1, 0]) for i in range(0x61)]
for b in by("category 0x"):
    cat = int(b["loader"].split()[1], 16)
    sysc += [("Audio_DispatchSoundBankCommand", "i", [cat << 16 | i, 0, 0]) for i in range(b["entries"])]
sysc += [("Audio_GetActivityScore", "i", [])]
write("system.calls", sysc, 2400, 45)

mv = [("func_800479C0", "i", [m, 25, mode]) for m in range(1, 0xA6) for mode in range(6)]
mv += [("func_800479C0", "i", [0xA6, 25, e]) for e in range(by("move FX bank 166")[0]["entries"])]
half = 83 * 6  # moves 1-83 / 84-165 and the last bank
write("moves1.calls", mv[:half], 2400, 120)
write("moves2.calls", mv[half:], 2400, 120)

sp = []
for b in by("species FX bank"):
    s = int(b["loader"].split()[3])
    sp += [("func_80048060", "i", [0, e, s, 16]) for e in range(b["entries"])]
write("species.calls", sp, 2400, 40)
