"""Where the generators find the decomp build the port's game code corresponds to.

The port runs US Rev 2 (1.2) code for every ROM it accepts (src/port/rom_version.cpp maps other ROMs' offsets onto
it), so the tables in src/port/generated/ come from the decomp's us_rev2 build:
    cd $PORT_DECOMP_DIR && make extract VERSION=us_rev2 && make VERSION=us_rev2
PORT_DECOMP_DIR is the unmodified decomp checkout (default ../pokestadium)."""
import os, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
DECOMP = pathlib.Path(os.environ.get("PORT_DECOMP_DIR", ROOT.parent / "pokestadium"))
# the decomp build of each version, by the port's version name
MAPS = {
    "us": DECOMP / "build/pokestadium-us.map",
    "us_rev1": DECOMP / "build/us_rev1/pokestadium-us_rev1.map",
    "us_rev2": DECOMP / "build/us_rev2/pokestadium-us_rev2.map",
}
# the version the port's game code is compiled as (VERSION_US_REV2 in CMakeLists.txt)
CODE_VERSION = "us_rev2"
CODE_MAP = MAPS[CODE_VERSION]
