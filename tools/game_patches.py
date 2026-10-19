#!/usr/bin/env python3 -I
"""The port's changes to the decompiled game, kept as patches on top of an unmodified decomp commit.

game/ is the decomp (michiiik/pokestadium) at the commit the port pins; patches/game/*.patch hold everything the
port changes in it (64-bit fixes, big-endian data, sound, Game Boy, port hooks), one patch per area so each one
stays readable and conflicts on its own when the decomp moves on.

Usage:
  tools/game_patches.py apply     apply the series to a clean game/ (setup, CI); fails if a patch does not apply
  tools/game_patches.py check     only test whether the series applies
  tools/game_patches.py export    write the series from the changes in game/ (after editing game files)
  tools/game_patches.py reset     restore game/ to the pinned decomp commit (drops the port's changes in the tree)

Editing: apply, change files in game/, export, commit patches/game. Moving to a newer decomp commit: reset,
check out the new commit in game/, apply (fix the patches that no longer apply, e.g. with `git apply --3way` and
the decomp's renames), export, and commit the submodule pointer together with patches/game.
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, "game")
PATCHES = os.path.join(ROOT, "patches", "game")
AREAS = ("include", "lib", "src")  # what the port compiles from the decomp; tools/ and asm/ stay untouched


def git(*args, check=True, capture=True):
    r = subprocess.run(["git", "-C", GAME, *args], capture_output=capture, text=True)
    if check and r.returncode != 0:
        sys.exit("git %s failed:\n%s" % (" ".join(args), r.stderr))
    return r.stdout


def pinned():
    # the submodule commit in the index (a newly staged pin counts before it is committed)
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "-s", "game"], capture_output=True, text=True).stdout.split()
    return out[1]


def group(path):
    """patch file name (without .patch) for a changed path"""
    p = path.split("/")
    if p[0] == "include":
        return "00-include"
    if p[0] == "lib":
        return "01-libultra"
    if p[1] == "fragments":
        return "20-fragment-" + p[2]
    name = p[-1]
    if p[1] in ("libgsm", "libnaudio", "libnumus") or name.startswith(("audio", "port_audio", "gsm", "cry", "sound", "music")):
        return "10-audio"
    if name.startswith(("gb_", "gbpak", "transfer_pak")):
        return "11-gameboy"
    return "02-core"


def series():
    return sorted(f for f in os.listdir(PATCHES) if f.endswith(".patch")) if os.path.isdir(PATCHES) else []


def apply(check_only):
    names = series()
    if not names:
        sys.exit("no patches in patches/game")
    paths = [os.path.join(PATCHES, n) for n in names]
    git("apply", "--check", *paths)
    if not check_only:
        git("apply", *paths)
    print("%s %d game patches" % ("checked" if check_only else "applied", len(names)))


def export():
    base = pinned()
    git("add", "-A", "--intent-to-add", *AREAS)  # new files show up in the diff
    changed = [l for l in git("diff", "--name-only", base, "--", *AREAS).splitlines() if l]
    groups = {}
    for path in changed:
        groups.setdefault(group(path), []).append(path)
    os.makedirs(PATCHES, exist_ok=True)
    old = set(series())
    for name, files in sorted(groups.items()):
        # bytes, not text: some decomp files have CRLF line endings that must survive unchanged
        diff = subprocess.run(["git", "-C", GAME, "diff", "--binary", "--full-index", base, "--", *files],
                              capture_output=True, check=True).stdout
        with open(os.path.join(PATCHES, name + ".patch"), "wb") as f:
            f.write(diff)
        old.discard(name + ".patch")
    for stale in old:  # an area the port no longer changes
        os.remove(os.path.join(PATCHES, stale))
    git("reset", "-q")
    print("exported %d patches (%d files) against %s" % (len(groups), len(changed), base[:8]))


def reset():
    base = pinned()
    git("checkout", "-q", base, "--", *AREAS)
    # files the patches added (untracked in the decomp)
    for path in git("ls-files", "--others", "--exclude-standard", "--", *AREAS).splitlines():
        os.remove(os.path.join(GAME, path))
    print("game/ restored to %s" % base[:8])


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "apply":
        apply(False)
    elif cmd == "check":
        apply(True)
    elif cmd == "export":
        export()
    elif cmd == "reset":
        reset()
    else:
        sys.exit(__doc__)
