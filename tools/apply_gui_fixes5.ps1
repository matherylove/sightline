# GUI-FIXES-5 patch script for ViewVideoDetail.h
# Run from repo root: .\tools\apply_gui_fixes5.ps1

$file = "src\ui\views\ViewVideoDetail.h"
$content = Get-Content $file -Raw -Encoding UTF8

# -----------------------------------------------------------------------
# FIX 1: Move prevVol from static local to VideoDetailState struct
# Add `int prevVol = 80;` field to struct before the closing brace
# -----------------------------------------------------------------------
$content = $content -replace `
    '(// Related videos local UI state\r?\n    std::string relatedLoadedForVideoId;\r?\n\};)',
    "`$1".Replace(
        '// Related videos local UI state' + [char]10 + '    std::string relatedLoadedForVideoId;' + [char]10 + '};',
        '// Related videos local UI state' + [char]10 +
        '    std::string relatedLoadedForVideoId;' + [char]10 +
        [char]10 +
        '    // Volume mute/restore' + [char]10 +
        '    int prevVol = 80;' + [char]10 +
        '};'
    )

# Simpler regex approach
$content = $content -replace `
    '(    std::string relatedLoadedForVideoId;?
\};)',
    "    std::string relatedLoadedForVideoId;`r`n`r`n    // Volume mute/restore`r`n    int prevVol = 80;`r`n};"

# -----------------------------------------------------------------------
# FIX 2: Replace `static int prevVol = 80;` with `int& prevVol = vds.prevVol;`
# -----------------------------------------------------------------------
$content = $content -replace `
    'static int prevVol = 80;',
    'int& prevVol = vds.prevVol; // moved to struct (GUI-FIXES-5)'

# -----------------------------------------------------------------------
# FIX 3: Add Share + OpenInBrowser buttons to the action row (4 -> 6 buttons)
# Replace the 4-button action row layout with 6 equal-width buttons
# -----------------------------------------------------------------------
$oldActionRow = @'
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
        if (ImGui::Button(vds.fullscreen ? ICON_FA_COMPRESS "  Exit FS" : ICON_FA_EXPAND "  Fullscreen", {actBtnW, BH}))
            vds.fullscreen = !vds.fullscreen;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
'@

$newActionRow = @'
        float actRowY = ctrRowY + CTR_H;
        // GUI-FIXES-5: 6 equal-width buttons (added Share + Open in Browser)
        float actBtnW = (LW - PAD * 2.f) / 6.f;
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

        // GUI-FIXES-5: Share button — copies YouTube URL to clipboard
        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_SHARE_NODES "  Share", {actBtnW, BH})) {
            if (!vds.videoId.empty()) {
                std::string shareUrl = "https://youtu.be/" + vds.videoId;
                ImGui::SetClipboardText(shareUrl.c_str());
                // Brief tooltip to confirm
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy YouTube link to clipboard");

        // GUI-FIXES-5: Open in Browser button
        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Browser", {actBtnW, BH})) {
            if (!vds.videoId.empty()) {
                std::wstring url = L"https://www.youtube.com/watch?v=" +
                    std::wstring(vds.videoId.begin(), vds.videoId.end());
                ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open in default browser");

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
        if (ImGui::Button(vds.fullscreen ? ICON_FA_COMPRESS "  Exit FS" : ICON_FA_EXPAND "  Fullscreen", {actBtnW, BH}))
            vds.fullscreen = !vds.fullscreen;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
'@

$content = $content.Replace($oldActionRow, $newActionRow)

# -----------------------------------------------------------------------
# FIX 4: Add keyboard shortcuts handler after VD_DrainSeek(vds)
# -----------------------------------------------------------------------
$oldDrainSeek = 'VD_DrainSeek(vds);'
$newDrainSeek = @'
VD_DrainSeek(vds);

    // GUI-FIXES-5: Keyboard shortcuts (only when this view is active and
    //              no ImGui widget is capturing keyboard input)
    if (isActivePage && !ImGui::GetIO().WantCaptureKeyboard) {
        ImGuiIO& kio = ImGui::GetIO();
        // Space -> play/pause
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && vds.playStarted) {
            PlayerState kps = vds.player.GetState();
            if (kps == PlayerState::Playing) vds.player.Pause();
            else vds.player.Play();
        }
        // Left arrow -> seek -10s
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && vds.playStarted) {
            double np = vds.player.GetPosition() - 10.0;
            vds.player.SeekTo(np < 0 ? 0 : np);
        }
        // Right arrow -> seek +10s
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
        // Up/Down arrows -> volume +/-5
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            vds.volume = std::min(100, vds.volume + 5);
            vds.player.SetVolume(vds.volume);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            vds.volume = std::max(0, vds.volume - 5);
            vds.player.SetVolume(vds.volume);
        }
    }
'@
$content = $content.Replace($oldDrainSeek, $newDrainSeek)

# -----------------------------------------------------------------------
# FIX 5: Add ICON_FA_THUMBS_UP before likeCount in comments
# -----------------------------------------------------------------------
$content = $content -replace `
    'ImGui\.TextColored\(\{0\.9f, 0\.75f, 0\.2f, 0\.85f\}, "%s", c\.likeCount\.c_str\(\)\);',
    'ImGui.TextColored({0.9f, 0.75f, 0.2f, 0.85f}, ICON_FA_THUMBS_UP " %s", c.likeCount.c_str());'

# -----------------------------------------------------------------------
# FIX 6: Related panel - limit rendered items to 50 max + skeleton shimmer
# Replace the placeholder play triangle with a shimmer-style placeholder
# -----------------------------------------------------------------------
$oldPlaceholder = @'
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
'@

$newPlaceholder = @'
            } else {
                // GUI-FIXES-5: shimmer-style skeleton placeholder
                float shimT = (float)(fmod(ImGui::GetTime() * 1.2, 1.0));
                ImU32 shimA = IM_COL32(70,70,75,255);
                ImU32 shimB = IM_COL32(90,90,95,255);
                float cx2   = thTL.x + (thBR.x - thTL.x) * shimT;
                dl->AddRectFilled(thTL, thBR, shimA, 4.f);
                float hw = (thBR.x - thTL.x) * 0.35f;
                dl->AddRectFilledMultiColor(
                    {cx2 - hw, thTL.y}, {cx2 + hw, thBR.y},
                    IM_COL32(90,90,95,0), shimB, shimB, IM_COL32(90,90,95,0));
            }
'@
$content = $content.Replace($oldPlaceholder, $newPlaceholder)

# Cap related items at 50
$content = $content -replace `
    'for \(int i = 0; i < \(int\)state\.pendingRelated\.items\.size\(\); i\+\+\)',
    'int relLimit = std::min((int)state.pendingRelated.items.size(), 50); // GUI-FIXES-5: cap at 50' + "`r`n" +
    '    for (int i = 0; i < relLimit; i++)'

# -----------------------------------------------------------------------
# FIX 7: Add basic popup player draw call before DrawDownloadDialog
# -----------------------------------------------------------------------
$oldDrawDL = '    DrawDownloadDialog(vds.dlDialog, mainHwnd);'
$newDrawDL = @'
    // GUI-FIXES-5: Popup player window
    if (vds.popup.open) {
        ImGui::SetNextWindowSize({640.f, 400.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(
            {x + w * 0.5f - 320.f, y + h * 0.5f - 200.f},
            ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.08f,0.08f,0.08f,1.f});
        bool popupOpen = true;
        if (ImGui::Begin(("Popup: " + vds.title).c_str(), &popupOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImVec2 sz = ImGui::GetContentRegionAvail();
            float popW = sz.x, popH = sz.y;
            HWND popParent = (HWND)ImGui::GetCurrentWindow()->Viewport->PlatformHandle;
            if (popParent && popW > 0 && popH > 0) {
                ImVec2 wPos = ImGui::GetWindowPos();
                ImVec2 cPos = ImGui::GetCursorScreenPos();
                int ppX = (int)cPos.x, ppY = (int)cPos.y;
                int ppW = (int)popW,   ppH = (int)popH;
                VD_TickPanelAndOverlay(popParent, ppX, ppY, ppW, ppH,
                    vds.popup.videoChild, vds.popup.overlayHwnd,
                    vds.cachedPX, vds.cachedPY, vds.cachedPW, vds.cachedPH,
                    nullptr);
                // Init player on first open
                if (!vds.popup.player) {
                    vds.popup.player = new VLCPlayer();
                    vds.popup.panelUD.player  = vds.popup.player;
                    vds.popup.panelUD.isFS    = &vds.fullscreen;
                    vds.popup.panelUD.lastMove= &vds.lastMouseMove;
                    vds.popup.player->Init(vds.popup.videoChild);
                    vds.popup.player->SetVolume(vds.volume);
                    std::string popUrl = vds.qualities.empty()
                        ? vds.streamUrl
                        : VD_BuildUrl(vds.qualities[vds.qualityIdx]);
                    vds.popup.player->Open(popUrl);
                    if (vds.popup.startPos > 0.5)
                        vds.popup.seekPending = true;
                }
                if (vds.popup.seekPending && !vds.popup.seekDone) {
                    if (vds.popup.player->GetState() == PlayerState::Playing) {
                        vds.popup.player->SeekTo(vds.popup.startPos);
                        vds.popup.seekPending = false;
                        vds.popup.seekDone    = true;
                    }
                }
                ImGui::Dummy({popW, popH});
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
        if (!popupOpen) VD_DestroyPopup(vds.popup);
    }

    DrawDownloadDialog(vds.dlDialog, mainHwnd);
'@
$content = $content.Replace($oldDrawDL, $newDrawDL)

# -----------------------------------------------------------------------
# Update header comment to reflect GUI-FIXES-5
# -----------------------------------------------------------------------
$content = $content -replace `
    '// GUI-FIXES-4:',
    "// GUI-FIXES-5: keyboard shortcuts (Space/arrows/F/M), Share+Browser btns,`r`n//              popup player render, prevVol moved to struct, like icon,`r`n//              sidebar skeleton shimmer, related list capped at 50.`r`n// GUI-FIXES-4:"

# Write file
[System.IO.File]::WriteAllText((Resolve-Path $file).Path, $content, [System.Text.Encoding]::UTF8)
Write-Host "GUI-FIXES-5 applied to $file"
