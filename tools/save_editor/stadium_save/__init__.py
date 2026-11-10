"""Pokémon Stadium (N64, US) save editor library. See ../SAVE_FORMAT.md for the format."""
import sys

if sys.version_info < (3, 9):  # checked before the modules below load; the launcher reads exit code 3 as "too old"
    sys.stderr.write("The Stadium save editor needs Python 3.9 or newer (this is %d.%d)\n" % sys.version_info[:2])
    sys.exit(3)

from .save import RECORDS, SaveFile, footer_state, new_save_bytes  # noqa: F401
from .pokemon import make_mon  # noqa: F401
