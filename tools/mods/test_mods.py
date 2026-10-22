#!/usr/bin/env python3 -I
"""Tests the mod loader (src/port/mods.cpp) without a ROM: builds small fixture mods in a temporary app folder, runs
the port with PORT_MODS_CHECK=1 (mount, print the report, exit) and checks which mod serves each path.

Usage: tools/mods/test_mods.py [path to the pokestadium executable]   (default: build/pokestadium)

What it pins down:
- priority: the FIRST entry of the list wins over the ones below it and over the base archive (pokestadium.o2r),
  both for PORT_MODS and for the stored list (gSettings.Mods.Enabled), for .o2r files and loose folders;
- PORT_MODS=off mounts nothing; disabled mods are not mounted;
- manifests: code ("main"), another game_version, a newer format, declared conflicts and missing requirements keep a
  mod off (and the run reports it with exit code 1)."""
import json, os, pathlib, subprocess, sys, tempfile, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROBE = "port/mods_probe.txt"
BASE_FILE = "textures/launcher/n64.png"  # a file pokestadium.o2r has


def make_o2r(path, files):
    with zipfile.ZipFile(path, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)


def make_folder(path, files):
    for name, data in files.items():
        p = path / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(data) if isinstance(data, str) else p.write_bytes(data)


def manifest(name, **port):
    m = {"name": name, "version": "1.0.0"}
    m.update({k: v for k, v in port.items() if k in ("main", "game_version")})
    extra = {k: v for k, v in port.items() if k not in ("main", "game_version")}
    if extra:
        m["pokestadium"] = extra
    return json.dumps(m)


def run(exe, home, mods_env=None, enabled=None, disabled=None):
    cfg = home / "pokestadium.cfg.json"
    if enabled is not None or disabled is not None:
        cfg.write_text(json.dumps({"CVars": {"gSettings": {"Mods": {"Enabled": "|".join(enabled or []),
                                                                     "Disabled": "|".join(disabled or [])}}}}))
    elif cfg.exists():
        cfg.unlink()
    env = {k: v for k, v in os.environ.items() if not k.startswith("PORT_") and k != "SHIP_HOME"}
    env["PORT_MODS_CHECK"] = "1"
    if mods_env is not None:
        env["PORT_MODS"] = mods_env
    p = subprocess.run([str(exe)], cwd=home, env=env, capture_output=True, text=True, timeout=60)
    lines = [l[len("[mods] "):] for l in (p.stdout + p.stderr).splitlines() if l.startswith("[mods] ")]
    return p.returncode, lines


def winner(lines, path):
    for l in lines:
        l = l.strip()
        if l.startswith(path + ": "):
            return l[len(path) + 2:].split(" over")[0]
    return None


def main():
    exe = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build/pokestadium").resolve()
    base = exe.parent / "pokestadium.o2r"
    if not exe.is_file() or not base.is_file():
        print(f"error: {exe} (and pokestadium.o2r next to it) not found: build first")
        return 2
    failures = []

    def check(what, got, want):
        ok = got == want
        print(("ok   " if ok else "FAIL ") + f"{what}: {got!r}" + ("" if ok else f" (expected {want!r})"))
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        home = pathlib.Path(tmp)
        # the base archive is found next to the executable; the app folder (config, mods/) is the working directory
        mods = home / "mods"
        mods.mkdir()
        make_o2r(mods / "a.o2r", {"manifest.json": manifest("A"), PROBE: "a", BASE_FILE: b"a"})
        make_folder(mods / "b", {"manifest.json": manifest("B"), PROBE: "b", BASE_FILE: b"b"})
        make_o2r(mods / "c.zip", {PROBE: "c"})  # no manifest: listed under its file name

        code, lines = run(exe, home, "a.o2r|b")
        check("PORT_MODS a|b: probe", winner(lines, PROBE), "a.o2r")
        check("PORT_MODS a|b: base file", winner(lines, BASE_FILE), "a.o2r")
        check("PORT_MODS a|b: exit code", code, 0)
        code, lines = run(exe, home, "b|a.o2r|c.zip")
        check("PORT_MODS b|a|c: probe", winner(lines, PROBE), "b")
        check("PORT_MODS b|a|c: base file", winner(lines, BASE_FILE), "b")
        code, lines = run(exe, home, "c.zip|a.o2r")
        check("PORT_MODS c|a: probe", winner(lines, PROBE), "c.zip")
        check("PORT_MODS c|a: base file (c does not have it)", winner(lines, BASE_FILE), "a.o2r")
        code, lines = run(exe, home, "off")
        check("PORT_MODS=off", lines[0].split(";")[0] if lines else None, "0 of 3 active (PORT_MODS)")

        # the stored list: first entry wins; disabled ones stay off
        code, lines = run(exe, home, None, enabled=["b", "a.o2r"], disabled=["c.zip"])
        check("stored b|a: probe", winner(lines, PROBE), "b")
        check("stored b|a: c.zip disabled", any("c.zip" in l and l.strip().startswith("+") for l in lines), False)
        code, lines = run(exe, home, None, enabled=["a.o2r"], disabled=["b", "c.zip"])
        check("stored a, b disabled: base file", winner(lines, BASE_FILE), "a.o2r")
        check("stored a, b disabled: probe only in a (no conflict line)", winner(lines, PROBE), None)
        # a mod in the folder but in neither list is new: enabled, at the top
        code, lines = run(exe, home, None, enabled=["a.o2r"], disabled=["b"])
        check("new mod c.zip goes on top", winner(lines, PROBE), "c.zip")

        # manifests that keep a mod off
        make_o2r(mods / "code.o2r", {"manifest.json": manifest("Code", main="init.c"), PROBE: "code"})
        make_o2r(mods / "other-game.o2r", {"manifest.json": manifest("Other", game_version=1234), PROBE: "x"})
        make_o2r(mods / "future.o2r", {"manifest.json": manifest("Future", format=99), PROBE: "x"})
        make_o2r(mods / "needs.o2r", {"manifest.json": manifest("Needs", requires=["Missing Mod"]), PROBE: "x"})
        make_o2r(mods / "hates-a.o2r", {"manifest.json": manifest("Hates A", conflicts=["A"]), PROBE: "x"})
        code, lines = run(exe, home, "code.o2r|other-game.o2r|future.o2r|needs.o2r|a.o2r|hates-a.o2r")
        text = "\n".join(lines)
        check("refused mods: exit code", code, 1)
        for mod, reason in [("code.o2r", "contains code"), ("other-game.o2r", "another game version"),
                            ("future.o2r", "needs a newer port"), ("needs.o2r", "requires Missing Mod"),
                            ("hates-a.o2r", "conflicts with A")]:
            check(f"{mod} refused", any(mod + " not loaded:" in l and reason in l for l in lines), True)
        check("a.o2r still mounted", winner(lines, PROBE), None)  # only a has the probe now: no conflict line
        check("a.o2r over base", winner(lines, BASE_FILE), "a.o2r")
        # requirements met, exclusive
        make_o2r(mods / "needs-a.o2r", {"manifest.json": manifest("Needs A", requires=["A"]), PROBE: "na"})
        make_o2r(mods / "alone.o2r", {"manifest.json": manifest("Alone", exclusive=True), PROBE: "alone"})
        code, lines = run(exe, home, "needs-a.o2r|a.o2r")
        check("requirement met", winner(lines, PROBE), "needs-a.o2r")
        code, lines = run(exe, home, "a.o2r|alone.o2r")
        check("exclusive mod runs alone", any("a.o2r not loaded: Alone runs alone" in l for l in lines), True)
    print("all passed" if not failures else f"{len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
