// The C++ port of tools/save_editor/stadium_save: text.py, schema.py, pokemon.py, save.py and the editor operations of
// web.py. Function by function it follows the Python code (named in the comments), so the two give the same JSON and
// the same bytes; tools/save_editor/tests/test_native.py compares them.
#include "stadium_save.h"
#include "gamedata.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <iterator>
#include <set>

namespace stadium_save {

namespace gd = gamedata;
namespace fs = std::filesystem;

// ---- helpers ---------------------------------------------------------------------------------------------------------

std::string Hex(const uint8_t* d, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        s[2 * i] = digits[d[i] >> 4];
        s[2 * i + 1] = digits[d[i] & 15];
    }
    return s;
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// bytes.fromhex: pairs of hex digits, spaces allowed between them
Bytes FromHex(const std::string& s) {
    Bytes out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == ' ') {
            i++;
            continue;
        }
        if (i + 1 >= s.size() || HexDigit(s[i]) < 0 || HexDigit(s[i + 1]) < 0) {
            throw Error("non-hexadecimal number found in fromhex() arg at position " + std::to_string(i));
        }
        out.push_back((uint8_t)(HexDigit(s[i]) << 4 | HexDigit(s[i + 1])));
        i += 2;
    }
    return out;
}

std::string RawToJson(const uint8_t* d, size_t n) {
    if (n > 0 && std::all_of(d, d + n, [&](uint8_t b) { return b == d[0]; })) {
        char buf[48];
        snprintf(buf, sizeof(buf), "fill:%02x*%zu", d[0], n);
        return buf;
    }
    return Hex(d, n);
}

Bytes RawFromJson(const std::string& v, long size) {
    Bytes data;
    if (v.rfind("fill:", 0) == 0) {
        size_t star = v.find('*');
        if (star == std::string::npos) throw Error("bad fill value " + v);
        int b = std::stoi(v.substr(5, star - 5), nullptr, 16);
        long n = std::stol(v.substr(star + 1));
        data.assign((size_t)std::max(0L, n), (uint8_t)b);
    } else {
        data = FromHex(v);
    }
    if (size >= 0 && (long)data.size() != size) {
        throw Error("raw field must be " + std::to_string(size) + " bytes, got " + std::to_string(data.size()));
    }
    return data;
}

static bool Truthy(const json& v) {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<long long>() != 0;
    if (v.is_number_float()) return v.get<double>() != 0;
    if (v.is_string()) return !v.get_ref<const std::string&>().empty();
    return !v.empty();
}

static std::string Strip(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\f\v"), b = s.find_last_not_of(" \t\r\n\f\v");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool IsDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

static long long ParseInt(const std::string& text, int base) {
    std::string s = Strip(text);
    bool neg = false;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        neg = s[0] == '-';
        s = s.substr(1);
    }
    if (base == 0) { // Python int(s, 0): 0x / 0o / 0b prefixes
        base = 10;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) base = 16, s = s.substr(2);
        else if (s.size() > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) base = 8, s = s.substr(2);
        else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) base = 2, s = s.substr(2);
    } else if (base == 16 && s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s = s.substr(2);
    }
    if (s.empty()) throw Error("invalid literal for int(): '" + text + "'");
    long long v = 0;
    for (char c : s) {
        if (c == '_') continue;
        int d = HexDigit(c);
        if (d < 0 || d >= base) throw Error("invalid literal for int(): '" + text + "'");
        v = v * base + d;
    }
    return neg ? -v : v;
}

long long PyInt(const json& v) {
    if (v.is_number_integer()) return v.get<long long>();
    if (v.is_number_unsigned()) return (long long)v.get<unsigned long long>();
    if (v.is_number_float()) return (long long)v.get<double>();
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_string()) return ParseInt(v.get<std::string>(), 10);
    throw Error("int() argument must be a string or a number, not " + std::string(v.type_name()));
}

static int ByteValue(const json& v) {
    long long x = PyInt(v);
    if (x < 0 || x > 255) throw Error("byte must be in range(0, 256)");
    return (int)x;
}

static json Get(const json& obj, const char* key, const json& dflt) {
    if (obj.is_object()) {
        auto it = obj.find(key);
        if (it != obj.end()) return *it;
    }
    return dflt;
}

// UTF-8 <-> code points
static std::vector<uint32_t> CodePoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c >> 5) == 6) cp = c & 0x1F, n = 2;
        else if ((c >> 4) == 14) cp = c & 0x0F, n = 3;
        else if ((c >> 3) == 30) cp = c & 0x07, n = 4;
        else cp = 0xFFFD, n = 1;
        if (i + n > s.size()) {
            out.push_back(0xFFFD);
            break;
        }
        for (int k = 1; k < n; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

static std::string Utf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

static uint32_t UpperCp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
    if (cp == 0xFF) return 0x178;
    if (cp == 0xB5) return 0x39C;
    return cp;
}

std::string Upper(const std::string& utf8) {
    std::string out;
    for (uint32_t cp : CodePoints(utf8)) {
        if (cp == 0xDF) out += "SS"; // ß
        else out += Utf8(UpperCp(cp));
    }
    return out;
}

std::string TextOf(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object() && v.contains("text") && v["text"].is_string()) return v["text"].get<std::string>();
    return "";
}

// ---- text.py ---------------------------------------------------------------------------------------------------------

namespace {

constexpr uint8_t GB_TERMINATOR = 0x50, GB_PLAYER = 0x5D, GB_SPACE = 0x7F;

std::string N64Special(uint8_t b) {
    switch (b) {
        case 0xA9: return "♂";
        case 0xBE: return "♀";
        case 0xBC: return "<PK>";
        case 0xBD: return "<MN>";
    }
    return "";
}

struct GbTables {
    std::string toChr[256];             // "" = no glyph
    std::map<std::string, uint8_t> fromChr;
    std::map<std::string, uint8_t> n64Rev;
    GbTables() {
        for (int b = 0; b < 256; b++) {
            uint8_t c = gd::GB_TO_N64[b];
            if (b == GB_SPACE) toChr[b] = " ";
            else if (c != 0x20) toChr[b] = !N64Special(c).empty() ? N64Special(c) : Utf8(c);
        }
        for (int b = 255; b >= 0; b--) { // lowest byte wins for duplicate glyphs
            if (!toChr[b].empty()) fromChr[toChr[b]] = (uint8_t)b;
        }
        for (int b : { 0xA9, 0xBE, 0xBC, 0xBD }) n64Rev[N64Special((uint8_t)b)] = (uint8_t)b;
    }
};

const GbTables& Tables() {
    static GbTables t;
    return t;
}

// text._tokens: "<...>" groups or single characters (as UTF-8); a "<" without a closing ">" is a character
std::vector<std::string> Tokens(const std::string& text) {
    std::vector<uint32_t> cps = CodePoints(text);
    std::vector<std::string> out;
    for (size_t i = 0; i < cps.size();) {
        size_t j = i;
        if (cps[i] == '<') {
            while (j < cps.size() && cps[j] != '>') j++;
        }
        if (cps[i] == '<' && j < cps.size() && j > i) {
            std::string tok;
            for (size_t k = i; k <= j; k++) tok += Utf8(cps[k]);
            out.push_back(tok);
            i = j + 1;
        } else {
            out.push_back(Utf8(cps[i]));
            i++;
        }
    }
    return out;
}

bool IsGroup(const std::string& tok) {
    return tok.size() > 1 && tok[0] == '<';
}

bool IsHexByte(const std::string& h) {
    return h.size() == 2 && isxdigit((unsigned char)h[0]) && isxdigit((unsigned char)h[1]);
}

int GroupByte(const std::string& tok) {
    long long v = ParseInt(tok.substr(1, tok.size() - 2), 16);
    if (v < 0 || v > 255) throw Error("byte must be in range(0, 256)");
    return (int)v;
}

// text._ALIASES: spellings accepted when encoding
std::string Alias(const std::string& tok) {
    if (tok == "<M>") return "♂";
    if (tok == "<F>") return "♀";
    if (tok == "\u2019" || tok == "\u2018" || tok == "\u00b4" || tok == "`") return "'";
    if (tok == "\u2013" || tok == "\u2014" || tok == "\u2212") return "-";
    if (tok == "\u2715") return "\u00d7";
    return "";
}

std::string UpperAscii(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

std::string Repr(const std::string& s) {
    return "'" + s + "'";
}

} // namespace

std::string DecodeGb(const uint8_t* d, size_t n) {
    const GbTables& t = Tables();
    std::string out;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = d[i];
        if (b == GB_TERMINATOR) break;
        if (b == GB_PLAYER) {
            out += "<PLAYER>";
        } else if (!t.toChr[b].empty() &&
                   (t.toChr[b] == "<PK>" || t.toChr[b] == "<MN>" || (t.toChr[b] != "<" && t.toChr[b] != ">"))) {
            out += t.toChr[b];
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "<%02X>", b);
            out += buf;
        }
    }
    return out;
}

Bytes EncodeGb(const std::string& text, size_t size, uint8_t pad) {
    const GbTables& t = Tables();
    Bytes out;
    for (const std::string& tok : Tokens(text)) {
        const std::string key = IsGroup(tok) ? UpperAscii(tok) : tok;
        auto it = t.fromChr.find(key);
        if (key == "<PLAYER>") {
            out.push_back(GB_PLAYER);
        } else if (it != t.fromChr.end()) {
            out.push_back(it->second);
        } else if (!Alias(key).empty() && t.fromChr.count(Alias(key))) {
            out.push_back(t.fromChr.at(Alias(key)));
        } else if (IsGroup(tok) && IsHexByte(tok.substr(1, tok.size() - 2))) {
            out.push_back((uint8_t)GroupByte(tok));
        } else if (!IsGroup(tok) && t.fromChr.count(Upper(tok))) {
            out.push_back(t.fromChr.at(Upper(tok)));
        } else {
            throw Error("character " + Repr(tok) + " has no Game Boy encoding");
        }
    }
    out.push_back(GB_TERMINATOR);
    if (out.size() > size) {
        throw Error("text " + Repr(text) + " needs " + std::to_string(out.size()) + " bytes, field has " +
                    std::to_string(size));
    }
    out.resize(size, pad);
    return out;
}

bool GbEncodable(const std::string& text, size_t size, std::string* error) {
    try {
        EncodeGb(text, size);
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return false;
    }
}

std::string DecodeN64(const uint8_t* d, size_t n) {
    std::string out;
    for (size_t i = 0; i < n && d[i] != 0; i++) {
        if (d[i] == '<') out += "<3C>";
        else if (!N64Special(d[i]).empty()) out += N64Special(d[i]);
        else out += Utf8(d[i]);
    }
    return out;
}

Bytes EncodeN64(const std::string& text, size_t size) {
    const GbTables& t = Tables();
    Bytes out;
    for (const std::string& tok : Tokens(text)) {
        auto it = t.n64Rev.find(tok);
        if (it != t.n64Rev.end()) {
            out.push_back(it->second);
        } else if (IsGroup(tok)) {
            out.push_back((uint8_t)GroupByte(tok));
        } else {
            uint32_t cp = CodePoints(tok)[0];
            if (cp > 0xFF) throw Error("'latin-1' codec can't encode character " + Repr(tok));
            out.push_back((uint8_t)cp);
        }
    }
    out.push_back(0);
    if (out.size() > size) {
        throw Error("text " + Repr(text) + " needs " + std::to_string(out.size()) + " bytes, field has " +
                    std::to_string(size));
    }
    out.resize(size, 0);
    return out;
}

// ---- schema.py -------------------------------------------------------------------------------------------------------

namespace {

using CodecPtr = std::shared_ptr<const Codec>;

struct IntCodec : Codec {
    bool sign;
    explicit IntCodec(size_t n, bool s) : sign(s) { size = n; }
    json decode(const uint8_t* d) const override {
        unsigned long long v = 0;
        for (size_t i = 0; i < size; i++) v = v << 8 | d[i];
        if (sign && (v >> (size * 8 - 1)) & 1) return (long long)v - (1LL << (size * 8));
        return (long long)v;
    }
    Bytes encode(const json& value) const override {
        long long v = value.is_string() ? ParseInt(value.get<std::string>(), 0) : PyInt(value);
        long long lo = sign ? -(1LL << (size * 8 - 1)) : 0;
        long long hi = sign ? (1LL << (size * 8 - 1)) - 1 : (long long)((1ULL << (size * 8)) - 1);
        if (v < lo || v > hi) throw Error("argument out of range");
        Bytes b(size);
        for (size_t i = 0; i < size; i++) b[size - 1 - i] = (uint8_t)((unsigned long long)v >> (8 * i));
        return b;
    }
};

struct F32Codec : Codec {
    F32Codec() { size = 4; }
    json decode(const uint8_t* d) const override {
        uint32_t bits = (uint32_t)d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3];
        float f;
        memcpy(&f, &bits, 4);
        if (std::isnan(f)) return json{ { "raw", Hex(d, 4) } };
        return (double)f;
    }
    Bytes encode(const json& value) const override {
        if (value.is_object()) return FromHex(value.at("raw").get<std::string>());
        float f = (float)value.get<double>();
        uint32_t bits;
        memcpy(&bits, &f, 4);
        return { (uint8_t)(bits >> 24), (uint8_t)(bits >> 16), (uint8_t)(bits >> 8), (uint8_t)bits };
    }
};

struct RawCodec : Codec {
    explicit RawCodec(size_t n) { size = n; }
    json decode(const uint8_t* d) const override { return RawToJson(d, size); }
    Bytes encode(const json& value) const override { return RawFromJson(value.get<std::string>(), (long)size); }
};

struct BytesCodec : Codec {
    explicit BytesCodec(size_t n) { size = n; }
    json decode(const uint8_t* d) const override {
        json a = json::array();
        for (size_t i = 0; i < size; i++) a.push_back(d[i]);
        return a;
    }
    Bytes encode(const json& value) const override {
        if (value.size() != size) throw Error("expected " + std::to_string(size) + " values");
        Bytes b;
        for (const json& x : value) b.push_back((uint8_t)ByteValue(x));
        return b;
    }
};

struct TextCodec : Codec {
    bool gb;
    TextCodec(size_t n, bool isGb) : gb(isGb) { size = n; }
    Bytes enc(const std::string& s) const { return gb ? EncodeGb(s, size) : EncodeN64(s, size); }
    std::string dec(const uint8_t* d, size_t n) const { return gb ? DecodeGb(d, n) : DecodeN64(d, n); }
    json decode(const uint8_t* d) const override {
        std::string s = dec(d, size);
        try {
            if (enc(s) == Bytes(d, d + size)) return s;
        } catch (const Error&) {
        }
        return json{ { "text", s }, { "raw", Hex(d, size) } };
    }
    Bytes encode(const json& value) const override {
        if (value.is_object()) {
            Bytes raw = FromHex(value.at("raw").get<std::string>());
            std::string cur = dec(raw.data(), raw.size());
            if (!value.contains("text") || value["text"] == json(cur)) return raw;
            return enc(value["text"].get<std::string>());
        }
        return enc(value.get<std::string>());
    }
};

struct ArrayCodec : Codec {
    CodecPtr codec;
    size_t count;
    ArrayCodec(CodecPtr c, size_t n) : codec(std::move(c)), count(n) { size = codec->size * n; }
    json decode(const uint8_t* d) const override {
        json a = json::array();
        for (size_t i = 0; i < count; i++) a.push_back(codec->decode(d + i * codec->size));
        return a;
    }
    Bytes encode(const json& value) const override {
        if (value.size() != count) {
            throw Error("expected " + std::to_string(count) + " entries, got " + std::to_string(value.size()));
        }
        Bytes b;
        for (const json& v : value) {
            Bytes e = codec->encode(v);
            b.insert(b.end(), e.begin(), e.end());
        }
        return b;
    }
};

struct Field {
    size_t off;
    std::string name;
    CodecPtr codec;
};

struct StructCodec : Codec {
    std::vector<Field> fields;
    bool emptyIsNull;
    std::vector<std::pair<size_t, size_t>> gaps;
    StructCodec(size_t n, std::vector<Field> f, bool nullWhenEmpty = false) : fields(std::move(f)), emptyIsNull(nullWhenEmpty) {
        size = n;
        std::sort(fields.begin(), fields.end(), [](const Field& a, const Field& b) { return a.off < b.off; });
        size_t pos = 0;
        for (const Field& x : fields) {
            if (x.off > pos) gaps.push_back({ pos, x.off - pos });
            pos = x.off + x.codec->size;
        }
        if (pos < size) gaps.push_back({ pos, size - pos });
    }
    static std::string GapKey(size_t off) {
        char buf[16];
        snprintf(buf, sizeof(buf), "_raw_%04zX", off);
        return buf;
    }
    json decode(const uint8_t* d) const override {
        if (emptyIsNull && std::all_of(d, d + size, [](uint8_t b) { return b == 0; })) return nullptr;
        json out = json::object();
        for (const Field& x : fields) out[x.name] = x.codec->decode(d + x.off);
        for (auto [off, n] : gaps) {
            if (std::any_of(d + off, d + off + n, [](uint8_t b) { return b != 0; })) out[GapKey(off)] = RawToJson(d + off, n);
        }
        return out;
    }
    Bytes encode(const json& value) const override {
        Bytes buf(size, 0);
        if (value.is_null()) return buf;
        for (const Field& x : fields) {
            if (value.contains(x.name)) {
                Bytes e = x.codec->encode(value[x.name]);
                std::copy(e.begin(), e.end(), buf.begin() + x.off);
            }
        }
        for (auto [off, n] : gaps) {
            std::string key = GapKey(off);
            if (value.contains(key)) {
                Bytes e = RawFromJson(value[key].get<std::string>(), (long)n);
                std::copy(e.begin(), e.end(), buf.begin() + off);
            }
        }
        return buf;
    }
};

CodecPtr U8() { static CodecPtr c = std::make_shared<IntCodec>(1, false); return c; }
CodecPtr U16() { static CodecPtr c = std::make_shared<IntCodec>(2, false); return c; }
CodecPtr S16() { static CodecPtr c = std::make_shared<IntCodec>(2, true); return c; }
CodecPtr U32() { static CodecPtr c = std::make_shared<IntCodec>(4, false); return c; }
CodecPtr F32() { return std::make_shared<F32Codec>(); }
CodecPtr Raw(size_t n) { return std::make_shared<RawCodec>(n); }
CodecPtr BytesOf(size_t n) { return std::make_shared<BytesCodec>(n); }
CodecPtr Text(size_t n, bool gb) { return std::make_shared<TextCodec>(n, gb); }
CodecPtr ArrayOf(CodecPtr c, size_t n) { return std::make_shared<ArrayCodec>(std::move(c), n); }
CodecPtr StructOf(size_t n, std::vector<Field> f, bool emptyIsNull = false) {
    return std::make_shared<StructCodec>(n, std::move(f), emptyIsNull);
}

unsigned Be(const uint8_t* d, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | d[i];
    return v;
}

void PutBe(uint8_t* d, int n, long long v, const char* what = "int too big to convert") {
    if (v < 0 || (n < 8 && v >= (1LL << (8 * n)))) throw Error(what);
    for (int i = 0; i < n; i++) d[n - 1 - i] = (uint8_t)(v >> (8 * i));
}

} // namespace

// ---- pokemon.py ------------------------------------------------------------------------------------------------------

namespace pokemon {

const char* const STAT_KEYS[5] = { "hp", "atk", "def", "spd", "spc" };

static const TextCodec& Name() {
    static TextCodec c(11, true);
    return c;
}

const char* SpeciesName(int dex) {
    return dex > 0 && dex <= 151 ? gd::SPECIES[dex] : nullptr;
}

const char* TypeName(int type) {
    return type >= 0 && type < gd::kTypeCount ? gd::TYPES[type] : nullptr;
}

static json TypeJson(int type) {
    const char* t = TypeName(type);
    return t ? json(t) : json(nullptr);
}

// norm: alphanumeric characters only, upper case (species: é counts as E)
static std::string Norm(const std::string& s, bool accents) {
    std::string out;
    for (uint32_t cp : CodePoints(Upper(s))) {
        if (accents && (cp == 0xE9 || cp == 0xC9)) cp = 'E';
        bool alnum = (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                     (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7);
        if (alnum) out += Utf8(cp);
    }
    return out;
}

int SpeciesLookup(const json& value) {
    if (value.is_number_integer() || value.is_boolean()) return (int)PyInt(value);
    std::string v = Upper(Strip(value.is_string() ? value.get<std::string>() : value.dump()));
    if (IsDigits(v)) return (int)ParseInt(v, 10);
    for (int dex = 1; dex < gd::kSpeciesCount; dex++) {
        if (Upper(gd::SPECIES[dex]) == v) return dex;
    }
    for (int dex = 1; dex < gd::kSpeciesCount; dex++) {
        if (Norm(gd::SPECIES[dex], true) == Norm(v, true)) return dex;
    }
    throw Error("unknown species '" + (value.is_string() ? value.get<std::string>() : value.dump()) + "'");
}

int MoveLookup(const json& value) {
    if (value.is_number_integer() || value.is_boolean()) return (int)PyInt(value);
    std::string v = Upper(Strip(value.is_string() ? value.get<std::string>() : value.dump()));
    if (IsDigits(v)) return (int)ParseInt(v, 10);
    for (int mid = 1; mid < gd::kMoveCount; mid++) {
        if (Norm(gd::MOVES[mid], false) == Norm(v, false)) return mid;
    }
    throw Error("unknown move '" + (value.is_string() ? value.get<std::string>() : value.dump()) + "'");
}

std::optional<int> IndexToDex(int index) {
    if (index > 0 && index < gd::kIndexCount) {
        int d = gd::INDEX_TO_DEX[index];
        if (d > 0 && d <= 151) return d;
    }
    return std::nullopt;
}

int DexToIndex(int dex) {
    return dex > 0 && dex <= 151 ? gd::DEX_TO_INDEX[dex] : 0;
}

int HpDv(const json& dvs) {
    return ((PyInt(dvs.at("atk")) & 1) << 3) | ((PyInt(dvs.at("def")) & 1) << 2) | ((PyInt(dvs.at("spd")) & 1) << 1) |
           (PyInt(dvs.at("spc")) & 1);
}

static int StatExpTerm(long long se) {
    int i = 1;
    while (i < 255 && (long long)i * i < se) i++;
    return i / 4;
}

// Python's // floors; the operands here are never negative except for a level 0 (floor matters then)
static long long FloorDiv(long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

json CalcStats(int dex, int level, const json& dvs, const json& statExp) {
    if (dex <= 0 || dex > 151) throw Error("'NoneType' object is not subscriptable");
    const gd::Base& b = gd::BASE[dex];
    const int base[5] = { b.hp, b.atk, b.def, b.spd, b.spc };
    const long long dv[5] = { HpDv(dvs), PyInt(dvs.at("atk")), PyInt(dvs.at("def")), PyInt(dvs.at("spd")), PyInt(dvs.at("spc")) };
    json out = json::object();
    for (int k = 0; k < 5; k++) {
        long long v = FloorDiv((StatExpTerm(PyInt(statExp.at(STAT_KEYS[k]))) + (base[k] + dv[k]) * 2) * level, 100);
        v += k == 0 ? level + 10 : 5;
        out[STAT_KEYS[k]] = std::min(v, 999LL);
    }
    return out;
}

long long ExpForLevel(int dex, int level) {
    int g = gd::BASE[dex].growth;
    long long n = level;
    switch (g) {
        case 0: return n * n * n;
        case 1: return FloorDiv(n * n * n * 3, 4) + 10 * n * n - 30;
        case 2: return FloorDiv(n * n * n * 3, 4) + 20 * n * n - 30;
        case 3: return FloorDiv(n * n * n * 6, 5) - 15 * n * n + 100 * n - 140;
        case 4: return FloorDiv(n * n * n * 4, 5);
    }
    return FloorDiv(n * n * n * 5, 4);
}

int LevelForExp(int dex, long long exp) {
    int lv = 1;
    while (lv < 100 && ExpForLevel(dex, lv + 1) <= exp) lv++;
    return lv;
}

int MaxPp(int move, int ppUp) {
    int base = move > 0 && move < gd::kMoveCount ? gd::MOVE_DATA[move].pp : 0;
    return base + (base / 5) * ppUp;
}

namespace {
struct MonCodec : Codec {
    MonCodec() { size = SIZE; }

    json decode(const uint8_t* d) const override {
        json out = json::object();
        std::optional<int> dex = IndexToDex(d[0]);
        if (dex && DexToIndex(*dex) == d[0]) out["species"] = gd::SPECIES[*dex];
        else out["species_index"] = d[0];
        out["level"] = d[3];
        out["hp"] = Be(d + 1, 2);
        out["status"] = d[4];
        out["type1"] = d[5];
        out["type2"] = d[6];
        out["catch_rate"] = d[7];
        json moves = json::array();
        for (int i = 0; i < 4; i++) {
            int mid = d[8 + i], pp = d[0x1D + i];
            if (mid == 0 && pp == 0) {
                moves.push_back(nullptr);
                continue;
            }
            json name = mid > 0 && mid <= 165 ? json(gd::MOVES[mid]) : json(mid);
            moves.push_back({ { "move", name }, { "pp", pp & 0x3F }, { "pp_up", pp >> 6 } });
        }
        out["moves"] = moves;
        out["ot_id"] = Be(d + 0xC, 2);
        out["exp"] = Be(d + 0xE, 3);
        json se = json::object();
        for (int i = 0; i < 5; i++) se[STAT_KEYS[i]] = Be(d + 0x11 + 2 * i, 2);
        out["stat_exp"] = se;
        unsigned dvw = Be(d + 0x1B, 2);
        out["dvs"] = { { "atk", dvw >> 12 }, { "def", (dvw >> 8) & 15 }, { "spd", (dvw >> 4) & 15 }, { "spc", dvw & 15 } };
        out["nickname"] = Name().decode(d + 0x21);
        out["ot_name"] = Name().decode(d + 0x2C);
        if (dex) {
            json info = json::object();
            info["dex"] = *dex;
            info["hp_dv"] = HpDv(out["dvs"]);
            info["stats"] = CalcStats(*dex, d[3], out["dvs"], out["stat_exp"]);
            info["types"] = json::array({ TypeJson(d[5]), TypeJson(d[6]) });
            info["level_from_exp"] = LevelForExp(*dex, out["exp"].get<long long>());
            out["_info"] = info;
        }
        return out;
    }

    Bytes encode(const json& v) const override {
        Bytes b(SIZE, 0);
        if (v.contains("species_index")) b[0] = (uint8_t)ByteValue(v["species_index"]);
        else b[0] = (uint8_t)DexToIndex(SpeciesLookup(v.at("species")));
        PutBe(&b[1], 2, PyInt(Get(v, "hp", 0)));
        b[3] = (uint8_t)ByteValue(Get(v, "level", 1));
        b[4] = (uint8_t)ByteValue(Get(v, "status", 0));
        b[5] = (uint8_t)ByteValue(Get(v, "type1", 0));
        b[6] = (uint8_t)ByteValue(Get(v, "type2", 0));
        b[7] = (uint8_t)ByteValue(Get(v, "catch_rate", 0));
        json moves = Get(v, "moves", json::array());
        for (int i = 0; i < 4 && i < (int)moves.size(); i++) {
            json m = moves[i];
            if (m.is_null()) continue;
            if (!m.is_object()) m = json{ { "move", m } };
            int mid = MoveLookup(m.at("move"));
            long long ppUp = PyInt(Get(m, "pp_up", 0));
            long long pp = m.contains("pp") ? PyInt(m["pp"]) : MaxPp(mid, (int)ppUp);
            b[8 + i] = (uint8_t)ByteValue(mid);
            b[0x1D + i] = (uint8_t)((pp & 0x3F) | ((ppUp & 3) << 6));
        }
        PutBe(&b[0xC], 2, PyInt(Get(v, "ot_id", 0)));
        PutBe(&b[0xE], 3, PyInt(Get(v, "exp", 0)));
        json se = Get(v, "stat_exp", json::object());
        for (int i = 0; i < 5; i++) PutBe(&b[0x11 + 2 * i], 2, PyInt(Get(se, STAT_KEYS[i], 0)));
        json dv = Get(v, "dvs", json::object());
        long long dvw = (PyInt(Get(dv, "atk", 0)) << 12) | (PyInt(Get(dv, "def", 0)) << 8) |
                        (PyInt(Get(dv, "spd", 0)) << 4) | PyInt(Get(dv, "spc", 0));
        PutBe(&b[0x1B], 2, dvw);
        Bytes nick = Name().encode(Get(v, "nickname", ""));
        Bytes ot = Name().encode(Get(v, "ot_name", ""));
        std::copy(nick.begin(), nick.end(), b.begin() + 0x21);
        std::copy(ot.begin(), ot.end(), b.begin() + 0x2C);
        return b;
    }
};
} // namespace

const Codec& Mon() {
    static MonCodec c;
    return c;
}

static CodecPtr MonPtr() {
    static CodecPtr p(&Mon(), [](const Codec*) {});
    return p;
}

json MakeMon(const json& species, int level, const json& movesIn, const json& nickname, const json& otName, int otId,
             const json& dvsIn, const json& statExpIn) {
    int dex = SpeciesLookup(species);
    if (dex <= 0 || dex > 151) throw Error("'NoneType' object is not subscriptable");
    const gd::Base& base = gd::BASE[dex];
    json dvs = Truthy(dvsIn) ? dvsIn : json{ { "atk", 15 }, { "def", 15 }, { "spd", 15 }, { "spc", 15 } };
    json statExp = statExpIn;
    if (!Truthy(statExp)) {
        statExp = json::object();
        for (const char* k : STAT_KEYS) statExp[k] = 0;
    }
    json moves = movesIn;
    if (moves.is_null()) {
        moves = json::array();
        for (uint8_t m : base.moves)
            if (m) moves.push_back(m);
    }
    json mlist = json::array();
    for (size_t i = 0; i < moves.size() && i < 4; i++) {
        int mid = MoveLookup(moves[i]);
        if (mid < 0 || mid >= gd::kMoveCount) throw Error("list index out of range");
        mlist.push_back({ { "move", gd::MOVES[mid] }, { "pp", MaxPp(mid) }, { "pp_up", 0 } });
    }
    json stats = CalcStats(dex, level, dvs, statExp);
    while (mlist.size() < 4) mlist.push_back(nullptr);
    return json{ { "species", gd::SPECIES[dex] }, { "level", level }, { "hp", stats["hp"] }, { "status", 0 },
                 { "type1", base.type1 }, { "type2", base.type2 }, { "catch_rate", base.catch_rate }, { "moves", mlist },
                 { "ot_id", otId }, { "exp", ExpForLevel(dex, level) }, { "stat_exp", statExp }, { "dvs", dvs },
                 { "nickname", nickname.is_null() ? json(gd::SPECIES[dex]) : nickname }, { "ot_name", otName } };
}

std::vector<std::string> Validate(const json& mon) {
    std::vector<std::string> errs;
    std::optional<int> dex;
    try {
        if (mon.contains("species")) dex = SpeciesLookup(mon["species"]);
        else dex = IndexToDex((int)PyInt(mon.at("species_index")));
    } catch (const Error& e) {
        return { e.what() };
    }
    if (!dex) return { "invalid species index" };
    long long level = PyInt(mon.at("level"));
    if (level < 1 || level > 100) errs.push_back("level " + std::to_string(level) + " outside 1..100");
    json stats = CalcStats(*dex, (int)level, mon.at("dvs"), mon.at("stat_exp"));
    if (PyInt(mon.at("hp")) > stats["hp"].get<long long>()) {
        errs.push_back("current HP " + std::to_string(PyInt(mon["hp"])) + " above max HP " +
                       std::to_string(stats["hp"].get<long long>()));
    }
    bool any = false;
    for (const json& m : mon.at("moves")) {
        if (!Truthy(m)) continue;
        any = true;
        int id = MoveLookup(m.at("move"));
        if (!(id > 0 && id <= 165)) errs.push_back("invalid move " + m["move"].dump());
    }
    if (!any) errs.push_back("no moves");
    return errs;
}

} // namespace pokemon

// ---- save.py ---------------------------------------------------------------------------------------------------------

const char* const TEAM_CATEGORIES[9] = { "free_battle", "category_1", "category_2", "poke_cup", "petit_cup",
                                         "pika_cup", "prime_cup", "gym_leader_castle", "mewtwo" };
const char* const CUP_DIVISIONS[4] = { "Poké Ball", "Great Ball", "Ultra Ball", "Master Ball" };
const char* const GLC_STAGES[10] = { "Pewter Gym", "Cerulean Gym", "Vermilion Gym", "Celadon Gym", "Fuchsia Gym",
                                     "Saffron Gym", "Cinnabar Gym", "Viridian Gym", "Elite Four", "Rival" };

const char* ModeName(int mode) {
    switch (mode) {
        case 0: return "Free Battle";
        case 1: return "Poké Cup (hidden copy 1)";
        case 2: return "Poké Cup (hidden copy 2)";
        case 3: return "Poké Cup";
        case 4: return "Petit Cup";
        case 5: return "Pika Cup";
        case 6: return "Prime Cup";
        case 7: return "Gym Leader Castle";
        case 8: return "Vs. Mewtwo";
        case 10: return "Event Battle";
    }
    return nullptr;
}

namespace {

const uint8_t MAGIC_VALID[4] = { 'P', 'O', 'K', 'E' };
const uint8_t MAGIC_PENDING[4] = { 'X', 'X', 'X', 'X' };
const size_t BANK_RECORD_END[4] = { 0x3700, 0x3700, 0x3E04, 0x3860 };

CodecPtr GbName() { return Text(11, true); }

std::vector<Field> HeaderFields() { // DeckRecordHeader
    return { { 0x00, "flags", U8() }, { 0x01, "name", GbName() }, { 0x0C, "trainer_id", U16() }, { 0x0E, "count", S16() } };
}

CodecPtr Photo() {
    CodecPtr vec = ArrayOf(F32(), 3);
    return StructOf(0x8C, {
        { 0x00, "y_position", F32() }, { 0x04, "subject", U8() }, { 0x05, "animation", U8() },
        { 0x06, "anim_frame", S16() }, { 0x08, "event_track", U8() }, { 0x09, "event_frame", U8() },
        { 0x0A, "slot_data", Raw(0x46) }, { 0x50, "camera_a", vec }, { 0x5C, "camera_b", vec },
        { 0x68, "word_68", U32() }, { 0x6C, "flags", U8() }, { 0x70, "ot_id", U16() }, { 0x72, "species", U8() },
        { 0x73, "nickname", Text(11, false) }, { 0x7E, "ot_name", Text(11, false) },
    }, true);
}

CodecPtr VictoryEntry() {
    return StructOf(0x1A, { { 0x00, "mode", U8() }, { 0x01, "level", U8() }, { 0x02, "ot_id", U16() },
                            { 0x04, "nickname", GbName() }, { 0x0F, "ot_name", GbName() } }, true);
}

CodecPtr ContinueData() {
    return StructOf(0x20, { { 0x00, "mode", U8() }, { 0x01, "mode_selection", U8() }, { 0x02, "battle", U8() },
                            { 0x03, "round2", U8() }, { 0x04, "continues", U8() }, { 0x05, "opponent_history", BytesOf(8) },
                            { 0x0D, "gb_starter_choice", U8() }, { 0x0E, "trainer_id", U16() },
                            { 0x10, "player_name", Text(16, true) } });
}

CodecPtr ModeProgress() {
    return StructOf(8, { { 0x00, "cleared_modes", U16() }, { 0x04, "gym_leader_castle", U8() },
                         { 0x05, "poke_cup", U8() }, { 0x06, "prime_cup", U8() }, { 0x07, "mewtwo_unlocked", U8() } });
}

CodecPtr Options() {
    return StructOf(0x22, { { 0x00, "flags", U8() }, { 0x02, "sound", U16() }, { 0x04, "gb_tower_speed", S16() },
                            { 0x08, "gb_tower_word", U32() }, { 0x0C, "progress", ArrayOf(ModeProgress(), 2) },
                            { 0x1C, "bonus_pokemon", U16() }, { 0x1E, "kids_club", U8() }, { 0x1F, "bonus_flag", U8() } });
}

struct ItemCodec : Codec {
    ItemCodec() { size = 2; }
    json decode(const uint8_t* d) const override {
        json out = { { "item", d[0] }, { "quantity", d[1] } };
        if (d[0] < gd::kItemCount && gd::ITEMS[d[0]][0]) out["_name"] = gd::ITEMS[d[0]];
        return out;
    }
    Bytes encode(const json& v) const override {
        const json& item = v.at("item");
        int id = -1;
        if (item.is_string()) {
            for (int i = 0; i < gd::kItemCount && id < 0; i++) {
                if (gd::ITEMS[i][0] && Upper(gd::ITEMS[i]) == Upper(item.get<std::string>())) id = i;
            }
            if (id < 0) throw Error("unknown item " + item.dump());
        } else {
            id = (int)PyInt(item);
        }
        return { (uint8_t)ByteValue(id), (uint8_t)ByteValue(v.at("quantity")) };
    }
};

// The body of a record (its size - 6 bytes)
struct Body {
    size_t size = 0;
    virtual ~Body() = default;
    virtual json decode(const uint8_t* d) const = 0;
    virtual Bytes encode(const json& v) const = 0;
};

struct ListBody : Body { // DeckRecordHeader + count entries of a codec + raw rest
    CodecPtr codec;
    size_t capacity, tailOff;
    StructCodec header{ 0x10, HeaderFields() };
    ListBody(size_t n, CodecPtr c, size_t cap) : codec(std::move(c)), capacity(cap) {
        size = n;
        tailOff = 0x10 + codec->size * cap;
    }
    json decode(const uint8_t* d) const override {
        json h = header.decode(d);
        long long count = h["count"].get<long long>();
        long long n = std::max(0LL, std::min(count, (long long)capacity));
        size_t es = codec->size;
        json out = h;
        out.erase("count");
        json entries = json::array();
        for (long long i = 0; i < n; i++) entries.push_back(codec->decode(d + 0x10 + i * es));
        out["entries"] = entries;
        if (n != count) out["count_raw"] = count;
        const uint8_t* un = d + 0x10 + n * es;
        size_t unLen = tailOff - (0x10 + n * es);
        if (std::any_of(un, un + unLen, [](uint8_t b) { return b != 0; })) out["_unused_entries"] = RawToJson(un, unLen);
        if (std::any_of(d + tailOff, d + size, [](uint8_t b) { return b != 0; })) {
            out["_tail"] = RawToJson(d + tailOff, size - tailOff);
        }
        return out;
    }
    Bytes encode(const json& v) const override {
        Bytes buf(size, 0);
        json entries = Get(v, "entries", json::array());
        if (entries.size() > capacity) throw Error("at most " + std::to_string(capacity) + " entries");
        json h = json::object();
        for (const char* k : { "flags", "name", "trainer_id" })
            if (v.contains(k)) h[k] = v[k];
        h["count"] = v.contains("count_raw") ? v["count_raw"] : json(entries.size());
        Bytes hb = header.encode(h);
        std::copy(hb.begin(), hb.end(), buf.begin());
        size_t es = codec->size;
        if (v.contains("_unused_entries")) {
            Bytes un = RawFromJson(v["_unused_entries"].get<std::string>());
            size_t start = 0x10 + entries.size() * es;
            for (size_t i = 0; i < un.size() && start + i < tailOff; i++) buf[start + i] = un[i]; // more entries: less room
        }
        for (size_t i = 0; i < entries.size(); i++) {
            Bytes e = codec->encode(entries[i]);
            std::copy(e.begin(), e.end(), buf.begin() + 0x10 + i * es);
        }
        if (v.contains("_tail")) {
            Bytes t = RawFromJson(v["_tail"].get<std::string>(), (long)(size - tailOff));
            std::copy(t.begin(), t.end(), buf.begin() + tailOff);
        }
        return buf;
    }
};

struct StructBody : Body {
    CodecPtr s;
    StructBody(size_t n, CodecPtr c) : s(std::move(c)) { size = n; }
    json decode(const uint8_t* d) const override { return s->decode(d); }
    Bytes encode(const json& v) const override { return s->encode(v); }
};

std::shared_ptr<Body> FlaggedArray(size_t n, CodecPtr entry, size_t count) {
    return std::make_shared<StructBody>(n, StructOf(n, { { 0x00, "flags", U8() }, { 0x02, "entries", ArrayOf(entry, count) } }));
}

} // namespace

struct RecordType {
    std::string key;
    size_t size;
    std::shared_ptr<Body> body;
    uint8_t resetFlags;
};

namespace {

const RecordType& TEAM() {
    static RecordType t{ "team", 0x160, std::make_shared<ListBody>(0x15A, pokemon::MonPtr(), 6), 0 };
    return t;
}
const RecordType& BOX() {
    static RecordType t{ "box", 0x468, std::make_shared<ListBody>(0x462, pokemon::MonPtr(), 20), 0 };
    return t;
}
const RecordType& ITEM_BOX() {
    static RecordType t{ "item_box", 0xE0, std::make_shared<ListBody>(0xDA, std::make_shared<ItemCodec>(), 100), 0 };
    return t;
}
const RecordType& VICTORY() {
    static RecordType t{ "victory_palace", 0xF60, FlaggedArray(0xF5A, VictoryEntry(), 151), 1 };
    return t;
}
const RecordType& CONTINUE() {
    static RecordType t{ "continue", 0x180,
                         std::make_shared<StructBody>(0x17A, StructOf(0x17A, {
                             { 0x00, "flags", U8() }, { 0x02, "session", ContinueData() }, { 0x22, "count", S16() },
                             { 0x24, "pokemon", ArrayOf(Raw(0x37), 6) }, { 0x16E, "battle_byte_52", BytesOf(6) },
                             { 0x174, "battle_byte_53", BytesOf(6) } })),
                         0 };
    return t;
}
const RecordType& OPTIONS_REC() {
    static RecordType t{ "options", 0x28, std::make_shared<StructBody>(0x22, Options()), 1 };
    return t;
}
const RecordType& ALBUM() {
    static RecordType t{ "album", 0x13B8, FlaggedArray(0x13B2, Photo(), 36), 1 };
    return t;
}
const RecordType& PHOTO_SET() {
    static RecordType t{ "photos", 0xB84, FlaggedArray(0xB7E, Photo(), 21), 1 };
    return t;
}

void WriteFooter(uint8_t* rec, size_t n, bool valid) {
    memcpy(rec + n - 6, valid ? MAGIC_VALID : MAGIC_PENDING, 4);
    uint16_t s = Checksum(rec, n);
    if (!valid) s ^= 0xFF;
    rec[n - 2] = (uint8_t)(s >> 8);
    rec[n - 1] = (uint8_t)s;
}

// Save_ClearTypedRecord followed by Save_CommitTypedRecord
Bytes ResetRecord(const RecordType& t) {
    Bytes rec(t.size, 0);
    if (&t == &TEAM() || &t == &BOX() || &t == &ITEM_BOX()) rec[1] = 0x50; // empty name (terminator only)
    rec[0] = t.resetFlags;
    WriteFooter(rec.data(), rec.size(), true);
    return rec;
}

} // namespace

size_t RecordRef::size() const {
    return type->size;
}

std::string RecordRef::key() const {
    std::string k;
    for (const json& p : path) {
        if (!k.empty()) k += "/";
        k += p.is_string() ? p.get<std::string>() : std::to_string(p.get<int>());
    }
    return k;
}

const std::vector<RecordRef>& Records() {
    static std::vector<RecordRef> recs = [] {
        std::vector<RecordRef> r;
        for (int c = 0; c < 9; c++) {
            for (int s = 0; s < 10; s++) {
                r.push_back({ json::array({ "teams", TEAM_CATEGORIES[c], s }), &TEAM(), c / 4, (c % 4) * 0xDC0 + s * 0x160 });
            }
        }
        r.push_back({ json::array({ "victory_palace" }), &VICTORY(), 2, 0x0DC0 });
        r.push_back({ json::array({ "continue" }), &CONTINUE(), 2, 0x1D20 });
        r.push_back({ json::array({ "options" }), &OPTIONS_REC(), 2, 0x1EA0 });
        r.push_back({ json::array({ "album" }), &ALBUM(), 2, 0x1EC8 });
        r.push_back({ json::array({ "photos" }), &PHOTO_SET(), 2, 0x3280 });
        for (int i = 0; i < 12; i++) r.push_back({ json::array({ "boxes", i }), &BOX(), 3, i * 0x468 });
        for (int i = 0; i < 4; i++) r.push_back({ json::array({ "item_boxes", i }), &ITEM_BOX(), 3, 0x34E0 + i * 0xE0 });
        return r;
    }();
    return recs;
}

uint16_t Checksum(const uint8_t* rec, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i + 2 < n; i++) s += rec[i];
    return (uint16_t)s;
}

std::string FooterState(const uint8_t* rec, size_t n) {
    uint16_t s = (uint16_t)(rec[n - 2] << 8 | rec[n - 1]);
    if (memcmp(rec + n - 6, MAGIC_VALID, 4) == 0 && s == Checksum(rec, n)) return "valid";
    if (memcmp(rec + n - 6, MAGIC_PENDING, 4) == 0 && s == (Checksum(rec, n) ^ 0xFF)) return "pending";
    return "invalid";
}

json& GetPath(json& root, const json& path) {
    json* node = &root;
    for (const json& p : path) node = p.is_string() ? &node->at(p.get<std::string>()) : &node->at(p.get<size_t>());
    return *node;
}

const json& GetPath(const json& root, const json& path) {
    const json* node = &root;
    for (const json& p : path) node = p.is_string() ? &node->at(p.get<std::string>()) : &node->at(p.get<size_t>());
    return *node;
}

Bytes NewSaveBytes() {
    Bytes data(FLASH_SIZE, 0xFF);
    for (const RecordRef& ref : Records()) {
        Bytes rec = ResetRecord(*ref.type);
        std::copy(rec.begin(), rec.end(), data.begin() + ref.primary());
        std::copy(rec.begin(), rec.end(), data.begin() + ref.backup());
    }
    return data;
}

SaveFile::SaveFile() : data(NewSaveBytes()) {}

SaveFile::SaveFile(Bytes bytes) : data(std::move(bytes)) {
    if (data.size() != FLASH_SIZE) {
        throw Error("a Pokémon Stadium FlashRAM image is " + std::to_string(FLASH_SIZE) + " bytes, got " +
                    std::to_string(data.size()));
    }
}

Bytes ReadFile(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) throw Error("cannot open " + path);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

SaveFile SaveFile::Load(const std::string& path) {
    return SaveFile(ReadFile(path));
}

Bytes SaveFile::RecordBytes(const RecordRef& ref, bool backup) const {
    size_t off = backup ? ref.backup() : ref.primary();
    return Bytes(data.begin() + off, data.begin() + off + ref.size());
}

std::string SaveFile::LoadedCopy(const RecordRef& ref) const {
    Bytes p = RecordBytes(ref), b = RecordBytes(ref, true);
    if (FooterState(p.data(), p.size()) == "valid") return "primary";
    return FooterState(b.data(), b.size()) == "valid" ? "backup" : "reset";
}

std::vector<CheckRow> SaveFile::Check() const {
    std::vector<CheckRow> out;
    for (const RecordRef& ref : Records()) {
        Bytes p = RecordBytes(ref), b = RecordBytes(ref, true);
        out.push_back({ ref.path, FooterState(p.data(), p.size()), FooterState(b.data(), b.size()), LoadedCopy(ref) });
    }
    return out;
}

void SaveFile::WriteRecord(const RecordRef& ref, const Bytes& rec) {
    std::copy(rec.begin(), rec.end(), data.begin() + ref.primary());
    std::copy(rec.begin(), rec.end(), data.begin() + ref.backup());
}

std::vector<json> SaveFile::Repair() {
    std::vector<json> changed;
    for (const RecordRef& ref : Records()) {
        std::string which = LoadedCopy(ref);
        Bytes good = which == "primary" ? RecordBytes(ref) : which == "backup" ? RecordBytes(ref, true) : ResetRecord(*ref.type);
        if (good != RecordBytes(ref) || good != RecordBytes(ref, true)) changed.push_back(ref.path);
        WriteRecord(ref, good);
    }
    return changed;
}

std::vector<std::pair<json, std::string>> SaveFile::FixChecksums(bool includePending) {
    std::vector<std::pair<json, std::string>> changed;
    for (const RecordRef& ref : Records()) {
        for (bool backup : { false, true }) {
            uint8_t* rec = data.data() + (backup ? ref.backup() : ref.primary());
            std::string st = FooterState(rec, ref.size());
            if (st == "valid" || (st == "pending" && !includePending)) continue;
            WriteFooter(rec, ref.size(), true);
            changed.push_back({ ref.path, backup ? "backup" : "primary" });
        }
    }
    return changed;
}

json SaveFile::DecodeRecord(const RecordRef& ref, bool backup) const {
    Bytes rec = RecordBytes(ref, backup);
    json body = ref.type->body->decode(rec.data());
    std::string st = FooterState(rec.data(), rec.size());
    body["_footer"] = st != "invalid" ? json(st) : json{ { "raw", Hex(rec.data() + rec.size() - 6, 6) } };
    if (ref.type == &CONTINUE()) {
        long long n = std::max(0LL, std::min(body["count"].get<long long>(), 6LL));
        for (long long i = 0; i < n; i++) {
            Bytes raw = RawFromJson(body["pokemon"][i].get<std::string>(), 0x37);
            body["pokemon"][i] = pokemon::Mon().decode(raw.data());
        }
    }
    return body;
}

Bytes SaveFile::EncodeRecord(const RecordRef& ref, const json& bodyIn) const {
    json body = bodyIn;
    if (ref.type == &CONTINUE() && body.contains("pokemon")) {
        for (json& m : body["pokemon"]) {
            if (m.is_object()) m = RawToJson(pokemon::Mon().encode(m));
        }
    }
    Bytes rec(ref.size(), 0);
    Bytes b = ref.type->body->encode(body);
    std::copy(b.begin(), b.end(), rec.begin());
    json foot = Get(body, "_footer", "valid");
    if (foot.is_object()) {
        Bytes raw = FromHex(foot.at("raw").get<std::string>());
        if (raw.size() != 6) throw Error("footer must be 6 bytes");
        std::copy(raw.begin(), raw.end(), rec.end() - 6);
    } else {
        WriteFooter(rec.data(), rec.size(), foot == json("valid"));
    }
    return rec;
}

void SaveFile::SetRecord(const RecordRef& ref, const json& bodyIn, bool commit) {
    json body = bodyIn;
    if (commit) {
        body["_footer"] = "valid";
        if (body.contains("flags")) body["flags"] = PyInt(body["flags"]) & ~2LL;
    }
    Bytes rec = EncodeRecord(ref, body);
    if (rec != RecordBytes(ref) || rec != RecordBytes(ref, true)) WriteRecord(ref, rec);
}

json SaveFile::ToDict() const {
    json out = { { "format", "pokestadium-us-flashram" }, { "version", 1 } };
    for (const RecordRef& ref : Records()) {
        json body = DecodeRecord(ref);
        Bytes back = RecordBytes(ref, true);
        if (back != RecordBytes(ref)) body["_backup"] = Hex(back);
        json* node = &out;
        for (size_t i = 0; i + 1 < ref.path.size(); i++) {
            const std::string k = ref.path[i].get<std::string>();
            if (!node->contains(k)) (*node)[k] = ref.path[i + 1].is_number() ? json::array() : json::object();
            node = &(*node)[k];
        }
        const json& key = ref.path.back();
        if (key.is_number()) node->push_back(body);
        else (*node)[key.get<std::string>()] = body;
    }
    json banks = json::array();
    for (int b = 0; b < 4; b++) {
        size_t end = BANK_RECORD_END[b];
        const uint8_t* prim = data.data() + b * BANK_STRIDE + end;
        const uint8_t* back = data.data() + BACKUP_BASE + b * BANK_STRIDE + end;
        size_t n = BANK_STRIDE - end;
        json e = { { "bank", b }, { "tail", RawToJson(prim, n) } };
        if (memcmp(prim, back, n) != 0) e["backup_tail"] = RawToJson(back, n);
        banks.push_back(e);
    }
    out["_banks"] = banks;
    return out;
}

SaveFile SaveFile::FromDict(const json& d) {
    SaveFile s(Bytes(FLASH_SIZE, 0xFF));
    const json& banks = d.at("_banks");
    for (size_t b = 0; b < banks.size() && b < 4; b++) {
        const json& e = banks[b];
        size_t end = BANK_RECORD_END[b], n = BANK_STRIDE - end;
        Bytes prim = RawFromJson(e.at("tail").get<std::string>(), (long)n);
        Bytes back = RawFromJson(Get(e, "backup_tail", e.at("tail")).get<std::string>(), (long)n);
        std::copy(prim.begin(), prim.end(), s.data.begin() + b * BANK_STRIDE + end);
        std::copy(back.begin(), back.end(), s.data.begin() + BACKUP_BASE + b * BANK_STRIDE + end);
    }
    for (const RecordRef& ref : Records()) {
        const json& body = GetPath(d, ref.path);
        Bytes rec = s.EncodeRecord(ref, body);
        std::copy(rec.begin(), rec.end(), s.data.begin() + ref.primary());
        Bytes back = body.contains("_backup") ? FromHex(body["_backup"].get<std::string>()) : rec;
        if (back.size() != ref.size()) throw Error("_backup of " + ref.key() + " has the wrong size");
        std::copy(back.begin(), back.end(), s.data.begin() + ref.backup());
    }
    return s;
}

const RecordRef& SaveFile::Record(const json& path) const {
    for (const RecordRef& ref : Records())
        if (ref.path == path) return ref;
    throw Error("no record " + path.dump());
}

SaveFile& SaveFile::Edit(const std::function<void(json&)>& fn) {
    json d = ToDict();
    const json before = d;
    fn(d);
    for (const RecordRef& ref : Records()) {
        const json& now = GetPath(d, ref.path);
        if (now != GetPath(before, ref.path)) {
            json n = now;
            n.erase("_backup");
            SetRecord(ref, n);
        }
    }
    return *this;
}

// ---- web.py ----------------------------------------------------------------------------------------------------------

bool IsValidRecord(const json& rec) {
    return rec.is_object() && rec.contains("_footer") && rec["_footer"] == json("valid");
}

Bytes EncodeEdit(const json& base, const json& editedIn) {
    SaveFile s = SaveFile::FromDict(base);
    json edited = editedIn;
    // a Pokémon leaving a Lab box: the game leaves the old bytes behind the new count (as its Lab PC writes them)
    const json& oldBoxes = base.at("boxes");
    json& newBoxes = edited.at("boxes");
    for (size_t i = 0; i < oldBoxes.size() && i < newBoxes.size(); i++) {
        const json& old = oldBoxes[i];
        json& now = newBoxes[i];
        if (Get(old, "_footer", nullptr) == json("valid") && now["entries"].size() < old["entries"].size() &&
            !now.contains("_unused_entries")) {
            Bytes rec = s.RecordBytes(s.Record(json::array({ "boxes", (int)i })));
            size_t a = 0x10 + now["entries"].size() * 0x37, b = 0x10 + 20 * 0x37;
            if (std::any_of(rec.begin() + a, rec.begin() + b, [](uint8_t x) { return x != 0; })) {
                now["_unused_entries"] = RawToJson(rec.data() + a, b - a);
            }
        }
    }
    s.Edit([&](json& cur) { cur = edited; });
    return s.data;
}

json MakeMonRequest(const json& p) {
    long long level = PyInt(p.at("level"));
    if (level < 1 || level > 100) throw Error("level must be 1..100");
    json dvs = json::object(), se = json::object();
    json dvIn = Truthy(Get(p, "dvs", nullptr)) ? p["dvs"] : json{ { "atk", 15 }, { "def", 15 }, { "spd", 15 }, { "spc", 15 } };
    for (auto& [k, v] : dvIn.items()) dvs[k] = PyInt(v);
    json seIn = Get(p, "stat_exp", nullptr);
    if (!Truthy(seIn)) {
        seIn = json::object();
        for (const char* k : pokemon::STAT_KEYS) seIn[k] = 0;
    }
    for (auto& [k, v] : seIn.items()) se[k] = PyInt(v);
    for (auto& [k, v] : dvs.items())
        if (v.get<long long>() < 0 || v.get<long long>() > 15) throw Error("DVs must be 0..15");
    for (auto& [k, v] : se.items())
        if (v.get<long long>() < 0 || v.get<long long>() > 65535) throw Error("stat experience must be 0..65535");
    json moves = Get(p, "moves", nullptr);
    if (!moves.is_null()) {
        json kept = json::array();
        for (const json& m : moves)
            if (Truthy(m) && Truthy(Get(m, "move", nullptr))) kept.push_back(m);
        moves = kept;
    }
    json moveIds = nullptr;
    if (!moves.is_null()) {
        moveIds = json::array();
        for (const json& x : moves) moveIds.push_back(x["move"]);
    }
    json nick = Get(p, "nickname", nullptr);
    json m = pokemon::MakeMon(p.at("species"), (int)level, moveIds, Truthy(nick) ? nick : json(nullptr),
                              Get(p, "ot_name", "STADIUM"), (int)PyInt(Get(p, "ot_id", 0)), dvs, se);
    if (!moves.is_null()) {
        for (size_t i = 0; i < moves.size(); i++) {
            const json& x = moves[i];
            if (i < 4 && !m["moves"][i].is_null() && Truthy(Get(x, "pp_up", nullptr))) {
                int up = (int)std::max(0LL, std::min(3LL, PyInt(x["pp_up"])));
                m["moves"][i]["pp_up"] = up;
                m["moves"][i]["pp"] = pokemon::MaxPp(pokemon::MoveLookup(x["move"]), up);
            }
        }
    }
    Bytes raw = pokemon::Mon().encode(m); // also checks the names fit the Game Boy charset
    json mon = pokemon::Mon().decode(raw.data());
    json warnings = json::array();
    for (const std::string& w : pokemon::Validate(mon)) warnings.push_back(w);
    return json{ { "mon", mon }, { "warnings", warnings } };
}

void WriteWithBackup(const std::string& path, const Bytes& data) {
    fs::path p = fs::u8path(path);
    std::error_code ec;
    if (fs::exists(p, ec)) {
        Bytes old = ReadFile(path);
        std::ofstream g(fs::u8path(path + ".bak"), std::ios::binary | std::ios::trunc);
        g.write((const char*)old.data(), (std::streamsize)old.size());
        if (!g) throw Error("cannot write " + path + ".bak");
    }
    fs::path tmp = fs::u8path(path + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write((const char*)data.data(), (std::streamsize)data.size());
        if (!f) throw Error("cannot write " + path + ".tmp");
    }
    fs::rename(tmp, p, ec); // replaces the old file (also on Windows: MoveFileEx with REPLACE_EXISTING)
    if (ec) {
        fs::remove(tmp, ec);
        throw Error("cannot replace " + path);
    }
}

// ---- gbsave.py -------------------------------------------------------------------------------------------------------

namespace gb {

const char* const BADGE_NAMES[8] = { "Boulder", "Cascade", "Thunder", "Rainbow", "Soul", "Marsh", "Volcano", "Earth" };

namespace {
constexpr size_t PLAYER_NAME = 0x2598, POKEDEX_OWNED = 0x25A3, POKEDEX_SEEN = 0x25B6, BAG = 0x25C9, MONEY = 0x25F3,
                 RIVAL_NAME = 0x25F6, BADGES = 0x2602, PLAYER_ID = 0x2605, PIKACHU_HAPPINESS = 0x271C,
                 PC_ITEMS = 0x27E6, CURRENT_BOX = 0x284C, PLAYTIME = 0x2CED, PARTY = 0x2F2C, CURRENT_BOX_DATA = 0x30C0,
                 MAIN_CHECKSUM = 0x3523, BOX_SIZE = 0x462;
constexpr size_t BANK_BOXES[2] = { 0x4000, 0x6000 };

uint8_t Sum8(const uint8_t* d, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; i++) s += d[i];
    return (uint8_t)(~s & 0xFF);
}

size_t BoxOffset(int i) {
    return BANK_BOXES[i / 6] + (i % 6) * BOX_SIZE;
}

long long Bcd(const uint8_t* b) {
    long long v = 0;
    for (int i = 0; i < 3; i++) {
        if ((b[i] >> 4) >= 10 || (b[i] & 15) >= 10) return 0;
        v = v * 100 + (b[i] >> 4) * 10 + (b[i] & 15);
    }
    return v;
}

void ToBcd(uint8_t* out, long long v) {
    v = std::max(0LL, std::min(999999LL, v));
    for (int i = 2; i >= 0; i--) {
        int two = (int)(v % 100);
        v /= 100;
        out[i] = (uint8_t)((two / 10) << 4 | (two % 10));
    }
}

const TextCodec& Name() {
    static TextCodec c(11, true);
    return c;
}

struct MonList { // party (capacity 6, 44-byte structures) or PC box (capacity 20, 33-byte structures)
    size_t capacity, monSize, mons, ots, nicks, size;
    MonList(size_t cap, size_t ms) : capacity(cap), monSize(ms) {
        mons = 2 + cap;
        ots = mons + cap * ms;
        nicks = ots + cap * 11;
        size = nicks + cap * 11;
    }
    json decode(const uint8_t* d) const {
        size_t n = d[0] <= capacity ? d[0] : 0;
        json out = json::array();
        for (size_t i = 0; i < n; i++) {
            Bytes raw(d + mons + i * monSize, d + mons + i * monSize + 33);
            raw.insert(raw.end(), d + nicks + i * 11, d + nicks + (i + 1) * 11);
            raw.insert(raw.end(), d + ots + i * 11, d + ots + (i + 1) * 11);
            out.push_back(pokemon::Mon().decode(raw.data()));
        }
        return out;
    }
    Bytes encode(const uint8_t* old, const json& list) const {
        if (list.size() > capacity) throw Error("at most " + std::to_string(capacity) + " Pokémon");
        Bytes d(old, old + size);
        size_t count = list.size();
        d[0] = (uint8_t)count;
        for (size_t i = 0; i < capacity + 1; i++) d[1 + i] = i == count ? 0xFF : (i > count ? d[1 + i] : 0);
        for (size_t i = 0; i < count; i++) {
            const json& m = list[i];
            Bytes raw = pokemon::Mon().encode(m);
            d[1 + i] = raw[0];
            Bytes core(raw.begin(), raw.begin() + 33);
            if (monSize == 44) { // party: level and stats
                std::optional<int> dex = pokemon::IndexToDex(raw[0]);
                if (!dex) throw Error("'NoneType' object is not subscriptable");
                json st = pokemon::CalcStats(*dex, (int)PyInt(m.at("level")), m.at("dvs"), m.at("stat_exp"));
                core.push_back((uint8_t)ByteValue(m["level"]));
                for (const char* k : pokemon::STAT_KEYS) {
                    long long v = st[k].get<long long>();
                    core.push_back((uint8_t)(v >> 8));
                    core.push_back((uint8_t)v);
                }
            }
            std::copy(core.begin(), core.end(), d.begin() + mons + i * monSize);
            const std::pair<size_t, size_t> names[2] = { { ots + i * 11, 0x2C }, { nicks + i * 11, 0x21 } };
            for (auto [off, src] : names) { // like the game: up to the terminator, the bytes after it stay
                size_t n = 11;
                for (size_t k = 0; k < 11; k++)
                    if (raw[src + k] == 0x50) {
                        n = k + 1;
                        break;
                    }
                std::copy(raw.begin() + src, raw.begin() + src + n, d.begin() + off);
            }
        }
        return d;
    }
};

const MonList& PartyList() {
    static MonList l(6, 44);
    return l;
}
const MonList& BoxList() {
    static MonList l(20, 33);
    return l;
}

json Items(const uint8_t* d, size_t cap) {
    size_t n = d[0] <= cap ? d[0] : 0;
    json out = json::array();
    for (size_t i = 0; i < n; i++) out.push_back({ { "item", d[1 + 2 * i] }, { "quantity", d[2 + 2 * i] } });
    return out;
}

void ItemsEncode(uint8_t* d, const json& items, size_t cap) {
    if (items.size() > cap) throw Error("there is room for " + std::to_string(cap) + " items at most");
    d[0] = (uint8_t)items.size();
    for (size_t i = 0; i < items.size(); i++) {
        d[1 + 2 * i] = (uint8_t)ByteValue(items[i].at("item"));
        d[2 + 2 * i] = (uint8_t)std::max(1LL, std::min(99LL, PyInt(items[i].at("quantity"))));
    }
    d[1 + 2 * items.size()] = 0xFF;
}

json Flags(const uint8_t* d) {
    json out = json::array();
    for (int dex = 1; dex < 152; dex++)
        if (d[(dex - 1) >> 3] & (1 << ((dex - 1) & 7))) out.push_back(dex);
    return out;
}

void FlagsEncode(uint8_t* d, const std::set<int>& dexes) {
    memset(d, 0, 19);
    for (int dex : dexes) d[(dex - 1) >> 3] |= (uint8_t)(1 << ((dex - 1) & 7));
}

std::set<int> DexSet(const json& list) {
    std::set<int> s;
    for (const json& x : list) s.insert((int)PyInt(x));
    return s;
}

std::vector<long long> Sorted(const json& list) {
    std::vector<long long> v;
    for (const json& x : list) v.push_back(PyInt(x));
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

GBSave::GBSave(Bytes bytes) : data(std::move(bytes)) {
    if (data.size() != SIZE) {
        throw Error("a Game Boy Pokémon save is " + std::to_string(SIZE) + " bytes, got " + std::to_string(data.size()));
    }
}

bool GBSave::MainValid() const {
    return Sum8(&data[PLAYER_NAME], MAIN_CHECKSUM - PLAYER_NAME) == data[MAIN_CHECKSUM];
}

bool GBSave::BoxesInitialized() const {
    return (data[CURRENT_BOX] & 0x80) != 0;
}

int GBSave::CurrentBox() const {
    return (data[CURRENT_BOX] & 0x7F) < 12 ? data[CURRENT_BOX] & 0x7F : 0;
}

void GBSave::FixChecksums(const std::vector<int>& banks) {
    data[MAIN_CHECKSUM] = Sum8(&data[PLAYER_NAME], MAIN_CHECKSUM - PLAYER_NAME);
    for (int b : banks) {
        size_t base = BANK_BOXES[b];
        for (int i = 0; i < 6; i++) data[base + 0x1A4D + i] = Sum8(&data[base + i * BOX_SIZE], BOX_SIZE);
        data[base + 0x1A4C] = Sum8(&data[base], 0x1A4C);
    }
}

Bytes GBSave::BoxBytes(int i) const {
    if (i == CurrentBox()) return Bytes(data.begin() + CURRENT_BOX_DATA, data.begin() + CURRENT_BOX_DATA + BOX_SIZE);
    if (!BoxesInitialized()) {
        Bytes b(BOX_SIZE, 0);
        b[1] = 0xFF;
        return b;
    }
    return Bytes(data.begin() + BoxOffset(i), data.begin() + BoxOffset(i) + BOX_SIZE);
}

json GBSave::ToDict() const {
    const uint8_t* d = data.data();
    const uint8_t* pt = d + PLAYTIME;
    json boxes = json::array();
    for (int i = 0; i < 12; i++) boxes.push_back(BoxList().decode(BoxBytes(i).data()));
    return json{
        { "format", "pokemon-gen1-sav" }, { "valid", MainValid() },
        { "trainer", { { "name", Name().decode(d + PLAYER_NAME) }, { "rival", Name().decode(d + RIVAL_NAME) },
                       { "id", Be(d + PLAYER_ID, 2) }, { "money", Bcd(d + MONEY) }, { "badges", d[BADGES] },
                       { "pikachu_happiness", d[PIKACHU_HAPPINESS] },
                       { "playtime", { { "hours", pt[0] }, { "minutes", pt[2] }, { "seconds", pt[3] } } } } },
        { "pokedex", { { "owned", Flags(d + POKEDEX_OWNED) }, { "seen", Flags(d + POKEDEX_SEEN) } } },
        { "bag", Items(d + BAG, 20) },
        { "pc_items", Items(d + PC_ITEMS, 50) },
        { "party", PartyList().decode(d + PARTY) },
        { "current_box", CurrentBox() },
        { "boxes", boxes },
    };
}

void GBSave::WriteBox(int i, const json& mons) {
    if (!BoxesInitialized()) { // what the game does on the first box switch: empty all bank boxes
        for (int j = 0; j < 12; j++) {
            size_t o = BoxOffset(j);
            std::fill(data.begin() + o, data.begin() + o + BOX_SIZE, 0);
            data[o + 1] = 0xFF;
        }
        data[CURRENT_BOX] |= 0x80;
        mBanks.push_back(0);
        mBanks.push_back(1);
    }
    mBanks.push_back(i / 6);
    Bytes raw = BoxList().encode(BoxBytes(i).data(), mons);
    if (i == CurrentBox()) std::copy(raw.begin(), raw.end(), data.begin() + CURRENT_BOX_DATA);
    std::copy(raw.begin(), raw.end(), data.begin() + BoxOffset(i));
}

GBSave& GBSave::Apply(const json& base, const json& edited) {
    uint8_t* d = data.data();
    mBanks.clear();
    const json &bt = base.at("trainer"), &et = edited.at("trainer");
    for (auto [key, off] : { std::pair<const char*, size_t>{ "name", PLAYER_NAME }, { "rival", RIVAL_NAME } }) {
        if (et.at(key) != bt.at(key)) {
            Bytes b = Name().encode(et[key]);
            std::copy(b.begin(), b.end(), d + off);
        }
    }
    if (et.at("id") != bt.at("id")) PutBe(d + PLAYER_ID, 2, PyInt(et["id"]));
    if (et.at("money") != bt.at("money")) ToBcd(d + MONEY, PyInt(et["money"]));
    if (et.at("badges") != bt.at("badges")) d[BADGES] = (uint8_t)(PyInt(et["badges"]) & 0xFF);
    if (Get(et, "pikachu_happiness", nullptr) != Get(bt, "pikachu_happiness", nullptr)) {
        d[PIKACHU_HAPPINESS] = (uint8_t)std::max(0LL, std::min(255LL, PyInt(et["pikachu_happiness"])));
    }
    for (auto [key, off] : { std::pair<const char*, size_t>{ "owned", POKEDEX_OWNED }, { "seen", POKEDEX_SEEN } }) {
        if (Sorted(edited.at("pokedex").at(key)) != Sorted(base.at("pokedex").at(key))) {
            FlagsEncode(d + off, DexSet(edited["pokedex"][key]));
        }
    }
    if (edited.at("bag") != base.at("bag")) ItemsEncode(d + BAG, edited["bag"], 20);
    if (edited.at("pc_items") != base.at("pc_items")) ItemsEncode(d + PC_ITEMS, edited["pc_items"], 50);
    if (edited.at("party") != base.at("party")) {
        if (edited["party"].empty()) throw Error("the party needs at least one Pokémon");
        Bytes p = PartyList().encode(d + PARTY, edited["party"]);
        std::copy(p.begin(), p.end(), d + PARTY);
    }
    for (int i = 0; i < 12; i++) {
        if (edited.at("boxes")[i] != base.at("boxes")[i]) WriteBox(i, edited["boxes"][i]);
    }
    // a Pokémon that arrives counts as seen and caught, as after a trade
    auto dexes = [](const json& dump) {
        std::set<int> s;
        auto add = [&](const json& list) {
            for (const json& m : list)
                if (m.contains("species")) s.insert(pokemon::SpeciesLookup(m["species"]));
        };
        add(dump.at("party"));
        for (const json& b : dump.at("boxes")) add(b);
        return s;
    };
    std::set<int> now = dexes(edited), before = dexes(base), arrived;
    std::set_difference(now.begin(), now.end(), before.begin(), before.end(), std::inserter(arrived, arrived.end()));
    if (!arrived.empty()) {
        for (size_t off : { POKEDEX_OWNED, POKEDEX_SEEN }) {
            std::set<int> cur = DexSet(Flags(d + off));
            cur.insert(arrived.begin(), arrived.end());
            FlagsEncode(d + off, cur);
        }
    }
    std::set<int> banks(mBanks.begin(), mBanks.end());
    FixChecksums(std::vector<int>(banks.begin(), banks.end()));
    return *this;
}

Bytes ApplyEdit(const Bytes& data, const json& base, const json& edited) {
    GBSave s(data);
    s.Apply(base, edited);
    return s.data;
}

json GameInfo(const std::string& romPath, const std::string& savePath) {
    std::string title = "Pokémon (Game Boy)", kind;
    std::string both = romPath + savePath;
    bool german = false;
    for (const char* s : { "Germany", "(G)", "_de.", "_de/" })
        if (both.find(s) != std::string::npos) german = true;
    if (!romPath.empty()) {
        std::ifstream f(fs::u8path(romPath), std::ios::binary);
        char head[0x150] = {};
        if (f.read(head, sizeof(head)) || f.gcount() > 0x134) {
            std::string t;
            for (int i = 0x134; i < 0x144; i++) {
                char c = head[i];
                t += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
            }
            struct G { const char *key, *k, *en, *de; };
            for (const G& g : { G{ "YEL", "yellow", "Pokémon Yellow", "Pokémon Gelb" }, G{ "RED", "red", "Pokémon Red", "Pokémon Rot" },
                                G{ "BLUE", "blue", "Pokémon Blue", "Pokémon Blau" }, G{ "GREEN", "green", "Pokémon Green", "Pokémon Grün" } }) {
                if (t.find(g.key) != std::string::npos) {
                    title = german ? g.de : g.en;
                    kind = g.k;
                    break;
                }
            }
        }
    }
    return json{ { "title", title }, { "kind", kind }, { "lang", german ? "de" : "en" } };
}

} // namespace gb

} // namespace stadium_save
