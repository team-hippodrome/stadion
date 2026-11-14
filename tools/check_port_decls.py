"""Fail if game code declares Port_* functions itself; they belong in src/port/StadionPort.h."""
import pathlib, re, sys
ALLOWED = {"StadionPort.h", "port_compat.h", "ultra64.h"}
pattern = re.compile(r"extern .*\bPort_")
hits = [f"{path}:{number}: {line.strip()}" for path in sorted(pathlib.Path("game").rglob("*.[ch]"))
        if path.name not in ALLOWED and "generated" not in path.parts
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1) if pattern.search(line)]
print("\n".join(hits) or "OK: no local Port_* declarations in game/")
sys.exit(1 if hits else 0)  # fix: include "StadionPort.h" and declare the function there
