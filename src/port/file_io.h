#pragma once

#include <cstdio>
#ifdef _WIN32
#include <filesystem>
#include <string>
#endif

namespace Port {

// SDL file dialogs and stored launcher paths use UTF-8. Windows fopen uses the
// process code page instead; use the wide API so every ROM read sees the same file.
inline FILE* OpenFileUtf8(const char* path, const char* mode) {
#ifdef _WIN32
    const auto nativePath = std::filesystem::u8path(path);
    const std::wstring nativeMode(mode, mode + std::char_traits<char>::length(mode));
    return _wfopen(nativePath.c_str(), nativeMode.c_str());
#else
    return fopen(path, mode);
#endif
}

} // namespace Port
