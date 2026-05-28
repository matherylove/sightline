#pragma once
#include "../AppState.h"
#include "../Widgets.h"
#include "../CTLogger.h"

enum class SettingsSection {
    Content=0, Appearance, Player, Network, Downloads,
    Notifications, History, Storage, About, COUNT
};
static const char* kSettingsSections[] = {
    "Content", "Appearance", "Player",
    "Network", "Downloads", "Notifications",
    "History", "Storage", "About"
};
static const char* kSettingsIcons[] = {
    "[*]","[O]","[>]","[~]","[v]","[!]","[H]","[D]","[i]"
};

struct SettingsState {
    SettingsSection activeSection = SettingsSection::Content;
};

inline void DrawSettingsView(AppState& state,
                             SettingsState& ss,
                             float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##settingsview", NULL,
        ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove      |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    if (ImGui::Button("<  Back"))
        state.activePage = AppPage::Main;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("Settings");
    ImGui::Separator(); ImGui::Spacing();

    float leftW  = 180.0f;
    float rightW = w - leftW - 24.0f;
    float panelH = h - 60.0f;

    ImGui::BeginChild("##settingsleft", ImVec2(leftW, panelH), false);
    for (int i = 0; i < (int)SettingsSection::COUNT; i++) {
        if (Widgets::DrawerItem(kSettingsIcons[i],
                kSettingsSections[i],
                (int)ss.activeSection == i))
            ss.activeSection = (SettingsSection)i;
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImVec2 lineTop = ImGui::GetCursorScreenPos();
    lineTop.x += 4;
    ImGui::GetWindowDrawList()->AddLine(
        lineTop,
        ImVec2(lineTop.x, lineTop.y + panelH),
        ImGui::ColorConvertFloat4ToU32(Theme::COL_CONTRAST_V4), 1.0f);
    ImGui::Dummy(ImVec2(12, panelH));
    ImGui::SameLine();

    ImGui::BeginChild("##settingsright", ImVec2(rightW, panelH), false);

    switch (ss.activeSection) {

    case SettingsSection::Network: {
        ImGui::TextColored(Theme::COL_ACCENT_V4, "Network & Debugging");
        ImGui::Separator(); ImGui::Spacing();

        ImGui::TextColored(Theme::COL_TEXT, "Debug console");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Opens a separate console window with\n"
                "real-time logs from the extractor, player\n"
                "and signature decipherer.");
        ImGui::Spacing();

        bool dbg = state.debugConsole;
        const char* btnLabel = dbg ? "[ON]  Disable" : "[OFF] Enable";
        ImVec4 btnCol = dbg
            ? ImVec4(0.15f, 0.55f, 0.25f, 1.0f)
            : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        btnCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(btnCol.x+0.1f, btnCol.y+0.1f, btnCol.z+0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  btnCol);
        if (ImGui::Button(btnLabel, ImVec2(180, 30))) {
            state.debugConsole = !state.debugConsole;
            CTLogger::Enable(state.debugConsole);
            if (state.debugConsole)
                CTLogger::LogC('I', "[Settings] Debug console enabled.");
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "Logs include:");
        ImGui::BulletText("InnerTube requests (player, search)");
        ImGui::BulletText("JS player version and signatureTimestamp");
        ImGui::BulletText("Signature decipher result");
        ImGui::BulletText("VLC player state");
        ImGui::BulletText("Network errors and HTTP status codes");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(Theme::COL_TEXT_DIM_V4,
            "Other network options - coming soon");
        break;
    }

    default: {
        char lbl[128];
        snprintf(lbl, sizeof(lbl), "[ %s - not yet implemented ]",
            kSettingsSections[(int)ss.activeSection]);
        Widgets::ComingSoon(lbl);
        break;
    }
    }

    ImGui::EndChild();
    ImGui::End();
}
