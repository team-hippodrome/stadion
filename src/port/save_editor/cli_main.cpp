// stadium_save_cli: a command-line driver for the C++ save library, so tools/save_editor/tests/test_native.py can
// compare it with the Python library (cmake --build <dir> --target stadium_save_cli). Not shipped.
//
//   dump SAVE                    the save as JSON (SaveFile.to_dict)
//   build JSON OUT               a save from a dump (SaveFile.from_dict)
//   roundtrip SAVE               "same" when from_dict(to_dict(save)) gives the same bytes
//   check SAVE                   [[path, primary, backup, loaded], ...]
//   repair SAVE OUT              Save_LoadWithRepair on every record; prints the changed paths
//   fixsum SAVE OUT [pending]    rewrite broken footers
//   new OUT                      the save the game writes for a blank chip
//   edit SAVE EDITED_JSON OUT    the editor's save: the dump of SAVE as base, EDITED_JSON as the edit
//   mon REQUEST_JSON             a new Pokémon as the editor's dialog makes it ({"mon", "warnings"})
//   gbdump SAV                   a Game Boy save as JSON
//   gbedit SAV EDITED_JSON OUT   a Game Boy save after an edit (base = gbdump of SAV)
//   text gb|n64 SIZE TEXT        encode a name; prints hex, then the decoded text
#include "stadium_save.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace stadium_save;

static json ReadJson(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return json::parse(ss.str());
}

static void WriteBytes(const std::string& path, const Bytes& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char*)data.data(), (std::streamsize)data.size());
    if (!f) throw Error("cannot write " + path);
}

static void Print(const json& j) {
    std::cout << j.dump(-1, ' ', false, json::error_handler_t::replace) << "\n";
}

// the arguments as UTF-8 (Windows hands main() its arguments in the ANSI code page, which loses "♂")
static std::vector<std::string> Arguments(int argc, char** argv) {
#ifdef _WIN32
    int n = 0;
    std::vector<std::string> out;
    if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 1; i < n; i++) {
            int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? len - 1 : 0, '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
            out.push_back(s);
        }
        LocalFree(w);
        return out;
    }
#endif
    return std::vector<std::string>(argv + 1, argv + argc);
}

int main(int argc, char** argv) {
    std::vector<std::string> a = Arguments(argc, argv);
    auto need = [&](size_t n) {
        if (a.size() < n) throw Error("missing arguments (see the comment at the top of cli_main.cpp)");
    };
    try {
        need(1);
        const std::string& cmd = a[0];
        if (cmd == "dump") {
            need(2);
            Print(SaveFile::Load(a[1]).ToDict());
        } else if (cmd == "build") {
            need(3);
            WriteBytes(a[2], SaveFile::FromDict(ReadJson(a[1])).data);
        } else if (cmd == "roundtrip") {
            need(2);
            SaveFile s = SaveFile::Load(a[1]);
            std::cout << (SaveFile::FromDict(s.ToDict()).data == s.data ? "same" : "different") << "\n";
        } else if (cmd == "check") {
            need(2);
            json out = json::array();
            for (const CheckRow& r : SaveFile::Load(a[1]).Check()) out.push_back({ r.path, r.primary, r.backup, r.loaded });
            Print(out);
        } else if (cmd == "repair") {
            need(3);
            SaveFile s = SaveFile::Load(a[1]);
            json changed = s.Repair();
            WriteBytes(a[2], s.data);
            Print(changed);
        } else if (cmd == "fixsum") {
            need(3);
            SaveFile s = SaveFile::Load(a[1]);
            json out = json::array();
            for (auto& [path, copy] : s.FixChecksums(a.size() > 3 && a[3] == "pending")) out.push_back({ path, copy });
            WriteBytes(a[2], s.data);
            Print(out);
        } else if (cmd == "new") {
            need(2);
            WriteBytes(a[1], NewSaveBytes());
        } else if (cmd == "edit") {
            need(4);
            WriteBytes(a[3], EncodeEdit(SaveFile::Load(a[1]).ToDict(), ReadJson(a[2])));
        } else if (cmd == "mon") {
            need(2);
            Print(MakeMonRequest(ReadJson(a[1])));
        } else if (cmd == "gbdump") {
            need(2);
            Print(gb::GBSave(ReadFile(a[1])).ToDict());
        } else if (cmd == "gbedit") {
            need(4);
            Bytes data = ReadFile(a[1]);
            WriteBytes(a[3], gb::ApplyEdit(data, gb::GBSave(data).ToDict(), ReadJson(a[2])));
        } else if (cmd == "text") {
            need(4);
            size_t size = std::stoul(a[2]);
            Bytes b = a[1] == "gb" ? EncodeGb(a[3], size) : EncodeN64(a[3], size);
            std::cout << Hex(b) << "\n" << (a[1] == "gb" ? DecodeGb(b.data(), b.size()) : DecodeN64(b.data(), b.size())) << "\n";
        } else {
            throw Error("unknown command " + cmd);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
    return 0;
}
