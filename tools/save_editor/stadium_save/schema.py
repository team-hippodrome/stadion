"""A small declarative struct codec with exact round trips.

A Struct lists (offset, name, codec) fields. decode() returns a dict; every byte that no field covers is kept as
"_raw_XXXX" (hex offset) so encode(decode(b)) == b for any input. Values that a codec cannot express canonically
(for example name padding after the terminator) fall back to {"text": ..., "raw": hex}.

All multi-byte numbers are big-endian (N64 byte order).
"""
import struct

from . import text as textcodec


def raw_to_json(data):
    """Bytes -> compact JSON string: 'fill:XX*N' when uniform, else hex."""
    data = bytes(data)
    if data and data.count(data[0]) == len(data):
        return "fill:%02x*%d" % (data[0], len(data))
    return data.hex()


def raw_from_json(value, size=None):
    if value.startswith("fill:"):
        b, n = value[5:].split("*")
        data = bytes([int(b, 16)]) * int(n)
    else:
        data = bytes.fromhex(value)
    if size is not None and len(data) != size:
        raise ValueError("raw field must be %d bytes, got %d" % (size, len(data)))
    return data


class Codec:
    size = 0

    def decode(self, data):
        raise NotImplementedError

    def encode(self, value):
        raise NotImplementedError


class Int(Codec):
    def __init__(self, fmt):
        self.fmt = ">" + fmt
        self.size = struct.calcsize(self.fmt)

    def decode(self, data):
        return struct.unpack(self.fmt, data)[0]

    def encode(self, value):
        return struct.pack(self.fmt, int(value, 0) if isinstance(value, str) else value)


class U24(Codec):
    size = 3

    def decode(self, data):
        return int.from_bytes(data, "big")

    def encode(self, value):
        return int(value).to_bytes(3, "big")


class F32(Codec):
    size = 4

    def decode(self, data):
        v = struct.unpack(">f", data)[0]
        # keep the float only when it reproduces the same bits (NaN payloads, -0.0 etc. stay raw)
        if struct.pack(">f", v) == data and v == v:
            return v
        return {"raw": data.hex()}

    def encode(self, value):
        if isinstance(value, dict):
            return bytes.fromhex(value["raw"])
        return struct.pack(">f", value)


class Raw(Codec):
    def __init__(self, size):
        self.size = size

    def decode(self, data):
        return raw_to_json(data)

    def encode(self, value):
        return raw_from_json(value, self.size)


class Bytes(Codec):
    """A list of small integers (e.g. opponent history)."""

    def __init__(self, size):
        self.size = size

    def decode(self, data):
        return list(data)

    def encode(self, value):
        if len(value) != self.size:
            raise ValueError("expected %d values" % self.size)
        return bytes(value)


class Text(Codec):
    """Game Boy ('gb') or N64 ('n64') text with canonical zero padding after the terminator."""

    def __init__(self, size, kind="gb"):
        self.size = size
        self.kind = kind

    def _enc(self, s):
        if self.kind == "gb":
            return textcodec.encode_gb(s, self.size)
        return textcodec.encode_n64(s, self.size)

    def decode(self, data):
        s = textcodec.decode_gb(data) if self.kind == "gb" else textcodec.decode_n64(data)
        try:
            if self._enc(s) == bytes(data):
                return s
        except ValueError:
            pass
        return {"text": s, "raw": bytes(data).hex()}

    def encode(self, value):
        if isinstance(value, dict):
            raw = bytes.fromhex(value["raw"])
            cur = textcodec.decode_gb(raw) if self.kind == "gb" else textcodec.decode_n64(raw)
            if value.get("text", cur) == cur:
                return raw
            return self._enc(value["text"])
        return self._enc(value)


class Array(Codec):
    def __init__(self, codec, count):
        self.codec = codec
        self.count = count
        self.size = codec.size * count

    def decode(self, data):
        n = self.codec.size
        return [self.codec.decode(data[i * n:(i + 1) * n]) for i in range(self.count)]

    def encode(self, value):
        if len(value) != self.count:
            raise ValueError("expected %d entries, got %d" % (self.count, len(value)))
        return b"".join(self.codec.encode(v) for v in value)


class Struct(Codec):
    def __init__(self, size, fields, empty_is_null=False):
        self.size = size
        self.fields = sorted(fields, key=lambda f: f[0])
        self.empty_is_null = empty_is_null
        pos = 0
        for off, name, codec in self.fields:
            if off < pos:
                raise ValueError("overlapping field %s at 0x%X" % (name, off))
            pos = off + codec.size
        if pos > size:
            raise ValueError("fields exceed struct size")

    def gaps(self):
        pos = 0
        for off, name, codec in self.fields:
            if off > pos:
                yield pos, off - pos
            pos = off + codec.size
        if pos < self.size:
            yield pos, self.size - pos

    def decode(self, data):
        data = bytes(data)
        assert len(data) == self.size
        if self.empty_is_null and not any(data):
            return None
        out = {}
        for off, name, codec in self.fields:
            out[name] = codec.decode(data[off:off + codec.size])
        for off, n in self.gaps():
            chunk = data[off:off + n]
            if any(chunk):
                out["_raw_%04X" % off] = raw_to_json(chunk)
        return out

    def encode(self, value):
        buf = bytearray(self.size)
        if value is None:
            return bytes(buf)
        for off, name, codec in self.fields:
            if name in value:
                buf[off:off + codec.size] = codec.encode(value[name])
        for off, n in self.gaps():
            key = "_raw_%04X" % off
            if key in value:
                buf[off:off + n] = raw_from_json(value[key], n)
        return bytes(buf)


U8, S8, U16, S16, U32, S32 = Int("B"), Int("b"), Int("H"), Int("h"), Int("I"), Int("i")
