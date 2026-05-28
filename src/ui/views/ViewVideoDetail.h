#pragma once
// ViewVideoDetail - adaptive 2-zone layout
// Original features fully preserved from commit 4da495.
// New features added: VP9 quality selector, seek-on-quality-change, English UI,
//                     Comments tab (InnerTube -> yt-dlp fallback, Load More),
//                     Up Next panel (GetRelatedVideos async).

#include "../AppState.h"
#include "../Widgets.h"
#include "../Theme.h"
#include "../ThumbnailCache.h"
#include "../../player/VLCPlayer.h"
#include "../../config/PersistentData.h"
#include "../../transcoder/FfmpegBootstrap.h"
#include "DownloadDialog.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

enum class VideoDetailTab { Description = 0, Comments, COUNT };
static const char* kVDTabs[] = { "Description", "Comments" };

struct QualityOption { std::string label, videoUrl, audioUrl; };

// ---- Click overlay userdata ------------------------------------------------
struct OverlayUD {
    VLCPlayer* player   = nullptr;
    bool*      isFS     = nullptr;
    double*    lastMove = nullptr;
};

struct PanelSubclassData {
    VLCPlayer* player   = nullptr;
    bool*      isFS     = nullptr;
    double*    lastMove = nullptr;
};

struct PopupPanelUD  { VLCPlayer* player = nullptr; };
struct PopupOverlayUD { VLCPlayer* player = nullptr; };

struct PopupPlayerState {
    HWND          hwnd        = NULL;
    HWND          videoChild  = NULL;
    HWND          overlayHwnd = NULL;
    VLCPlayer*    player      = nullptr;
    bool          open        = false;
    double        startPos    = 0.0;
    bool          seekPending = false;
    bool          seekDone    = false;
    PopupPanelUD  panelUD;
    PopupOverlayUD overlayUD;
};

struct VideoDetailState {
    VideoDetailTab    activeTab    = VideoDetailTab::Description;
    VLCPlayer         player;
    bool              playerInited = false;
    bool              playStarted  = false;
    bool              fullscreen   = false;
    HWND              playerHwnd   = NULL;
    HWND              overlayHwnd  = NULL;
    PanelSubclassData subclassData;
    OverlayUD         overlayUD;

    int cachedPX=-1, cachedPY=-1, cachedPW=-1, cachedPH=-1;

    std::string       streamUrl;
    std::string       title, channelName, channelId;
    std::string       viewCount, duration, description, videoId;
    std::string       lastPlayedUrl;

    int               volume   = 80;
    PopupPlayerState  popup;

    // VP9 quality selector
    std::vector<QualityOption> qualities;
    int               qualityIdx        = 0;
    int               pendingQualityIdx = -1;
    double            qualityChangePos  = 0.0;

    // Seek drain after quality change / initial start
    bool   seekPending2 = false;
    bool   seekDone2    = false;
    double seekPos2     = 0.0;

    double  lastMouseMove = 0.0;
    bool    fsCtrlVisible = true;

    // Download dialog (one instance per VideoDetailState)
    DownloadDialogState dlDialog;

    // Comments local UI state
    std::string commentsLoadedForVideoId;
    bool        commentsTabOpened = false;

    // Related videos local UI state
    std::string relatedLoadedForVideoId;
};

static inline std::string VD_FmtTime(double s) {
    if (s < 0) s = 0;
    int t = (int)s; char b[16];
    if (t >= 3600) snprintf(b,sizeof(b),"%d:%02d:%02d",t/3600,(t%3600)/60,t%60);
    else           snprintf(b,sizeof(b),"%d:%02d",t/60,t%60);
    return b;
}

static inline std::string VD_GetVideosFolder() {
    typedef HRESULT (WINAPI* SHGetKnownFolderPathFn)(const GUID*, DWORD, HANDLE, PWSTR*);
    HMODULE hShell = GetModuleHandleW(L"shell32.dll");
    if (hShell) {
        SHGetKnownFolderPathFn fn = (SHGetKnownFolderPathFn)
            GetProcAddress(hShell, "SHGetKnownFolderPath");
        if (fn) {
            static const GUID FOLDERID_Videos =
                {0x18989B1D,0x99B5,0x455B,{0x84,0x1C,0xAB,0x7C,0x74,0xE4,0xDD,0xFC}};
            PWSTR path = nullptr;
            if (SUCCEEDED(fn(&FOLDERID_Videos, 0, NULL, &path)) && path) {
                int n = WideCharToMultiByte(CP_UTF8,0,path,-1,NULL,0,NULL,NULL);
                std::string r(n-1,'\0');
                WideCharToMultiByte(CP_UTF8,0,path,-1,&r[0],n,NULL,NULL);
                CoTaskMemFree(path); return r;
            }
            if (path) CoTaskMemFree(path);
        }
    }
    wchar_t vp[MAX_PATH]={};
    if (SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_MYVIDEO,NULL,0,vp))) {
        int n=WideCharToMultiByte(CP_UTF8,0,vp,-1,NULL,0,NULL,NULL);
        std::string r(n-1,'\0'); WideCharToMultiByte(CP_UTF8,0,vp,-1,&r[0],n,NULL,NULL); return r;
    }
    wchar_t buf[MAX_PATH]={}; GetModuleFileNameW(NULL,buf,MAX_PATH);
    std::wstring p(buf); auto pos2=p.rfind(L'\\');
    std::wstring ed=(pos2!=std::wstring::npos)?p.substr(0,pos2):p;
    int n=WideCharToMultiByte(CP_UTF8,0,ed.c_str(),-1,NULL,0,NULL,NULL);
    std::string r(n-1,'\0'); WideCharToMultiByte(CP_UTF8,0,ed.c_str(),-1,&r[0],n,NULL,NULL); return r;
}

// ===========================================================================
// Window procs
// ===========================================================================
static LRESULT CALLBACK VD_OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayUD* d = (OverlayUD*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hwnd, msg, wp, lp);
        if (hit == HTNOWHERE || hit < 0) return HTTRANSPARENT;
        return hit;
    }
    case WM_LBUTTONDOWN: SetCapture(hwnd); return 0;
    case WM_LBUTTONDBLCLK:
        if (d && d->isFS) *d->isFS = !(*d->isFS);
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (!d || !d->player) return 0;
        int cx=(int)(short)LOWORD(lp);
        RECT rc; GetClientRect(hwnd,&rc);
        int W=rc.right-rc.left, zW=W/3;
        PlayerState ps=d->player->GetState();
        if (cx<zW) { double np=d->player->GetPosition()-10.0; d->player->SeekTo(np<0?0:np); }
        else if (cx>=W-zW) { double du=d->player->GetDuration(); double np=d->player->GetPosition()+10.0; d->player->SeekTo(np>du?du:np); }
        else { if(ps==PlayerState::Playing) d->player->Pause(); else if(ps==PlayerState::Paused) d->player->Play(); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (d && d->lastMove) *d->lastMove=(double)GetTickCount()/1000.0;
        return 0;
    case WM_MOUSEWHEEL: return SendMessage(GetParent(hwnd),msg,wp,lp);
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

static LRESULT CALLBACK VD_PopupOverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PopupOverlayUD* d=(PopupOverlayUD*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
    switch (msg) {
    case WM_NCHITTEST: { LRESULT h=DefWindowProc(hwnd,msg,wp,lp); if(h==HTNOWHERE||h<0)return HTTRANSPARENT; return h; }
    case WM_LBUTTONDOWN: SetCapture(hwnd); return 0;
    case WM_LBUTTONDBLCLK: return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (!d||!d->player) return 0;
        int cx=(int)(short)LOWORD(lp);
        RECT rc; GetClientRect(hwnd,&rc);
        int W=rc.right-rc.left, zW=W/3;
        PlayerState ps=d->player->GetState();
        if (cx<zW) { double np=d->player->GetPosition()-10.0; d->player->SeekTo(np<0?0:np); }
        else if (cx>=W-zW) { double du=d->player->GetDuration(); double np=d->player->GetPosition()+10.0; d->player->SeekTo(np>du?du:np); }
        else { if(ps==PlayerState::Playing) d->player->Pause(); else if(ps==PlayerState::Paused) d->player->Play(); }
        return 0;
    }
    case WM_MOUSEWHEEL: return SendMessage(GetParent(hwnd),msg,wp,lp);
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

static LRESULT CALLBACK VD_PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProc(hwnd,msg,wp,lp);
}

static bool VD_RegisterClass(const wchar_t* name, WNDPROC proc, HBRUSH bg, UINT style) {
    WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc);
    if (GetClassInfoExW(GetModuleHandle(NULL),name,&wc)) return true;
    wc={}; wc.cbSize=sizeof(wc);
    wc.style=style; wc.lpfnWndProc=proc;
    wc.hInstance=GetModuleHandle(NULL); wc.hbrBackground=bg; wc.lpszClassName=name;
    return RegisterClassExW(&wc)!=0;
}

static void VD_TickPanelAndOverlay(
    HWND parent, int cx, int cy, int cw, int ch,
    HWND& vlcHwnd, HWND& ovHwnd,
    int& cPX, int& cPY, int& cPW, int& cPH,
    OverlayUD* ovUD)
{
    if (!vlcHwnd||!IsWindow(vlcHwnd)) {
        VD_RegisterClass(L"CTVideoPanel2",VD_PanelProc,(HBRUSH)GetStockObject(BLACK_BRUSH),CS_HREDRAW|CS_VREDRAW);
        vlcHwnd=CreateWindowExW(0,L"CTVideoPanel2",L"",WS_CHILD|WS_CLIPSIBLINGS|WS_VISIBLE,
            cx,cy,cw,ch,parent,NULL,GetModuleHandle(NULL),NULL);
    }
    if (!ovHwnd||!IsWindow(ovHwnd)) {
        VD_RegisterClass(L"CTVideoOverlay",VD_OverlayProc,(HBRUSH)GetStockObject(NULL_BRUSH),CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS);
        ovHwnd=CreateWindowExW(WS_EX_LAYERED,L"CTVideoOverlay",L"",WS_CHILD|WS_CLIPSIBLINGS|WS_VISIBLE,
            cx,cy,cw,ch,parent,NULL,GetModuleHandle(NULL),NULL);
        if (ovHwnd) {
            SetLayeredWindowAttributes(ovHwnd,0,1,LWA_ALPHA);
            SetWindowPos(ovHwnd,HWND_TOP,cx,cy,cw,ch,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        }
    }
    if (ovHwnd&&IsWindow(ovHwnd)&&ovUD) SetWindowLongPtr(ovHwnd,GWLP_USERDATA,(LONG_PTR)ovUD);
    if (cx==cPX&&cy==cPY&&cw==cPW&&ch==cPH) return;
    HDWP hdwp=BeginDeferWindowPos(2);
    if (hdwp) {
        if (vlcHwnd) DeferWindowPos(hdwp,vlcHwnd,NULL,cx,cy,cw,ch,SWP_NOZORDER|SWP_NOACTIVATE);
        if (ovHwnd)  DeferWindowPos(hdwp,ovHwnd,HWND_TOP,cx,cy,cw,ch,SWP_NOACTIVATE);
        EndDeferWindowPos(hdwp);
    } else {
        if (vlcHwnd) SetWindowPos(vlcHwnd,NULL,cx,cy,cw,ch,SWP_NOZORDER|SWP_NOACTIVATE);
        if (ovHwnd)  SetWindowPos(ovHwnd,HWND_TOP,cx,cy,cw,ch,SWP_NOACTIVATE);
    }
    cPX=cx; cPY=cy; cPW=cw; cPH=ch;
}

static void VD_DestroyPopup(PopupPlayerState& p) {
    if (p.player)      { p.player->Stop(); delete p.player; p.player=nullptr; }
    if (p.overlayHwnd&&IsWindow(p.overlayHwnd)) { DestroyWindow(p.overlayHwnd); p.overlayHwnd=NULL; }
    if (p.videoChild &&IsWindow(p.videoChild))  { DestroyWindow(p.videoChild);  p.videoChild=NULL;  }
    if (p.hwnd       &&IsWindow(p.hwnd))         { DestroyWindow(p.hwnd);        p.hwnd=NULL;        }
    p.open=p.seekPending=p.seekDone=false;
}

static inline std::string VD_BuildUrl(const QualityOption& q) {
    return q.audioUrl.empty()?q.videoUrl:q.videoUrl+"\n"+q.audioUrl;
}

// Custom seekbar: red fill, white knob, gray buffer track.
static float VD_Seekbar(float cx, float cy, float sw,
                         float pf, float bf, bool canSeek,
                         ImDrawList* dl, const char* id)
{
    const float SH=6.f, KR=8.f;
    ImGui::SetCursorPos({cx,cy});
    ImGui::InvisibleButton(id,{sw,KR*2.f+SH});
    ImVec2 bMin=ImGui::GetItemRectMin();
    float sx=bMin.x, sy=bMin.y+KR;
    float result=-1.f;
    auto clamp01=[](float v){return v<0.f?0.f:v>1.f?1.f:v;};
    if ((ImGui::IsItemActive()||ImGui::IsItemDeactivatedAfterEdit())&&canSeek)
        result=clamp01((ImGui::GetIO().MousePos.x-sx)/sw);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    dl->AddRectFilled({sx,sy},{sx+sw,sy+SH},IM_COL32(55,55,55,230),3);
    if (bf>0.f&&bf<=1.f) dl->AddRectFilled({sx,sy},{sx+sw*bf,sy+SH},IM_COL32(130,130,130,130),3);
    dl->AddRectFilled({sx,sy},{sx+sw*pf,sy+SH},IM_COL32(255,40,40,255),3);
    float kx=sx+sw*pf, ky=sy+SH*.5f;
    dl->AddCircleFilled({kx,ky},KR,IM_COL32(255,255,255,255));
    dl->AddCircle      ({kx,ky},KR,IM_COL32(0,0,0,60),12,1.5f);
    return result;
}

// ===========================================================================
// Quality helpers
// ===========================================================================
static inline void VD_SyncDlDialogQualities(VideoDetailState& vds) {
    vds.dlDialog.qualityLabels.clear();
    vds.dlDialog.qualityVideoUrls.clear();
    vds.dlDialog.qualityAudioUrls.clear();
    for (const auto& q : vds.qualities) {
        vds.dlDialog.qualityLabels.push_back(q.label);
        vds.dlDialog.qualityVideoUrls.push_back(q.videoUrl);
        vds.dlDialog.qualityAudioUrls.push_back(q.audioUrl);
    }
}

static inline void VD_ApplyVP9Qualities(VideoDetailState& vds,
    const std::vector<QualityOption>& opts)
{
    if (opts.empty()) return;
    std::string autoAudio = vds.qualities.empty() ? "" : vds.qualities[0].audioUrl;
    vds.qualities.clear();
    for (const auto& q : opts) {
        QualityOption o = q;
        if (o.audioUrl.empty()) o.audioUrl = autoAudio;
        vds.qualities.push_back(o);
    }
    VD_SyncDlDialogQualities(vds);
}

static inline void VD_DrainSeek(VideoDetailState& vds) {
    if (!vds.seekPending2 || vds.seekDone2) return;
    if (vds.player.GetState() != PlayerState::Playing) return;
    double dur = vds.player.GetDuration();
    if (dur <= 0.0) return;
    vds.player.SeekTo(vds.seekPos2 > dur ? dur : vds.seekPos2);
    vds.seekPending2 = false;
    vds.seekDone2    = true;
}

// ===========================================================================
// Comments helpers
// ===========================================================================

struct VD_CommentsCtx { std::string vid, token; AppState* state; };

static DWORD WINAPI VD_CommentsThreadProc(LPVOID param) {
    VD_CommentsCtx* ctx = (VD_CommentsCtx*)param;
    AppState* st = ctx->state;

    CommentsPage page = InnerTube::GetComments(ctx->vid, ctx->token);

    for (auto& c : page.comments)
        st->pendingComments.comments.push_back(c);

    st->pendingComments.continuationToken = page.continuationToken;
    st->pendingComments.error             = page.error;
    st->pendingComments.ready             = true;
    st->pendingComments.loading           = false;
    st->commentsResolving.store(false);

    delete ctx;
    return 0;
}

static inline void VD_RequestComments(AppState& state,
                                       const std::string& videoId,
                                       const std::string& continuationToken = "")
{
    if (videoId.empty()) return;
    if (state.commentsResolving.load()) return;

    if (continuationToken.empty()) {
        state.pendingComments.comments.clear();
        state.pendingComments.continuationToken.clear();
        state.pendingComments.error.clear();
        state.pendingComments.ready   = false;
        state.pendingComments.loadMore = false;
    }

    state.pendingComments.videoId  = videoId;
    state.pendingComments.loading  = true;
    state.commentsResolving.store(true);

    HANDLE hThread = CreateThread(NULL, 0, VD_CommentsThreadProc,
        new VD_CommentsCtx{videoId, continuationToken, &state}, 0, NULL);

    if (hThread) CloseHandle(hThread);
    else {
        state.pendingComments.loading = false;
        state.commentsResolving.store(false);
    }
}

// ===========================================================================
// Related videos helpers
// ===========================================================================

struct VD_RelatedCtx { std::string vid; AppState* state; };

static DWORD WINAPI VD_RelatedThreadProc(LPVOID param) {
    VD_RelatedCtx* ctx = (VD_RelatedCtx*)param;
    AppState* st = ctx->state;

    std::vector<RelatedItem> items = InnerTube::GetRelatedVideos(ctx->vid);

    st->pendingRelated.items   = std::move(items);
    st->pendingRelated.error   = st->pendingRelated.items.empty() ? "No results" : "";
    st->pendingRelated.ready   = true;
    st->pendingRelated.loading = false;
    st->relatedResolving.store(false);

    delete ctx;
    return 0;
}

static inline void VD_RequestRelated(AppState& state, const std::string& videoId) {
    if (videoId.empty()) return;
    if (state.relatedResolving.load()) return;

    state.pendingRelated.items.clear();
    state.pendingRelated.error.clear();
    state.pendingRelated.ready   = false;
    state.pendingRelated.loading = true;
    state.pendingRelated.videoId = videoId;
    state.relatedResolving.store(true);

    HANDLE hThread = CreateThread(NULL, 0, VD_RelatedThreadProc,
        new VD_RelatedCtx{videoId, &state}, 0, NULL);

    if (hThread) CloseHandle(hThread);
    else {
        state.pendingRelated.loading = false;
        state.relatedResolving.store(false);
    }
}

// Draw the Up Next / related videos panel. Called inside ##vd_right.
static inline void VD_DrawRelatedPanel(AppState& state,
                                        VideoDetailState& vds,
                                        float panelW, float PAD)
{
    const bool loading  = state.relatedResolving.load();
    const bool hasItems = !state.pendingRelated.items.empty();
    const bool hasError = !state.pendingRelated.error.empty() && !hasItems;

    // Auto-trigger load when videoId changes
    if (!vds.videoId.empty() && vds.relatedLoadedForVideoId != vds.videoId && !loading) {
        vds.relatedLoadedForVideoId = vds.videoId;
        VD_RequestRelated(state, vds.videoId);
    }

    if (loading && !hasItems) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Loading...");
        return;
    }

    if (hasError) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "%s", state.pendingRelated.error.c_str());
        ImGui::SetCursorPosX(PAD);
        if (ImGui::Button("Retry", {panelW - PAD*2.f, 24.f}))
            VD_RequestRelated(state, vds.videoId);
        return;
    }

    if (!hasItems) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Nothing to show yet.");
        return;
    }

    const float THUMB_H  = 56.f;
    const float THUMB_W  = 100.f;
    const float ITEM_PAD = 6.f;
    const float TEXT_X   = PAD + THUMB_W + ITEM_PAD;
    const float TEXT_W   = panelW - TEXT_X - PAD;

    for (int i = 0; i < (int)state.pendingRelated.items.size(); i++) {
        const RelatedItem& it = state.pendingRelated.items[i];

        bool canPlay = !it.videoId.empty();

        ImGui::SetCursorPosX(0);
        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        float  rowY     = ImGui::GetCursorPosY();

        char btnId[32]; snprintf(btnId, sizeof(btnId), "##rel_%d", i);
        ImGui::SetCursorPos({0, rowY});
        bool clicked = ImGui::InvisibleButton(btnId, {panelW, THUMB_H + ITEM_PAD*2.f});
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (hovered && canPlay) {
            dl->AddRectFilled(rowStart,
                {rowStart.x + panelW, rowStart.y + THUMB_H + ITEM_PAD*2.f},
                IM_COL32(255,255,255,14));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        // Thumbnail placeholder
        ImVec2 thTL = {rowStart.x + PAD, rowStart.y + ITEM_PAD};
        ImVec2 thBR = {thTL.x + THUMB_W, thTL.y + THUMB_H};
        dl->AddRectFilled(thTL, thBR, IM_COL32(35,35,35,255), 3.f);

        if (!it.videoId.empty() && ThumbnailCache::s_instance) {
            IDirect3DTexture9* tex = ThumbnailCache::s_instance->Get(it.videoId);
            if (tex) {
                dl->AddImage((ImTextureID)(uintptr_t)tex, thTL, thBR,
                             {0,0}, {1,1}, IM_COL32(255,255,255,255));
            } else {
                float cx2 = (thTL.x + thBR.x) * 0.5f;
                float cy2 = (thTL.y + thBR.y) * 0.5f;
                float r   = 10.f;
                dl->AddTriangleFilled(
                    {cx2 - r * 0.6f, cy2 - r},
                    {cx2 - r * 0.6f, cy2 + r},
                    {cx2 + r,        cy2},
                    IM_COL32(180,180,180,120));
            }
        }

        // Duration badge
        if (!it.duration.empty()) {
            ImVec2 ts = ImGui::CalcTextSize(it.duration.c_str());
            float bx = thBR.x - ts.x - 6.f;
            float by = thBR.y - ts.y - 4.f;
            dl->AddRectFilled({bx-3.f,by-2.f},{bx+ts.x+3.f,by+ts.y+2.f},IM_COL32(0,0,0,180),2.f);
            dl->AddText({bx,by}, IM_COL32(255,255,255,240), it.duration.c_str());
        }

        // Playlist badge
        if (it.isPlaylist) {
            const char* lbl = "PLAYLIST";
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            float bx = thTL.x + 3.f, by = thTL.y + 3.f;
            dl->AddRectFilled({bx,by},{bx+ts.x+6.f,by+ts.y+4.f},IM_COL32(0,0,0,180),2.f);
            dl->AddText({bx+3.f,by+2.f}, IM_COL32(200,200,200,230), lbl);
        }

        // Title
        ImGui::SetCursorPos({TEXT_X, rowY + ITEM_PAD});
        ImGui::PushTextWrapPos(TEXT_X + TEXT_W);
        const ImVec4 titleColor = canPlay ? Theme::COL_TEXT : ImVec4{0.47f, 0.47f, 0.47f, 1.f};
        std::string displayTitle = it.title.empty() ? "(no title)" : it.title;
        if ((int)displayTitle.size() > 80) displayTitle = displayTitle.substr(0,77) + "...";
        ImGui::TextColored(titleColor, "%s", displayTitle.c_str());

        // Channel name
        if (!it.channelName.empty()) {
            ImGui::SetCursorPosX(TEXT_X);
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", it.channelName.c_str());
        }

        // View count
        if (!it.viewCount.empty()) {
            ImGui::SetCursorPosX(TEXT_X);
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", it.viewCount.c_str());
        }
        ImGui::PopTextWrapPos();

        // Handle click -> play
        if (clicked && canPlay) {
            state.pendingPlay             = PlayerRequest{};
            state.pendingPlay.videoId     = it.videoId;
            state.pendingPlay.title       = it.title;
            state.pendingPlay.channelName = it.channelName;
            state.playRequested           = true;
        }

        ImGui::SetCursorPos({0, rowY + THUMB_H + ITEM_PAD*2.f});
        ImGui::SetCursorPosX(PAD);
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4{1,1,1,0.05f});
        ImGui::Separator();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
}

// ===========================================================================
// Draw comments tab
// ===========================================================================
static inline void VD_DrawCommentsTab(AppState& state,
                                       VideoDetailState& vds,
                                       float availW, float PAD)
{
    const bool loading  = state.commentsResolving.load();
    const bool hasItems = !state.pendingComments.comments.empty();
    const bool hasMore  = !state.pendingComments.continuationToken.empty();
    const bool hasError = !state.pendingComments.error.empty();

    if (vds.commentsLoadedForVideoId != vds.videoId && !loading) {
        vds.commentsLoadedForVideoId = vds.videoId;
        VD_RequestComments(state, vds.videoId);
    }

    if (loading && !hasItems) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Loading comments...");
        return;
    }

    if (!hasItems) {
        if (hasError) {
            ImGui::SetCursorPosX(PAD);
            ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "Could not load comments: %s",
                               state.pendingComments.error.c_str());
            ImGui::SetCursorPosX(PAD);
            if (ImGui::Button("Retry", {80.f, 24.f}))
                VD_RequestComments(state, vds.videoId);
        } else if (!loading) {
            ImGui::SetCursorPosX(PAD);
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "No comments found.");
        }
        return;
    }

    const float AVATAR_SZ  = 32.f;
    const float INNER_W    = availW - PAD * 2.f;
    const float TEXT_X     = PAD + AVATAR_SZ + 10.f;
    const float TEXT_W     = INNER_W - AVATAR_SZ - 10.f;

    for (const auto& c : state.pendingComments.comments) {
        ImGui::SetCursorPosX(PAD);
        ImVec2 rowStart = ImGui::GetCursorScreenPos();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 avCenter = { rowStart.x + AVATAR_SZ * .5f,
                            rowStart.y + AVATAR_SZ * .5f };
        dl->AddCircleFilled(avCenter, AVATAR_SZ * .5f, IM_COL32(60, 60, 60, 255));
        if (!c.authorName.empty()) {
            char letter[2] = { (char)toupper((unsigned char)c.authorName[0]), 0 };
            ImVec2 ts = ImGui::CalcTextSize(letter);
            dl->AddText({ avCenter.x - ts.x * .5f, avCenter.y - ts.y * .5f },
                        IM_COL32(200, 200, 200, 255), letter);
        }

        ImGui::SetCursorPos({
            ImGui::GetCursorPosX() + AVATAR_SZ + 10.f,
            ImGui::GetCursorPosY()
        });
        ImGui::PushTextWrapPos(TEXT_X + TEXT_W);

        if (c.isAuthor) {
            ImGui::TextColored(Theme::COL_ACCENT_V4, "%s", c.authorName.c_str());
        } else {
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", c.authorName.empty() ? "Unknown" : c.authorName.c_str());
        }
        ImGui::SameLine(0, 8);

        if (!c.publishedAt.empty())
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", c.publishedAt.c_str());

        if (!c.likeCount.empty()) {
            ImGui::SameLine(0, 8);
            ImGui::TextColored({0.9f, 0.75f, 0.2f, 0.85f}, "%s", c.likeCount.c_str());
        }

        if (!c.text.empty()) {
            ImGui::SetCursorPosX(TEXT_X);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT);
            ImGui::TextWrapped("%s", c.text.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();

        ImGui::SetCursorPosX(0);
        ImGui::Spacing();
        ImGui::SetCursorPosX(PAD);
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4{1,1,1,0.06f});
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (hasError && hasItems) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "Error loading more: %s",
                           state.pendingComments.error.c_str());
    }

    if (hasMore && !loading) {
        ImGui::SetCursorPosX(PAD);
        ImGui::PushStyleColor(ImGuiCol_Button,       {.18f,.18f,.18f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_ACCENT_HOV_V4);
        if (ImGui::Button("Load more comments", {INNER_W, 30.f}))
            VD_RequestComments(state, vds.videoId,
                               state.pendingComments.continuationToken);
        ImGui::PopStyleColor(3);
    } else if (loading && hasItems) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Loading more...");
    }

    ImGui::Spacing(); ImGui::Spacing();
}

// ===========================================================================
// DrawVideoDetailView
// ===========================================================================
inline void DrawVideoDetailView(
    AppState&         state,
    VideoDetailState& vds,
    const char*       /*unused*/,
    float x, float y, float w, float h,
    HWND mainHwnd,
    bool isActivePage)
{
    VD_DrainSeek(vds);

    if (!isActivePage) {
        if (vds.playerHwnd  && IsWindow(vds.playerHwnd))  ShowWindow(vds.playerHwnd,  SW_HIDE);
        if (vds.overlayHwnd && IsWindow(vds.overlayHwnd)) ShowWindow(vds.overlayHwnd, SW_HIDE);
        return;
    }

    // -- Absorb new play request
    if (state.playRequested) {
        state.playRequested = false;
        vds.player.Stop();
        vds.playStarted=vds.playerInited=false;
        vds.fullscreen=false;
        vds.qualities.clear(); vds.qualityIdx=0;
        vds.pendingQualityIdx=-1;
        vds.seekPending2=vds.seekDone2=false;
        vds.cachedPX=vds.cachedPY=vds.cachedPW=vds.cachedPH=-1;
        vds.lastPlayedUrl.clear();
        if (vds.playerHwnd)  ShowWindow(vds.playerHwnd,  SW_HIDE);
        if (vds.overlayHwnd) ShowWindow(vds.overlayHwnd, SW_HIDE);
        vds.title       = state.pendingPlay.title;
        vds.channelName = state.pendingPlay.channelName;
        vds.channelId   = state.pendingPlay.channelId;
        vds.duration    = state.pendingPlay.duration;
        vds.viewCount   = state.pendingPlay.viewCount;
        vds.description = state.pendingPlay.description;
        vds.videoId     = state.pendingPlay.videoId;
        vds.streamUrl.clear();
        // Reset comments for new video
        vds.commentsLoadedForVideoId.clear();
        vds.commentsTabOpened = false;
        state.pendingComments = PendingComments{};
        state.commentsResolving.store(false);
        // Reset related for new video
        vds.relatedLoadedForVideoId.clear();
        state.pendingRelated = PendingRelated{};
        state.relatedResolving.store(false);
        if (!vds.videoId.empty()) PersistentData::PushHistory(vds.videoId, vds.title);
    }

    // Sync metadata that arrives late from the resolver
    if (state.pendingPlay.videoId == vds.videoId) {
        if (vds.title.empty()       && !state.pendingPlay.title.empty())       vds.title=state.pendingPlay.title;
        if (vds.channelName.empty() && !state.pendingPlay.channelName.empty()) vds.channelName=state.pendingPlay.channelName;
        if (vds.description.empty() && !state.pendingPlay.description.empty()) vds.description=state.pendingPlay.description;
        if (vds.channelId.empty()   && !state.pendingPlay.channelId.empty())   vds.channelId=state.pendingPlay.channelId;
        if (vds.viewCount.empty()   && !state.pendingPlay.viewCount.empty())   vds.viewCount=state.pendingPlay.viewCount;
        if (vds.duration.empty()    && !state.pendingPlay.duration.empty())    vds.duration=state.pendingPlay.duration;
    }

    // Stream URL arrived from resolver — no videoId guard so it always fires
    if (!state.streamResolving.load() && vds.streamUrl.empty() && !state.pendingPlay.videoUrl.empty()) {
        vds.streamUrl = state.pendingPlay.videoUrl;
        if (vds.qualities.empty()) {
            QualityOption q; q.label="Auto (Best)";
            q.videoUrl=state.pendingPlay.videoUrl;
            q.audioUrl=state.pendingPlay.audioUrl;
            vds.qualities.push_back(q);
            VD_SyncDlDialogQualities(vds);
        } else {
            vds.qualities[0].videoUrl = state.pendingPlay.videoUrl;
            if (vds.qualities[0].audioUrl.empty())
                vds.qualities[0].audioUrl = state.pendingPlay.audioUrl;
            VD_SyncDlDialogQualities(vds);
        }
    }
    // Absorb VP9 qualities
    if (state.pendingPlay.vp9QualitiesReady &&
        state.pendingPlay.videoId == vds.videoId &&
        vds.qualities.size() <= 1)
    {
        std::vector<QualityOption> opts;
        for (const auto& q : state.pendingPlay.vp9Qualities) {
            QualityOption o;
            o.label    = q.label;
            o.videoUrl = q.videoUrl;
            o.audioUrl = q.audioUrl.empty() ? state.pendingPlay.audioUrl : q.audioUrl;
            opts.push_back(o);
        }
        VD_ApplyVP9Qualities(vds, opts);
    }

    const bool streamError = !vds.streamUrl.empty() && vds.streamUrl.rfind("ERROR:",0)==0;

    // Apply pending quality switch
    if (vds.pendingQualityIdx >= 0) {
        vds.qualityIdx        = vds.pendingQualityIdx;
        vds.pendingQualityIdx = -1;
        vds.dlDialog.qualityIdx = vds.qualityIdx;
        vds.player.Stop();
        vds.streamUrl     = VD_BuildUrl(vds.qualities[vds.qualityIdx]);
        vds.playStarted   = false;
        vds.lastPlayedUrl = vds.streamUrl;
        state.pendingPlay.videoUrl = vds.qualities[vds.qualityIdx].videoUrl;
        state.pendingPlay.audioUrl = vds.qualities[vds.qualityIdx].audioUrl;
        if (vds.qualityChangePos > 0.5) {
            vds.seekPos2     = vds.qualityChangePos;
            vds.seekPending2 = true;
            vds.seekDone2    = false;
        }
    }

    // =====================================================================
    // LAYOUT
    // =====================================================================
    const float PAD     = 12.0f;
    const float RW      = 280.0f;
    const bool  wide    = (w >= 900.0f);
    const float LW      = wide ? w - RW - PAD*3.f : w;

    const float BH      = 28.0f;
    const float BACKH   = 36.0f;
    const float SB_KR   = 8.0f;
    const float SB_SH   = 6.0f;
    const float SB_GAP  = 8.0f;
    const float SB_ROW  = SB_KR*2.f + SB_SH + 14.f;
    const float CTR_H   = BH + 10.0f;
    const float ACT_H   = BH + 10.0f;
    const float CHROME  = BACKH + SB_GAP + SB_ROW + CTR_H + ACT_H;
    const float BOT_MIN = 200.0f;

    float vidW = LW - PAD*2.f;
    float vidH = vidW * 9.f/16.f;
    float vidMaxH = h - CHROME - BOT_MIN;
    if (vidMaxH < 80.f) vidMaxH = 80.f;
    if (vidH > vidMaxH) { vidH = vidMaxH; vidW = vidH * 16.f/9.f; }
    if (vidH < 80.f) vidH = 80.f;

    const float topZoneH = BACKH + vidH + SB_GAP + SB_ROW + CTR_H + ACT_H;
    const float botH     = h - topZoneH;

    // =====================================================================
    // OUTER WINDOW
    // =====================================================================
    ImGui::SetNextWindowPos({x,y});
    ImGui::SetNextWindowSize({w,h});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::COL_BG);
    ImGui::Begin("##vd",NULL,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
        ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar|
        ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // =====================================================================
    // TOP ZONE
    // =====================================================================
    ImGui::SetCursorPos({0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::COL_BG);
    ImGui::BeginChild("##vd_top",{LW,topZoneH},false,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
    ImDrawList* dlTop=ImGui::GetWindowDrawList();

    // Back button
    ImGui::SetCursorPos({PAD,(BACKH-BH)*.5f});
    ImGui::PushStyleColor(ImGuiCol_Button,       {0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.22f,.22f,.22f,1});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {.32f,.32f,.32f,1});
    if (ImGui::Button("< Back",{72,BH})) {
        vds.player.Stop();
        vds.playStarted=vds.playerInited=false;
        vds.streamUrl.clear(); state.pendingPlay.videoUrl.clear();
        if (vds.playerHwnd)  { ShowWindow(vds.playerHwnd,SW_HIDE);  DestroyWindow(vds.playerHwnd);  vds.playerHwnd=NULL; }
        if (vds.overlayHwnd) { ShowWindow(vds.overlayHwnd,SW_HIDE); DestroyWindow(vds.overlayHwnd); vds.overlayHwnd=NULL; }
        vds.cachedPX=vds.cachedPY=vds.cachedPW=vds.cachedPH=-1;
        state.activePage=state.prevPage;
    }
    ImGui::PopStyleColor(3);

    // Video rect
    float vidOffX=(LW-PAD*2.f-vidW)*.5f+PAD;
    float vidOffY=BACKH;
    ImGui::SetCursorPos({vidOffX,vidOffY});
    ImVec2 vidTL=ImGui::GetCursorScreenPos();
    dlTop->AddRectFilled(vidTL,{vidTL.x+vidW,vidTL.y+vidH},IM_COL32(0,0,0,255));

    PlayerState ps  = vds.player.GetState();
    double pos      = vds.player.GetPosition();
    double dur      = vds.player.GetDuration();
    bool isPlaying  = (ps==PlayerState::Playing);
    bool canCtrl    = vds.playStarted && !streamError;
    bool canSeek    = dur>0.0 && canCtrl;

    // Status overlay
    {
        const char* ov=nullptr; ImU32 oc=IM_COL32(200,200,200,200);
        if      (vds.videoId.empty())                                    ov="Select a video";
        else if (streamError)                                          { ov=vds.streamUrl.c_str()+6; oc=IM_COL32(255,80,80,255); }
        else if (state.streamResolving.load()||vds.streamUrl.empty())    ov="Resolving stream...";
        else if (!vds.playStarted)                                       ov="Starting...";
        else if (ps==PlayerState::Loading)                               ov="Loading...";
        else if (ps==PlayerState::Error)                               { ov=vds.player.GetError().c_str(); oc=IM_COL32(255,80,80,255); }
        if (ov) {
            ImVec2 ts=ImGui::CalcTextSize(ov);
            dlTop->AddText({vidTL.x+(vidW-ts.x)*.5f,vidTL.y+(vidH-ts.y)*.5f},oc,ov);
        }
    }

    int panelX=(int)vidTL.x, panelY=(int)vidTL.y;
    int panelW=(int)vidW,    panelH=(int)vidH;
    if (vds.fullscreen) {
        HMONITOR hm=MonitorFromWindow(mainHwnd,MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi={}; mi.cbSize=sizeof(mi); GetMonitorInfo(hm,&mi);
        POINT fsTopLeft={mi.rcMonitor.left,mi.rcMonitor.top};
        ScreenToClient(mainHwnd,&fsTopLeft);
        panelX=fsTopLeft.x; panelY=fsTopLeft.y;
        panelW=mi.rcMonitor.right-mi.rcMonitor.left;
        panelH=mi.rcMonitor.bottom-mi.rcMonitor.top;
    }

    vds.overlayUD={&vds.player,&vds.fullscreen,&vds.lastMouseMove};

    if (!streamError) {
        VD_TickPanelAndOverlay(mainHwnd,panelX,panelY,panelW,panelH,
            vds.playerHwnd,vds.overlayHwnd,
            vds.cachedPX,vds.cachedPY,vds.cachedPW,vds.cachedPH,
            &vds.overlayUD);
        bool iconified=IsIconic(mainHwnd);
        if (vds.playerHwnd)  ShowWindow(vds.playerHwnd,  iconified?SW_HIDE:SW_SHOWNA);
        if (vds.overlayHwnd) ShowWindow(vds.overlayHwnd, iconified?SW_HIDE:SW_SHOWNA);
        if (!vds.playerInited&&vds.playerHwnd) {
            vds.player.Init(vds.playerHwnd);
            vds.player.SetVolume(vds.volume);
            vds.playerInited=true;
        }
    } else {
        if (vds.playerHwnd &&IsWindow(vds.playerHwnd))  ShowWindow(vds.playerHwnd, SW_HIDE);
        if (vds.overlayHwnd&&IsWindow(vds.overlayHwnd)) ShowWindow(vds.overlayHwnd,SW_HIDE);
    }

    if (!vds.playStarted&&vds.playerInited&&!vds.streamUrl.empty()&&!streamError) {
        std::string url=vds.qualities.empty()?vds.streamUrl:VD_BuildUrl(vds.qualities[vds.qualityIdx]);
        if (vds.player.Open(url)) {
            vds.playStarted=true;
            vds.lastPlayedUrl=url;
            if (vds.playerHwnd)  ShowWindow(vds.playerHwnd,  SW_SHOWNA);
            if (vds.overlayHwnd) ShowWindow(vds.overlayHwnd, SW_SHOWNA);
        }
    }

    ImGui::SetCursorPos({vidOffX,vidOffY});
    ImGui::Dummy({vidW,vidH});

    // -----------------------------------------------------------------------
    // SEEKBAR ROW
    // -----------------------------------------------------------------------
    {
        const float sbX=PAD+2.f, sbW=LW-PAD*2.f-4.f;
        const float rowY=vidOffY+vidH+SB_GAP;
        float pf=canSeek?(float)(pos/dur):0.f;
        float bf=vds.player.GetBufferPct();
        float nr=VD_Seekbar(sbX,rowY,sbW,pf,bf,canSeek,dlTop,"##sb");
        if (nr>=0.f) vds.player.SeekTo((double)nr*dur);
        ImVec2 bMin=ImGui::GetItemRectMin();
        float trackY=bMin.y+SB_KR;
        float labelY=trackY+SB_SH+SB_KR+2.f;
        std::string tL=VD_FmtTime(pos), tR=VD_FmtTime(dur);
        dlTop->AddText({bMin.x,labelY},IM_COL32(160,160,160,255),tL.c_str());
        ImVec2 trSz=ImGui::CalcTextSize(tR.c_str());
        dlTop->AddText({bMin.x+sbW-trSz.x,labelY},IM_COL32(160,160,160,255),tR.c_str());
        ImGui::SetCursorPos({sbX,rowY+SB_ROW});
    }

    // -----------------------------------------------------------------------
    // TRANSPORT ROW
    // -----------------------------------------------------------------------
    float ctrRowY;
    {
        const float BW=BH+2.f, G=4.f;
        ctrRowY=ImGui::GetCursorPosY();
        ImGui::SetCursorPos({PAD,ctrRowY+(CTR_H-BH)*.5f});

        ImGui::PushStyleColor(ImGuiCol_Button,       {.17f,.17f,.17f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.28f,.28f,.28f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {.38f,.38f,.38f,1});
        if (!canCtrl) ImGui::BeginDisabled();

        if (ImGui::Button("|<",{BW,BH})) vds.player.SeekTo(0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart");
        ImGui::SameLine(0,G);
        if (ImGui::Button("<<",{BW,BH})) { double np=pos-10; vds.player.SeekTo(np<0?0:np); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("-10s");
        ImGui::SameLine(0,G);

        ImGui::PushStyleColor(ImGuiCol_Button,       isPlaying?ImVec4{.15f,.15f,.15f,1}:Theme::COL_ACCENT_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_V4);
        if (ImGui::Button(isPlaying?" || ":" >  ",{BW+16.f,BH})) {
            if (isPlaying) vds.player.Pause();
            else if (ps==PlayerState::Stopped||ps==PlayerState::Idle) {
                if (!vds.lastPlayedUrl.empty()) vds.player.Open(vds.lastPlayedUrl);
            } else vds.player.Play();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0,G);
        if (ImGui::Button(">>",{BW,BH})) { double np=pos+10; vds.player.SeekTo(np>dur?dur:np); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("+10s");
        ImGui::SameLine(0,G);
        if (ImGui::Button(">|",{BW,BH})) vds.player.SeekTo(dur);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("End");

        if (!canCtrl) ImGui::EndDisabled();
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0,10);
        {
            ImU32 dc=IM_COL32(90,90,90,255); const char* dt="";
            if (state.streamResolving.load())     { dc=IM_COL32(255,200,0,255); dt="Resolving..."; }
            else if (streamError)                 { dc=IM_COL32(255,60,60,255); dt="Error"; }
            else if (vds.playStarted) switch(ps) {
                case PlayerState::Loading: dc=IM_COL32(255,200,0,255); dt="Loading";  break;
                case PlayerState::Playing: dc=IM_COL32(0,220,80,255);  dt="Playing";  break;
                case PlayerState::Paused:  dc=IM_COL32(180,180,180,255);dt="Paused"; break;
                case PlayerState::Stopped: dc=IM_COL32(90,90,90,255);  dt="Stopped"; break;
                case PlayerState::Error:   dc=IM_COL32(255,60,60,255); dt="Error";   break;
                default: break;
            }
            ImVec2 dotP=ImGui::GetCursorScreenPos(); dotP.y+=(BH-10.f)*.5f;
            dlTop->AddCircleFilled({dotP.x+5,dotP.y+5},5.f,dc);
            ImGui::Dummy({12,BH});
            if (dt[0]&&ImGui::IsMouseHoveringRect(dotP,{dotP.x+12,dotP.y+12})) ImGui::SetTooltip("%s",dt);
        }

        const float VOL_W=90.f, QUAL_W=130.f, MG=6.f;
        ImGui::SetCursorPos({LW-PAD-QUAL_W-MG-VOL_W,ctrRowY+(CTR_H-BH)*.5f});
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,      {1,1,1,1});
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,{.8f,.8f,.8f,1});
        ImGui::PushStyleColor(ImGuiCol_FrameBg,         {.2f,.2f,.2f,1});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  {.3f,.3f,.3f,1});
        ImGui::SetNextItemWidth(VOL_W);
        int vol=vds.volume;
        if (ImGui::SliderInt("##vol",&vol,0,100,"Vol %d%%")) { vds.volume=vol; vds.player.SetVolume(vol); }
        ImGui::PopStyleColor(4);
        ImGui::SameLine(0,MG);

        if (!vds.qualities.empty()) {
            bool loading=(vds.qualities.size()==1 && !vds.qualities[0].videoUrl.empty());
            const char* cur=loading?"Loading qualities...":vds.qualities[vds.qualityIdx].label.c_str();
            ImGui::PushStyleColor(ImGuiCol_FrameBg,       {.18f,.18f,.18f,1});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{.28f,.28f,.28f,1});
            ImGui::PushStyleColor(ImGuiCol_PopupBg,       {.12f,.12f,.12f,1});
            ImGui::SetNextItemWidth(QUAL_W);
            if (ImGui::BeginCombo("##q",cur,ImGuiComboFlags_NoArrowButton)) {
                for (int qi=0;qi<(int)vds.qualities.size();qi++) {
                    bool sel=qi==vds.qualityIdx;
                    if (ImGui::Selectable(vds.qualities[qi].label.c_str(),sel)&&qi!=vds.qualityIdx) {
                        vds.qualityChangePos  = vds.player.GetPosition();
                        vds.pendingQualityIdx = qi;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleColor(3);
        } else { ImGui::Dummy({QUAL_W,BH}); }
        ImGui::SetCursorPosY(ctrRowY+CTR_H);
    }

    // -----------------------------------------------------------------------
    // ACTIONS ROW
    // -----------------------------------------------------------------------
    {
        const int N=4; const float G=6.f;
        float AW=(LW-PAD*2.f-G*(N-1))/(float)N;
        if (AW<80.f) AW=80.f;
        float rowY=ImGui::GetCursorPosY();
        ImGui::SetCursorPos({PAD,rowY+(ACT_H-BH)*.5f});
        ImGui::PushStyleColor(ImGuiCol_Button,       {.15f,.15f,.15f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COL_ACCENT_HOV_V4);

        bool inFav=!vds.videoId.empty()&&PersistentData::IsInPlaylist("favorites",vds.videoId);

        bool dlRunning=(vds.dlDialog.jobState==DownloadDialogState::JobState::Running);
        const char* dlLabel=dlRunning?"[~] Downloading...":"[v] Download";
        if (ImGui::Button(dlLabel,{AW,BH})&&!vds.videoId.empty()&&!dlRunning) {
            if (FfmpegBootstrap::RequireFfmpeg(mainHwnd)) {
                vds.dlDialog.Reset();
                vds.dlDialog.videoId     = vds.videoId;
                vds.dlDialog.title       = vds.title;
                vds.dlDialog.channelName = vds.channelName;
                vds.dlDialog.description = vds.description;
                if (!vds.qualities.empty()) {
                    int qi=vds.qualityIdx<(int)vds.qualities.size()?vds.qualityIdx:0;
                    vds.dlDialog.videoUrl=vds.qualities[qi].videoUrl;
                    vds.dlDialog.audioUrl=vds.qualities[qi].audioUrl;
                } else {
                    vds.dlDialog.videoUrl=state.pendingPlay.videoUrl;
                    vds.dlDialog.audioUrl=state.pendingPlay.audioUrl;
                }
                VD_SyncDlDialogQualities(vds);
                vds.dlDialog.qualityIdx=vds.qualityIdx;
                std::string safe;
                for (unsigned char c:vds.title) {
                    if (c>=32&&c<128&&std::string("\\/:*?\"<>|").find((char)c)==std::string::npos) safe+=(char)c;
                    else if (c>127) safe+='_';
                }
                while (!safe.empty()&&(safe.back()==' '||safe.back()=='.')) safe.pop_back();
                if (safe.size()>120) safe.resize(120);
                if (safe.empty()) safe="video";
                strncpy_s(vds.dlDialog.filename,sizeof(vds.dlDialog.filename),safe.c_str(),_TRUNCATE);
                std::string vf=VD_GetVideosFolder();
                strncpy_s(vds.dlDialog.outFolder,sizeof(vds.dlDialog.outFolder),vf.c_str(),_TRUNCATE);
                vds.dlDialog.open=true;
            }
        }
        ImGui::SameLine(0,G);

        if (ImGui::Button(inFav?"[*] Favorited":"[+] Favorite",{AW,BH})&&!vds.videoId.empty())
            inFav?PersistentData::RemoveFromPlaylist("favorites",vds.videoId)
                 :PersistentData::AddToPlaylist("favorites",vds.videoId,vds.title);
        ImGui::SameLine(0,G);

        if (ImGui::Button("[^] Window",{AW,BH})&&!vds.streamUrl.empty()&&!streamError&&!vds.popup.open) {
            double capturedPos=vds.player.GetPosition();
            if (vds.player.GetState()==PlayerState::Playing) vds.player.Pause();
            VD_RegisterClass(L"CTVideoPopupFrame",DefWindowProcW,(HBRUSH)GetStockObject(BLACK_BRUSH),CS_HREDRAW|CS_VREDRAW);
            HWND pw=CreateWindowExW(WS_EX_TOPMOST,L"CTVideoPopupFrame",L"Sightline - Mini Player",
                WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,800,500,
                NULL,NULL,GetModuleHandle(NULL),NULL);
            if (pw) {
                RECT fr; GetClientRect(pw,&fr);
                int fw=fr.right-fr.left, fh=fr.bottom-fr.top;
                VD_RegisterClass(L"CTVideoPanel2",VD_PanelProc,(HBRUSH)GetStockObject(BLACK_BRUSH),CS_HREDRAW|CS_VREDRAW);
                HWND videoChild=CreateWindowExW(0,L"CTVideoPanel2",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
                    0,0,fw,fh,pw,NULL,GetModuleHandle(NULL),NULL);
                VD_RegisterClass(L"CTVideoPopupOverlay",VD_PopupOverlayProc,(HBRUSH)GetStockObject(NULL_BRUSH),CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS);
                HWND ovChild=CreateWindowExW(WS_EX_LAYERED,L"CTVideoPopupOverlay",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
                    0,0,fw,fh,pw,NULL,GetModuleHandle(NULL),NULL);
                if (ovChild) {
                    SetLayeredWindowAttributes(ovChild,0,1,LWA_ALPHA);
                    SetWindowPos(ovChild,HWND_TOP,0,0,fw,fh,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                }
                vds.popup.startPos=capturedPos;
                vds.popup.seekPending=true; vds.popup.seekDone=false;
                vds.popup.player=new VLCPlayer();
                vds.popup.hwnd=pw; vds.popup.videoChild=videoChild; vds.popup.overlayHwnd=ovChild;
                vds.popup.open=true;
                vds.popup.overlayUD.player=vds.popup.player;
                if (ovChild) SetWindowLongPtr(ovChild,GWLP_USERDATA,(LONG_PTR)&vds.popup.overlayUD);
                vds.popup.player->Init(videoChild?videoChild:pw);
                vds.popup.player->SetVolume(vds.volume);
                std::string pu=vds.qualities.empty()?vds.streamUrl:VD_BuildUrl(vds.qualities[vds.qualityIdx]);
                vds.popup.player->Open(pu);
            }
        }
        ImGui::SameLine(0,G);
        if (ImGui::Button("[/] Fullscreen",{AW,BH})) vds.fullscreen=true;

        ImGui::PopStyleColor(3);
        ImGui::SetCursorPosY(rowY+ACT_H);
    }

    ImGui::EndChild(); // ##vd_top

    // =====================================================================
    // BOTTOM ZONE
    // =====================================================================
    ImGui::SetCursorPos({0,topZoneH});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleColor(ImGuiCol_ChildBg,{.14f,.14f,.14f,1});
    ImGui::BeginChild("##vd_bot",{LW,botH},false);
    ImGui::PopStyleColor(); ImGui::PopStyleVar();

    ImGui::Spacing(); ImGui::SetCursorPosX(PAD);
    ImGui::PushTextWrapPos(LW-PAD);
    ImGui::PushStyleColor(ImGuiCol_Text,Theme::COL_TEXT);
    ImGui::TextWrapped("%s",vds.title.empty()?"No title":vds.title.c_str());
    ImGui::PopStyleColor(); ImGui::PopTextWrapPos();
    ImGui::Spacing(); ImGui::SetCursorPosX(0); ImGui::Separator(); ImGui::Spacing();

    {
        ImGui::SetCursorPosX(PAD);
        const char* chName=vds.channelName.empty()?"Unknown channel":vds.channelName.c_str();
        bool sub=!vds.channelId.empty()&&PersistentData::IsSubscribed(vds.channelId);
        const char* subLbl=sub?"[v] Subscribed":"[+] Subscribe";
        float chW=ImGui::CalcTextSize(chName).x+20.f;
        float subW=ImGui::CalcTextSize(subLbl).x+20.f;
        ImGui::PushStyleColor(ImGuiCol_Button,       {.12f,.12f,.12f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.22f,.22f,.22f,1});
        ImGui::PushStyleColor(ImGuiCol_Text,         Theme::COL_ACCENT_V4);
        if (ImGui::Button(chName,{chW,BH})&&!vds.channelId.empty()) {
            state.prevPage=AppPage::VideoDetail;
            state.pendingChannelId=vds.channelId;
            state.pendingChannelName=vds.channelName;
            state.activePage=AppPage::Channel;
        }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0,8);
        ImGui::PushStyleColor(ImGuiCol_Button,sub?ImVec4{.1f,.38f,.1f,1}:ImVec4{.18f,.18f,.18f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_V4);
        if (ImGui::Button(subLbl,{subW,BH})&&!vds.channelId.empty())
            sub?PersistentData::Unsubscribe(vds.channelId):PersistentData::Subscribe(vds.channelId,vds.channelName);
        ImGui::PopStyleColor(2);
        char meta[128]={};
        if (!vds.viewCount.empty()&&!vds.duration.empty())
            snprintf(meta,sizeof(meta),"%s  -  %s",vds.viewCount.c_str(),vds.duration.c_str());
        else if (!vds.viewCount.empty())
            snprintf(meta,sizeof(meta),"%s",vds.viewCount.c_str());
        if (meta[0]) { ImGui::SetCursorPosX(PAD); ImGui::TextColored(Theme::COL_TEXT_DIM_V4,"%s",meta); }
        ImGui::Spacing();
    }

    // Tab bar
    {
        ImGui::SetCursorPosX(PAD);
        float tabW=(LW-PAD*2.f-2.f)/(float)(int)VideoDetailTab::COUNT;
        for (int i=0;i<(int)VideoDetailTab::COUNT;i++) {
            if (i>0) ImGui::SameLine(0,2);
            if (Widgets::TabButton(kVDTabs[i],(int)vds.activeTab==i,tabW)) {
                vds.activeTab=(VideoDetailTab)i;
                if (vds.activeTab == VideoDetailTab::Comments && !vds.videoId.empty()) {
                    vds.commentsTabOpened = true;
                    if (vds.commentsLoadedForVideoId != vds.videoId &&
                        !state.commentsResolving.load())
                    {
                        vds.commentsLoadedForVideoId = vds.videoId;
                        VD_RequestComments(state, vds.videoId);
                    }
                }
            }
        }
        ImGui::Spacing(); ImGui::SetCursorPosX(0); ImGui::Separator(); ImGui::Spacing();
    }

    ImGui::SetCursorPosX(PAD);
    ImGui::PushTextWrapPos(LW-PAD*2.f);
    switch(vds.activeTab) {
    case VideoDetailTab::Description:
        if (vds.description.empty())
            ImGui::TextColored(Theme::COL_TEXT_DIM_V4,"Loading description...");
        else {
            ImGui::PushStyleColor(ImGuiCol_Text,Theme::COL_TEXT);
            ImGui::TextUnformatted(vds.description.c_str());
            ImGui::PopStyleColor();
        }
        break;
    case VideoDetailTab::Comments:
        ImGui::PopTextWrapPos();
        VD_DrawCommentsTab(state, vds, LW, PAD);
        ImGui::PushTextWrapPos(LW-PAD*2.f);
        break;
    default: break;
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::EndChild(); // ##vd_bot

    // =====================================================================
    // RIGHT COLUMN — Up Next
    // =====================================================================
    if (wide) {
        ImGui::SetCursorPos({LW+PAD,0});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,{.13f,.13f,.13f,1});
        ImGui::BeginChild("##vd_right",{RW,h},false);
        ImGui::PopStyleColor();
        ImGui::Spacing(); ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_ACCENT_V4,"Up Next");
        ImGui::Separator(); ImGui::Spacing();
        VD_DrawRelatedPanel(state, vds, RW, PAD);
        ImGui::EndChild();
    }

    ImGui::End(); // ##vd

    // =====================================================================
    // FULLSCREEN OVERLAY CONTROLS
    // =====================================================================
    if (vds.fullscreen) {
        double now=(double)GetTickCount()/1000.0;
        ImGuiIO& io=ImGui::GetIO();
        if (io.MouseDelta.x!=0.f||io.MouseDelta.y!=0.f) vds.lastMouseMove=now;
        float el=(float)(now-vds.lastMouseMove);
        vds.fsCtrlVisible=el<2.5f;
        if (vds.fsCtrlVisible) {
            float alpha=el>1.5f?1.f-(el-1.5f):1.f; if(alpha<0)alpha=0;
            float fW=(float)panelW, cbH=96.f;
            float cbX=(float)panelX, cbY=(float)(panelY+panelH)-cbH;
            ImGui::SetNextWindowPos({cbX,cbY});
            ImGui::SetNextWindowSize({fW,cbH});
            ImGui::SetNextWindowBgAlpha(.78f*alpha);
            ImGui::Begin("##fs_ctrl",NULL,
                ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
                ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoScrollbar|
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImDrawList* fsdl=ImGui::GetWindowDrawList();
            float sbW2=fW-20.f;
            float nr2=VD_Seekbar(10.f,6.f,sbW2,
                canSeek?(float)(pos/dur):0.f,vds.player.GetBufferPct(),canSeek,fsdl,"##fssb");
            if (nr2>=0.f) vds.player.SeekTo((double)nr2*dur);
            ImGui::SetCursorPos({10.f,6.f+SB_KR*2.f+SB_SH+6.f});
            ImGui::PushStyleColor(ImGuiCol_Button,       {.18f,.18f,.18f,1});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.3f,.3f,.3f,1});
            if (ImGui::Button("|<",{26,26})) vds.player.SeekTo(0);
            ImGui::SameLine(0,4);
            if (ImGui::Button("<<",{26,26})) { double np=pos-10; vds.player.SeekTo(np<0?0:np); }
            ImGui::SameLine(0,4);
            if (ImGui::Button(isPlaying?" || ":" >  ",{40,26}))
                isPlaying?vds.player.Pause():vds.player.Play();
            ImGui::SameLine(0,4);
            if (ImGui::Button(">>",{26,26})) { double np=pos+10; vds.player.SeekTo(np>dur?dur:np); }
            ImGui::SameLine(0,4);
            if (ImGui::Button(">|",{26,26})) vds.player.SeekTo(dur);
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0,16);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,{1,1,1,1});
            ImGui::PushStyleColor(ImGuiCol_FrameBg,   {.2f,.2f,.2f,1});
            ImGui::SetNextItemWidth(80);
            int vol2=vds.volume;
            if (ImGui::SliderInt("##fsvol",&vol2,0,100,"%d%%")) { vds.volume=vol2; vds.player.SetVolume(vol2); }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0,12);
            ImGui::PushStyleColor(ImGuiCol_Button,{.5f,.1f,.1f,1});
            if (ImGui::Button("[X] Exit FS",{90,26})) vds.fullscreen=false;
            ImGui::PopStyleColor();
            ImGui::End();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) vds.fullscreen=false;
    }

    // =====================================================================
    // POPUP (PiP) PLAYER
    // =====================================================================
    if (vds.popup.open) {
        if (vds.popup.hwnd&&IsWindow(vds.popup.hwnd)&&vds.popup.player) {
            if (vds.popup.seekPending&&!vds.popup.seekDone) {
                if (vds.popup.player->GetState()==PlayerState::Playing) {
                    if (vds.popup.startPos>1.0) vds.popup.player->SeekTo(vds.popup.startPos);
                    vds.popup.seekDone=true;
                }
            }
        } else {
            double resumePos=vds.popup.player?vds.popup.player->GetPosition():0.0;
            VD_DestroyPopup(vds.popup);
            vds.player.SeekTo(resumePos);
            vds.player.Play();
        }
    }

    // =====================================================================
    // DOWNLOAD DIALOG
    // =====================================================================
    DrawDownloadDialog(vds.dlDialog,mainHwnd);
}
