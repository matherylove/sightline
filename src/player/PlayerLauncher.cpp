#include "PlayerLauncher.h"
#include "../config/Config.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <cstdio>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")

static HWND   s_hMPCWnd   = NULL;
static HANDLE s_hProcess  = NULL;

// Find MPC-HC window after launching (wait up to 5s)
static HWND WaitForMPCWindow(DWORD pid, int timeoutMs = 5000) {
    struct Finder {
        DWORD pid;
        HWND  found;
        static BOOL CALLBACK Proc(HWND hWnd, LPARAM lp) {
            Finder* f = (Finder*)lp;
            DWORD wPid = 0;
            GetWindowThreadProcessId(hWnd, &wPid);
            if (wPid == f->pid && IsWindowVisible(hWnd)) {
                f->found = hWnd;
                return FALSE;
            }
            return TRUE;
        }
    };
    Finder finder = {pid, NULL};
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        EnumWindows(Finder::Proc, (LPARAM)&finder);
        if (finder.found) return finder.found;
        Sleep(200);
        elapsed += 200;
    }
    return NULL;
}

bool PlayerLauncher::Launch(HWND hParent, const std::string& streamUrl) {
    std::wstring path = Config::GetPlayerPath();

    // If no path saved, ask the user
    if (path.empty() || !PathFileExistsW(path.c_str())) {
        OPENFILENAMEW ofn = {};
        wchar_t szFile[MAX_PATH] = {};
        ofn.lStructSize  = sizeof(ofn);
        ofn.hwndOwner    = hParent;
        ofn.lpstrFilter  = L"Ejecutable\0*.exe\0";
        ofn.lpstrFile    = szFile;
        ofn.nMaxFile     = MAX_PATH;
        ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle   = L"Selecciona MPC-HC (32 o 64 bit)";

        if (!GetOpenFileNameW(&ofn)) return false;
        path = szFile;
        Config::SetPlayerPath(path);
    }

    // Build command line: "<path>" /slave <HWND> "<url>"
    wchar_t cmd[4096];
    // Convert stream URL to wstring
    std::wstring wUrl(streamUrl.begin(), streamUrl.end());
    swprintf(cmd, 4096, L"\"%s\" /slave %lu \"%s\"",
        path.c_str(), (ULONG)(ULONG_PTR)hParent, wUrl.c_str());

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessW(NULL, cmd, NULL, NULL,
            FALSE, 0, NULL, NULL, &si, &pi)) {
        int ret = MessageBoxW(hParent,
            L"No se pudo iniciar MPC-HC.\n"
            L"\u00bfDeseas cambiar la ruta del reproductor?",
            L"Error", MB_YESNO | MB_ICONWARNING);
        if (ret == IDYES) {
            Config::ClearPlayerPath();
            return Launch(hParent, streamUrl);
        }
        return false;
    }

    s_hProcess = pi.hProcess;
    s_hMPCWnd  = WaitForMPCWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    return true;
}

bool PlayerLauncher::SendCommand(int cmdId) {
    if (!s_hMPCWnd || !IsWindow(s_hMPCWnd)) return false;
    PostMessage(s_hMPCWnd, WM_COMMAND, cmdId, 0);
    return true;
}
