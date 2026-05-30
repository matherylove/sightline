#pragma once
// ViewVideoDetail - adaptive 2-zone layout
// Original features fully preserved from commit 4da495.
// New features added: VP9 quality selector, seek-on-quality-change, English UI,
//                     Comments tab (InnerTube -> yt-dlp fallback, Load More),
//                     Up Next panel (GetRelatedVideos async).
// GUI-FIXES: FA6 icons, volume label, Subscribe fixed-width, title hierarchy,
//            seekbar hit-target, Up Next header.
// GUI-FIXES-2: uniform action-btn widths, consistent PAD on all panels,
//              channel < title font hierarchy, meta line dimmed+separated,
//              status dot tooltip always visible, sidebar item gap+separator,
//              Back btn vertically centred, tab equal padding, view-count
//              uses COL_TEXT_DIM_V4, related item channel/views use FAINT.
// GUI-FIXES-3: fix PopFont() assert (removed mismatched PushFont/PopFont around
//              title), removed debug red rect around description panel, home
//              btn left-padding restored, vol slider styled consistently with
//              seekbar (thin frame, no thumb square), status dot has proper
//              right-margin before vol block, sidebar search deduplicated
//              (no double search bar in Up Next), btn play width = other btns.
// GUI-FIXES-4: header title ellipsis, Back btn baseline-aligned with title,
//              tab bar uses underline indicator style (not filled bg),
//              Subscribe btn accent-filled like a real CTA, meta line has
//              explicit top-margin spacing, sidebar thin scrollbar, related
//              panel item gap increased, description text has proper left PAD,
//              transport row items vertically centred consistently.
// GUI-FIXES-5: keyboard shortcuts (Space/arrows/F/M), Share+Browser btns,
//              popup player render, prevVol moved to struct, like icon,
//              sidebar skeleton shimmer, related list capped at 50.

#include "../AppState.h"
#include "../Widgets.h"
#include "../Theme.h"
#include "../ThumbnailCache.h"
#include "../../player/VLCPlayer.h"
#include "../../config/PersistentData.h"
#include "../../transcoder/FfmpegBootstrap.h"
#include "DownloadDialog.h"
#include "IconsFontAwesome6.h"
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

    // Volume mute/restore
    int prevVol = 80;
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

// ---------------------------------------------------------------------------
// Custom seekbar — larger hit target (SH=6, KR=7), teal accent fill
// ---------------------------------------------------------------------------
static float VD_Seekbar(float cx, float cy, float sw,
                         float pf, float bf, bool canSeek,
                         ImDrawList* dl, const char* id)
{
    const float SH=6.f, KR=7.f;
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
    dl->AddRectFilled({sx,sy},{sx+sw*pf,sy+SH},Theme::U32_ACCENT(),3);
    float kx=sx+sw*pf, ky=sy+SH*.5f;
    dl->AddCircleFilled({kx,ky},KR,ImGui::ColorConvertFloat4ToU32(Theme::COL_ACCENT_HOV_V4));
    dl->AddCircle      ({kx,ky},KR,IM_COL32(0,0,0,80),16,1.2f);
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
    HANDLE hThread = CreateThread(NULL, 0, VD_Co