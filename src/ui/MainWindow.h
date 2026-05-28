#pragma once
#include <windows.h>
#include <d3d9.h>
#include <string>
#include <vector>
#include <atomic>
#include "AppState.h"
#include "ThumbnailCache.h"
#include "../extractor/InnerTube.h"
#include "views/ViewSearch.h"
#include "views/ViewMain.h"
#include "views/ViewVideoDetail.h"
#include "views/ViewChannel.h"
#include "views/ViewSettings.h"

#pragma comment(lib, "d3d9.lib")

class MainWindow {
public:
    explicit MainWindow(HINSTANCE hInst);
    ~MainWindow();

    bool   Create();
    void   Run();   // shows the window and runs the message loop

private:
    // D3D / window
    HINSTANCE         m_hInst;
    HWND              m_hWnd;
    IDirect3D9*       m_pD3D;
    IDirect3DDevice9* m_pDevice;
    D3DPRESENT_PARAMETERS m_d3dpp;

    // Deferred D3D9 resize — reset is applied once on WM_EXITSIZEMOVE,
    // not on every WM_SIZE pixel, so thumbnails and the VLC HWND stay valid
    // throughout the drag.
    bool  m_pendingResize;
    UINT  m_pendingW;
    UINT  m_pendingH;

    // UI state
    AppState               m_state;
    VideoDetailState       m_videoDetailState;
    ChannelState           m_channelState;
    SettingsState          m_settingsState;
    SearchViewState        m_searchViewState;
    std::vector<VideoItem> m_results;
    int                    m_selectedIdx;
    std::wstring           m_statusMsg;
    std::string            m_currentVideoTitle;

    // Thumbnail cache
    ThumbnailCache         m_thumbCache;

    // Async search
    std::atomic<bool>      m_searching;
    HANDLE                 m_hSearchThread;

    // Async stream resolve
    HANDLE                 m_hStreamThread;

    // Async VP9 quality resolve
    HANDLE                 m_hVP9Thread;

    // Async related videos resolve
    HANDLE                 m_hRelatedThread;

    // Track which videoId the VP9 thread is working on to avoid stale writes
    std::string            m_vp9ResolveVideoId;

    bool  InitD3D();
    void  CleanupD3D();
    void  ResetD3D();
    void  ApplyTheme();
    void  DoSearch();
    void  DoResolveStream(const std::string& videoId);
    void  RenderFrame();

    void  DrawTopBar(float w);
    void  DrawDrawer(float h);
    void  DrawContent(float drawerW, float topH, float w, float h);
    void  DrawMiniPlayer(float w, float h);
    void  DrawStatusBar(float w, float h);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static MainWindow* s_pThis;

    static DWORD WINAPI SearchThreadProc(LPVOID param);
    static DWORD WINAPI StreamResolveProc(LPVOID param);
    static DWORD WINAPI VP9ResolveProc(LPVOID param);

    struct SearchTask {
        MainWindow* self;
        std::string query;
    };
    struct StreamTask {
        MainWindow* self;
        std::string videoId;
    };
    struct VP9Task {
        MainWindow* self;
        std::string videoId;
    };
};
