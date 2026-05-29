#include "MainWindow.h"
#include "Theme.h"
#include "CTLogger.h"
#include "../extractor/InnerTube.h"
#include "../player/PlayerLauncher.h"
#include "../config/Config.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include <IconsFontAwesome6.h>

#include <wchar.h>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

MainWindow* MainWindow::s_pThis = nullptr;

MainWindow::MainWindow(HINSTANCE hInst)
    : m_hInst(hInst), m_hWnd(NULL),
      m_pD3D(NULL), m_pDevice(NULL),
      m_selectedIdx(-1), m_statusMsg(L"Ready."),
      m_searching(false), m_hSearchThread(NULL),
      m_hStreamThread(NULL), m_hVP9Thread(NULL),
      m_hRelatedThread(NULL),
      m_pendingResize(false), m_pendingW(0), m_pendingH(0) {
    memset(&m_d3dpp, 0, sizeof(m_d3dpp));
}

MainWindow::~MainWindow() {
    if (m_hSearchThread) {
        WaitForSingleObject(m_hSearchThread, 3000);
        CloseHandle(m_hSearchThread);
    }
    if (m_hStreamThread) {
        WaitForSingleObject(m_hStreamThread, 5000);
        CloseHandle(m_hStreamThread);
    }
    if (m_hVP9Thread) {
        WaitForSingleObject(m_hVP9Thread, 5000);
        CloseHandle(m_hVP9Thread);
    }
    if (m_hRelatedThread) {
        WaitForSingleObject(m_hRelatedThread, 5000);
        CloseHandle(m_hRelatedThread);
    }
    m_thumbCache.Clear();
    ThumbnailCache::s_instance = nullptr;
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupD3D();
}

bool MainWindow::Create() {
    s_pThis = this;
    HINSTANCE hInst = GetModuleHandleW(NULL);
    m_hInst = hInst;

    UnregisterClassW(L"SightlineVisualizer", hInst);
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"SightlineVisualizer";
    if (!RegisterClassEx(&wc)) return false;

    m_hWnd = CreateWindowEx(0, L"SightlineVisualizer", L"Sightline Visualizer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        960, 640, NULL, NULL, hInst, NULL);
    if (!m_hWnd) return false;

    if (!InitD3D()) {
        MessageBoxW(m_hWnd, L"Failed to initialise Direct3D 9.",
            L"Error", MB_ICONERROR);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = NULL;
    ImGui_ImplWin32_Init(m_hWnd);
    ImGui_ImplDX9_Init(m_pDevice);
    ApplyTheme();

    m_thumbCache.Init(m_pDevice);
    ThumbnailCache::s_instance = &m_thumbCache;

    return true;
}

bool MainWindow::InitD3D() {
    m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!m_pD3D) return false;
    m_d3dpp.Windowed               = TRUE;
    m_d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    m_d3dpp.BackBufferFormat       = D3DFMT_UNKNOWN;
    m_d3dpp.EnableAutoDepthStencil = FALSE;
    m_d3dpp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = m_pD3D->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_hWnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &m_d3dpp, &m_pDevice);
    if (FAILED(hr))
        hr = m_pD3D->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_hWnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &m_d3dpp, &m_pDevice);
    return SUCCEEDED(hr);
}

void MainWindow::CleanupD3D() {
    if (m_pDevice) { m_pDevice->Release(); m_pDevice = NULL; }
    if (m_pD3D)    { m_pD3D->Release();    m_pD3D    = NULL; }
}

void MainWindow::ResetD3D() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    m_thumbCache.OnDeviceLost();
    m_pDevice->Reset(&m_d3dpp);
    ImGui_ImplDX9_CreateDeviceObjects();
    m_thumbCache.OnDeviceReset(m_pDevice);
}

static void LoadUIFont() {
    ImGuiIO& io = ImGui::GetIO();

    // Base glyph ranges: Latin + Latin Extended + General Punctuation + Arrows
    static const ImWchar kRanges[] = {
        0x0020, 0x017E,
        0x2000, 0x206F,
        0x2190, 0x21FF,
        0,
    };
    const char* kCandidates[] = {
        "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/arial.ttf",
        nullptr
    };

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    bool baseLoaded = false;
    for (int i = 0; kCandidates[i]; i++) {
        DWORD attr = GetFileAttributesA(kCandidates[i]);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        ImFont* f = io.Fonts->AddFontFromFileTTF(kCandidates[i], 14.0f, &cfg, kRanges);
        if (f) {
            CTLogger::LogC('I', "[Font] Loaded base font: %s", kCandidates[i]);
            baseLoaded = true;
            break;
        }
    }
    if (!baseLoaded) {
        io.Fonts->AddFontDefault();
        CTLogger::LogC('W', "[Font] No TTF found, using ProggyClean default.");
    }

    // Merge Font Awesome 6 Solid into the base font
    // fa-solid-900.ttf must live next to ClientTube.exe in assets/
    const char* kFAPaths[] = {
        "assets/fa-solid-900.ttf",
        "../assets/fa-solid-900.ttf",
        nullptr
    };
    static const ImWchar kIconRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig icfg;
    icfg.MergeMode        = true;
    icfg.PixelSnapH       = true;
    icfg.GlyphMinAdvanceX = 14.0f;  // keep icons monospaced with the text
    bool iconLoaded = false;
    for (int i = 0; kFAPaths[i]; i++) {
        DWORD attr = GetFileAttributesA(kFAPaths[i]);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        ImFont* fi = io.Fonts->AddFontFromFileTTF(kFAPaths[i], 14.0f, &icfg, kIconRanges);
        if (fi) {
            CTLogger::LogC('I', "[Font] Merged Font Awesome 6: %s", kFAPaths[i]);
            iconLoaded = true;
            break;
        }
    }
    if (!iconLoaded)
        CTLogger::LogC('W', "[Font] fa-solid-900.ttf not found — icons will show as rectangles.");
}

void MainWindow::ApplyTheme() {
    LoadUIFont();

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 4.0f;
    s.FrameRounding     = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding      = 4.0f;
    s.FramePadding      = ImVec2(8, 5);
    s.ItemSpacing       = ImVec2(8, 6);
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 1.0f;
    s.ScrollbarSize     = 12.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = Theme::COL_BG;
    c[ImGuiCol_ChildBg]              = Theme::COL_CARD;
    c[ImGuiCol_PopupBg]              = Theme::COL_CARD;
    c[ImGuiCol_Border]               = Theme::COL_CONTRAST_V4;
    c[ImGuiCol_FrameBg]              = Theme::COL_CONTRAST_V4;
    c[ImGuiCol_FrameBgHovered]       = Theme::COL_SURFACE2;
    c[ImGuiCol_FrameBgActive]        = Theme::COL_ACCENT_SOFT;
    c[ImGuiCol_TitleBg]              = Theme::COL_BG;
    c[ImGuiCol_TitleBgActive]        = Theme::COL_BG;
    c[ImGuiCol_MenuBarBg]            = Theme::COL_CARD;
    c[ImGuiCol_ScrollbarBg]          = Theme::COL_BG;
    c[ImGuiCol_ScrollbarGrab]        = Theme::COL_CONTRAST_V4;
    c[ImGuiCol_ScrollbarGrabHovered] = Theme::COL_ACCENT_V4;
    c[ImGuiCol_ScrollbarGrabActive]  = Theme::COL_ACCENT_V4;
    c[ImGuiCol_Button]               = Theme::COL_CONTRAST_V4;
    c[ImGuiCol_ButtonHovered]        = Theme::COL_ACCENT_V4;
    c[ImGuiCol_ButtonActive]         = Theme::COL_ACCENT_HOV_V4;
    c[ImGuiCol_Header]               = Theme::COL_SELECTED;
    c[ImGuiCol_HeaderHovered]        = Theme::COL_ACCENT_SOFT;
    c[ImGuiCol_HeaderActive]         = Theme::COL_ACCENT_V4;
    c[ImGuiCol_Separator]            = Theme::COL_CONTRAST_V4;
    c[ImGuiCol_Text]                 = Theme::COL_TEXT;
    c[ImGuiCol_TextDisabled]         = Theme::COL_TEXT_DIM_V4;
    c[ImGuiCol_NavHighlight]         = Theme::COL_ACCENT_V4;
    c[ImGuiCol_CheckMark]            = Theme::COL_ACCENT_V4;
    c[ImGuiCol_SliderGrab]           = Theme::COL_ACCENT_V4;
    c[ImGuiCol_SliderGrabActive]     = Theme::COL_ACCENT_HOV_V4;
    c[ImGuiCol_Tab]                  = Theme::COL_CARD;
    c[ImGuiCol_TabHovered]           = Theme::COL_ACCENT_SOFT;
    c[ImGuiCol_TabActive]            = Theme::COL_ACCENT_SOFT;
    c[ImGuiCol_TabUnfocused]         = Theme::COL_BG;
    c[ImGuiCol_TabUnfocusedActive]   = Theme::COL_CARD;
}

DWORD WINAPI MainWindow::SearchThreadProc(LPVOID param) {
    SearchTask* task = (SearchTask*)param;
    MainWindow* self = task->self;
    std::string query = task->query;
    delete task;

    std::string error;
    std::vector<VideoItem> results = InnerTube::Search(query, &error);
    self->m_results = results;

    if (!results.empty()) {
        wchar_t buf[128];
        swprintf(buf, 128, L"%d result%s.",
            (int)results.size(), results.size() == 1 ? L"" : L"s");
        self->m_statusMsg = buf;
    } else if (!error.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, error.c_str(), -1, NULL, 0);
        std::wstring werr(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, error.c_str(), -1, &werr[0], n);
        if (!werr.empty() && werr.back() == L'\0') werr.pop_back();
        self->m_statusMsg = werr;
    } else {
        self->m_statusMsg = L"No results.";
    }

    self->m_searching.store(false);
    return 0;
}

void MainWindow::DoSearch() {
    if (!strlen(m_state.searchBuf)) return;
    if (m_searching.load()) return;

    if (m_hSearchThread) {
        CloseHandle(m_hSearchThread);
        m_hSearchThread = NULL;
    }

    m_results.clear();
    m_selectedIdx  = -1;
    m_statusMsg    = L"Searching...";
    m_state.activePage = AppPage::Search;
    m_searching.store(true);
    m_thumbCache.Clear();

    SearchTask* task = new SearchTask{ this, std::string(m_state.searchBuf) };
    m_hSearchThread = CreateThread(NULL, 0, SearchThreadProc, task, 0, NULL);
    if (!m_hSearchThread) {
        std::string error;
        m_results = InnerTube::Search(std::string(m_state.searchBuf), &error);
        if (!m_results.empty()) {
            wchar_t buf[128];
            swprintf(buf, 128, L"%d result(s).", (int)m_results.size());
            m_statusMsg = buf;
        } else {
            int n = MultiByteToWideChar(CP_UTF8, 0, error.c_str(), -1, NULL, 0);
            std::wstring werr(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, error.c_str(), -1, &werr[0], n);
            if (!werr.empty() && werr.back() == L'\0') werr.pop_back();
            m_statusMsg = werr.empty() ? L"No results." : werr;
        }
        delete task;
        m_searching.store(false);
    }
}

DWORD WINAPI MainWindow::StreamResolveProc(LPVOID param) {
    StreamTask* task = (StreamTask*)param;
    MainWindow* self = task->self;
    std::string videoId = task->videoId;
    delete task;

    CTLogger::LogC('I', "[StreamResolve] starting for videoId = %s", videoId.c_str());
    std::string url = InnerTube::GetStreamUrl(videoId);
    CTLogger::LogC('I', "[StreamResolve] result (first 120): %.120s", url.c_str());

    self->m_state.pendingPlay.videoId  = videoId;
    self->m_state.pendingPlay.videoUrl = url;
    self->m_state.streamResolving.store(false);
    return 0;
}

DWORD WINAPI MainWindow::VP9ResolveProc(LPVOID param) {
    VP9Task* task = (VP9Task*)param;
    MainWindow* self    = task->self;
    std::string videoId = task->videoId;
    delete task;

    CTLogger::LogC('I', "[VP9Resolve] fetching qualities for %s", videoId.c_str());
    std::vector<VP9Quality> qualities = InnerTube::GetVP9Qualities(videoId);
    CTLogger::LogC('I', "[VP9Resolve] got %d VP9 qualities", (int)qualities.size());

    if (self->m_state.pendingPlay.videoId == videoId) {
        self->m_state.pendingPlay.vp9Qualities        = qualities;
        self->m_state.pendingPlay.vp9QualitiesReady   = true;
        self->m_state.pendingPlay.vp9QualitiesLoading = false;
    }
    return 0;
}

void MainWindow::DoResolveStream(const std::string& videoId) {
    if (m_state.streamResolving.load()) return;

    m_state.pendingPlay.vp9Qualities.clear();
    m_state.pendingPlay.vp9QualitiesReady   = false;
    m_state.pendingPlay.vp9QualitiesLoading = true;

    if (m_hStreamThread) { CloseHandle(m_hStreamThread); m_hStreamThread = NULL; }
    if (m_hVP9Thread)    { CloseHandle(m_hVP9Thread);    m_hVP9Thread    = NULL; }

    m_state.pendingPlay.videoUrl.clear();
    m_state.streamResolving.store(true);
    CTLogger::LogC('I', "[MainWindow] launching StreamResolveProc + VP9ResolveProc for %s", videoId.c_str());

    StreamTask* st = new StreamTask{ this, videoId };
    m_hStreamThread = CreateThread(NULL, 0, StreamResolveProc, st, 0, NULL);
    if (!m_hStreamThread) {
        CTLogger::LogC('W', "[MainWindow] StreamResolveProc CreateThread failed, resolving sync");
        m_state.pendingPlay.videoId  = videoId;
        m_state.pendingPlay.videoUrl = InnerTube::GetStreamUrl(videoId);
        m_state.streamResolving.store(false);
        delete st;
    }

    VP9Task* vt = new VP9Task{ this, videoId };
    m_hVP9Thread = CreateThread(NULL, 0, VP9ResolveProc, vt, 0, NULL);
    if (!m_hVP9Thread) {
        CTLogger::LogC('W', "[MainWindow] VP9ResolveProc CreateThread failed, resolving sync");
        auto qs = InnerTube::GetVP9Qualities(videoId);
        if (m_state.pendingPlay.videoId == videoId) {
            m_state.pendingPlay.vp9Qualities        = qs;
            m_state.pendingPlay.vp9QualitiesReady   = true;
            m_state.pendingPlay.vp9QualitiesLoading = false;
        }
        delete vt;
    }
}

void MainWindow::DrawTopBar(float w) {
    const float TOP_H  = 48.0f;
    const float BTN_W  = 40.0f;
    const float SRCH_W = 200.0f;
    const float SBTN_W = 76.0f;
    const float GAP    = 6.0f;
    const float R_PAD  = 8.0f;
    const float BLOCK_W = SRCH_W + GAP + SBTN_W + R_PAD;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, TOP_H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::COL_CARD);
    ImGui::Begin("##topbar", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();

    float itemY = (TOP_H - 28.0f) * 0.5f;

    ImGui::SetCursorPos(ImVec2(R_PAD, itemY));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_CONTRAST_V4);
    if (ImGui::Button(ICON_FA_HOUSE, ImVec2(BTN_W, 28))) {
        m_state.activePage = AppPage::Settings;
        m_state.drawerOpen = false;
    }
    ImGui::PopStyleColor(2);

    const char* pageTitle = "Sightline Visualizer";
    if (m_state.activePage == AppPage::Search)      pageTitle = "Search";
    if (m_state.activePage == AppPage::Settings)    pageTitle = "Settings";
    if (m_state.activePage == AppPage::Channel)     pageTitle = "Channel";
    if (m_state.activePage == AppPage::VideoDetail) {
        if (!m_state.pendingPlay.title.empty())
            m_currentVideoTitle = m_state.pendingPlay.title;
        pageTitle = m_currentVideoTitle.empty()
            ? "Now Playing" : m_currentVideoTitle.c_str();
    }
    if (m_state.activePage == AppPage::About) pageTitle = "About";

    float leftEdge  = R_PAD + BTN_W + GAP;
    float rightEdge = w - BLOCK_W;
    float midZoneW  = rightEdge - leftEdge;
    float titleW    = ImGui::CalcTextSize(pageTitle).x;
    float titleX    = leftEdge + (midZoneW - titleW) * 0.5f;
    if (titleX < leftEdge) titleX = leftEdge;
    float textH = ImGui::GetTextLineHeight();
    ImGui::SetCursorPos(ImVec2(titleX, (TOP_H - textH) * 0.5f));
    ImGui::TextColored(Theme::COL_ACCENT_V4, "%s", pageTitle);

    float blockX = w - BLOCK_W;
    ImGui::SetCursorPos(ImVec2(blockX, itemY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        Theme::COL_CONTRAST_V4);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::COL_SURFACE2);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  Theme::COL_ACCENT_SOFT);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::SetNextItemWidth(SRCH_W);

    bool searching = m_searching.load();
    if (searching) ImGui::BeginDisabled();
    bool enter = ImGui::InputText("##topsearch",
        m_state.searchBuf, sizeof(m_state.searchBuf),
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (searching) ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, GAP);
    ImGui::PushStyleColor(ImGuiCol_Button,        Theme::COL_ACCENT_V4);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_HOV_V4);
    if (searching) ImGui::BeginDisabled();
    if ((ImGui::Button(searching ? "..." : ICON_FA_MAGNIFYING_GLASS " Search", ImVec2(SBTN_W, 28)) || enter) && !searching)
        DoSearch();
    if (searching) ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    ImGui::End();
}

static const char* kDrawerIcons[]  = {
    ICON_FA_NEWSPAPER,
    ICON_FA_FIRE,
    ICON_FA_RSS,
    ICON_FA_HEART,
    ICON_FA_CLOCK_ROTATE_LEFT,
    ICON_FA_DOWNLOAD
};
static const char* kDrawerLabels[] = {
    "What's New","Trending","Subscriptions",
    "Favourites","History","Downloads"
};

void MainWindow::DrawDrawer(float h) {
    if (!m_state.drawerOpen) return;
    float drawerW = 220.0f;
    ImGui::SetNextWindowPos(ImVec2(0, 48));
    ImGui::SetNextWindowSize(ImVec2(drawerW, h - 48));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::Begin("##drawer", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_ACCENT_V4);
    ImGui::Text(" Sightline Visualizer");
    ImGui::PopStyleColor();
    ImGui::TextColored(Theme::COL_TEXT_DIM_V4, " v0.1.0");
    ImGui::Separator(); ImGui::Spacing();
    for (int i = 0; i < 6; i++) {
        bool sel = (m_state.activePage == AppPage::Main &&
                    (int)m_state.activeTab == i);
        if (Widgets::DrawerItem(kDrawerIcons[i], kDrawerLabels[i], sel)) {
            m_state.activePage = AppPage::Main;
            m_state.activeTab  = (AppTab)i;
            m_state.drawerOpen = false;
        }
    }
    ImGui::End();
}

void MainWindow::DrawContent(float, float topH, float w, float h) {
    float cx = 0, cy = topH, cw = w, ch = h - topH;

    DrainSearchViewInfo(m_state, m_searchViewState);

    if (m_state.pendingRelated.hThread != NULL) {
        if (m_hRelatedThread) CloseHandle(m_hRelatedThread);
        m_hRelatedThread = m_state.pendingRelated.hThread;
        m_state.pendingRelated.hThread = NULL;
    }

    if (m_state.playRequested && !m_state.pendingPlay.videoId.empty()) {
        CTLogger::LogC('I', "[MainWindow] playRequested -> videoId=%s",
            m_state.pendingPlay.videoId.c_str());
        DoResolveStream(m_state.pendingPlay.videoId);
    }

    if (m_state.pendingPlay.vp9QualitiesReady &&
        m_state.activePage == AppPage::VideoDetail &&
        m_videoDetailState.qualities.size() <= 1)
    {
        std::vector<QualityOption> opts;
        opts.reserve(m_state.pendingPlay.vp9Qualities.size());
        for (const auto& q : m_state.pendingPlay.vp9Qualities) {
            QualityOption o;
            o.label    = q.label;
            o.videoUrl = q.videoUrl;
            o.audioUrl = q.audioUrl;
            opts.push_back(o);
        }
        VD_ApplyVP9Qualities(m_videoDetailState, opts);
    }

    {
        PopupPlayerState& pop = m_videoDetailState.popup;
        if (pop.open && pop.seekPending && !pop.seekDone && pop.player) {
            PlayerState ps  = pop.player->GetState();
            double      dur = pop.player->GetDuration();
            if (ps == PlayerState::Playing && dur > 0.5) {
                double target = pop.startPos > dur ? dur : pop.startPos;
                pop.player->SeekTo(target);
                pop.seekPending = false;
                pop.seekDone    = true;
            }
        }
        if (pop.open && pop.hwnd && !IsWindow(pop.hwnd)) {
            if (pop.player) {
                double resumePos = pop.player->GetPosition();
                pop.player->Stop();
                if (m_videoDetailState.playerInited && resumePos > 0.5) {
                    m_videoDetailState.player.Play();
                    m_videoDetailState.seekPos2     = resumePos;
                    m_videoDetailState.seekPending2 = true;
                    m_videoDetailState.seekDone2    = false;
                }
                delete pop.player;
                pop.player = nullptr;
            }
            if (pop.overlayHwnd && IsWindow(pop.overlayHwnd)) {
                DestroyWindow(pop.overlayHwnd); pop.overlayHwnd = NULL;
            }
            if (pop.videoChild && IsWindow(pop.videoChild)) {
                DestroyWindow(pop.videoChild); pop.videoChild = NULL;
            }
            pop.hwnd        = NULL;
            pop.open        = false;
            pop.seekPending = false;
            pop.seekDone    = false;
        }
    }

    switch (m_state.activePage) {
        case AppPage::Main:
            DrawMainView(m_state, cx, cy, cw, ch);
            break;
        case AppPage::Search:
            DrawSearchView(m_state, m_searchViewState, m_results, m_selectedIdx,
                m_statusMsg, m_searching.load(), cx, cy, cw, ch);
            break;
        case AppPage::VideoDetail:
            DrawVideoDetailView(m_state, m_videoDetailState,
                m_currentVideoTitle.c_str(), cx, cy, cw, ch, m_hWnd,
                m_state.activePage == AppPage::VideoDetail);
            DrawDownloadDialog(m_videoDetailState.dlDialog, m_hWnd);
            break;
        case AppPage::Channel:
            DrawChannelView(m_state, m_channelState, "Channel", cx, cy, cw, ch);
            break;
        case AppPage::Settings:
            DrawSettingsView(m_state, m_settingsState, cx, cy, cw, ch);
            break;
        case AppPage::About: {
            ImGui::SetNextWindowPos(ImVec2(cx, cy));
            ImGui::SetNextWindowSize(ImVec2(cw, ch));
            ImGui::Begin("##about", NULL,
                ImGuiWindowFlags_NoTitleBar  |
                ImGuiWindowFlags_NoResize    |
                ImGuiWindowFlags_NoMove      |
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            if (ImGui::Button(ICON_FA_ARROW_LEFT "  Back")) m_state.activePage = AppPage::Main;
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_ACCENT_V4);
            ImGui::Text("Sightline Visualizer");
            ImGui::PopStyleColor();
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Version 0.1.0");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextWrapped("A lightweight YouTube frontend for Windows.");
            ImGui::End();
            break;
        }
        default: break;
    }
}

void MainWindow::DrawStatusBar(float w, float h) {
    const float BAR_H = 22.0f;
    ImGui::SetNextWindowPos(ImVec2(0, h - BAR_H));
    ImGui::SetNextWindowSize(ImVec2(w, BAR_H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::COL_CARD);
    ImGui::Begin("##statusbar", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY((BAR_H - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextColored(Theme::COL_TEXT_DIM_V4, " %ls", m_statusMsg.c_str());
    ImGui::End();
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    MainWindow* self = s_pThis;
    switch (msg) {
    case WM_SIZE:
        if (self && self->m_pDevice && wParam != SIZE_MINIMIZED) {
            UINT newW = LOWORD(lParam);
            UINT newH = HIWORD(lParam);
            self->m_pendingW = newW;
            self->m_pendingH = newH;
            if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED) {
                self->m_pendingResize = false;
                self->m_d3dpp.BackBufferWidth  = newW;
                self->m_d3dpp.BackBufferHeight = newH;
                self->ResetD3D();
            } else {
                self->m_pendingResize = true;
            }
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (self && self->m_pDevice && self->m_pendingResize) {
            self->m_d3dpp.BackBufferWidth  = self->m_pendingW;
            self->m_d3dpp.BackBufferHeight = self->m_pendingH;
            self->m_pendingResize = false;
            self->ResetD3D();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void MainWindow::Run() {
    ShowWindow(m_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hWnd);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        m_thumbCache.Tick();

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RECT rc; GetClientRect(m_hWnd, &rc);
        float w = (float)(rc.right  - rc.left);
        float h = (float)(rc.bottom - rc.top);
        const float TOP_H    = 48.0f;
        const float STATUS_H = 22.0f;

        DrawTopBar(w);
        DrawDrawer(h - STATUS_H);
        DrawContent(0, TOP_H, w, h - STATUS_H);
        DrawStatusBar(w, h);

        ImGui::EndFrame();
        m_pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        m_pDevice->Clear(0, NULL, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
        if (m_pDevice->BeginScene() == D3D_OK) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            m_pDevice->EndScene();
        }
        HRESULT hr = m_pDevice->Present(NULL, NULL, NULL, NULL);
        if (hr == D3DERR_DEVICELOST) {
            if (m_pDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
                ResetD3D();
        }
    }
}
