#!/usr/bin/env python3 -I
"""Packs a built port into a self-contained, double-clickable "Pokemon Stadium.app" (macOS releases).

Usage: tools/make_macos_bundle.py <build dir> <output .app> [version] [icon .png]

- Contents/MacOS/pokestadium: the game; started without arguments it shows the launcher (src/port/launcher.cpp)
- Contents/Frameworks: the non-system dylibs (Homebrew's SDL3, GLEW, libzip, ...) and theirs, with every reference
  rewritten to @rpath, so the app runs without Homebrew
- Config, logs and saves live beside the .app, outside the signed bundle; a missing config is generated on first start.
- an icon: tools/macos/AppIcon.png ("POKéMON STADIUM" lettering, drawn by tools/macos_icon.swift) or the given
  picture; and an ad-hoc signature (needed after install_name_tool; not notarized: the first
  start of a downloaded copy needs "Open Anyway" or `xattr -dr com.apple.quarantine`)
"""
import os, shutil, subprocess, sys, tempfile

BUNDLE_ID = "com.team-hippodrome.pokestadium"


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def deps(path):
    """non-system libraries a Mach-O file links (first line of otool -L is the file itself for dylibs)"""
    out = run("otool", "-L", path).splitlines()[1:]
    libs = []
    for line in out:
        lib = line.strip().split(" (compatibility")[0]
        if lib.startswith(("/usr/lib/", "/System/")) or lib.startswith("@"):
            continue
        libs.append(lib)
    return libs


def rpaths(path):
    """the LC_RPATH entries of a Mach-O file"""
    out, result = run("otool", "-l", path).splitlines(), []
    for i, line in enumerate(out):
        if line.strip() == "cmd LC_RPATH":
            for l in out[i + 1:i + 4]:
                if l.strip().startswith("path "):
                    result.append(l.strip()[5:].rsplit(" (offset", 1)[0])
    return result


def bundle_libs(exe, frameworks):
    """copies every non-system dylib exe needs (recursively) into frameworks and points all references at @rpath"""
    done = {}  # original path -> bundled name
    todo = [exe]
    while todo:
        f = todo.pop()
        for lib in deps(f):
            real = os.path.realpath(lib)
            name = os.path.basename(lib)
            if lib not in done:
                done[lib] = name
                dst = os.path.join(frameworks, name)
                if not os.path.exists(dst):
                    shutil.copy2(real, dst)
                    os.chmod(dst, 0o755)
                    run("install_name_tool", "-id", "@rpath/" + name, dst)
                    todo.append(dst)
            run("install_name_tool", "-change", lib, "@rpath/" + done[lib], f)
    # only bundle-relative rpaths: absolute ones (CMake's /opt/homebrew/lib, Homebrew's Cellar paths) come first in
    # dyld's search and would load the system's copies again
    for f in [exe] + [os.path.join(frameworks, n) for n in set(done.values())]:
        want = "@executable_path/../Frameworks" if f == exe else "@loader_path"
        have = rpaths(f)
        for r in have:
            if not r.startswith("@"):
                run("install_name_tool", "-delete_rpath", r, f)
        if want not in have:
            run("install_name_tool", "-add_rpath", want, f)
    return sorted(set(done.values()))


def make_icon(src_png, out_icns):
    with tempfile.TemporaryDirectory() as tmp:
        square = os.path.join(tmp, "square.png")
        shutil.copyfile(src_png, square)
        w = int(run("sips", "-g", "pixelWidth", square).split()[-1])
        h = int(run("sips", "-g", "pixelHeight", square).split()[-1])
        side = max(w, h)
        run("sips", "--padToHeightWidth", str(side), str(side), square)
        iconset = os.path.join(tmp, "AppIcon.iconset")
        os.makedirs(iconset)
        for s in (16, 32, 128, 256, 512):
            run("sips", "-z", str(s), str(s), square, "--out", os.path.join(iconset, f"icon_{s}x{s}.png"))
            run("sips", "-z", str(2 * s), str(2 * s), square, "--out", os.path.join(iconset, f"icon_{s}x{s}@2x.png"))
        run("iconutil", "-c", "icns", iconset, "-o", out_icns)


PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Pokemon Stadium</string>
    <key>CFBundleDisplayName</key><string>Pokémon Stadium</string>
    <key>CFBundleIdentifier</key><string>{bid}</string>
    <key>CFBundleVersion</key><string>{ver}</string>
    <key>CFBundleShortVersionString</key><string>{ver}</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleExecutable</key><string>pokestadium</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
"""


def main():
    build, app = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    version = sys.argv[3] if len(sys.argv) > 3 else "0.0.0"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if os.path.exists(app):
        shutil.rmtree(app)
    macos, res, fw = (os.path.join(app, "Contents", d) for d in ("MacOS", "Resources", "Frameworks"))
    for d in (macos, res, fw):
        os.makedirs(d)
    exe = os.path.join(macos, "pokestadium")
    shutil.copy2(os.path.join(build, "pokestadium"), exe)
    with open(os.path.join(app, "Contents", "Info.plist"), "w") as f:
        f.write(PLIST.format(bid=BUNDLE_ID, ver=version))
    icon = os.path.abspath(sys.argv[4]) if len(sys.argv) > 4 else os.path.join(root, "tools/macos/AppIcon.png")
    make_icon(icon, os.path.join(res, "AppIcon.icns"))
    libs = bundle_libs(exe, fw)
    run("codesign", "--force", "--deep", "--sign", "-", app)
    files = [exe] + [os.path.join(fw, n) for n in libs]
    left = [l for f in files for l in deps(f) + rpaths(f) if not l.startswith("@")]
    print("bundled %d libraries: %s" % (len(libs), ", ".join(libs)))
    if left:
        print("still outside the bundle:", ", ".join(sorted(set(left))))
        sys.exit(1)
    subprocess.run([sys.executable, "-I", os.path.join(root, "tools", "check_build_paths.py"), app], check=True)
    print("wrote", app)


if __name__ == "__main__":
    main()
