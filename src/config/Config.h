#pragma once
#include <windows.h>
#include <string>

namespace Config {
    // Player path
    std::wstring GetPlayerPath();
    void         SetPlayerPath(const std::wstring& path);
    void         ClearPlayerPath();

    // Generic helpers
    std::wstring GetString(const wchar_t* section, const wchar_t* key, const wchar_t* def = L"");
    void         SetString(const wchar_t* section, const wchar_t* key, const wchar_t* value);

    // Returns path to wintube.ini next to the .exe
    std::wstring GetIniPath();
}
