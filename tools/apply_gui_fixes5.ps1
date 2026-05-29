# GUI-FIXES-5 patch script for ViewVideoDetail.h
# Run from repo root: .\tools\apply_gui_fixes5.ps1
# Uses only .Replace() - no -replace operator to avoid argument count errors.

$file = "src\ui\views\ViewVideoDetail.h"
if (-not (Test-Path $file)) {
    Write-Error "File not found: $file  (run from repo root)"
    exit 1
}

$content = [System.IO.File]::ReadAllText((Resolve-Path $file), [System.Text.Encoding]::UTF8)
$nl = "`r`n"

# -----------------------------------------------------------------------
# FIX 1: Move prevVol from static local to VideoDetailState struct
# -----------------------------------------------------------------------
$old1 = "    std::string relatedLoadedForVideoId;$nl};"
$new1 = "    std::string relatedLoadedForVideoId;$nl$nl    // Volume mute/restore (GUI-FIXES-5: was static local)$nl    int prevVol = 80;$nl};"
$content = $content.Replace($old1, $new1)

# -----------------------------------------------------------------------
# FIX 2: Replace static int prevVol = 80 with reference to struct field
# -----------------------------------------------------------------------
$old2 = "        static int prevVol = 80;"
$new2 = "        int& prevVol = vds.prevVol; // GUI-FIXES-5: moved to struct"
$content = $content.Replace($old2, $new2)

# -----------------------------------------------------------------------
# FIX 3: Action row - change from 4 to 6 buttons (Share + Browser added)
# -----------------------------------------------------------------------
$old3 = "        float actBtnW = (LW - PAD * 2.f) / 4.f;"
$new3 = "        float actBtnW = (LW - PAD * 2.f) / 6.f; // GUI-FIXES-5: 6 buttons"
$content = $content.Replace($old3, $new3)

# Insert Share + Browser buttons before the Window button
$old3b = "        ImGui::SameLine(0, 0);" + $nl + "        if (ImGui::Button(ICON_FA_WINDOW_RESTORE ""  Window"", {actBtnW, BH})) {"
$new3b = @"
        // GUI-FIXES-5: Share button - copies YouTube URL to clipboard
        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_SHARE_NODES "  Share", {actBtnW, BH})) {
            if (!vds.videoId.empty()) {
                std::string shareUrl = "https://youtu.be/" + vds.videoId;
                ImGui::SetClipboardText(shareUrl.c_str());
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy YouTube link to clipboard");

        // GUI-FIXES-5: Open in Browser button
        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Browser", {actBtnW, BH})) {
            if (!vds.videoId.empty()) {
                std::wstring wurl = L"https://www.youtube.com/watch?v=" +
                    std::wstring(vds.videoId.begin(), vds.videoId.end());
                ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open in default browser");

        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_WINDOW_RESTORE "  Window", {actBtnW, BH})) {
"@
$content = $content.Replace($old3b, $new3b)

# -----------------------------------------------------------------------
# FIX 4: Keyboard shortcuts - insert after VD_DrainSeek(vds);
# -----------------------------------------------------------------------
$old4 = "    VD_DrainSeek(vds);"
$new4 = @"
    VD_DrainSeek(vds);

    // GUI-FIXES-5: Keyboard shortcuts (Space/arrows/F/M/UpDown)
    if (isActivePage && !ImGui::GetIO().WantCaptureKeyboard) {
        // Space -> play/pause
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && vds.playStarted) {
            PlayerState kps = vds.player.GetState();
            if (kps == PlayerState::Playing) vds.player.Pause();
            else vds.player.Play();
        }
        // Left -> seek -10s
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && vds.playStarted) {
            double np = vds.player.GetPosition() - 10.0;
            vds.player.SeekTo(np < 0 ? 0 : np);
        }
        // Right -> seek +10s
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && vds.playStarted) {
            double dur2 = vds.player.GetDuration();
            double np   = vds.player.GetPosition() + 10.0;
            vds.player.SeekTo(np > dur2 ? dur2 : np);
        }
        // F -> toggle fullscreen
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            vds.fullscreen = !vds.fullscreen;
        // M -> toggle mute
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
            if (vds.volume > 0) { vds.prevVol = vds.volume; vds.volume = 0; }
            else                { vds.volume = vds.prevVol; }
            vds.player.SetVolume(vds.volume);
        }
        // Up arrow -> volume +5
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            vds.volume = std::min(100, vds.volume + 5);
            vds.player.SetVolume(vds.volume);
        }
        // Down arrow -> volume -5
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            vds.volume = std::max(0, vds.volume - 5);
            vds.player.SetVolume(vds.volume);
        }
    }
"@
$content = $content.Replace($old4, $new4)

# -----------------------------------------------------------------------
# FIX 5: Add ICON_FA_THUMBS_UP before likeCount in comments
# -----------------------------------------------------------------------
$old5 = 'ImGui::TextColored({0.9f, 0.75f, 0.2f, 0.85f}, "%s", c.likeCount.c_str());'
$new5 = 'ImGui::TextColored({0.9f, 0.75f, 0.2f, 0.85f}, ICON_FA_THUMBS_UP " %s", c.likeCount.c_str());'
$content = $content.Replace($old5, $new5)

# -----------------------------------------------------------------------
# FIX 6a: Skeleton shimmer - replace static triangle placeholder
# -----------------------------------------------------------------------
$old6 = @"
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
"@
$new6 = @"
            } else {
                // GUI-FIXES-5: animated shimmer skeleton
                float shimT = (float)(fmod(ImGui::GetTime() * 1.2, 1.0));
                float cx2   = thTL.x + (thBR.x - thTL.x) * shimT;
                float hw    = (thBR.x - thTL.x) * 0.35f;
                dl->AddRectFilled(thTL, thBR, IM_COL32(70,70,75,255), 4.f);
                dl->AddRectFilledMultiColor(
                    {cx2 - hw, thTL.y}, {cx2 + hw, thBR.y},
                    IM_COL32(90,90,95,0), IM_COL32(90,90,95,255),
                    IM_COL32(90,90,95,255), IM_COL32(90,90,95,0));
            }
"@
$content = $content.Replace($old6, $new6)

# -----------------------------------------------------------------------
# FIX 6b: Cap related items at 50
# -----------------------------------------------------------------------
$old6b = "    for (int i = 0; i < (int)state.pendingRelated.items.size(); i++) {"
$new6b = "    int relLimit = std::min((int)state.pendingRelated.items.size(), 50); // GUI-FIXES-5$nl    for (int i = 0; i < relLimit; i++) {"
$content = $content.Replace($old6b, $new6b)

# -----------------------------------------------------------------------
# FIX 7: Popup player draw - insert before DrawDownloadDialog
# -----------------------------------------------------------------------
$old7 = "    DrawDownloadDialog(vds.dlDialog, mainHwnd);"
$new7 = @"
    // GUI-FIXES-5: Popup player window draw
    if (vds.popup.open) {
        ImGui::SetNextWindowSize({640.f, 400.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            {x + w * 0.5f - 320.f, y + h * 0.5f - 200.f},
            ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.08f, 0.08f, 0.08f, 1.f});
        bool popupOpen = true;
        if (ImGui::Begin(("Now Playing: " + vds.title).c_str(), &popupOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImVec2 sz   = ImGui::GetContentRegionAvail();
            float popW  = sz.x;
            float popH  = sz.y;
            HWND popPar = (HWND)ImGui::GetCurrentWindow()->Viewport->PlatformHandle;
            if (popPar && popW > 10 && popH > 10) {
                ImVec2 cPos = ImGui::GetCursorScreenPos();
                int ppX = (int)cPos.x, ppY = (int)cPos.y;
                int ppW = (int)popW,   ppH = (int)popH;
                static int ppCX=-1,ppCY=-1,ppCW=-1,ppCH=-1;
                VD_TickPanelAndOverlay(popPar, ppX, ppY, ppW, ppH,
                    vds.popup.videoChild, vds.popup.overlayHwnd,
                    ppCX, ppCY, ppCW, ppCH, nullptr);
                if (!vds.popup.player) {
                    vds.popup.player = new VLCPlayer();
                    vds.popup.player->Init(vds.popup.videoChild);
                    vds.popup.player->SetVolume(vds.volume);
                    std::string popUrl = vds.qualities.empty()
                        ? vds.streamUrl
                        : VD_BuildUrl(vds.qualities[vds.qualityIdx]);
                    vds.popup.player->Open(popUrl);
                    if (vds.popup.startPos > 0.5) {
                        vds.popup.seekPending = true;
                        vds.popup.seekDone    = false;
                    }
                }
                if (vds.popup.seekPending && !vds.popup.seekDone &&
                    vds.popup.player->GetState() == PlayerState::Playing) {
                    vds.popup.player->SeekTo(vds.popup.startPos);
                    vds.popup.seekPending = false;
                    vds.popup.seekDone    = true;
                }
                ImGui::Dummy({popW, popH});
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        if (!popupOpen) VD_DestroyPopup(vds.popup);
    }

    DrawDownloadDialog(vds.dlDialog, mainHwnd);
"@
$content = $content.Replace($old7, $new7)

# -----------------------------------------------------------------------
# FIX 8: Update header comment block
# -----------------------------------------------------------------------
$old8 = "// GUI-FIXES-4:"
$new8 = "// GUI-FIXES-5: keyboard shortcuts (Space/arrows/F/M), Share+Browser btns,$nl//              popup player render, prevVol moved to struct, like icon,$nl//              sidebar skeleton shimmer, related list capped at 50.$nl// GUI-FIXES-4:"
$content = $content.Replace($old8, $new8)

# -----------------------------------------------------------------------
# Write result
# -----------------------------------------------------------------------
[System.IO.File]::WriteAllText((Resolve-Path $file).Path, $content, [System.Text.Encoding]::UTF8)
Write-Host "GUI-FIXES-5 applied successfully to $file" -ForegroundColor Green
