#!/usr/bin/env python3 -I
"""Packs a built port into a single-file Linux AppImage (Linux releases).

Usage: tools/make_appimage.py <build dir> <output .AppImage> [version] [appimagetool]

- usr/bin/pokestadium (its resources are built in); started without arguments the game shows the launcher
- AppRun keeps config, logs and saves beside the original .AppImage, outside its read-only mount. SHIP_HOME overrides it.
- a .desktop file and the icon from tools/launcher/assets
- no bundled libraries: the release build links SDL3, libzip, spdlog, tinyxml2 and the C++ runtime statically
  (build.yml), so the executable needs only glibc and the system's OpenGL (libOpenGL/libGLX, from the GPU driver,
  which must not be bundled); SDL loads X11/Wayland/PulseAudio/PipeWire at run time. Anything else is an error here.
- appimagetool (default: on PATH) makes the AppImage; ARCH comes from the executable
"""
import os, re, shutil, subprocess, sys

# libraries the executable may link: glibc and the OpenGL dispatch libraries of the system's driver (libglvnd)
SYSTEM_LIBS = re.compile(r"^(linux-vdso|linux-gate|ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|"
                         r"libOpenGL|libGL|libGLX|libEGL|libGLdispatch)\.so")

APPRUN = """#!/bin/sh
# Pokémon Stadium AppImage entry point. Keep data beside the original artifact, outside its read-only mount.
HERE="$(dirname "$(readlink -f "$0")")"
if [ -z "$SHIP_HOME" ]; then
    if [ -n "$APPIMAGE" ]; then
        SHIP_HOME="$(dirname "$(readlink -f "$APPIMAGE")")"
    else
        SHIP_HOME="$HERE"
    fi
    export SHIP_HOME
fi
mkdir -p "$SHIP_HOME"
# the window's app id matches the .desktop file (taskbar icon on Wayland)
export SDL_APP_ID="${SDL_APP_ID:-pokestadium}"
exec "$HERE/usr/bin/pokestadium" "$@"
"""

DESKTOP = """[Desktop Entry]
Type=Application
Name=Pokémon Stadium
Comment=Native PC port of Pokémon Stadium (needs your own ROM)
Exec=pokestadium %f
Icon=pokestadium
Terminal=false
Categories=Game;
X-AppImage-Version={ver}
"""


def run(*args, **kw):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kw).stdout


def needed_libs(exe):
    """the DT_NEEDED entries of an ELF file"""
    return re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", run("readelf", "-d", exe))


def elf_arch(exe):
    machine = re.search(r"Machine:\s+(.+)", run("readelf", "-h", exe)).group(1)
    return "aarch64" if "AArch64" in machine else "x86_64"


def main():
    build, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    version = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else "0.0.0"
    appimagetool = sys.argv[4] if len(sys.argv) > 4 else "appimagetool"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    exe_src = os.path.join(build, "pokestadium")

    extra = [l for l in needed_libs(exe_src) if not SYSTEM_LIBS.match(l)]
    if extra:
        print("pokestadium links libraries an AppImage cannot rely on:", ", ".join(extra))
        print("(build with the static dependencies of build.yml)")
        sys.exit(1)

    appdir = out + ".AppDir"
    if os.path.exists(appdir):
        shutil.rmtree(appdir)
    bindir = os.path.join(appdir, "usr", "bin")
    apps = os.path.join(appdir, "usr", "share", "applications")
    icons = os.path.join(appdir, "usr", "share", "icons", "hicolor", "256x256", "apps")
    for d in (bindir, apps, icons):
        os.makedirs(d)
    shutil.copy2(exe_src, os.path.join(bindir, "pokestadium"))
    icon = os.path.join(root, "tools", "launcher", "assets", "pokestadium.png")
    shutil.copy2(icon, os.path.join(icons, "pokestadium.png"))
    shutil.copy2(icon, os.path.join(appdir, "pokestadium.png"))
    shutil.copy2(icon, os.path.join(appdir, ".DirIcon"))
    for d in (appdir, apps):
        with open(os.path.join(d, "pokestadium.desktop"), "w") as f:
            f.write(DESKTOP.format(ver=version))
    apprun = os.path.join(appdir, "AppRun")
    with open(apprun, "w") as f:
        f.write(APPRUN)
    os.chmod(apprun, 0o755)

    subprocess.run([sys.executable, "-I", os.path.join(root, "tools", "check_build_paths.py"), appdir], check=True)

    env = dict(os.environ, ARCH=elf_arch(exe_src), VERSION=version,
               APPIMAGE_EXTRACT_AND_RUN="1")  # appimagetool is an AppImage itself; containers have no FUSE
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([appimagetool, "--no-appstream", appdir, out], check=True, env=env)
    shutil.rmtree(appdir)
    print("wrote", out)


if __name__ == "__main__":
    main()
