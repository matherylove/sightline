#pragma once
// FfmpegBootstrap.h
// Utilidad para verificar si ffmpeg.exe esta disponible cuando se necesita.
// NO se llama en el arranque; solo se invoca al intentar descargar un video.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

namespace FfmpegBootstrap {

inline std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.rfind(L'\\');
    return (pos != std::wstring::npos) ? p.substr(0, pos) : p;
}

// Busca ffmpeg.exe en: 1) directorio del exe, 2) PATH del sistema
// Retorna true y rellena outPath si lo encuentra.
inline bool FindFfmpeg(std::wstring& outPath) {
    // 1. Junto al ejecutable
    std::wstring dir = GetExeDir();
    std::wstring local = dir + L"\\ffmpeg.exe";
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
        outPath = local;
        return true;
    }
    // 2. En PATH
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(NULL, L"ffmpeg.exe", NULL, MAX_PATH, found, NULL)) {
        outPath = found;
        return true;
    }
    outPath.clear();
    return false;
}

// Muestra un MessageBox de error con instrucciones si ffmpeg no esta.
// Llama esto desde el boton de descarga antes de intentar nada.
inline bool RequireFfmpeg(HWND hParent) {
    std::wstring path;
    if (FindFfmpeg(path)) return true;
    MessageBoxW(hParent,
        L"No se encontro ffmpeg.exe.\n\n"
        L"Para habilitar descargas, coloca ffmpeg.exe en el mismo directorio\n"
        L"que ClientTube.exe, o instalalo y agrega su ruta al PATH del sistema.\n\n"
        L"Descarga: https://github.com/BtbN/FFmpeg-Builds/releases",
        L"ClientTube - FFmpeg no encontrado",
        MB_ICONWARNING | MB_OK);
    return false;
}

} // namespace FfmpegBootstrap
