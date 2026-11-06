#!/usr/bin/env python3 -I
"""Before/after pictures of the widescreen (16:9) fixes, for review (PORTING_NOTES: Battle HUD at the widescreen edges).

Each case runs a route twice in 16:9: "before" with the fixes off (the case's "off" switches, by default
PORT_NO_HUD_ANCHOR=1: the anchor/no-cut markers are ignored, as before the change) and "after" with them on, and saves the chosen frames:
<out>/<case>/<frame>_before.png, <frame>_after.png, <frame>_compare.png (before above after), and <out>/index.html.
Add a case to CASES for every new widescreen fix.

Usage: tools/widescreen_review.py [--out DIR] [--rom STADIUM.z64] [--gb-rom GB.gb] [--fixtures DIR] [case ...]
  --out       default build/widescreen_review
  --fixtures  the function-evidence catalog's generated saves (tools/evidence/whole_game_catalog_windows.py
              prepare(): base.sav, ...); cases that need one are skipped without it
"""
import html, os, shutil, subprocess, sys

WT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARGS = sys.argv[1:]
def _opt(key, default):
    if key in ARGS:
        i = ARGS.index(key)
        value = ARGS[i + 1]
        del ARGS[i:i + 2]
        return value
    return default
HERE = os.path.abspath(_opt("--out", os.path.join(WT, "build", "widescreen_review")))
ROM = _opt("--rom", os.environ.get("PORT_ROM", os.path.expanduser("~/Downloads/romis/Pokemon Stadium (USA).z64")))
GBROM = _opt("--gb-rom", os.path.join(WT, "gb", "yellow_de.gb"))
FIX = _opt("--fixtures", "")
BIN = os.path.join(WT, "build", "pokestadium.exe" if os.name == "nt" else "pokestadium")

CASES = [
    dict(id="battle_hud_slide_in", title="Battle HUD slides in",
         note="Status boxes, portraits and command bar slide in after the strip wipe. Before: they appear out of the "
              "middle of the picture, cut at the 4:3 line. After: they slide in from the screen's edges.",
         rec="replays/boot_battle_move.rec", env=dict(PORT_BOOT="battle"), frames=[1201, 1210, 1219, 1228]),
    dict(id="battle_hud_resting", title="Battle HUD at rest",
         note="Command menu up and during the turn. After: each side's HUD sits at its screen edge.",
         rec="replays/boot_battle_move.rec", env=dict(PORT_BOOT="battle"), frames=[1300, 2770]),
    dict(id="battle_compact_boxes", title="Compact HP boxes during moves",
         note="The small boxes shown while a move plays. After: at the screen's top corners.",
         rec="replays/boot_battle_move.rec", env=dict(PORT_BOOT="battle"), frames=[1800, 2000]),
    dict(id="snap_film_reel", title="Pokémon Snap camera: film reel",
         note="On a 4:3 screen the film strip runs off the right edge. Before: cut off at the 4:3 line in the middle "
              "of the picture. After: the reel stays where the game puts it (clear of the viewfinder) and the strip "
              "runs on to the picture's right edge.",
         rec="evidence/whole_game/routes/minigames_gallery/gal_album_boot.rec",
         env=dict(PORT_CONTROLLERS="1,2"), gb="base.sav", frames=[4568]),
    dict(id="elite_four_room", title="Elite Four room (and the rival's)",
         note="Its sunset sky is drawn as full-screen rectangles before the arena. Before: that made the frame a 2D "
              "screen, so the whole battle was kept inside the 4:3 area with the sides empty. After: the arena fills "
              "the picture like the other stages. Stage 16 (the rival's room) is the same room.",
         rec="replays/boot_battle_move.rec", env=dict(PORT_BOOT="battle", PORT_BOOT_STAGE="15"),
         off=dict(PORT_NO_3D_SCENE_MARK="1"), frames=[1201, 1300, 1800]),
]


def run(case, mode, out):
    work = os.path.join(out, "_run_" + mode)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copyfile(case.get("flash") or os.path.join(WT, "replays/default.flash"), os.path.join(work, "port.flash"))
    env = dict(os.environ, PORT_REPLAY=os.path.join(WT, case["rec"]), PORT_STOP_AT=str(max(case["frames"]) + 2),
               PORT_SHOT_AT=",".join(map(str, case["frames"])), PORT_SPEED="max", PORT_HIDDEN_WINDOW="1",
               PORT_EVID_BACKGROUND="1", PORT_ASPECT="16:9", PORT_DEBUG_RESIZE="1:1280x720",
               PORT_SCREENSHOT_DIR=work, PORT_SCREENSHOT_STOP_ONLY="1", PORT_RECORD="/dev/null",
               PORT_FLASH_FILE=os.path.join(work, "port.flash"), SHIP_HOME=os.path.join(work, "home"))
    off = case.get("off", dict(PORT_NO_HUD_ANCHOR="1"))
    for key in off:
        env.pop(key, None)
    if mode == "before":
        env.update(off)
    env.update(case.get("env", {}))
    if case.get("gb"):
        shutil.copyfile(os.path.join(FIX, case["gb"]), os.path.join(work, "gb.sav"))
        env.update(PORT_GB_ROM=GBROM, PORT_GB_SAVE=os.path.join(work, "gb.sav"))
    with open(os.path.join(work, "log"), "w") as log:
        subprocess.run([BIN, ROM], cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    shots = sorted(int(x[5:10]) for x in os.listdir(work) if x.startswith("stop_") and x.endswith(".ppm"))
    for f in case["frames"]:
        got = min((t for t in shots if t >= f), default=None)  # the first frame drawn at or after f
        src = os.path.join(work, "stop_%05d.ppm" % got)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-vf", "scale=1280:-1",
                        os.path.join(out, "%05d_%s.png" % (f, mode))], check=True)
    shutil.rmtree(work)


def main():
    want = ARGS
    os.makedirs(HERE, exist_ok=True)
    for case in CASES:
        if want and case["id"] not in want:
            continue
        if case.get("gb") and not (FIX and os.path.exists(os.path.join(FIX, case["gb"])) and os.path.exists(GBROM)):
            print("skipped", case["id"], "(needs --fixtures with %s and --gb-rom)" % case["gb"])
            continue
        out = os.path.join(HERE, case["id"])
        os.makedirs(out, exist_ok=True)
        for mode in ("before", "after"):
            run(case, mode, out)
        for f in case["frames"]:
            b, a = os.path.join(out, "%05d_before.png" % f), os.path.join(out, "%05d_after.png" % f)
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", b, "-i", a, "-filter_complex",
                            "[0]pad=iw:ih+8:0:0:white[t];[t][1]vstack", os.path.join(out, "%05d_compare.png" % f)],
                           check=True)
        print("done", case["id"])
    # index
    parts = ["<!doctype html><meta charset=utf-8><title>Widescreen review</title><style>body{font:14px system-ui;"
             "margin:24px;background:#111;color:#ddd}h2{margin-top:40px}img{width:640px;margin:4px;border:1px solid #333}"
             ".row{display:flex;flex-wrap:wrap}.cap{color:#999}</style><h1>Widescreen (16:9) fixes: before / after</h1>"
             "<p class=cap>Left: before (fixes off). Right: after. Pictures at 1280 px.</p>"]
    for case in CASES:
        if not os.path.exists(os.path.join(HERE, case["id"])):
            continue
        parts.append("<h2>%s</h2><p class=cap>%s<br>Route: %s</p>" % (html.escape(case["title"]),
                     html.escape(case["note"]), html.escape(case["rec"])))
        for f in case["frames"]:
            parts.append("<div class=row><div><div class=cap>frame %d, before</div><img src='%s/%05d_before.png'></div>"
                         "<div><div class=cap>after</div><img src='%s/%05d_after.png'></div></div>" %
                         (f, case["id"], f, case["id"], f))
    open(os.path.join(HERE, "index.html"), "w").write("\n".join(parts))


if __name__ == "__main__":
    main()
