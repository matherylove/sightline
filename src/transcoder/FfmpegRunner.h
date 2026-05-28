#pragma once
// FfmpegRunner.h
// Builds and executes an ffmpeg command line, waits for completion,
// and returns whether it succeeded.
// ffmpeg.exe must be placed next to ClientTube.exe (extracted by FfmpegBootstrap).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace FfmpegRunner {

inline std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.rfind(L'\\');
    return (pos != std::wstring::npos) ? p.substr(0, pos) : p;
}

struct RunResult {
    bool   ok;
    DWORD  exitCode;
    std::string error;
};

// args: vector of argument strings (no need to quote paths here; we pass via cmdline).
inline RunResult Run(const std::vector<std::wstring>& args) {
    RunResult r = {false, 1, ""};

    std::wstring ffmpegPath = GetExeDir() + L"\\ffmpeg.exe";
    if (GetFileAttributesW(ffmpegPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        r.error = "ffmpeg.exe not found next to ClientTube.exe";
        return r;
    }

    // Build quoted command line
    std::wstring cmdLine = L"\"" + ffmpegPath + L"\"";
    for (const auto& a : args) {
        cmdLine += L" ";
        // Quote arguments that contain spaces
        bool needsQuote = (a.find(L' ') != std::wstring::npos);
        if (needsQuote) cmdLine += L"\"";
        cmdLine += a;
        if (needsQuote) cmdLine += L"\"";
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);

    if (!CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        r.error = "CreateProcess failed for ffmpeg.exe";
        return r;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &r.exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    r.ok = (r.exitCode == 0);
    return r;
}

} // namespace FfmpegRunner
