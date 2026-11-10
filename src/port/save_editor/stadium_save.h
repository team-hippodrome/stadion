// Pokémon Stadium (US) save library: the C++ port of tools/save_editor/stadium_save (save.py, schema.py, pokemon.py,
// text.py, gbsave.py), used by the save editor built into the launcher (save_editor_ui.cpp).
//
// It keeps the Python library's model on purpose: a save decodes to the same JSON document (nlohmann::json here,
// dicts there), every byte no field covers is kept raw, and building a save from that document gives the same bytes.
// Edits are made on the document and written back record by record, committed the way the game commits them.
// tools/save_editor/tests/test_native.py checks this library against the Python one (stadium_save_cli, below).
// Format reference: tools/save_editor/SAVE_FORMAT.md.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace stadium_save {

using json = nlohmann::json;
using Bytes = std::vector<uint8_t>;

// What the Python library raises as ValueError (and the few other errors an edit can hit)
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ---- helpers ----
std::string Hex(const uint8_t* d, size_t n);
inline std::string Hex(const Bytes& b) { return Hex(b.data(), b.size()); }
Bytes FromHex(const std::string& s);
std::string RawToJson(const uint8_t* d, size_t n);           // 'fill:XX*N' when uniform, else hex
inline std::string RawToJson(const Bytes& b) { return RawToJson(b.data(), b.size()); }
Bytes RawFromJson(const std::string& v, long size = -1);
long long PyInt(const json& v);                              // Python int(v): numbers, bools, decimal strings
std::string Upper(const std::string& utf8);                  // str.upper() for ASCII and Latin-1 letters
std::string TextOf(const json& v);                           // a name field: "TEXT" or {"text": ..., "raw": ...}

// ---- text.py ----
std::string DecodeGb(const uint8_t* d, size_t n);
Bytes EncodeGb(const std::string& text, size_t size, uint8_t pad = 0);
std::string DecodeN64(const uint8_t* d, size_t n);
Bytes EncodeN64(const std::string& text, size_t size);
bool GbEncodable(const std::string& text, size_t size, std::string* error = nullptr);

// ---- schema.py ----
struct Codec {
    size_t size = 0;
    virtual ~Codec() = default;
    virtual json decode(const uint8_t* d) const = 0;
    virtual Bytes encode(const json& v) const = 0;
};

// ---- pokemon.py ----
namespace pokemon {
constexpr size_t SIZE = 0x37;
extern const char* const STAT_KEYS[5];
const Codec& Mon();
int SpeciesLookup(const json& v);
int MoveLookup(const json& v);
std::optional<int> IndexToDex(int index);
int DexToIndex(int dex);
int HpDv(const json& dvs);
json CalcStats(int dex, int level, const json& dvs, const json& statExp);
long long ExpForLevel(int dex, int level);
int LevelForExp(int dex, long long exp);
int MaxPp(int move, int ppUp = 0);
const char* SpeciesName(int dex);                            // nullptr outside 1..151
const char* TypeName(int type);                              // nullptr = none
json MakeMon(const json& species, int level, const json& moves = nullptr, const json& nickname = nullptr,
             const json& otName = "STADIUM", int otId = 0, const json& dvs = nullptr, const json& statExp = nullptr);
std::vector<std::string> Validate(const json& mon);
} // namespace pokemon

// ---- save.py ----
constexpr size_t FLASH_SIZE = 0x20000;
constexpr size_t BANK_STRIDE = 0x4000;
constexpr size_t BACKUP_BASE = 0x10000;
extern const char* const TEAM_CATEGORIES[9];
extern const char* const CUP_DIVISIONS[4];
extern const char* const GLC_STAGES[10];
const char* ModeName(int mode);                              // save.MODES; nullptr when unknown

struct RecordType;
struct RecordRef {
    json path;                // ["teams", "poke_cup", 0], ["options"], ["boxes", 3] ...
    const RecordType* type;
    int bank, offset;
    size_t size() const;
    size_t primary() const { return bank * BANK_STRIDE + offset; }
    size_t backup() const { return BACKUP_BASE + bank * BANK_STRIDE + offset; }
    std::string key() const;  // "teams/poke_cup/0"
};
const std::vector<RecordRef>& Records();

uint16_t Checksum(const uint8_t* rec, size_t n);
std::string FooterState(const uint8_t* rec, size_t n);       // "valid", "pending" or "invalid"
json& GetPath(json& root, const json& path);
const json& GetPath(const json& root, const json& path);

struct CheckRow {
    json path;
    std::string primary, backup, loaded;
};

class SaveFile {
  public:
    Bytes data;

    SaveFile();                       // a new save, as the game writes it for a blank chip
    explicit SaveFile(Bytes bytes);   // throws Error unless 128 KiB
    static SaveFile Load(const std::string& path);

    Bytes RecordBytes(const RecordRef& ref, bool backup = false) const;
    std::string LoadedCopy(const RecordRef& ref) const;      // "primary", "backup" or "reset"
    std::vector<CheckRow> Check() const;
    std::vector<json> Repair();                              // Save_LoadWithRepair on every record; changed paths
    std::vector<std::pair<json, std::string>> FixChecksums(bool includePending = false);

    json DecodeRecord(const RecordRef& ref, bool backup = false) const;
    Bytes EncodeRecord(const RecordRef& ref, const json& body) const;
    void SetRecord(const RecordRef& ref, const json& body, bool commit = true);
    json ToDict() const;
    static SaveFile FromDict(const json& d);
    const RecordRef& Record(const json& path) const;
    SaveFile& Edit(const std::function<void(json&)>& fn);

  private:
    void WriteRecord(const RecordRef& ref, const Bytes& rec);
};

Bytes NewSaveBytes();

// web.py: the editor's operations
Bytes EncodeEdit(const json& base, const json& edited);     // _encode: base dump + edited dump -> save bytes
json MakeMonRequest(const json& request);                    // _make_mon: {"mon", "warnings"}
bool IsValidRecord(const json& rec);                         // editor.html valid(): _footer == "valid"
void WriteWithBackup(const std::string& path, const Bytes& data); // previous contents kept as path + ".bak"
Bytes ReadFile(const std::string& path);

// ---- gbsave.py: Pokémon Red / Blue / Yellow saves (32 KiB, international layout) ----
namespace gb {
constexpr size_t SIZE = 0x8000;
extern const char* const BADGE_NAMES[8];

class GBSave {
  public:
    Bytes data;
    explicit GBSave(Bytes bytes);     // throws Error unless 32 KiB
    bool MainValid() const;
    bool BoxesInitialized() const;
    int CurrentBox() const;
    void FixChecksums(const std::vector<int>& banks = { 0, 1 });
    Bytes BoxBytes(int i) const;
    json ToDict() const;
    GBSave& Apply(const json& base, const json& edited);

  private:
    void WriteBox(int i, const json& mons);
    std::vector<int> mBanks;
};

Bytes ApplyEdit(const Bytes& data, const json& base, const json& edited);
json GameInfo(const std::string& romPath, const std::string& savePath);   // {"title", "kind", "lang"}
} // namespace gb

} // namespace stadium_save
