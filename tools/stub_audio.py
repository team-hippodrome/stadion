#!/usr/bin/env python3 -I
"""Makes the game-facing audio API return immediately when PORT_NO_AUDIO is defined (the audio engine and its RSP
microcode are not ported). Inserts an early return into every non-static top-level function of the audio files."""
import re, pathlib
root = pathlib.Path(__file__).resolve().parent.parent / "game/src"
files = ["audio_cache.c", "audio_category_fade.c", "audio_channel.c", "audio_commands.c", "audio_commands_category2.c",
         "audio_fade.c", "audio_loop_point.c", "audio_sfx.c", "audio_sfx_wrapper.c", "audio_sound_state.c",
         "audio_stored_fade.c", "audio_stream.c", "cry.c", "gb_audio.c", "gb_audio_render.c", "3D140.c", "3D140_2.c"]
hdr = re.compile(r"^((?:[A-Za-z_][\w]*[\s\*]+)+)([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{\s*$")
total = 0
for f in files:
    p = root / f
    L = p.read_text().split("\n")
    out = []; n = 0
    for i, line in enumerate(L):
        out.append(line)
        m = hdr.match(line)
        if m and not line.startswith(("static", "if", "else", "while", "for", "switch")) and "PORT_NO_AUDIO" not in (L[i+1] if i+1 < len(L) else ""):
            ret = m.group(1).strip()
            if ret.split()[0] in ("typedef", "struct", "union", "enum") and "(" not in ret:
                continue
            if re.fullmatch(r"void", ret): r = "return;"
            elif "*" in ret: r = "return NULL;"
            else: r = "return 0;"
            out += ["#ifdef PORT_NO_AUDIO", f"    {r}", "#endif"]; n += 1
    p.write_text("\n".join(out)); total += n
print("stubbed", total)
