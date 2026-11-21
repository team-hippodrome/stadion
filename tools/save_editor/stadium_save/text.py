"""Text codecs used in the save.

Game Boy text (Gen 1 character set, terminated by 0x50) is what Text_TranscodeName (src/gb_data.c) writes for
names in registered teams, boxes, Victory Palace records and the continue record. The game turns it back into its
own N64 font bytes with D_80073030 (Text_UntranscodeName). Byte 0x5D is the "player label" token that
Text_UntranscodeName expands to the player's name; it is shown as <PLAYER> here.

N64 text (photo records) is the game's own font encoding: latin-1 compatible bytes, NUL terminated.

Names are decoded to str. A byte without a glyph is written as <xx> (hex) so every byte sequence survives a
decode/encode round trip up to the terminator. Bytes after the terminator are not part of the text; callers keep
them separately when they are not the canonical padding.
"""
from . import gamedata

# N64 font bytes whose latin-1 meaning differs from the glyph the game draws
N64_SPECIAL = {0xA9: "\u2642", 0xBE: "\u2640", 0xBC: "<PK>", 0xBD: "<MN>"}
_N64_SPECIAL_REV = {v: k for k, v in N64_SPECIAL.items()}

GB_TERMINATOR = 0x50
GB_PLAYER = 0x5D
GB_SPACE = 0x7F

_GB_TO_CHR = {}
for _b in range(256):
    _c = gamedata.GB_TO_N64[_b]
    if _b == GB_SPACE:
        _GB_TO_CHR[_b] = " "
    elif _c != 0x20:
        _GB_TO_CHR[_b] = N64_SPECIAL.get(_c, bytes([_c]).decode("latin-1"))
_CHR_TO_GB = {}
for _b, _c in sorted(_GB_TO_CHR.items(), reverse=True):  # lowest byte wins for duplicate glyphs
    _CHR_TO_GB[_c] = _b


def decode_gb(data):
    """Game Boy text up to the 0x50 terminator (or the end of the field)."""
    out = []
    for b in data:
        if b == GB_TERMINATOR:
            break
        if b == GB_PLAYER:
            out.append("<PLAYER>")
        elif b in _GB_TO_CHR and (_GB_TO_CHR[b] in ("<PK>", "<MN>") or _GB_TO_CHR[b] not in "<>"):
            out.append(_GB_TO_CHR[b])
        else:
            out.append("<%02X>" % b)
    return "".join(out)


# Alternative spellings accepted when encoding (typing "<M>" is easier than a ♂ the keyboard does not have, and
# word processors turn ' and - into typographic characters)
_ALIASES = {"<M>": "\u2642", "<F>": "\u2640", "<PK>": "<PK>", "<MN>": "<MN>",
            "\u2019": "'", "\u2018": "'", "\u00b4": "'", "`": "'", "\u2013": "-", "\u2014": "-", "\u2212": "-",
            "\u00d7": "\u00d7", "\u2715": "\u00d7"}


def _tokens(text):
    """Characters, and <...> groups (<PK>, <MN>, <PLAYER>, <xx> hex byte). A "<" without a closing ">" is a character."""
    i = 0
    while i < len(text):
        j = text.find(">", i) if text[i] == "<" else -1
        if j > 0:
            yield text[i:j + 1]
            i = j + 1
        else:
            yield text[i]
            i += 1


def encode_gb(text, size, pad=0x00):
    """Game Boy text + 0x50 terminator, padded to size bytes."""
    out = bytearray()
    for tok in _tokens(text):
        key = tok.upper() if len(tok) > 1 else tok
        if key == "<PLAYER>":
            out.append(GB_PLAYER)
        elif key in _CHR_TO_GB:
            out.append(_CHR_TO_GB[key])
        elif key in _ALIASES and _ALIASES[key] in _CHR_TO_GB:
            out.append(_CHR_TO_GB[_ALIASES[key]])
        elif len(tok) > 1 and _is_hex_byte(tok[1:-1]):
            out.append(int(tok[1:-1], 16))
        elif len(tok) == 1 and tok.upper() in _CHR_TO_GB:
            out.append(_CHR_TO_GB[tok.upper()])
        else:
            raise ValueError("character %r has no Game Boy encoding" % tok)
    out.append(GB_TERMINATOR)
    if len(out) > size:
        raise ValueError("text %r needs %d bytes, field has %d" % (text, len(out), size))
    return bytes(out) + bytes([pad]) * (size - len(out))


def _is_hex_byte(h):
    return len(h) == 2 and all(c in "0123456789abcdefABCDEF" for c in h)


def decode_n64(data):
    end = bytes(data).find(b"\0")
    raw = bytes(data) if end < 0 else bytes(data)[:end]
    return "".join(N64_SPECIAL.get(b, chr(b)) if b != ord("<") else "<3C>" for b in raw)


def encode_n64(text, size):
    out = bytearray()
    for tok in _tokens(text):
        if tok in _N64_SPECIAL_REV:
            out.append(_N64_SPECIAL_REV[tok])
        elif len(tok) > 1:
            out.append(int(tok[1:-1], 16))
        else:
            out += tok.encode("latin-1")
    out = bytes(out) + b"\0"
    if len(out) > size:
        raise ValueError("text %r needs %d bytes, field has %d" % (text, len(out), size))
    return out + bytes(size - len(out))
