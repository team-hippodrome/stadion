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
  tools/game_patches.py merge <ref> | --continue | --abort
                                  merge another branch when both changed the patch files (see below)

Editing: apply, change files in game/, export, commit patches/game. Moving to a newer decomp commit: reset,
check out the new commit in game/, apply (fix the patches that no longer apply, e.g. with `git apply --3way` and
the decomp's renames), export, and commit the submodule pointer together with patches/game.

Merging: when this branch and <ref> both changed patches/game, `git merge` conflicts
on hunk offsets and index lines even where the source changes don't overlap. `merge <ref>` merges at the source level
instead: it builds the merge base's and <ref>'s patch series on the pinned decomp commit (in a
temporary index, nothing is checked out), applies the difference to game/, exports the game patches
again and commits the merge. game/ must match this branch's patches first, and the merge base and
<ref> must pin the same submodule commits. A hunk that does not apply is reported with its context and left in a
.rej file beside the file: apply it by hand, delete the .rej file, then `merge --continue`. Conflicts in other
files are resolved as usual (edit, `git add`) before `merge --continue`. `merge --abort` undoes the merge and
restores game/ to this branch's patches. libultraship is a committed fork dependency, managed by ordinary Git
submodule merges; this helper never edits it. Merges spanning the old libultraship patch workflow must be
migrated manually before using the helper.
"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, "game")
PATCHES = os.path.join(ROOT, "patches", "game")
AREAS = ("include", "lib", "src")  # what the port compiles from the decomp; tools/ and asm/ stay untouched


def git(*args, check=True, capture=True):
    r = subprocess.run(["git", "-C", GAME, *args], capture_output=capture, text=True)
    if check and r.returncode != 0:
        sys.exit("git %s failed:\n%s" % (" ".join(args), r.stderr))
    return r.stdout


def pinned(path="game"):
    # the submodule commit in the index (a newly staged pin counts before it is committed)
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "-s", path], capture_output=True, text=True).stdout.split()
    return out[1]


def run(repo, *args, check=True, env=None):
    """git in any repo, bytes out (patches must survive byte for byte)"""
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, env=env)
    if check and r.returncode != 0:
        sys.exit("git %s failed in %s:\n%s" % (" ".join(args), os.path.relpath(repo, ROOT) or ".",
                                                 r.stderr.decode(errors="replace")))
    return r.stdout


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


# ---- merge ----

def pin_at(ref, path):
    out = run(ROOT, "ls-tree", ref, "--", path).split()
    return out[2].decode() if out else None


def patches_at(ref):
    """game patches as stored in a commit"""
    names = run(ROOT, "ls-tree", "--name-only", ref, "patches/game/").decode().split()
    game = [run(ROOT, "show", "%s:%s" % (ref, n)) for n in sorted(names) if n.endswith(".patch")]
    return game


def check_patch_history(*refs):
    """Do not silently lose dependency changes from pre-fork branches."""
    for ref in refs:
        if run(ROOT, "ls-tree", "--name-only", ref, "--", "patches/libultraship.patch").strip():
            sys.exit("%s still uses the legacy libultraship patch; migrate that branch to the fork "
                     "and merge it manually before using this helper" % ref)


def tree_with(repo, pin, patches, tmp):
    """tree of the pinned commit with the patches applied, built in a scratch index"""
    env = dict(os.environ, GIT_INDEX_FILE=os.path.join(tmp, "index"))
    run(repo, "read-tree", pin, env=env)
    files = []
    for i, data in enumerate(p for p in patches if p):
        files.append(os.path.join(tmp, "%03d.patch" % i))
        with open(files[-1], "wb") as f:
            f.write(data)
    if files:
        run(repo, "apply", "--cached", *files, env=env)
    tree = run(repo, "write-tree", env=env).decode().strip()
    for f in files + [env["GIT_INDEX_FILE"]]:
        os.remove(f)
    return tree


def rejects(repo):
    return [l for l in run(repo, "ls-files", "--others", "--", ":(glob)**/*.rej").decode().splitlines() if l]


def in_merge():
    return run(ROOT, "rev-parse", "-q", "--verify", "MERGE_HEAD", check=False).strip() != b""


def apply_change(repo, diff, tmp):
    """apply a source-level diff to a submodule's tree; returns the .rej files of hunks that did not apply"""
    if not diff:
        return []
    path = os.path.join(tmp, "change.patch")
    with open(path, "wb") as f:
        f.write(diff)
    r = subprocess.run(["git", "-C", repo, "apply", "--reject", "--whitespace=nowarn", path], capture_output=True)
    err = r.stderr.decode(errors="replace")
    rej = [m + ".rej" for m in re.findall(r"^Applying patch (\S+) with \d+ reject", err, re.M)]
    if r.returncode != 0 and not rej:
        sys.exit("git apply failed in %s:\n%s" % (os.path.relpath(repo, ROOT), err))
    return rej


def merge(ref):
    if in_merge():
        sys.exit("a merge is already in progress: finish it with `merge --continue` or `merge --abort`")
    other = run(ROOT, "rev-parse", "--verify", ref + "^{commit}").decode().strip()
    base = run(ROOT, "merge-base", "HEAD", other).decode().strip()
    check_patch_history("HEAD", base, other)
    if run(GAME, "rev-parse", "HEAD").decode().strip() != pin_at("HEAD", "game"):
        sys.exit("game/ is not checked out at the commit this branch pins")
    if run(ROOT, "status", "--porcelain", "--", "patches"):
        sys.exit("patches/ has uncommitted changes; commit or discard them first")
    # game/ must hold exactly this branch's patches, or the merge would carry stray edits along
    export()
    if run(ROOT, "status", "--porcelain", "--", "patches"):
        sys.exit("game/ does not match patches/game (see `git diff patches/game`, just exported); "
                 "commit or drop those changes first")
    base_game = patches_at(base)
    other_game = patches_at(other)
    changes = []
    with tempfile.TemporaryDirectory(prefix="game_patches_merge.") as tmp:
        for repo, name, a, b, paths in ((GAME, "game", base_game, other_game, AREAS),):
            pin = pin_at(base, name)
            if pin_at(other, name) != pin:
                sys.exit("%s pins a different %s commit than the merge base; this helper only merges the patches"
                         % (ref, name))
            # binary files under the decomp's tools/ are left out: the port never changes them
            diff = run(repo, "diff", "--binary", "--full-index", tree_with(repo, pin, a, tmp),
                       tree_with(repo, pin, b, tmp), "--", *paths)
            changes.append((repo, name, diff))

        out = subprocess.run(["git", "-C", ROOT, "merge", "--no-ff", "--no-commit", ref],
                             capture_output=True, text=True)
        print(out.stdout.strip())
        if not in_merge():
            sys.exit(out.stderr.strip() or "nothing to merge")
        failed = []
        for repo, name, diff in changes:
            print("%s: %s" % (name, "applying %s's change" % ref if diff else "no change in %s" % ref))
            failed += [(name, f) for f in apply_change(repo, diff, tmp)]
    if failed:
        print("\nThese hunks did not apply; apply them by hand, delete the .rej files, then run "
              "`tools/game_patches.py merge --continue`:")
        for name, f in failed:
            print("\n--- %s/%s" % (name, f))
            with open(os.path.join(ROOT, name, f)) as r:
                print(r.read().rstrip())
        sys.exit(1)
    finish()


def finish():
    if not in_merge():
        sys.exit("no merge in progress")
    check_patch_history("HEAD", "MERGE_HEAD")
    left = ["game/%s" % f for f in rejects(GAME)]
    if left:
        sys.exit("apply and delete these first:\n  " + "\n  ".join(left))
    export()
    run(ROOT, "add", "-A", "--", "patches/game")
    unmerged = run(ROOT, "diff", "--name-only", "--diff-filter=U").decode().split()
    if unmerged:
        sys.exit("resolve these conflicts, `git add` them, then run `merge --continue`:\n  " + "\n  ".join(unmerged))
    # the regenerated series must apply to clean pinned submodules
    with tempfile.TemporaryDirectory(prefix="game_patches_merge.") as tmp:
        game = []
        for n in series():
            with open(os.path.join(PATCHES, n), "rb") as f:
                game.append(f.read())
        tree_with(GAME, pinned(), game, tmp)
    run(ROOT, "commit", "--no-edit", "-q")
    print("merged; patches/game regenerated and checked against the clean game submodule")


def abort():
    run(ROOT, "merge", "--abort")
    for f in rejects(GAME):
        os.remove(os.path.join(GAME, f))
    reset()
    apply(False)
    print("merge aborted; game/ holds this branch's patches again; libultraship/ was left untouched")


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
    elif cmd == "merge" and len(sys.argv) == 3:
        {"--continue": finish, "--abort": abort}.get(sys.argv[2], lambda: merge(sys.argv[2]))()
    else:
        sys.exit(__doc__)
