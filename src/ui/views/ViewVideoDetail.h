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
    double        openTime    = 0.0;  // FIX 4: for seek timeout
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

// FIX 3: preserve "Auto (Best)" at index 0 when VP9 options arrive
static inline void VD_ApplyVP9Qualities(VideoDetailState& vds,
    const std::vector<QualityOption>& opts)
{
    if (opts.empty()) return;
    // Always preserve the Auto entry (index 0) — append VP9 options after it
    QualityOption autoEntry;
    if (!vds.qualities.empty()) {
        autoEntry = vds.qualities[0]; // keep "Auto (Best)" with its URLs
    } else {
        autoEntry.label = "Auto (Best)";
    }
    vds.qualities.clear();
    vds.qualities.push_back(autoEntry); // index 0 = Auto always
    for (const auto& q : opts) {
        QualityOption o = q;
        if (o.audioUrl.empty()) o.audioUrl = autoEntry.audioUrl;
        vds.qualities.push_back(o);
    }
    // qualityIdx stays at 0 (Auto) after a new video loads
    vds.qualityIdx = 0;
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

        // FIX 1: dl->AddText + PushClipRect keeps text inside item row bounds
        {
            ImVec2 clipMin = {rowStart.x + TEXT_X, rowStart.y};
            ImVec2 clipMax = {rowStart.x + panelW - PAD, rowStart.y + THUMB_H + ITEM_PAD * 2.f};
            dl->PushClipRect(clipMin, clipMax, true);

            float lineH = ImGui::GetTextLineHeight();
            float ty    = rowStart.y + ITEM_PAD;

            std::string displayTitle = it.title.empty() ? "(no title)" : it.title;
            if ((int)displayTitle.size() > 60) displayTitle = displayTitle.substr(0, 57) + "...";
            ImU32 titleCol = canPlay
                ? ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT)
                : IM_COL32(120, 120, 120, 255);
            dl->AddText({rowStart.x + TEXT_X, ty}, titleCol, displayTitle.c_str());

            if (!it.channelName.empty())
                dl->AddText({rowStart.x + TEXT_X, ty + lineH + 2.f},
                    ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT_DIM_V4),
                    it.channelName.c_str());

            if (!it.viewCount.empty())
                dl->AddText({rowStart.x + TEXT_X, ty + lineH * 2.f + 4.f},
                    ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT_DIM_V4),
                    it.viewCount.c_str());

            dl->PopClipRect();
        }

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
                               state.pendin