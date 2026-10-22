#!/usr/bin/env python3 -I
"""Packs a mod folder into an .o2r archive (a zip, like tools/make_o2r.py writes pokestadium.o2r).

Usage: tools/mods/pack.py <mod folder> [out.o2r] [--base build/pokestadium.o2r]

The paths inside the archive are the paths inside the folder (manifest.json at the top). The folder is checked first:
manifest.json must be valid JSON with a "name", and files that are byte-identical to the base archive's are left out
(they change nothing). See docs/MODDING.md."""
import json, pathlib, sys, zipfile

def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    base = None
    if "--base" in argv:
        base = pathlib.Path(argv[argv.index("--base") + 1])
        args.remove(str(base))
    if not args:
        print(__doc__)
        return 2
    src = pathlib.Path(args[0])
    out = pathlib.Path(args[1]) if len(args) > 1 else src.with_suffix(".o2r")
    if not src.is_dir():
        print(f"error: {src} is not a folder")
        return 1
    manifest = src / "manifest.json"
    if manifest.is_file():
        try:
            m = json.loads(manifest.read_text(encoding="utf-8"))
        except ValueError as e:
            print(f"error: manifest.json is not valid JSON: {e}")
            return 1
        if not isinstance(m.get("name"), str) or not m["name"]:
            print('error: manifest.json needs a "name"')
            return 1
        if m.get("main") or m.get("binaries"):
            print('error: the port does not run code from mods ("main"/"binaries")')
            return 1
    else:
        print("warning: no manifest.json; the mod is listed under its file name")
    base_files = {}
    if base is not None:
        with zipfile.ZipFile(base) as z:
            base_files = {n: z.read(n) for n in z.namelist()}
    files = sorted(p for p in src.rglob("*") if p.is_file() and not p.name.startswith("."))
    written = 0
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            name = p.relative_to(src).as_posix()
            data = p.read_bytes()
            if base_files.get(name) == data:
                print(f"skipped {name}: the same as in {base.name}")
                continue
            z.writestr(name, data)
            written += 1
    print(f"wrote {out} ({written} files)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
