"""Fail if game code declares Port_* functions itself (they belong in src/port/StadionPort.h), or defines the same
Port* helper in more than one file (shared helpers belong in one header, e.g. game/include/port_endian.h)."""
import collections, pathlib, re, sys
ALLOWED = {"StadionPort.h", "port_compat.h", "ultra64.h"}
extern_pattern = re.compile(r"extern .*\bPort_")
define_pattern = re.compile(r"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w \*]*?\b(Port[A-Za-z]*_\w+)\s*\([^;]*$")
hits, definitions = [], collections.defaultdict(list)
for path in sorted(pathlib.Path("game").rglob("*.[ch]")):
    if path.name in ALLOWED or "generated" in path.parts:
        continue
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if extern_pattern.search(line):
            hits.append(f"{path}:{number}: local declaration: {line.strip()}")
        elif (match := define_pattern.match(line)) and line.rstrip().endswith("{"):
            definitions[match.group(1)].append(f"{path}:{number}")
for name, places in sorted(definitions.items()):
    if len(places) > 1:
        hits.append(f"{name} defined {len(places)} times: {', '.join(places)}")
print("\n".join(hits) or "OK: no local Port_* declarations or duplicate Port* helpers in game/")
sys.exit(1 if hits else 0)  # fix: declare it once in StadionPort.h (port boundary) or port_endian.h (byte order)
