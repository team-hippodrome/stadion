#!/usr/bin/env python3 -I
"""List every song and FX bank the game's loaders can reach, following Audio_InitBanks (audio_commands.c) and the
loaders that pick a file by id, and check that this set is exactly the set decode_seq.py decodes (all Yay0 blobs of
the audio region that parse as song_t / fx_t).  Prints each source with its id range (for the call files).

usage: audio_banks.py ROM [--json out.json]
"""
import json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode_seq as d  # noqa: E402


def main():
    R = open(sys.argv[1], "rb").read()
    be = lambda o: struct.unpack(">I", R[o:o + 4])[0]
    be16 = lambda o: struct.unpack(">H", R[o:o + 2])[0]
    G = 0x15C0000
    reg = [be(G + 4 * i) + (G if i else 0) for i in range(7)]  # gAudioRegions, made absolute
    seqfile = lambda o: [be(o + 4 + 8 * i) + o for i in range(be16(o + 2))]  # ALSeqFile entry offsets
    hdr3 = lambda o: [be(o + 4 * i) + o for i in range(3)]  # File2SubHeader1: fx/song, sample bank, wave tables
    files = lambda o: [be(o + 12 + 4 * i) + o for i in range(be(o))]  # File2SubHeader2: num, off1, wave tables, files[]
    files1 = lambda o: [be(o + 12 + 4 * i) + o for i in range(be(o + 8))]  # file1: off1, wave tables, num, files[]
    src = []  # (loader, kind, rom offset of the Yay0 blob, entries)

    def add(loader, kind, off):
        b = d.yay0(R, off)
        n = len(d.as_song(b)) if kind == "song" else d.u32(b, 0)
        src.append((loader, kind, off, n))

    for i, f in enumerate(files1(reg[1])):  # D_800FC6E4: Audio_StartMusicTrack / Audio_LoadWaveTablesAndMusic
        add("music file %d (Audio_StartMusicTrack -> D_800FC680)" % i, "song", f)
    s2 = seqfile(reg[2])  # D_800FC6E8
    add("D_800FC688 (system FX: SFX_TITLE_START, Audio_PlayPersistentSoundCommand)", "fx", hdr3(s2[0])[0])
    for i, f in enumerate(files(s2[1])):  # D_800FC6F0: func_800479C0 (move sounds)
        add("move FX bank %d (func_800479C0 -> D_800FC698)" % (i + 1), "fx", f)
    for i, f in enumerate(files(s2[2])):  # D_800FC6FC: func_80048060 (species sounds)
        add("species FX bank %d (func_80048060 -> D_800FC6B0)" % (i + 1), "fx", f)
    f4 = seqfile(s2[3])  # D_800FC6F4
    add("Audio_PlayCommand bank (D_800FC6F4[0] -> D_800FC6A8)", "fx", hdr3(f4[0])[0])
    for i, o in enumerate(seqfile(f4[1])):  # D_800FC6F8: Audio_DispatchSoundBankCommand
        add("category 0x%X bank (Audio_DispatchSoundBankCommand 0x%X0000 | id)" % (i + 2, i + 2), "fx", hdr3(o)[0])
    add("direct bank (Audio_PlayDirectSoundCommand, D_800FC6F4[2])", "fx", hdr3(f4[2])[0])
    add("category 11 bank (Audio_PlayCategory11SoundCommand 0x01100000 | id, D_800FC6F4[3])", "fx", hdr3(f4[3])[0])
    add("category 12 bank (Audio_PlayCategory12SoundCommand 0x01200000 | id, D_800FC6F4[4])", "fx", hdr3(f4[4])[0])

    # the decoded set: every Yay0 blob of the audio region that parses as a song or an FX bank
    end = G + be(G)
    decoded = {}
    for m in re.finditer(b"Yay0", R):
        o = m.start()
        if G <= o < end:
            b = d.yay0(R, o)
            if d.as_song(b) is not None:
                decoded[o] = "song"
            elif d.as_fx(b) is not None:
                decoded[o] = "fx"
    loaded = {o: k for _, k, o, _ in src}
    for loader, kind, off, n in src:
        print("%-90s %-4s 0x%07x entries %d" % (loader, kind, off, n))
    print("loader-reachable: %d songs, %d FX banks; decoded: %d songs, %d FX banks" % (
        sum(1 for k in loaded.values() if k == "song"), sum(1 for k in loaded.values() if k == "fx"),
        sum(1 for k in decoded.values() if k == "song"), sum(1 for k in decoded.values() if k == "fx")))
    print("decoded but no loader:", [hex(o) for o in sorted(set(decoded) - set(loaded))])
    print("loaded but not decoded:", [hex(o) for o in sorted(set(loaded) - set(decoded))])
    print("kind mismatches:", [hex(o) for o in loaded if o in decoded and loaded[o] != decoded[o]])
    if "--json" in sys.argv:
        json.dump([{"loader": l, "kind": k, "rom": "0x%x" % o, "entries": n} for l, k, o, n in src],
                  open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)


if __name__ == "__main__":
    main()
