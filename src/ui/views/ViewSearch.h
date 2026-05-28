#pragma once
#include "../AppState.h"
#include "../Widgets.h"
#include "../../extractor/InnerTube.h"
#include <vector>
#include <thread>
#include <atomic>
#include <windows.h>

static inline std::string WsToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,NULL,0,NULL,NULL);
    std::string s(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,&s[0],n,NULL,NULL);
    if (!s.empty() && s.back()=='\0') s.pop_back();
    return s;
}

static inline const char* SearchSpinner() {
    int t = (int)(ImGui::GetTime() * 2.0) % 3;
    if (t == 0) return "Searching.  ";
    if (t == 1) return "Searching.. ";
    return             "Searching...";
}

struct SearchViewState {
    std::atomic<bool> infoFetching{ false };
    std::string       infoVideoId;
    VideoInfo         infoPending;
    std::atomic<bool> infoReady{ false };
    std::thread       infoThread;

    ~SearchViewState() {
        if (infoThread.joinable()) infoThread.detach();
    }
};

inline void DrainSearchViewInfo(AppState& state, SearchViewState& svs) {
    if (!svs.infoReady.load()) return;
    svs.infoReady.store(false);
    if (svs.infoThread.joinable()) svs.infoThread.join();
    if (state.pendingPlay.videoId == svs.infoVideoId) {
        if (!svs.infoPending.description.empty())
            state.pendingPlay.description = svs.infoPending.description;
        if (!svs.infoPending.channelId.empty())
            state.pendingPlay.channelId   = svs.infoPending.channelId;
        if (!svs.infoPending.viewCount.empty())
            state.pendingPlay.viewCount   = svs.infoPending.viewCount;
    }
}

inline void DrawSearchView(AppState& state,
                           SearchViewState& svs,
                           std::vector<VideoItem>& results,
                           int& selectedIdx,
                           std::wstring& statusMsg,
                           bool searching,
                           float x, float y, float w, float h) {

    DrainSearchViewInfo(state, svs);

    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##searchview", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (searching)
        ImGui::TextColored(Theme::COL_ACCENT_V4, "%s", SearchSpinner());
    else {
        std::string statusUtf8 = WsToUtf8(statusMsg);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", statusUtf8.c_str());
    }
    ImGui::Separator(); ImGui::Spacing();

    float listH = h - 52.0f;
    ImGui::BeginChild("##sresults", ImVec2(0, listH), false);

    if (searching) {
        float textW = ImGui::CalcTextSize("Loading results...").x;
        ImGui::SetCursorPosX((w - textW) * 0.5f);
        ImGui::SetCursorPosY(listH * 0.4f);
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Loading results...");
    } else if (results.empty()) {
        Widgets::ComingSoon("Search something using the top bar.");
    } else {
        for (int i = 0; i < (int)results.size(); i++) {
            std::string title    = WsToUtf8(results[i].title);
            std::string channel  = WsToUtf8(results[i].channel);
            std::string duration = WsToUtf8(results[i].duration);
            std::string views    = WsToUtf8(results[i].views);

            if (Widgets::VideoCard(i,
                    title.c_str(), channel.c_str(),
                    duration.c_str(), views.c_str(),
                    selectedIdx == i,
                    results[i].videoId.c_str())) {

                if (state.streamResolving.load()) break;
                selectedIdx = i;

                state.pendingPlay.title       = title;
                state.pendingPlay.channelName = channel;
                state.pendingPlay.duration    = duration;
                state.pendingPlay.viewCount   = views;
                state.pendingPlay.description = "";
                state.pendingPlay.channelId   = "";
                state.pendingPlay.videoId     = results[i].videoId;
                state.pendingPlay.videoUrl    = "";
                state.pendingPlay.audioUrl    = "";
                state.playRequested           = true;
                state.activePage              = AppPage::VideoDetail;

                const std::string vidId = results[i].videoId;
                svs.infoVideoId = vidId;
                svs.infoReady.store(false);
                svs.infoPending = {};
                if (svs.infoThread.joinable()) svs.infoThread.detach();
                svs.infoFetching.store(true);
                svs.infoThread = std::thread([&svs, vidId]() {
                    svs.infoPending = InnerTube::GetVideoInfo(vidId);
                    svs.infoFetching.store(false);
                    svs.infoReady.store(true);
                });
            }
        }
    }

    ImGui::EndChild();
    ImGui::End();
}
