#pragma once
// DownloadDialog.h
// Download dialog rendered in its own top-level Win32 window so that the
// VLC player HWND (which sits above all ImGui draws in the main window)
// can never cover it.
//
// Codec matrix (fixed):
//   mkv  -> -c:v copy -c:a copy      (VP9+Opus stream copy, always works)
//   webm -> -c:v vp9  -c:a libopus   (re-encode so container accepts it)
//   mp4  -> libx264 + aac            (only if caps detected)
//   mov  -> libx264 + aac            (only if caps detected)
//   avi  -> libx264 + libmp3lame     (only if caps detected)
//
// VP9 quality list is fetched via yt-dlp --dump-json on first open.
// All shared state between the quality-fetch thread and the render thread
// is protected by DownloadDialogState::qualityMux.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <cstdio>
#include <cstring>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "../Theme.h"
#include "../../transcoder/FfmpegBootstrap.h"
#include "../../transcoder/FfmpegCapabilities.h"

// json.hpp is at third_party/json.hpp; compiler has -Ithird_party in its flags
#include "json.hpp"
using json = nlohmann::json;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline std::wstring DD_ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static inline std::string DD_ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    return s;
}
static inline std::wstring DD_ExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.rfind(L'\\');
    return pos != std::wstring::npos ? p.substr(0, pos) : p;
}
static inline std::string DD_BrowseFolder(HWND owner, const std::string& current) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpszTitle = L"Select download folder";
    wchar_t displayName[MAX_PATH] = {};
    bi.pszDisplayName = displayName;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return current;
    wchar_t path[MAX_PATH] = {};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return DD_ToUtf8(path);
}
static inline std::wstring DD_Quote(const std::wstring& s) {
    return L"\"" + s + L"\"";
}
static inline std::string DD_WatchUrl(const std::string& videoId) {
    return "https://www.youtube.com/watch?v=" + videoId;
}

// ---------------------------------------------------------------------------
// OS detection -> yt-dlp exe name
// ---------------------------------------------------------------------------
static inline std::wstring DD_GetYtDlpExe() {
    typedef LONG (WINAPI* RtlGetVersionFn)(OSVERSIONINFOEXW*);
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(hNtdll, "RtlGetVersion");
        if (fn) fn(&osvi);
    }
    DWORD maj = osvi.dwMajorVersion, min = osvi.dwMinorVersion;
    if (maj == 5 && (min == 1 || min == 2)) return L"yt-dlpxp.exe";
    if (maj == 6 && min == 1)               return L"yt-dlp7.exe";
    return L"yt-dlp.exe";
}

// ---------------------------------------------------------------------------
// Format tables
// ---------------------------------------------------------------------------
struct DD_FormatEntry { const char* ext; const char* label; };

static inline std::vector<DD_FormatEntry> DD_GetVideoFormats() {
    std::vector<DD_FormatEntry> fmts;
    fmts.push_back({"mkv",  "VP9+Opus stream copy"});
    fmts.push_back({"webm", "VP9+Opus (re-encode)"});
    if (FfmpegCapabilities::IsReady()) {
        auto caps = FfmpegCapabilities::Get();
        if (caps.videoMp4()) fmts.push_back({"mp4", "re-encode H.264+AAC"});
        if (caps.videoMov()) fmts.push_back({"mov", "re-encode H.264+AAC"});
        if (caps.videoAvi()) fmts.push_back({"avi", "re-encode H.264+MP3"});
    }
    return fmts;
}
static inline std::vector<DD_FormatEntry> DD_GetAudioFormats() {
    std::vector<DD_FormatEntry> fmts;
    if (!FfmpegCapabilities::IsReady()) return fmts;
    auto caps = FfmpegCapabilities::Get();
    fmts.push_back({"opus", "stream copy"});
    if (caps.audioMp3())  fmts.push_back({"mp3",  "re-encode"});
    if (caps.audioAac())  fmts.push_back({"aac",  "re-encode"});
    if (caps.audioFlac()) fmts.push_back({"flac", "re-encode"});
    if (caps.audioWav())  fmts.push_back({"wav",  "re-encode"});
    if (caps.audioOgg())  fmts.push_back({"ogg",  "re-encode"});
    return fmts;
}
static inline const char* DD_AudioCodecFor(const char* ext) {
    if (strcmp(ext, "mp3")  == 0) return "libmp3lame";
    if (strcmp(ext, "aac")  == 0) return "aac";
    if (strcmp(ext, "opus") == 0) return "libopus";
    if (strcmp(ext, "flac") == 0) return "flac";
    if (strcmp(ext, "wav")  == 0) return "pcm_s16le";
    if (strcmp(ext, "ogg")  == 0) return "libvorbis";
    return "libopus";
}
static const char* kBitrates[]   = { "320k", "256k", "192k", "128k", "96k" };
static const int   kBitrateCount = 5;

// ---------------------------------------------------------------------------
// VP9QualityEntry - a single resolution option
// ---------------------------------------------------------------------------
struct DD_VP9Quality {
    std::string label;    // e.g. "1080p", "720p60"
    int         height;
    std::string formatId; // yt-dlp format id for the video stream
};

// ---------------------------------------------------------------------------
// DownloadDialogState
// ---------------------------------------------------------------------------
struct DownloadDialogState {
    bool open = false;

    std::string videoId, title, channelName, description;
    std::string videoUrl, audioUrl;

    // VP9 quality list (fetched from yt-dlp --dump-json in a background thread).
    // ALL reads AND writes of vp9Qualities, qualitiesFetched, qualitiesFetching
    // MUST be done while holding qualityMux.
    std::mutex              qualityMux;
    std::vector<DD_VP9Quality> vp9Qualities;
    bool qualitiesFetched  = false;
    bool qualitiesFetching = false;
    // Flag set by background thread to signal it finished; main thread
    // reads this without holding qualityMux only as a quick hint — the
    // real copy of the data is always done under the lock.
    std::atomic<bool> qualityThreadDone { false };
    std::thread qualityThread;

    // Local (render-thread-only) snapshot of vp9Qualities, refreshed each
    // frame under the lock.  The ImGui combo reads from this copy so that
    // the lock is held for the shortest possible time.
    std::vector<DD_VP9Quality> vp9QualitiesSnap;
    int  qualityIdx = 0;   // index into vp9QualitiesSnap (0 = best)

    // Legacy labels/urls from the player (kept for compatibility, not used for download)
    std::vector<std::string> qualityLabels, qualityVideoUrls, qualityAudioUrls;

    char filename[512]  = {};
    char outFolder[512] = {};
    bool isAudio        = false;
    int  videoFmtIdx    = 0;
    int  audioFmtIdx    = 0;
    int  bitrateIdx     = 2;
    bool embedMeta      = true;
    bool openWhenDone   = false;

    enum class JobState { Idle, Running, Done, Error };
    JobState           jobState  = JobState::Idle;
    std::atomic<float> progress  { 0.f };
    std::string        statusMsg;
    std::thread        worker;
    std::mutex         logMux;
    std::deque<std::string> logLines;
    bool               logScrollToBottom = false;

    void Reset() {
        jobState = JobState::Idle;
        progress.store(0.f);
        statusMsg.clear();
        { std::lock_guard<std::mutex> lk(logMux); logLines.clear(); }
        logScrollToBottom = false;
    }
    void PushLog(const std::string& line) {
        std::lock_guard<std::mutex> lk(logMux);
        logLines.push_back(line);
        if (logLines.size() > 2000) logLines.pop_front();
        logScrollToBottom = true;
    }

    // Refreshes vp9QualitiesSnap from vp9Qualities under lock.
    // Call once per frame from the render thread.
    void RefreshQualitySnapshot() {
        std::lock_guard<std::mutex> lk(qualityMux);
        vp9QualitiesSnap = vp9Qualities;
        if (qualityIdx >= (int)vp9QualitiesSnap.size())
            qualityIdx = 0;
    }

    // Called when dialog is about to be shown with a new video.
    // MUST be inline to avoid ODR violations when included in multiple TUs.
    inline void StartQualityFetch(const std::wstring& ytdlpPath);

    // Safe join: waits for quality thread to finish.
    // Call from main thread before destroying this object or resetting videoId.
    void JoinQualityThread() {
        if (qualityThread.joinable()) {
            qualityThread.join();
        }
    }
};

// ---------------------------------------------------------------------------
// DD_DialogWindow - owns the separate Win32 window + ImGui context
// ---------------------------------------------------------------------------
struct DD_DialogWindow {
    HWND               hwnd        = NULL;
    IDirect3D9*        d3d         = NULL;
    IDirect3DDevice9*  device      = NULL;
    ImGuiContext*      ctx         = NULL;
    bool               initOk      = false;

    // NON-OWNING pointer - updated every frame from DrawDownloadDialog
    DownloadDialogState* ds        = nullptr;
    HWND               mainHwnd    = NULL;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        DD_DialogWindow* self = (DD_DialogWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

        // Route input through the dialog's own ImGui context
        if (self && self->ctx && self->initOk) {
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(self->ctx);
            LRESULT r = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            ImGui::SetCurrentContext(prev);
            if (r) return r;
        }

        switch (msg) {
        case WM_SIZE:
            // Guard: only handle if our device and context are still alive.
            // Windows can deliver WM_SIZE after DD_CloseDialogWindow() has already
            // torn down the device, so we MUST check initOk here.
            if (self && self->initOk && self->device && self->ctx &&
                wParam != SIZE_MINIMIZED)
            {
                // Always switch to the dialog's own ImGui context before calling
                // any ImGui DX9 functions, because the main window may have its
                // own context set as current.
                ImGuiContext* prev = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(self->ctx);

                ImGui_ImplDX9_InvalidateDeviceObjects();
                D3DPRESENT_PARAMETERS pp = {};
                pp.Windowed             = TRUE;
                pp.SwapEffect           = D3DSWAPEFFECT_DISCARD;
                pp.BackBufferFormat     = D3DFMT_UNKNOWN;
                pp.EnableAutoDepthStencil = FALSE;
                pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
                pp.BackBufferWidth  = LOWORD(lParam);
                pp.BackBufferHeight = HIWORD(lParam);
                self->device->Reset(&pp);
                ImGui_ImplDX9_CreateDeviceObjects();

                ImGui::SetCurrentContext(prev);
            }
            return 0;
        case WM_CLOSE:
            if (self && self->ds &&
                self->ds->jobState == DownloadDialogState::JobState::Running)
                return 0; // block close while downloading
            if (self && self->ds) self->ds->open = false;
            return 0;
        case WM_DESTROY:
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

static DD_DialogWindow g_DlgWin;

static void DD_Worker(DownloadDialogState* ds, HWND mainHwnd);

// ---------------------------------------------------------------------------
// Forward-declare LoadUIFont so DD_OpenDialogWindow can call it on the
// dialog's own ImGuiContext.  The definition lives in MainWindow.cpp;
// we only need the prototype here.
// ---------------------------------------------------------------------------
static void LoadUIFont();

// ---------------------------------------------------------------------------
// DD_OpenDialogWindow
// ---------------------------------------------------------------------------
static bool DD_OpenDialogWindow(DownloadDialogState* ds, HWND mainHwnd) {
    // Always update the ds pointer even if window already exists,
    // so we never render with a stale pointer from a previous video.
    g_DlgWin.ds       = ds;
    g_DlgWin.mainHwnd = mainHwnd;

    if (g_DlgWin.initOk) {
        if (g_DlgWin.hwnd && IsWindow(g_DlgWin.hwnd))
            SetForegroundWindow(g_DlgWin.hwnd);
        return true;
    }

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = DD_DialogWindow::WndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"CTDownloadDialog";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT mr = {}; GetWindowRect(mainHwnd, &mr);
    int dlgW = 660, dlgH = 720;
    int dlgX = mr.left + (mr.right  - mr.left - dlgW) / 2;
    int dlgY = mr.top  + (mr.bottom - mr.top  - dlgH) / 2;
    if (dlgX < 0) dlgX = 0;
    if (dlgY < 0) dlgY = 0;

    g_DlgWin.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"CTDownloadDialog",
        L"ClientTube - Download",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        dlgX, dlgY, dlgW, dlgH,
        mainHwnd, NULL, GetModuleHandle(NULL), NULL);

    if (!g_DlgWin.hwnd) return false;

    SetWindowLongPtr(g_DlgWin.hwnd, GWLP_USERDATA, (LONG_PTR)&g_DlgWin);

    g_DlgWin.d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_DlgWin.d3d) { DestroyWindow(g_DlgWin.hwnd); g_DlgWin.hwnd = NULL; return false; }

    RECT cr; GetClientRect(g_DlgWin.hwnd, &cr);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed             = TRUE;
    pp.SwapEffect           = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat     = D3DFMT_UNKNOWN;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    pp.BackBufferWidth      = cr.right;
    pp.BackBufferHeight     = cr.bottom;

    HRESULT hr = g_DlgWin.d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_DlgWin.hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_DlgWin.device);
    if (FAILED(hr))
        hr = g_DlgWin.d3d->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_DlgWin.hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_DlgWin.device);
    if (FAILED(hr)) {
        g_DlgWin.d3d->Release(); g_DlgWin.d3d = NULL;
        DestroyWindow(g_DlgWin.hwnd); g_DlgWin.hwnd = NULL;
        return false;
    }

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    g_DlgWin.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_DlgWin.ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = NULL;

    // Load the same UI font (Verdana/Tahoma/Arial + FA6 icons) that the main
    // window uses.  Each ImGuiContext owns its own font atlas, so we must
    // call LoadUIFont() here while the dialog context is current.
    LoadUIFont();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.f;
    style.FrameRounding     = 4.f;
    style.ScrollbarRounding = 4.f;
    style.WindowBorderSize  = 1.f;

    ImGui_ImplWin32_Init(g_DlgWin.hwnd);
    ImGui_ImplDX9_Init(g_DlgWin.device);

    ImGui::SetCurrentContext(prevCtx);

    ShowWindow(g_DlgWin.hwnd, SW_SHOW);
    SetForegroundWindow(g_DlgWin.hwnd);

    g_DlgWin.initOk = true;
    return true;
}

static void DD_CloseDialogWindow() {
    if (!g_DlgWin.initOk) return;

    // Clear initOk FIRST so that any pending WM_SIZE messages delivered
    // after this point are ignored by the WndProc guard.
    g_DlgWin.initOk = false;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_DlgWin.ctx);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(g_DlgWin.ctx);
    ImGui::SetCurrentContext(prevCtx);
    g_DlgWin.ctx = NULL;

    if (g_DlgWin.device) { g_DlgWin.device->Release(); g_DlgWin.device = NULL; }
    if (g_DlgWin.d3d)    { g_DlgWin.d3d->Release();    g_DlgWin.d3d    = NULL; }
    if (g_DlgWin.hwnd && IsWindow(g_DlgWin.hwnd)) {
        DestroyWindow(g_DlgWin.hwnd);
        g_DlgWin.hwnd = NULL;
    }
    g_DlgWin.ds = nullptr;
}

// ---------------------------------------------------------------------------
// VP9 quality fetch via yt-dlp --dump-json  (runs in a background thread)
// ---------------------------------------------------------------------------

// Runs yt-dlp --dump-json and returns its stdout as a string.
static std::string DD_RunAndCapture(const std::wstring& cmdLine) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE hR = NULL, hW = NULL;
    if (!CreatePipe(&hR, &hW, &sa, 0)) return {};
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hW; si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end()); cmd.push_back(0);
    if (!CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hR); CloseHandle(hW);
        return {};
    }
    CloseHandle(hW);

    std::string out;
    char buf[4096]; DWORD bytes = 0;
    while (ReadFile(hR, buf, sizeof(buf) - 1, &bytes, NULL) && bytes > 0)
        out.append(buf, bytes);
    CloseHandle(hR);
    WaitForSingleObject(pi.hProcess, 30000); // 30s timeout
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return out;
}

// Background thread: calls yt-dlp --dump-json and parses VP9 format list.
// Writes results under ds->qualityMux, then signals via qualityThreadDone.
static void DD_FetchQualitiesThread(DownloadDialogState* ds,
                                     std::wstring ytdlpPath,
                                     std::string  videoId)
{
    // ---- run yt-dlp --------------------------------------------------------
    std::wstring cmd = DD_Quote(ytdlpPath)
        + L" --dump-json --no-playlist "
        + DD_Quote(DD_ToWide("https://www.youtube.com/watch?v=" + videoId));

    std::string jsonStr = DD_RunAndCapture(cmd);

    // ---- parse -------------------------------------------------------------
    std::vector<DD_VP9Quality> quals;

    // Always add "Best" as first option
    DD_VP9Quality bestQ;
    bestQ.label    = "Best (auto)";
    bestQ.height   = 0;
    bestQ.formatId = "bestvideo[vcodec^=vp9]/bestvideo[vcodec^=vp09]/bestvideo[vcodec!^=av01]";
    quals.push_back(bestQ);

    if (!jsonStr.empty()) {
        try {
            auto j = json::parse(jsonStr);
            if (j.contains("formats")) {
                struct Fmt { int height; int fps; int bitrate; std::string fmtId; };
                std::map<int, Fmt> best;

                for (const auto& f : j["formats"]) {
                    std::string vcodec = f.value("vcodec", "none");
                    std::string acodec = f.value("acodec", "none");
                    if (acodec != "none") continue;
                    if (vcodec == "none") continue;

                    bool isVP9 = vcodec.find("vp9")  != std::string::npos ||
                                 vcodec.find("vp09") != std::string::npos;
                    bool isAV1 = vcodec.rfind("av01", 0) == 0 ||
                                 vcodec.rfind("av1",  0) == 0;
                    if (isAV1 || !isVP9) continue;

                    int height  = f.value("height",  0);
                    int fps     = f.value("fps",     0);
                    int bitrate = f.value("tbr",     0);
                    if (bitrate == 0) bitrate = f.value("vbr", 0);
                    std::string fmtId = f.value("format_id", "");
                    if (height <= 0 || fmtId.empty()) continue;

                    auto it = best.find(height);
                    if (it == best.end() || bitrate > it->second.bitrate)
                        best[height] = {height, fps, bitrate, fmtId};
                }

                // Sort descending by height
                std::vector<std::pair<int,Fmt>> sorted(best.begin(), best.end());
                std::sort(sorted.begin(), sorted.end(),
                          [](const auto& a, const auto& b){ return a.first > b.first; });

                for (const auto& p : sorted) {
                    DD_VP9Quality q;
                    char lbl[32];
                    if (p.second.fps > 30)
                        snprintf(lbl, sizeof(lbl), "%dp%d", p.first, p.second.fps);
                    else
                        snprintf(lbl, sizeof(lbl), "%dp", p.first);
                    q.label    = lbl;
                    q.height   = p.first;
                    q.formatId = p.second.fmtId;
                    quals.push_back(q);
                }
            }
        } catch (...) {
            // JSON parse failure - keep only "Best" option
        }
    }

    // ---- write results under lock ------------------------------------------
    {
        std::lock_guard<std::mutex> lk(ds->qualityMux);
        ds->vp9Qualities     = std::move(quals);
        ds->qualitiesFetched = true;
    }
    // Signal done AFTER the lock is released
    ds->qualityThreadDone.store(true);
}

// MUST be inline - defined in header included by multiple TUs.
inline void DownloadDialogState::StartQualityFetch(const std::wstring& ytdlpPath) {
    std::lock_guard<std::mutex> lk(qualityMux);
    if (qualitiesFetching || qualitiesFetched) return;
    qualitiesFetching = true;

    // Seed with "Fetching..." immediately so the UI is never empty
    if (vp9Qualities.empty()) {
        DD_VP9Quality q;
        q.label    = "Fetching...";
        q.height   = 0;
        q.formatId = "bestvideo[vcodec^=vp9]/bestvideo[vcodec^=vp09]/bestvideo[vcodec!^=av01]";
        vp9Qualities.push_back(q);
    }

    qualityThreadDone.store(false);
    if (qualityThread.joinable()) qualityThread.join();
    qualityThread = std::thread(DD_FetchQualitiesThread, this,
                                ytdlpPath, videoId);
}

// ---------------------------------------------------------------------------
// DD_CopyLogToClipboard
// ---------------------------------------------------------------------------
static void DD_CopyLogToClipboard(DownloadDialogState& ds) {
    std::string all;
    {
        std::lock_guard<std::mutex> lk(ds.logMux);
        for (const auto& line : ds.logLines) { all += line; all += '\n'; }
    }
    if (all.empty()) return;
    if (!OpenClipboard(g_DlgWin.hwnd)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, all.size() + 1);
    if (hMem) {
        char* ptr = (char*)GlobalLock(hMem);
        if (ptr) { memcpy(ptr, all.c_str(), all.size() + 1); GlobalUnlock(hMem); }
        SetClipboardData(CF_TEXT, hMem);
    }
    CloseClipboard();
}

// ---------------------------------------------------------------------------
// DD_RenderDialogContent
// ---------------------------------------------------------------------------
static void DD_RenderDialogContent(DownloadDialogState& ds, HWND ownerHwnd) {
    // Refresh the quality snapshot once per frame under the lock
    ds.RefreshQualitySnapshot();

    RECT cr; GetClientRect(g_DlgWin.hwnd, &cr);
    float mw = (float)(cr.right  - cr.left);
    float mh = (float)(cr.bottom - cr.top);

    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({mw, mh}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.10f, 0.10f, 0.10f, 1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16.f, 16.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {8.f,  8.f});

    ImGui::Begin("##dlgcontent", NULL,
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Pop style stacks immediately so every code path below is balanced
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    bool running = (ds.jobState == DownloadDialogState::JobState::Running);
    bool done    = (ds.jobState == DownloadDialogState::JobState::Done);
    bool errored = (ds.jobState == DownloadDialogState::JobState::Error);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_ACCENT_V4);
    ImGui::Text("Download");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    if (!ds.title.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_DIM_V4);
        ImGui::PushTextWrapPos(mw - 32.f);
        ImGui::TextUnformatted(ds.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Spinner while ffmpeg probing
    if (FfmpegCapabilities::IsProbing()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_DIM_V4);
        ImGui::Text("Probing ffmpeg capabilities...");
        ImGui::PopStyleColor();
        ImGui::End();
        return;
    }

    auto videoFmts = DD_GetVideoFormats();
    auto audioFmts = DD_GetAudioFormats();

    if (ds.videoFmtIdx >= (int)videoFmts.size()) ds.videoFmtIdx = 0;
    if (ds.audioFmtIdx >= (int)audioFmts.size()) ds.audioFmtIdx = 0;

    if (running) ImGui::BeginDisabled();

    float labelW = 130.f;
    float fieldW = mw - 32.f - labelW - 8.f;

    auto RowLabel = [&](const char* lbl) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_DIM_V4);
        ImGui::Text("%s", lbl);
        ImGui::PopStyleColor();
        ImGui::SameLine(labelW);
    };

    // Filename
    RowLabel("File name");
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{.25f,.25f,.25f,1.f});
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText("##fname", ds.filename, sizeof(ds.filename));
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // Output folder
    RowLabel("Output folder");
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{.25f,.25f,.25f,1.f});
    ImGui::SetNextItemWidth(fieldW - 70.f);
    ImGui::InputText("##folder", ds.outFolder, sizeof(ds.outFolder));
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0, 6.f);
    ImGui::PushStyleColor(ImGuiCol_Button,       {.22f,.22f,.22f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_V4);
    // Height 0.f: inherits FramePadding from the active theme so the button
    // stays flush with the InputText widget beside it.
    if (ImGui::Button("Browse", {64.f, 0.f})) {
        std::string picked = DD_BrowseFolder(g_DlgWin.hwnd, ds.outFolder);
        if (!picked.empty()) strncpy_s(ds.outFolder, picked.c_str(), sizeof(ds.outFolder)-1);
    }
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // Media type toggle
    RowLabel("Media type");
    ImGui::PushStyleColor(ImGuiCol_Button,        ds.isAudio?ImVec4{.18f,.18f,.18f,1.f}:Theme::COL_ACCENT_V4);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_HOV_V4);
    if (ImGui::Button("Video", {70.f, 24.f})) ds.isAudio = false;
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0, 4.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ds.isAudio?Theme::COL_ACCENT_V4:ImVec4{.18f,.18f,.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_HOV_V4);
    if (ImGui::Button("Audio", {70.f, 24.f})) ds.isAudio = true;
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // Format
    RowLabel("Format");
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{.25f,.25f,.25f,1.f});
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       {.12f,.12f,.12f,1.f});
    ImGui::SetNextItemWidth(120.f);

    if (!ds.isAudio) {
        const char* curVFmt = videoFmts.empty() ? "---" : videoFmts[ds.videoFmtIdx].ext;
        if (ImGui::BeginCombo("##vfmt", curVFmt, ImGuiComboFlags_NoArrowButton)) {
            for (int i = 0; i < (int)videoFmts.size(); i++) {
                bool sel = (i == ds.videoFmtIdx);
                if (ImGui::Selectable(videoFmts[i].ext, sel)) ds.videoFmtIdx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        const bool audioEmpty = audioFmts.empty();
        if (audioEmpty) ImGui::BeginDisabled();
        const char* curAFmt = audioEmpty ? "---" : audioFmts[ds.audioFmtIdx].ext;
        if (ImGui::BeginCombo("##afmt", curAFmt, ImGuiComboFlags_NoArrowButton)) {
            for (int i = 0; i < (int)audioFmts.size(); i++) {
                bool sel = (i == ds.audioFmtIdx);
                if (ImGui::Selectable(audioFmts[i].ext, sel)) ds.audioFmtIdx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (audioEmpty) ImGui::EndDisabled();
    }
    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    // Quality / Bitrate
    if (!ds.isAudio) {
        // VP9 quality selector — reads from vp9QualitiesSnap (render-thread copy)
        RowLabel("Quality");
        ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1.f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{.25f,.25f,.25f,1.f});
        ImGui::PushStyleColor(ImGuiCol_PopupBg,       {.12f,.12f,.12f,1.f});
        ImGui::SetNextItemWidth(180.f);

        // Determine if we're still fetching (snap is empty or has only "Fetching...")
        bool fetching = ds.vp9QualitiesSnap.empty() ||
                        (ds.vp9QualitiesSnap.size() == 1 &&
                         ds.vp9QualitiesSnap[0].label == "Fetching...");

        const char* qlbl = ds.vp9QualitiesSnap.empty()
            ? "Fetching..."
            : (ds.qualityIdx < (int)ds.vp9QualitiesSnap.size()
               ? ds.vp9QualitiesSnap[ds.qualityIdx].label.c_str()
               : "Best (auto)");

        if (fetching) ImGui::BeginDisabled();
        if (ImGui::BeginCombo("##qual", qlbl, ImGuiComboFlags_NoArrowButton)) {
            for (int i = 0; i < (int)ds.vp9QualitiesSnap.size(); i++) {
                bool sel = (i == ds.qualityIdx);
                if (ImGui::Selectable(ds.vp9QualitiesSnap[i].label.c_str(), sel))
                    ds.qualityIdx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (fetching) ImGui::EndDisabled();
        ImGui::PopStyleColor(3);

        if (!videoFmts.empty()) {
            ImGui::SameLine(0, 8.f);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_DIM_V4);
            ImGui::Text("(%s)", videoFmts[ds.videoFmtIdx].label);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    } else {
        RowLabel("Bitrate");
        ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1.f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{.25f,.25f,.25f,1.f});
        ImGui::PushStyleColor(ImGuiCol_PopupBg,       {.12f,.12f,.12f,1.f});
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::BeginCombo("##br", kBitrates[ds.bitrateIdx], ImGuiComboFlags_NoArrowButton)) {
            for (int i = 0; i < kBitrateCount; i++) {
                bool sel = (i == ds.bitrateIdx);
                if (ImGui::Selectable(kBitrates[i], sel)) ds.bitrateIdx = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(3);
        ImGui::Spacing();
    }

    // Checkboxes
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Theme::COL_ACCENT_V4);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,   {.18f,.18f,.18f,1.f});
    ImGui::Checkbox("Embed metadata", &ds.embedMeta);
    ImGui::SameLine(0, 24.f);
    ImGui::Checkbox("Open when done", &ds.openWhenDone);
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (running) ImGui::EndDisabled();

    // Start / Cancel button
    if (!running) {
        bool canStart = !ds.videoId.empty() && !videoFmts.empty();
        if (!canStart) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,       Theme::COL_ACCENT_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_HOV_V4);
        if (ImGui::Button(done||errored ? "Restart download" : "Start download",
                          {mw - 32.f, 32.f})) {
            ds.Reset();
            ds.jobState = DownloadDialogState::JobState::Running;
            if (ds.worker.joinable()) ds.worker.join();
            ds.worker = std::thread(DD_Worker, &ds, ownerHwnd);
        }
        ImGui::PopStyleColor(2);
        if (!canStart) ImGui::EndDisabled();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, {.25f,.25f,.25f,1.f});
        ImGui::Button("Processing...", {mw - 32.f, 32.f});
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    // Progress bar
    float prog = ds.progress.load();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Theme::COL_ACCENT_V4);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1.f});
    char progBuf[32]; snprintf(progBuf, sizeof(progBuf), "%.0f%%", prog*100.f);
    ImGui::ProgressBar(prog, {mw - 32.f, 18.f}, prog > 0.f ? progBuf : "Waiting");
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    if (!ds.statusMsg.empty()) {
        ImU32 col = errored ? IM_COL32(255,80,80,255)
                  : done    ? IM_COL32(80,220,80,255)
                            : IM_COL32(200,200,200,255);
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetCursorScreenPos(), col, ds.statusMsg.c_str());
        ImGui::Dummy({0, ImGui::GetTextLineHeight()});
    }
    ImGui::Spacing();

    // Log console
    float logH = mh - ImGui::GetCursorPosY() - 24.f;
    if (logH < 60.f) logH = 60.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,     {0.06f,0.06f,0.06f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, {0.08f,0.08f,0.08f,1.f});
    ImGui::BeginChild("##log", {mw - 32.f, logH}, true,
        ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(0.f);
    {
        std::lock_guard<std::mutex> lk(ds.logMux);
        for (auto& line : ds.logLines) {
            ImVec4 col = {100/255.f, 255/255.f, 100/255.f, 1.f};
            if      (line.rfind("[ERROR]",0)==0) col = {1.f,  80/255.f,  80/255.f, 1.f};
            else if (line.rfind("[WARN", 0)==0)  col = {1.f, 200/255.f,  60/255.f, 1.f};
            else if (line.rfind("[OK]",  0)==0)  col = {80/255.f, 220/255.f,  80/255.f, 1.f};
            else if (line.rfind("[DONE]",0)==0)  col = {80/255.f, 220/255.f,  80/255.f, 1.f};
            else if (line.rfind("[INFO]", 0)==0) col = {100/255.f, 180/255.f, 1.f, 1.f};
            else if (line.rfind("===",   0)==0)  col = {120/255.f, 180/255.f, 1.f, 1.f};
            else if (line.rfind(">",     0)==0)  col = {160/255.f, 160/255.f, 160/255.f, 1.f};
            ImGui::TextColored(col, "%s", line.c_str());
        }
        if (ds.logScrollToBottom) {
            ImGui::SetScrollHereY(1.f);
            ds.logScrollToBottom = false;
        }
    }
    ImGui::PopTextWrapPos();
    if (ImGui::BeginPopupContextWindow("##logctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, {0.10f,0.10f,0.10f,1.f});
        if (ImGui::MenuItem("Copy all"))  DD_CopyLogToClipboard(ds);
        if (ImGui::MenuItem("Clear log")) {
            std::lock_guard<std::mutex> lk(ds.logMux);
            ds.logLines.clear();
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// DD_TickDialogWindow
// ---------------------------------------------------------------------------
static void DD_TickDialogWindow() {
    if (!g_DlgWin.initOk || !g_DlgWin.hwnd || !g_DlgWin.ds) return;

    MSG msg;
    while (PeekMessage(&msg, g_DlgWin.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (!IsWindow(g_DlgWin.hwnd)) {
        g_DlgWin.initOk = false;
        g_DlgWin.ds = nullptr;
        return;
    }

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_DlgWin.ctx);

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DD_RenderDialogContent(*g_DlgWin.ds, g_DlgWin.mainHwnd);

    ImGui::EndFrame();
    ImGui::Render();

    g_DlgWin.device->Clear(0, NULL, D3DCLEAR_TARGET,
        D3DCOLOR_RGBA(26, 26, 26, 255), 1.0f, 0);
    if (g_DlgWin.device->BeginScene() == D3D_OK) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_DlgWin.device->EndScene();
    }
    g_DlgWin.device->Present(NULL, NULL, NULL, NULL);

    ImGui::SetCurrentContext(prevCtx);
}

// ---------------------------------------------------------------------------
// Background download worker
// ---------------------------------------------------------------------------
static void DD_Worker(DownloadDialogState* ds, HWND mainHwnd) {
    std::wstring exeDir    = DD_ExeDir();
    std::wstring ytdlpExe  = DD_GetYtDlpExe();
    std::wstring ytdlpPath = exeDir + L"\\" + ytdlpExe;
    std::wstring ffmpegPath= exeDir + L"\\ffmpeg.exe";

    auto pushLog = [&](const std::string& s) { ds->PushLog(s); };
    pushLog("[INFO] Using: " + DD_ToUtf8(ytdlpExe));

    if (GetFileAttributesW(ytdlpPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        pushLog("[ERROR] " + DD_ToUtf8(ytdlpExe) + " not found next to the executable");
        ds->jobState = DownloadDialogState::JobState::Error; return;
    }
    if (GetFileAttributesW(ffmpegPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        pushLog("[ERROR] ffmpeg.exe not found next to the executable");
        ds->jobState = DownloadDialogState::JobState::Error; return;
    }

    std::string outDir = std::string(ds->outFolder);
    std::string fname  = std::string(ds->filename);
    if (outDir.empty()) outDir = DD_ToUtf8(exeDir);
    if (fname.empty())  fname  = ds->videoId;

    auto videoFmts = DD_GetVideoFormats();
    auto audioFmts = DD_GetAudioFormats();
    const char* ext = ds->isAudio
        ? (audioFmts.empty() ? "opus" : audioFmts[ds->audioFmtIdx].ext)
        : (videoFmts.empty() ? "mkv"  : videoFmts[ds->videoFmtIdx].ext);

    std::string finalPath = outDir + "\\" + fname + "." + ext;
    std::string tmpVid    = outDir + "\\tmp_" + ds->videoId + "_v.%(ext)s";
    std::string tmpAud    = outDir + "\\tmp_" + ds->videoId + "_a.%(ext)s";
    std::string tmpVidResolved, tmpAudResolved;

    std::wstring watchUrl = DD_ToWide("https://www.youtube.com/watch?v=" + ds->videoId);

    // Snapshot quality selection under lock before starting download
    std::string vidOnlyFmt;
    {
        std::lock_guard<std::mutex> lk(ds->qualityMux);
        if (!ds->vp9Qualities.empty() &&
            ds->qualityIdx > 0 &&
            ds->qualityIdx < (int)ds->vp9Qualities.size() &&
            ds->vp9Qualities[ds->qualityIdx].height > 0)
        {
            vidOnlyFmt = ds->vp9Qualities[ds->qualityIdx].formatId;
        }
    }
    if (vidOnlyFmt.empty())
        vidOnlyFmt = "bestvideo[vcodec^=vp9]/bestvideo[vcodec^=vp09]/bestvideo[vcodec!^=av01]";

    // Process runner
    auto runProc = [&](const std::wstring& cmdLine,
                        bool parseProgress,
                        float progBase, float progRange) -> bool
    {
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
        HANDLE hR = NULL, hW = NULL;
        if (!CreatePipe(&hR, &hW, &sa, 0)) return false;
        SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput  = hW; si.hStdError = hW;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end()); cmd.push_back(0);
        if (!CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hR); CloseHandle(hW);
            pushLog("[ERROR] Could not start process"); return false;
        }
        CloseHandle(hW);

        static const size_t kLineCap = 64 * 1024;
        std::string lineBuf;
        lineBuf.reserve(4096);
        char chunk[4096];
        DWORD bytes = 0;
        while (ReadFile(hR, chunk, sizeof(chunk) - 1, &bytes, NULL) && bytes > 0) {
            lineBuf.append(chunk, bytes);
            size_t pos;
            while ((pos = lineBuf.find('\n')) != std::string::npos) {
                std::string line = lineBuf.substr(0, pos);
                lineBuf.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) {
                    pushLog(line);
                    if (parseProgress) {
                        auto dl = line.find("[download]");
                        if (dl != std::string::npos) {
                            auto pct = line.find('%', dl);
                            if (pct != std::string::npos) {
                                size_t start = pct;
                                while (start > dl &&
                                    (line[start-1] == ' ' ||
                                     isdigit((unsigned char)line[start-1]) ||
                                     line[start-1] == '.'))
                                    --start;
                                try {
                                    float v = std::stof(line.substr(start, pct - start));
                                    ds->progress.store(progBase + (v / 100.f) * progRange);
                                } catch (...) {}
                            }
                        }
                    }
                }
            }
            if (lineBuf.size() > kLineCap) {
                if (!lineBuf.empty()) pushLog(lineBuf);
                lineBuf.clear();
            }
        }
        if (!lineBuf.empty()) {
            if (lineBuf.back() == '\r') lineBuf.pop_back();
            if (!lineBuf.empty()) pushLog(lineBuf);
        }
        CloseHandle(hR);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        return code == 0;
    };

    auto findFile = [&](const std::string& dir, const std::string& prefix) -> std::string {
        std::wstring pat = DD_ToWide(dir + "\\" + prefix + "*");
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return {};
        std::string found = dir + "\\" + DD_ToUtf8(fd.cFileName);
        FindClose(h); return found;
    };

    // Step 1: video stream
    pushLog("=== Downloading video stream ===");
    {
        std::wstring cmd = DD_Quote(ytdlpPath)
            + L" --no-playlist --no-part -f " + DD_ToWide(vidOnlyFmt)
            + L" -o " + DD_Quote(DD_ToWide(tmpVid))
            + L" " + DD_Quote(watchUrl);
        pushLog("> " + DD_ToUtf8(cmd));
        if (!runProc(cmd, true, 0.f, 0.45f))
            pushLog("[WARN] yt-dlp video stream download failed or non-zero exit");
        tmpVidResolved = findFile(outDir, "tmp_" + ds->videoId + "_v.");
        if (tmpVidResolved.empty()) {
            pushLog("[ERROR] Downloaded video file not found");
            ds->jobState = DownloadDialogState::JobState::Error; return;
        }
        pushLog("[OK] Video: " + tmpVidResolved);
    }

    // Step 2: audio stream
    pushLog("=== Downloading audio stream ===");
    {
        std::wstring cmd = DD_Quote(ytdlpPath)
            + L" --no-playlist --no-part -f bestaudio[acodec=opus]/bestaudio"
            + L" -o " + DD_Quote(DD_ToWide(tmpAud))
            + L" " + DD_Quote(watchUrl);
        pushLog("> " + DD_ToUtf8(cmd));
        if (!runProc(cmd, true, 0.45f, 0.45f))
            pushLog("[WARN] yt-dlp audio stream download failed");
        tmpAudResolved = findFile(outDir, "tmp_" + ds->videoId + "_a.");
        if (!tmpAudResolved.empty()) pushLog("[OK] Audio: " + tmpAudResolved);
    }
    ds->progress.store(0.90f);

    // Step 3: mux with ffmpeg
    pushLog("=== Muxing with ffmpeg ===");
    {
        // Escape ffmpeg metadata values (= ; # \ and newlines must be escaped)
        auto metaEsc = [](const std::string& s) {
            std::string r;
            for (char c : s) { if(c=='='||c==';'||c=='#'||c=='\n'||c=='\\') r+='\\'; r+=c; }
            return r;
        };

        const char* vfmt = videoFmts.empty() ? "mkv" : videoFmts[ds->videoFmtIdx].ext;

        std::wstring cmd = DD_Quote(ffmpegPath) + L" -y";

        // Input files
        cmd += L" -i " + DD_Quote(DD_ToWide(tmpVidResolved));
        if (!tmpAudResolved.empty())
            cmd += L" -i " + DD_Quote(DD_ToWide(tmpAudResolved));

        if (ds->isAudio) {
            // ----- Audio-only output ----------------------------------------
            cmd += L" -vn";
            const char* audExt   = audioFmts.empty() ? "opus" : audioFmts[ds->audioFmtIdx].ext;
            const char* audCodec = DD_AudioCodecFor(audExt);
            if (strcmp(audExt, "opus") == 0) {
                // Opus must go into a container that supports it.
                // The output file is .opus so ffmpeg will use ogg/webm automatically,
                // but to be explicit we specify the codec:
                cmd += L" -c:a copy";
            } else {
                cmd += L" -c:a " + DD_ToWide(audCodec);
                cmd += L" -b:a " + DD_ToWide(kBitrates[ds->bitrateIdx]);
            }

        } else {
            // ----- Video output ---------------------------------------------
            if (strcmp(vfmt, "mkv") == 0) {
                // MKV: pure stream copy, always works with VP9+Opus
                cmd += L" -c:v copy -c:a copy";

            } else if (strcmp(vfmt, "webm") == 0) {
                // WebM only accepts VP8/VP9 video and Vorbis/Opus audio.
                // Stream copy works if the source is already VP9+Opus (which it is).
                // We must NOT use -c:v copy when the source is in a .webm wrapper
                // that ffmpeg reports as "experimental"; instead re-encode to be safe.
                cmd += L" -c:v libvpx-vp9 -crf 33 -b:v 0 -deadline realtime";
                cmd += L" -c:a libopus -b:a 128k";

            } else if (strcmp(vfmt, "mp4") == 0 || strcmp(vfmt, "mov") == 0) {
                // MP4/MOV: re-encode to H.264+AAC (Opus-in-MP4 is experimental)
                cmd += L" -c:v libx264 -preset fast -crf 20";
                cmd += L" -c:a aac -b:a 192k";

            } else if (strcmp(vfmt, "avi") == 0) {
                // AVI: re-encode to H.264+MP3
                cmd += L" -c:v libx264 -preset fast -crf 20";
                cmd += L" -c:a libmp3lame -b:a 192k";

            } else {
                // Fallback: stream copy into whatever container was requested
                cmd += L" -c:v copy -c:a copy";
            }
        }

        // Metadata — added AFTER codec flags so ffmpeg can write the header
        if (ds->embedMeta) {
            std::wstring title   = DD_ToWide(metaEsc(ds->title));
            std::wstring artist  = DD_ToWide(metaEsc(ds->channelName));
            std::string  descStr = ds->description.size() > 500
                ? ds->description.substr(0, 500) : ds->description;
            std::wstring comment = DD_ToWide(metaEsc(descStr));

            // Use -map_metadata -1 first to strip any existing tags, then set ours.
            // This prevents ffmpeg from inheriting broken/partial tags from the
            // downloaded stream containers (some yt-dlp .webm files carry garbage tags).
            cmd += L" -map_metadata -1";
            cmd += L" -metadata " + DD_Quote(L"title="   + title);
            cmd += L" -metadata " + DD_Quote(L"artist="  + artist);
            cmd += L" -metadata " + DD_Quote(L"comment=" + comment);
        }

        cmd += L" " + DD_Quote(DD_ToWide(finalPath));
        pushLog("> " + DD_ToUtf8(cmd));
        bool ok = runProc(cmd, false, 0.90f, 0.10f);
        ds->progress.store(1.f);

        DeleteFileW(DD_ToWide(tmpVidResolved).c_str());
        if (!tmpAudResolved.empty()) DeleteFileW(DD_ToWide(tmpAudResolved).c_str());

        if (!ok) {
            pushLog("[ERROR] ffmpeg exited with an error - check log above");
            ds->statusMsg = "ffmpeg error";
            ds->jobState  = DownloadDialogState::JobState::Error; return;
        }
    }

    pushLog("[DONE] Saved to: " + finalPath);
    ds->statusMsg = "Done: " + finalPath;
    ds->jobState  = DownloadDialogState::JobState::Done;
    if (ds->openWhenDone)
        ShellExecuteW(mainHwnd, L"open",
            DD_ToWide(finalPath).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// DrawDownloadDialog  -  public entry point, call every frame
// ---------------------------------------------------------------------------
inline void DrawDownloadDialog(DownloadDialogState& ds, HWND mainHwnd) {
    if (ds.open) {
        if (!g_DlgWin.initOk)
            DD_OpenDialogWindow(&ds, mainHwnd);
        else
            g_DlgWin.ds = &ds;

        // Kick off quality fetch if not done yet.
        // Use a plain bool flag instead of goto to avoid crossing variable
        // initialization (C++ UB / MSVC warning C4533).
        bool needFetch = false;
        {
            std::lock_guard<std::mutex> lk(ds.qualityMux);
            if (!ds.qualitiesFetching && !ds.qualitiesFetched && !ds.videoId.empty())
                needFetch = true;
        }
        if (needFetch) {
            std::wstring ytdlpPath = DD_ExeDir() + L"\\" + DD_GetYtDlpExe();
            if (GetFileAttributesW(ytdlpPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                ds.StartQualityFetch(ytdlpPath);
        }

        DD_TickDialogWindow();
    } else {
        if (g_DlgWin.initOk) {
            // Join worker threads before closing window
            if (ds.worker.joinable() &&
                ds.jobState != DownloadDialogState::JobState::Running)
                ds.worker.join();
            // Always join quality thread - it holds a raw pointer to ds,
            // so we MUST ensure it finishes before the caller can free ds.
            ds.JoinQualityThread();
            DD_CloseDialogWindow();
        }
    }
}
