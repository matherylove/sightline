#pragma once
#include "../AppState.h"
#include "../Widgets.h"

enum class ChannelTab { Videos=0, Playlists, About, COUNT };
static const char* kChannelTabs[] = { "Videos", "Playlists", "About" };

struct ChannelState {
    ChannelTab activeTab = ChannelTab::Videos;
};

inline void DrawChannelView(AppState& state,
                            ChannelState& cs,
                            const char* channelName,
                            float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##channelview", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    float bannerH = 72.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float bw = w - 16;
    ImGui::GetWindowDrawList()->AddRectFilled(
        p, ImVec2(p.x + bw, p.y + bannerH),
        ImGui::ColorConvertFloat4ToU32(Theme::COL_CARD));
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(p.x + 16, p.y + 26),
        ImGui::ColorConvertFloat4ToU32(Theme::COL_ACCENT_V4),
        channelName);
    ImGui::Dummy(ImVec2(0, bannerH + 4));

    ImGui::PushStyleColor(ImGuiCol_Button, Theme::COL_ACCENT_V4);
    ImGui::Button("Subscribe", ImVec2(130, 28));
    ImGui::PopStyleColor();
    ImGui::Separator(); ImGui::Spacing();

    int ctCount = (int)ChannelTab::COUNT;
    float tabW  = (w - 16.0f) / ctCount;
    for (int i = 0; i < ctCount; i++) {
        if (i > 0) ImGui::SameLine(0, 2);
        if (Widgets::TabButton(kChannelTabs[i],
                (int)cs.activeTab == i, tabW))
            cs.activeTab = (ChannelTab)i;
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::BeginChild("##channelcontent", ImVec2(0,0), false);
    switch (cs.activeTab) {
        case ChannelTab::Videos:
            Widgets::ComingSoon("[ Channel videos - pending ]");
            break;
        case ChannelTab::Playlists:
            Widgets::ComingSoon("[ Channel playlists - pending ]");
            break;
        case ChannelTab::About:
            Widgets::ComingSoon("[ Channel description - pending ]");
            break;
        default: break;
    }
    ImGui::EndChild();
    ImGui::End();
}
