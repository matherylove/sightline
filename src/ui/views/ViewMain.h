#pragma once
#include "../AppState.h"
#include "../Widgets.h"

static const char* kTabLabels[] = {
    "What's New", "Trending", "Subscriptions",
    "Bookmarks", "History", "Downloads"
};

inline void DrawMainView(AppState& state,
                         float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##mainview", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    int   tabCount  = (int)AppTab::COUNT;
    float tabW      = 110.0f;
    float totalTabW = tabCount * tabW + (tabCount - 1) * 2.0f;
    float startX    = (w - totalTabW) * 0.5f;
    if (startX < 0) startX = 0;

    ImGui::SetCursorPosX(startX);
    for (int i = 0; i < tabCount; i++) {
        if (i > 0) { ImGui::SameLine(0, 2); }
        if (Widgets::TabButton(kTabLabels[i], (int)state.activeTab == i, tabW))
            state.activeTab = (AppTab)i;
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    float contentH = h - 68.0f;
    ImGui::BeginChild("##tabcontent", ImVec2(0, contentH), false);
    switch (state.activeTab) {
        case AppTab::What_sNew:
            Widgets::ComingSoon("[ What's New - not yet implemented ]");
            break;
        case AppTab::Trending:
            Widgets::ComingSoon("[ Trending - not yet implemented ]");
            break;
        case AppTab::Subscriptions:
            Widgets::ComingSoon("[ Subscriptions - not yet implemented ]");
            break;
        case AppTab::Bookmarks:
            Widgets::ComingSoon("[ Bookmarks - not yet implemented ]");
            break;
        case AppTab::History:
            Widgets::ComingSoon("[ History - not yet implemented ]");
            break;
        case AppTab::Downloads:
            Widgets::ComingSoon("[ Downloads - not yet implemented ]");
            break;
        default: break;
    }
    ImGui::EndChild();
    ImGui::End();
}
