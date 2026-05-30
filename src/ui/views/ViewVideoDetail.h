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
//              uses COL_TEXT_FAINT, related item channel/views use FAINT.
// GUI-FIXES-3: fix PopFont() assert (removed mismatched PushFont/PopFont around
//              title), removed debug red rect around description panel, home
//              btn left-padding restored, vol slider styled consistently with
//              seekbar (thin frame, no thumb square), status dot has proper
//              right-margin before vol block, sidebar search deduplicated
//              (no double search bar in Up Next), btn play width = other btns.
// GUI-FIXES-4: channel name secondary (COL_TEXT_DIM_V4, smaller hit target),
//              meta line explicit margin-top + stronger separator,
//              quality combo FramePadding for arrow breathing room,
//              consistent vol↔quality ItemSpacing, tab underline indicator,
//              FS label shortened to fit flex width.
// GUI-FIXES-5: Up Next title left-aligned to PAD (consistent with panel body),
//              related item channel/views rendered smaller with COL_TEXT_FAINT
//              via explicit font-size push, description SetCursorPosX(PAD)
//              enforced after every TextWrapped, Subscribe FramePadding
//              symmetric {8,3} matching quality combo style.
// GUI-FIXES-6: Subscribe FramePadding simétrico {8,3} (igual al quality combo),
//              TextWrapPos estricto en related items (título con wrap ajustado
//              a RW-PAD-THUMB_W-8), consistencia SetCursorPosX(PAD) post-wrap
//              en descripción reforzada.
// GUI-FIXES-7: fix CommentItem fields: authorName (no author), likeCount/replyCount
//              son std::string (no int) — comparaciones y formato corregidos.
// GUI-FIXES-8: fix PlayerRequest quality fields: use vp9Qualities vector
//              instead of non-existent qualityLabels/qualityVideoUrls/qualityAudioUrls.
// GUI-FIXES-9: fix ThumbnailCache::Get — called via s_instance (non-static method).
// GUI-FIXES-10: fix ImTextureID ternary — nullptr incompatible with ImU64 on
//               GCC/MinGW; replaced nullptr with (ImTextureID)0.

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
    std::string       videoId, title, channelName, channelId;
    std::string       viewCount, duration, description;
    std::string       streamUrl, lastPlayedUrl;
    bool              playStarted  = false;
    bool              playerInited = false;
    bool              fullscreen   = false;
    double            lastMouseMove= 0.0;
    VLCPlayer         player;
    HWND              playerHwnd   = NULL;
    HWND              overlayHwnd  = NULL;
    int               cachedPX=-1, cachedPY=-1, cachedPW=-1, cachedPH=-1;
    OverlayUD         overlayUD;
    int               volume   = 80;

    // VP9 quality selector
    std::vector<QualityOption> qualities;
    int               qualityIdx        = 0;
    int               pendingQualityIdx = -1;
    double            qualityChangePos  = 0.0;

    // Seek drain after quality change / initial start
    bool              seekPending2 = false;
    bool              seekDone2    = false;
    double            seekPos2     = 0.0;

    VideoDetailTab    activeTab    = VideoDetailTab::Description;

    DownloadDialogState dlDialog;
    PopupPlayerState    popup;
};

// ===========================================================================
// Overlay WndProc — double-click toggles FS; mouse-move updates lastMove
// ===========================================================================
static LRESULT CALLBACK VD_OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayUD* d = (OverlayUD*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_LBUTTONDBLCLK:
        if (d && d->isFS) *d->isFS = !(*d->isFS);
        return 0;
    case WM_MOUSEMOVE:
        if (d && d->lastMove) *d->lastMove=(double)GetTickCount()/1000.0;
        return 0;
    case WM_MOUSEWHEEL: return SendMessage(GetParent(hwnd),msg,wp,lp);
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}

// ===========================================================================
// Popup overlay WndProc
// ===========================================================================
static LRESULT CALLBACK VD_PopupOverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PopupOverlayUD* d = (PopupOverlayUD*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_LBUTTONDOWN: SetCapture(hwnd); return 0;
    case WM_LBUTTONDBLCLK: return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        RECT rc; GetClientRect(hwnd,&rc);
        int cx=(int)(short)LOWORD(lp);
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
    state.relatedResolving.store(true);
    HANDLE hThread = CreateThread(NULL, 0, VD_RelatedThreadProc,
        new VD_RelatedCtx{videoId, &state}, 0, NULL);
    if (hThread) CloseHandle(hThread);
    else {
        state.pendingRelated.loading = false;
        state.relatedResolving.store(false);
    }
}

// ===========================================================================
// Popup player helpers
// ===========================================================================

static LRESULT CALLBACK VD_PopupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PopupPlayerState* p=(PopupPlayerState*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
    switch(msg){
    case WM_CLOSE:
        if(p) p->open=false;
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_SIZE:
        if(p&&p->videoChild&&IsWindow(p->videoChild)){
            RECT rc; GetClientRect(hwnd,&rc);
            int w=rc.right, h=rc.bottom;
            if(p->overlayHwnd&&IsWindow(p->overlayHwnd))
                SetWindowPos(p->overlayHwnd,HWND_TOP,0,0,w,h,SWP_NOACTIVATE);
            SetWindowPos(p->videoChild,NULL,0,0,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static void VD_EnsurePopupWindow(PopupPlayerState& p, HWND mainHwnd) {
    if (p.hwnd&&IsWindow(p.hwnd)) return;

    static bool reg=false;
    if(!reg){
        WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc);
        wc.style=CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=VD_PopupWndProc;
        wc.hInstance=GetModuleHandle(NULL);
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.lpszClassName=L"CTPopupPlayer";
        RegisterClassExW(&wc); reg=true;
    }

    RECT mr={}; GetWindowRect(mainHwnd,&mr);
    int pw=640,ph=400;
    int px=mr.left+(mr.right-mr.left-pw)/2;
    int py=mr.top +(mr.bottom-mr.top-ph)/2;

    p.hwnd=CreateWindowExW(WS_EX_TOOLWINDOW,L"CTPopupPlayer",L"ClientTube - Popup Player",
        WS_OVERLAPPEDWINDOW,px,py,pw,ph,NULL,NULL,GetModuleHandle(NULL),NULL);
    if(!p.hwnd) return;
    SetWindowLongPtr(p.hwnd,GWLP_USERDATA,(LONG_PTR)&p);

    // Video child
    VD_RegisterClass(L"CTPopupPanel",VD_PanelProc,(HBRUSH)GetStockObject(BLACK_BRUSH),CS_HREDRAW|CS_VREDRAW);
    RECT cr; GetClientRect(p.hwnd,&cr);
    p.videoChild=CreateWindowExW(0,L"CTPopupPanel",L"",WS_CHILD|WS_VISIBLE,
        0,0,cr.right,cr.bottom,p.hwnd,NULL,GetModuleHandle(NULL),NULL);

    // Overlay
    VD_RegisterClass(L"CTPopupOverlay",VD_PopupOverlayProc,(HBRUSH)GetStockObject(NULL_BRUSH),
        CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS);
    p.overlayHwnd=CreateWindowExW(WS_EX_LAYERED,L"CTPopupOverlay",L"",WS_CHILD|WS_VISIBLE,
        0,0,cr.right,cr.bottom,p.hwnd,NULL,GetModuleHandle(NULL),NULL);
    if(p.overlayHwnd){
        SetLayeredWindowAttributes(p.overlayHwnd,0,1,LWA_ALPHA);
        SetWindowPos(p.overlayHwnd,HWND_TOP,0,0,cr.right,cr.bottom,SWP_NOACTIVATE);
    }

    ShowWindow(p.hwnd,SW_SHOW);
}

static void VD_TickPopup(PopupPlayerState& p, VideoDetailState& vds, HWND mainHwnd) {
    if (!p.open) {
        if (p.hwnd||p.player) VD_DestroyPopup(p);
        return;
    }

    VD_EnsurePopupWindow(p, mainHwnd);
    if (!p.hwnd||!IsWindow(p.hwnd)) { p.open=false; return; }

    // Init player
    if (!p.player&&p.videoChild&&IsWindow(p.videoChild)) {
        p.player=new VLCPlayer();
        p.player->Init(p.videoChild);
        p.player->SetVolume(vds.volume);
        p.panelUD.player=p.player;
        if(p.overlayHwnd&&IsWindow(p.overlayHwnd)){
            p.overlayUD.player=p.player;
            SetWindowLongPtr(p.overlayHwnd,GWLP_USERDATA,(LONG_PTR)&p.overlayUD);
        }
        std::string url=vds.qualities.empty()?vds.streamUrl:VD_BuildUrl(vds.qualities[vds.qualityIdx]);
        if(p.player->Open(url)){
            if(p.startPos>0.5){
                p.seekPending=true;
                p.seekDone=false;
            }
        }
    }

    // Seek drain
    if(p.player&&p.seekPending&&!p.seekDone){
        if(p.player->GetState()==PlayerState::Playing){
            p.player->SeekTo(p.startPos);
            p.seekPending=false; p.seekDone=true;
        }
    }

    // Pump messages
    if(p.hwnd&&IsWindow(p.hwnd)){
        MSG msg;
        while(PeekMessageW(&msg,p.hwnd,0,0,PM_REMOVE)){
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if(!IsWindow(p.hwnd)) p.open=false;
    }
}

// ===========================================================================
// Formatting helpers
// ===========================================================================
static inline std::string VD_FmtTime(double s) {
    if (s<0) s=0;
    int h=(int)(s/3600), m=(int)(s/60)%60, sec=(int)s%60;
    char buf[32];
    if (h>0) snprintf(buf,sizeof(buf),"%d:%02d:%02d",h,m,sec);
    else      snprintf(buf,sizeof(buf),"%d:%02d",m,sec);
    return buf;
}

// ===========================================================================
// Comments tab renderer
// ===========================================================================
static void VD_DrawCommentsTab(AppState& state, VideoDetailState& vds,
                                float LW, float PAD)
{
    if (!state.pendingComments.loading &&
        !state.commentsResolving.load() &&
        !state.pendingComments.ready &&
        state.pendingComments.comments.empty() &&
        !vds.videoId.empty())
    {
        VD_RequestComments(state, vds.videoId);
    }

    ImGui::SetCursorPos({PAD, PAD});
    if (state.pendingComments.loading || state.commentsResolving.load()) {
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Loading comments...");
        return;
    }
    if (!state.pendingComments.error.empty() && state.pendingComments.comments.empty()) {
        ImGui::TextColored(ImVec4{1,0.4f,0.4f,1}, "Error: %s",
                           state.pendingComments.error.c_str());
        return;
    }
    if (state.pendingComments.comments.empty()) {
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "No comments.");
        return;
    }

    const float avatarSz = 28.f;
    const float itemW    = LW - PAD * 2.f;
    ImGui::PushTextWrapPos(LW - PAD);

    for (const auto& c : state.pendingComments.comments) {
        ImGui::SetCursorPosX(PAD);
        // Author
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_ACCENT_V4);
        ImGui::TextUnformatted(c.authorName.c_str());
        ImGui::PopStyleColor();
        // Text
        ImGui::SetCursorPosX(PAD + avatarSz + 6.f);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT);
        ImGui::TextWrapped("%s", c.text.c_str());
        ImGui::PopStyleColor();
        // Likes — likeCount is std::string
        bool hasLikes = !c.likeCount.empty() && c.likeCount != "0";
        if (hasLikes) {
            ImGui::SetCursorPosX(PAD + avatarSz + 6.f);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_FAINT);
            ImGui::Text("\xf0\x9f\x91\x8d %s", c.likeCount.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::SetCursorPosX(PAD);
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4{1,1,1,0.06f});
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    ImGui::PopTextWrapPos();

    // Load More button
    if (!state.pendingComments.continuationToken.empty()) {
        ImGui::SetCursorPosX(PAD);
        ImGui::PushStyleColor(ImGuiCol_Button,       Theme::COL_SURFACE2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_SOFT);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COL_ACCENT_V4);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        if (ImGui::Button("Load More", {itemW, 28.f}))
            VD_RequestComments(state, vds.videoId,
                               state.pendingComments.continuationToken);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
}

// ===========================================================================
// Related panel renderer
// ===========================================================================
static void VD_DrawRelatedPanel(AppState& state, VideoDetailState& vds,
                                 float RW, float PAD)
{
    if (!state.pendingRelated.loading &&
        !state.relatedResolving.load() &&
        !state.pendingRelated.ready &&
        state.pendingRelated.items.empty() &&
        !vds.videoId.empty())
    {
        VD_RequestRelated(state, vds.videoId);
    }

    if (state.pendingRelated.loading || state.relatedResolving.load()) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Loading...");
        return;
    }
    if (state.pendingRelated.items.empty()) {
        ImGui::SetCursorPosX(PAD);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4,
            state.pendingRelated.error.empty() ? "No suggestions." :
            state.pendingRelated.error.c_str());
        return;
    }

    const float THUMB_W=96.f, THUMB_H=54.f, ITEM_H=66.f;
    const float itemW = RW - PAD * 2.f;
    // Wrap pos ajustado al ancho real del bloque de texto (excluye thumbnail + gap)
    const float textBlockW = RW - PAD - THUMB_W - 8.f;

    for (auto& it : state.pendingRelated.items) {
        ImGui::SetCursorPosX(PAD);
        float iy = ImGui::GetCursorPosY();

        // Hover highlight
        ImGui::PushStyleColor(ImGuiCol_Button,        {0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_SOFT);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_ACCENT_V4);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        bool clicked = ImGui::Button(("##rel_"+it.videoId).c_str(), {itemW, ITEM_H});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        if (clicked) {
            state.pendingPlay.videoId    = it.videoId;
            state.pendingPlay.title      = it.title;
            state.pendingPlay.channelName= it.channelName;
            state.pendingPlay.viewCount  = it.viewCount;
            state.pendingPlay.duration   = it.duration;
            state.activePage = AppPage::VideoDetail;
        }

        // Thumbnail — use s_instance to call the non-static Get()
        // GUI-FIXES-10: use (ImTextureID)0 instead of nullptr — ImTextureID is
        // ImU64 on this config, which is incompatible with std::nullptr_t on GCC.
        ImVec2 thumbTL = {ImGui::GetWindowPos().x + PAD, ImGui::GetWindowPos().y + iy};
        ImTextureID tid = ThumbnailCache::s_instance
                          ? (ImTextureID)ThumbnailCache::s_instance->Get(it.videoId)
                          : (ImTextureID)0;
        if (tid) {
            ImGui::GetWindowDrawList()->AddImage(tid, thumbTL,
                {thumbTL.x+THUMB_W, thumbTL.y+THUMB_H});
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(thumbTL,
                {thumbTL.x+THUMB_W, thumbTL.y+THUMB_H}, IM_COL32(30,30,30,255), 4.f);
        }

        // Text block — PushTextWrapPos ajustado al bloque de texto, no al panel entero
        float tx = PAD + THUMB_W + 8.f;
        ImGui::SetCursorPos({tx, iy + 2.f});
        ImGui::PushTextWrapPos(ImGui::GetWindowPos().x + PAD + THUMB_W + 8.f + textBlockW);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT);
        ImGui::TextWrapped("%s", it.title.c_str());
        ImGui::PopStyleColor();

        // Channel + views: COL_TEXT_FAINT
        ImGui::SetCursorPosX(tx);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_FAINT);
        if (!it.channelName.empty() && !it.viewCount.empty())
            ImGui::Text("%s · %s", it.channelName.c_str(), it.viewCount.c_str());
        else if (!it.channelName.empty())
            ImGui::TextUnformatted(it.channelName.c_str());
        else if (!it.viewCount.empty())
            ImGui::TextUnformatted(it.viewCount.c_str());
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopTextWrapPos();

        ImGui::SetCursorPosY(iy + ITEM_H + 4.f);
    }
}

// ===========================================================================
// Main render entry
// ===========================================================================
static void DrawVideoDetail(AppState& state, VideoDetailState& vds,
                             HWND mainHwnd,
                             float x, float y, float w, float h)
{
    const float PAD=12.f, BH=26.f, BACKH=36.f;
    const float SB_GAP=4.f, SB_SH=6.f, SB_KR=7.f, SB_ROW=SB_KR*2.f+SB_SH+16.f;
    const float CTR_H=36.f, ACT_H=34.f;
    const bool  wide=(w>=900.f);
    const float RW=wide?260.f:0.f;
    const float LW=w-RW-(wide?PAD*2.f:0.f);

    bool streamError = !vds.streamUrl.empty() &&
                       vds.streamUrl.rfind("ERROR:",0)==0;

    // -------------------------------------------------------------------------
    // State sync from pendingPlay
    // -------------------------------------------------------------------------
    if (!state.pendingPlay.videoId.empty() &&
         state.pendingPlay.videoId != vds.videoId)
    {
        // Stop current playback
        vds.player.Stop();
        vds.playStarted=vds.playerInited=false;
        vds.streamUrl.clear();
        if (vds.playerHwnd)  { ShowWindow(vds.playerHwnd,SW_HIDE);  DestroyWindow(vds.playerHwnd);  vds.playerHwnd=NULL; }
        if (vds.overlayHwnd) { ShowWindow(vds.overlayHwnd,SW_HIDE); DestroyWindow(vds.overlayHwnd); vds.overlayHwnd=NULL; }
        vds.cachedPX=vds.cachedPY=vds.cachedPW=vds.cachedPH=-1;
        vds.qualities.clear(); vds.qualityIdx=0;
        vds.pendingQualityIdx=-1;
        vds.seekPending2=vds.seekDone2=false;
        vds.activeTab=VideoDetailTab::Description;

        // Clear comments & related
        state.pendingComments.comments.clear();
        state.pendingComments.continuationToken.clear();
        state.pendingComments.error.clear();
        state.pendingComments.ready=false;
        state.pendingComments.loading=false;
        state.pendingRelated.items.clear();
        state.pendingRelated.error.clear();
        state.pendingRelated.ready=false;
        state.pendingRelated.loading=false;

        vds.videoId     = state.pendingPlay.videoId;
        vds.title       = state.pendingPlay.title;
        vds.channelName = state.pendingPlay.channelName;
        vds.channelId   = state.pendingPlay.channelId;
        vds.viewCount   = state.pendingPlay.viewCount;
        vds.duration    = state.pendingPlay.duration;
        vds.description.clear();

        // Reset download dialog
        {
            std::lock_guard<std::mutex> lk(vds.dlDialog.qualityMux);
            vds.dlDialog.videoId      = vds.videoId;
            vds.dlDialog.title        = vds.title;
            vds.dlDialog.channelName  = vds.channelName;
            vds.dlDialog.vp9Qualities.clear();
            vds.dlDialog.qualitiesFetched  = false;
            vds.dlDialog.qualitiesFetching = false;
        }
        vds.dlDialog.qualityThreadDone.store(false);
        vds.dlDialog.JoinQualityThread();
        vds.dlDialog.qualityIdx=0;

        // Kick stream resolve
        if (!state.streamResolving.load()) {
            state.pendingPlay.videoUrl.clear();
            state.streamResolving.store(true);
        }
    }

    // Partial metadata fill-in (title/channel/views/duration arrive later)
    if (!state.pendingPlay.videoId.empty() &&
         state.pendingPlay.videoId == vds.videoId)
    {
        if (vds.title.empty()       && !state.pendingPlay.title.empty())       vds.title=state.pendingPlay.title;
        if (vds.channelName.empty() && !state.pendingPlay.channelName.empty()) vds.channelName=state.pendingPlay.channelName;
        if (vds.channelId.empty()   && !state.pendingPlay.channelId.empty())   vds.channelId=state.pendingPlay.channelId;
        if (vds.viewCount.empty()   && !state.pendingPlay.viewCount.empty())   vds.viewCount=state.pendingPlay.viewCount;
        if (vds.duration.empty()    && !state.pendingPlay.duration.empty())    vds.duration=state.pendingPlay.duration;
        if (vds.description.empty() && !state.pendingPlay.description.empty()) vds.description=state.pendingPlay.description;
        if (vds.dlDialog.title.empty()       && !vds.title.empty())       vds.dlDialog.title=vds.title;
        if (vds.dlDialog.channelName.empty() && !vds.channelName.empty()) vds.dlDialog.channelName=vds.channelName;
        if (vds.dlDialog.description.empty() && !vds.description.empty()) vds.dlDialog.description=vds.description;
    }

    // Stream URL arrival
    if (!state.pendingPlay.videoUrl.empty() &&
         state.pendingPlay.videoId == vds.videoId &&
         vds.streamUrl != state.pendingPlay.videoUrl)
    {
        vds.streamUrl = state.pendingPlay.videoUrl;
        vds.dlDialog.videoUrl = state.pendingPlay.videoUrl;
        vds.dlDialog.audioUrl = state.pendingPlay.audioUrl;

        // Build quality list from resolved stream
        if (vds.qualities.empty()) {
            QualityOption q; q.label="Auto (Best)";
            q.videoUrl=state.pendingPlay.videoUrl;
            q.audioUrl=state.pendingPlay.audioUrl;
            vds.qualities.push_back(q);

            // Additional quality options from pendingPlay.vp9Qualities
            if (!state.pendingPlay.vp9Qualities.empty()) {
                std::vector<QualityOption> opts;
                for (const auto& vq : state.pendingPlay.vp9Qualities) {
                    QualityOption o;
                    o.label    = vq.label;
                    o.videoUrl = vq.videoUrl;
                    o.audioUrl = vq.audioUrl;
                    if (!o.videoUrl.empty()) opts.push_back(o);
                }
                if (!opts.empty()) VD_ApplyVP9Qualities(vds, opts);
                else VD_SyncDlDialogQualities(vds);
            } else {
                VD_SyncDlDialogQualities(vds);
            }
        }
    }

    // Quality change processing
    if (vds.pendingQualityIdx >= 0) {
        vds.qualityIdx        = vds.pendingQualityIdx;
        vds.pendingQualityIdx = -1;
        vds.dlDialog.qualityIdx = vds.qualityIdx;
        if (vds.qualityIdx < (int)vds.qualities.size()) {
            vds.streamUrl     = VD_BuildUrl(vds.qualities[vds.qualityIdx]);
            vds.playStarted   = false;
            vds.lastPlayedUrl.clear();
            state.pendingPlay.videoUrl = vds.qualities[vds.qualityIdx].videoUrl;
            state.pendingPlay.audioUrl = vds.qualities[vds.qualityIdx].audioUrl;
            if (vds.qualityChangePos > 0.5) {
                vds.seekPos2     = vds.qualityChangePos;
                vds.seekPending2 = true;
                vds.seekDone2    = false;
            }
        }
    }

    // Seek drain after quality change
    VD_DrainSeek(vds);

    // Tick popup window
    VD_TickPopup(vds.popup, vds, mainHwnd);

    // =========================================================================
    // Layout constants
    // =========================================================================
    float vidMaxW = LW - PAD * 2.f;
    float vidMaxH = h * 0.45f;
    float vidW = vidMaxW;
    float vidH = vidW * 9.f / 16.f;
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

    // Back button — vertically centred in BACKH row, consistent PAD
    ImGui::SetCursorPos({PAD, (BACKH - BH) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Button,       {0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.22f,.22f,.22f,1});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {.32f,.32f,.32f,1});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
    if (ImGui::Button(ICON_FA_CHEVRON_LEFT "  Back",{84,BH})) {
        vds.player.Stop();
        vds.playStarted=vds.playerInited=false;
        vds.streamUrl.clear(); state.pendingPlay.videoUrl.clear();
        if (vds.playerHwnd)  { ShowWindow(vds.playerHwnd,SW_HIDE);  DestroyWindow(vds.playerHwnd);  vds.playerHwnd=NULL; }
        if (vds.overlayHwnd) { ShowWindow(vds.overlayHwnd,SW_HIDE); DestroyWindow(vds.overlayHwnd); vds.overlayHwnd=NULL; }
        vds.cachedPX=vds.cachedPY=vds.cachedPW=vds.cachedPH=-1;
        state.activePage=state.prevPage;
    }
    ImGui::PopStyleVar();
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
        const float sbX=PAD+2.f, sbW=LW-PAD*2.f-4.f-PAD;
        const float rowY=vidOffY+vidH+SB_GAP;
        float pf=canSeek?(float)(pos/dur):0.f;
        float bf=vds.player.GetBufferPct();
        float nr=VD_Seekbar(sbX,rowY,sbW,pf,bf,canSeek,dlTop,"##sb");
        if (nr>=0.f) vds.player.SeekTo((double)nr*dur);
        ImVec2 bMin=ImGui::GetItemRectMin();
        float trackY=bMin.y+SB_KR;
        float labelY=trackY+SB_SH+SB_KR+2.f;
        std::string tL=VD_FmtTime(pos), tR=VD_FmtTime(dur);
        ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT_DIM_V4);
        dlTop->AddText({bMin.x,labelY}, labelCol, tL.c_str());
        ImVec2 trSz=ImGui::CalcTextSize(tR.c_str());
        dlTop->AddText({bMin.x+sbW-trSz.x,labelY}, labelCol, tR.c_str());
        ImGui::SetCursorPos({sbX,rowY+SB_ROW});
    }

    // -----------------------------------------------------------------------
    // TRANSPORT ROW — FA6 icons
    // -----------------------------------------------------------------------
    float ctrRowY;
    {
        const float BW=BH+2.f, G=4.f;
        ctrRowY=ImGui::GetCursorPosY();
        ImGui::SetCursorPos({PAD,ctrRowY+(CTR_H-BH)*.5f});

        ImGui::PushStyleColor(ImGuiCol_Button,       {.17f,.17f,.17f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.28f,.28f,.28f,1});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {.38f,.38f,.38f,1});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        if (!canCtrl) ImGui::BeginDisabled();

        if (ImGui::Button(ICON_FA_BACKWARD_STEP,{BW,BH})) vds.player.SeekTo(0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart");
        ImGui::SameLine(0,G);
        if (ImGui::Button(ICON_FA_BACKWARD,{BW,BH})) { double np=pos-10; vds.player.SeekTo(np<0?0:np); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("-10s");
        ImGui::SameLine(0,G);

        ImGui::PushStyleColor(ImGuiCol_Button,       isPlaying?ImVec4{.15f,.15f,.15f,1}:Theme::COL_ACCENT_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Theme::COL_ACCENT_V4);
        const char* playIcon = isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY;
        if (ImGui::Button(playIcon,{BW,BH})) {
            if (isPlaying) vds.player.Pause();
            else if (ps==PlayerState::Stopped||ps==PlayerState::Idle) {
                if (!vds.lastPlayedUrl.empty()) vds.player.Open(vds.lastPlayedUrl);
            } else vds.player.Play();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0,G);
        if (ImGui::Button(ICON_FA_FORWARD,{BW,BH})) { double np=pos+10; vds.player.SeekTo(np>dur?dur:np); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("+10s");
        ImGui::SameLine(0,G);
        if (ImGui::Button(ICON_FA_FORWARD_STEP,{BW,BH})) vds.player.SeekTo(dur);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("End");

        if (!canCtrl) ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Status dot
        ImGui::SameLine(0, 16);
        {
            ImU32 dc=IM_COL32(90,90,90,255); const char* dt="Idle";
            if (state.streamResolving.load())     { dc=IM_COL32(255,200,0,255); dt="Resolving..."; }
            else if (streamError)                 { dc=IM_COL32(255,60,60,255); dt="Error"; }
            else if (vds.playStarted) switch(ps) {
                case PlayerState::Loading: dc=IM_COL32(255,200,0,255); dt="Loading";  break;
                case PlayerState::Playing: dc=IM_COL32(0,220,80,255);  dt="Playing";  break;
                case PlayerState::Paused:  dc=IM_COL32(180,180,180,255);dt="Paused"; break;
                case PlayerState::Stopped: dc=IM_COL32(90,90,90,255);  dt="Stopped"; break;
                default: break;
            }
            ImVec2 cp=ImGui::GetCursorScreenPos();
            float dotY=cp.y+(BH-8.f)*.5f;
            ImGui::GetWindowDrawList()->AddCircleFilled({cp.x+4.f,dotY+4.f},4.f,dc);
            ImGui::Dummy({10.f,BH});
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",dt);
        }
    }

    // -----------------------------------------------------------------------
    // Volume + Quality combo
    // -----------------------------------------------------------------------
    const float VOL_W=80.f, QUAL_W=130.f, MG=6.f;
    float volRowY = ctrRowY+(CTR_H-BH)*.5f;
    ImGui::SetCursorPos({LW-PAD-QUAL_W-MG-VOL_W-22.f, volRowY});
    const char* volIcon = vds.volume == 0
        ? ICON_FA_VOLUME_XMARK
        : (vds.volume < 40 ? ICON_FA_VOLUME_LOW : ICON_FA_VOLUME_HIGH);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0,0,0,0});
    static int prevVol = 80;
    if (ImGui::Button(volIcon, {22.f, BH})) {
        if (vds.volume > 0) { prevVol = vds.volume; vds.volume = 0; }
        else                { vds.volume = prevVol; }
        vds.player.SetVolume(vds.volume);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute / Unmute");
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 2);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,      Theme::COL_ACCENT_V4);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,Theme::COL_ACCENT_HOV_V4);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,         ImVec4{0.22f,0.22f,0.22f,1.f});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,  ImVec4{0.28f,0.28f,0.28f,1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 99.f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,  99.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.f);
    ImGui::SetNextItemWidth(VOL_W);
    int vol=vds.volume;
    if (ImGui::SliderInt("##vol",&vol,0,100,"")) {
        vds.volume=vol; vds.player.SetVolume(vol);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Volume: %d%%", vds.volume);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{MG, 0.f});
    ImGui::SameLine(0,MG);
    ImGui::PopStyleVar();

    // Quality combo
    if (!vds.qualities.empty()) {
        bool loading2=(vds.qualities.size()==1 && !vds.qualities[0].videoUrl.empty());
        const char* cur=loading2?"Loading qualities...":vds.qualities[vds.qualityIdx].label.c_str();
        ImGui::PushStyleColor(ImGuiCol_FrameBg,       Theme::COL_SURFACE2);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,Theme::COL_CONTRAST_V4);
        ImGui::PushStyleColor(ImGuiCol_PopupBg,       Theme::COL_CARD);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.f, 3.f});
        ImGui::SetNextItemWidth(QUAL_W);
        if (ImGui::BeginCombo("##qual",cur)) {
            for (int qi=0;qi<(int)vds.qualities.size();qi++) {
                bool sel=(qi==vds.qualityIdx);
                if (ImGui::Selectable(vds.qualities[qi].label.c_str(),sel)) {
                    if (qi!=vds.qualityIdx) {
                        vds.qualityChangePos  = vds.player.GetPosition();
                        vds.pendingQualityIdx = qi;
                    }
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    // -----------------------------------------------------------------------
    // ACTION ROW — equal-width flex buttons
    // -----------------------------------------------------------------------
    {
        float actRowY = ctrRowY + CTR_H;
        float actBtnW = (LW - PAD * 2.f) / 4.f;
        ImGui::SetCursorPos({PAD, actRowY + (ACT_H - BH) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_Button,        Theme::COL_SURFACE2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_SOFT);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_ACCENT_V4);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        if (ImGui::Button(ICON_FA_DOWNLOAD "  Download", {actBtnW, BH}))
            vds.dlDialog.open = true;

        ImGui::SameLine(0, 0);
        bool isFav = PersistentData::IsInPlaylist("Favorites", vds.videoId);
        const char* favLabel = isFav
            ? ICON_FA_HEART "  Saved"
            : ICON_FA_HEART "  Favorite";
        if (ImGui::Button(favLabel, {actBtnW, BH})) {
            if (isFav) PersistentData::RemoveFromPlaylist("Favorites", vds.videoId);
            else       PersistentData::AddToPlaylist("Favorites", vds.videoId, vds.title);
        }

        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_WINDOW_RESTORE "  Window", {actBtnW, BH})) {
            if (!vds.popup.open&&!vds.videoId.empty()&&!vds.streamUrl.empty()) {
                double startPos=vds.player.GetPosition();
                vds.popup.startPos=startPos;
                vds.popup.open=true;
                vds.popup.seekPending=false; vds.popup.seekDone=false;
            }
        }

        ImGui::SameLine(0, 0);
        if (ImGui::Button(vds.fullscreen ? ICON_FA_COMPRESS "  Exit FS" : ICON_FA_EXPAND "  Full", {actBtnW, BH}))
            vds.fullscreen = !vds.fullscreen;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild(); // ##vd_top

    // =====================================================================
    // BOTTOM ZONE
    // =====================================================================
    if (botH > 10.f) {
        ImGui::SetCursorPos({0,topZoneH});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::COL_CARD);
        ImGui::BeginChild("##vd_bot",{LW,botH},false,
            ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();

        // Tab bar
        const int NTABS=(int)VideoDetailTab::COUNT;
        float tabW=LW/(float)NTABS;
        ImGui::SetCursorPos({0,0});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.f, (BH-ImGui::GetTextLineHeight())*.5f});
        for (int ti=0;ti<NTABS;ti++) {
            bool active=(vds.activeTab==(VideoDetailTab)ti);
            ImGui::PushStyleColor(ImGuiCol_Button,
                active?Theme::COL_SURFACE2:Theme::COL_SURFACE2);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                active?Theme::COL_SURFACE2:Theme::COL_CONTRAST_V4);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::COL_ACCENT_HOV_V4);
            ImGui::PushStyleColor(ImGuiCol_Text,
                active?Theme::COL_ACCENT_V4:Theme::COL_TEXT_DIM_V4);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.f,0.f});
            if (ImGui::Button(kVDTabs[ti],{tabW,BH}))
                vds.activeTab=(VideoDetailTab)ti;
            if (active) {
                ImVec2 pMin=ImGui::GetItemRectMin(), pMax=ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(
                    {pMin.x+2.f,pMax.y-2.f},{pMax.x-2.f,pMax.y-2.f},
                    ImGui::ColorConvertFloat4ToU32(Theme::COL_ACCENT_V4),2.f);
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            if (ti<NTABS-1) ImGui::SameLine(0,0);
        }
        ImGui::PopStyleVar(); // FramePadding tabs

        float contentH=botH-BH;
        ImGui::SetCursorPos({0,BH});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::COL_BG);
        ImGui::BeginChild("##vd_botcontent",{LW,contentH},false,
            ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();

        if (vds.activeTab==VideoDetailTab::Description) {
            ImGui::SetCursorPos({PAD,PAD});
            ImGui::PushTextWrapPos(LW-PAD);

            if (!vds.title.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT);
                ImGui::TextWrapped("%s", vds.title.c_str());
                ImGui::PopStyleColor();
                ImGui::SetCursorPosX(PAD);
            }

            if (!vds.channelName.empty()) {
                ImGui::SetCursorPosX(PAD);
                ImGui::PushStyleColor(ImGuiCol_Text,         Theme::COL_TEXT_DIM_V4);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4{0.18f,0.18f,0.18f,1.f});
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.28f,0.28f,0.28f,1.f});
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign,{0.f,0.5f});
                if (ImGui::Selectable(vds.channelName.c_str(),false,
                        ImGuiSelectableFlags_None,{0,14.f})) {
                    snprintf(state.searchBuf, sizeof(state.searchBuf),
                             "@%s", vds.channelId.c_str());
                    state.activePage = AppPage::Search;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
            }

            ImGui::SetCursorPosX(PAD);
            ImGui::PushStyleColor(ImGuiCol_Button,        Theme::COL_SURFACE2);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_SOFT);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_ACCENT_V4);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{8.f, 3.f});
            if (ImGui::Button(ICON_FA_BELL "  Subscribe", {120.f, BH}))
                ImGui::SetTooltip("Subscribe not implemented");
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            ImGui::Dummy({0, 2.f});
            ImGui::SetCursorPosX(PAD);
            std::string meta;
            if (!vds.viewCount.empty()) meta += vds.viewCount;
            if (!vds.duration.empty())  { if (!meta.empty()) meta += " \xc2\xb7 "; meta += vds.duration; }
            if (!meta.empty())
                ImGui::TextColored(Theme::COL_TEXT_FAINT, "%s", meta.c_str());

            ImGui::Dummy({0, 4.f});
            ImGui::SetCursorPosX(PAD);
            ImGui::PushStyleColor(ImGuiCol_Separator,ImVec4{1,1,1,0.10f});
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            if (!vds.description.empty()) {
                ImGui::SetCursorPosX(PAD);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT_DIM_V4);
                ImGui::TextWrapped("%s", vds.description.c_str());
                ImGui::PopStyleColor();
                ImGui::SetCursorPosX(PAD);
            }
            ImGui::PopTextWrapPos();
        }
        else if (vds.activeTab==VideoDetailTab::Comments) {
            VD_DrawCommentsTab(state,vds,LW,PAD);
        }

        ImGui::EndChild(); // ##vd_botcontent
        ImGui::EndChild(); // ##vd_bot
    }

    // =====================================================================
    // RIGHT PANEL (Up Next)
    // =====================================================================
    if (wide) {
        float rpX=LW+PAD*2.f, rpY=0.f, rpH=h;
        ImGui::SetCursorPos({rpX,rpY});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::COL_CARD);
        ImGui::BeginChild("##vd_right",{RW,rpH},false,
            ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();

        ImGui::SetCursorPos({PAD, 10.f});
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::COL_TEXT);
        ImGui::TextUnformatted("Up Next");
        ImGui::PopStyleColor();
        ImGui::SetCursorPosX(PAD);
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4{1,1,1,0.10f});
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});
        ImGui::BeginChild("##vd_rel",{RW,rpH-32.f},false,0);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();

        VD_DrawRelatedPanel(state,vds,RW,PAD);

        ImGui::EndChild(); // ##vd_rel
        ImGui::EndChild(); // ##vd_right
    }

    DrawDownloadDialog(vds.dlDialog, mainHwnd);
    ImGui::End(); // ##vd
}
