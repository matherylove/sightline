#include "Config.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

std::wstring Config::GetIniPath() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);
    std::wstring ini = exePath;
    ini += L"\\clienttube.ini";
    return ini;
}

std::wstring Config::GetString(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(section, key, def, buf, MAX_PATH, GetIniPath().c_str());
    return buf;
}

void Config::SetString(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    WritePrivateProfileStringW(section, key, value, GetIniPath().c_str());
}

std::wstring Config::GetPlayerPath() {
    return GetString(L"Player", L"Path");
}

void Config::SetPlayerPath(const std::wstring& path) {
    SetString(L"Player", L"Path", path.c_str());
}

void Config::ClearPlayerPath() {
    SetString(L"Player", L"Path", L"");
}
