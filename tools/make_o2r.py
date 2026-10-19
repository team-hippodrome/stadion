#!/usr/bin/env python3 -I
"""Packs the port's runtime resources into build/pokestadium.o2r (a zip libultraship opens as an archive):
the Fast3D shader sources, the shader warm-up list, ImGui fonts, the launcher pictures and a port version marker. Game data is not in here; it is read from the ROM."""
import zipfile, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent
out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/pokestadium.o2r"
out.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("portVersion", b"\x00\x00\x00\x01\x00\x00")
    shaders = root / "libultraship/src/fast/shaders"
    for p in sorted(shaders.rglob("*")):
        if p.is_file():
            data = p.read_bytes()
            if p.suffix == ".metal":
                # The macOS 27 Metal compiler rejects address-space qualifiers on by-value helper parameters
                # ("parameter may not be qualified with an address space"); drop them there. References keep
                # theirs: older Metal language versions (binaries built with an older SDK, e.g. the macOS 15 CI
                # runner) require it ("reference type must have explicit address space qualifier").
                import re
                text = data.decode()
                text = re.sub(r"\bthread (const \w+(?:<\w+>)? )(?=\w)", r"\1", text)
                data = text.encode()
            z.writestr("shaders/" + p.relative_to(shaders).as_posix(), data)
    # the shader programs built ahead while the launcher shows (src/port/shader_warmup.cpp, tools/shader_list.py)
    z.write(root / "src/port/shader_warmup.txt", "port/shader_warmup.txt")
    # launcher pictures (cartridges, N64), shown by src/port/launcher.cpp
    for p in sorted((root / "tools/launcher/assets").glob("*.png")):
        z.write(p, "textures/launcher/" + p.name)
    fonts = root / "ghostship-ref/port/fonts"
    if fonts.is_dir():
        for p in sorted(fonts.rglob("*")):
            if p.is_file():
                z.write(p, "fonts/" + p.relative_to(fonts).as_posix())
print("wrote", out)
