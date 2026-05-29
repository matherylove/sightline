# GUI-FIXES-5 patch script for ViewVideoDetail.h
# Run from repo root: .\tools\apply_gui_fixes5.ps1
# All replacements use .Replace() only - no -replace operator.

$file = "src\ui\views\ViewVideoDetail.h"
if (-not (Test-Path $file)) {
    Write-Error "File not found: $file  (run from repo root)"
    exit 1
}

$content = [System.IO.File]::ReadAllText((Resolve-Path $file), [System.Text.Encoding]::UTF8)
$nl = "`r`n"

$fixes = 0

function Apply($desc, $old, $new) {
    if ($script:content.Contains($old)) {
        $script:content = $script:content.Replace($old, $new)
        $script:fixes++
        Write-Host "  [OK] $desc" -ForegroundColor Green
    } else {
        Write-Host "  [SKIP] $desc - pattern not found (may already be applied)" -ForegroundColor Yellow
    }
}

Write-Host "`nApplying GUI-FIXES-5 to $file ...`n"

# -----------------------------------------------------------------------
# FIX 1: Move prevVol from static local to VideoDetailState struct
# -----------------------------------------------------------------------
Apply "FIX 1: prevVol moved to VideoDetailState struct" `
    "    std::string relatedLoadedForVideoId;`r`n};" `
    "    std::string relatedLoadedForVideoId;`r`n`r`n    // Volume mute/restore (GUI-FIXES-5)`r`n    int prevVol = 80;`r`n};"

# -----------------------------------------------------------------------
# FIX 2: Replace static int prevVol with reference to struct field
# -----------------------------------------------------------------------
Apply "FIX 2: static prevVol -> struct reference" `
    "        static int prevVol = 80;" `
    "        int& prevVol = vds.prevVol; // GUI-FIXES-5: moved to struct"

# -----------------------------------------------------------------------
# FIX 3: Action row 4 -> 6 buttons (Share + Browser)
# -----------------------------------------------------------------------
Apply "FIX 3a: actBtnW divisor 4 -> 6" `
    "        float actBtnW = (LW - PAD * 2.f) / 4.f;" `
    "        float actBtnW = (LW - PAD * 2.f) / 6.f; // GUI-FIXES-5: 6 buttons"

# Insert Share + Browser before the Window button
# We anchor on the unique line that precedes the Window button
$old3b = "        ImGui::SameLine(0, 0);`r`n        if (ImGui::Button(ICON_FA_WINDOW_RESTORE `"  Window`", {actBtnW, BH})) {"
$new3b = @"
        // GUI-FIXES-5: Share - copies YouTube URL to clipboard
        ImGui::SameLine(0, 0);
        if (ImGui::Button(ICON_FA_SHARE_NODES "  Share", {actBtnW, BH})) {
            if (!vds.videoId.empty()) {
                std::string shareUrl = "https://youtu.be/" + vds.videoId;
                ImGui::SetClipboardText(shareUrl.c_str());
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy YouTube link to clipboard");

        // GUI-FIXES-5: Open in Browser
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
Apply "FIX 3b: insert Share+Browser buttons" $old3b $new3b

# -----------------------------------------------------------------------
# FIX 4: Keyboard shortcuts after VD_DrainSeek
# NOTE: Remove unused 'kio' variable - only call ImGui::GetIO() in branches
# -----------------------------------------------------------------------
$old4 = "    VD_DrainSeek(vds);"
$new4 = @"
    VD_DrainSeek(vds);

    // GUI-FIXES-5: Keyboard shortcuts (Space/arrows/F/M)
    if (isActivePage && !ImGui::GetIO().WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && vds.playStarted) {
            PlayerState kps = vds.player.GetState();
            if (kps == PlayerState::Playing) vds.player.Pause();
            else vds.player.Play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) && vds.playStarted) {
            double np = vds.player.GetPosition() - 10.0;
            vds.player.SeekTo(np < 0 ? 0 : np);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) && vds.playStarted) {
            double dur2 = vds.player.GetDuration();
            double np   = vds.player.GetPosition() + 10.0;
            vds.player.SeekTo(np > dur2 ? dur2 : np);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            vds.fullscreen = !vds.fullscreen;
        if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
            if (vds.volume > 0) { vds.prevVol = vds.volume; vds.volume = 0; }
            else                { vds.volume = vds.prevVol; }
            vds.player.SetVolume(vds.volume);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            vds.volume = std::min(100, vds.volume + 5);
            vds.player.SetVolume(vds.volume);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            vds.volume = std::max(0, vds.volume - 5);
            vds.player.SetVolume(vds.volume);
        }
    }
"@
Apply "FIX 4: keyboard shortcuts (no kio variable)" $old4 $new4

# -----------------------------------------------------------------------
# FIX 5: ICON_FA_THUMBS_UP in comment like count
# -----------------------------------------------------------------------
Apply "FIX 5: thumbs-up icon in like count" `
    'ImGui::TextColored({0.9f, 0.75f, 0.2f, 0.85f}, "%s", c.likeCount.c_str());' `
    'ImGui::TextColored({0.9f, 0.75f, 0.2f, 0.85f}, ICON_FA_THUMBS_UP " %s", c.likeCount.c_str());'

# -----------------------------------------------------------------------
# FIX 6a: Skeleton shimmer instead of triangle placeholder
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
                float hw    = (thBR.x - thTL.x) * 0.35f;
                float scx   = thTL.x + (thBR.x - thTL.x) * shimT;
                dl->AddRectFilled(thTL, thBR, IM_COL32(70,70,75,255), 4.f);
                dl->AddRectFilledMultiColor(
                    {scx - hw, thTL.y}, {scx + hw, thBR.y},
                    IM_COL32(90,90,95,0), IM_COL32(90,90,95,255),
                    IM_COL32(90,90,95,255), IM_COL32(90,90,95,0));
            }
"@
Apply "FIX 6a: shimmer skeleton placeholder" $old6 $new6

# -----------------------------------------------------------------------
# FIX 6b: Cap related items at 50
# -----------------------------------------------------------------------
Apply "FIX 6b: related list cap at 50" `
    "    for (int i = 0; i < (int)state.pendingRelated.items.size(); i++) {" `
    "    int relLimit = std::min((int)state.pendingRelated.items.size(), 50); // GUI-FIXES-5`r`n    for (int i = 0; i < relLimit; i++) {"

# -----------------------------------------------------------------------
# FIX 7: Popup player draw - uses mainHwnd (NOT ImGui::GetCurrentWindow)
#         PopupPanelUD only has 'player' field - no isFS/lastMove
# -----------------------------------------------------------------------
$old7 = "    DrawDownloadDialog(vds.dlDialog, mainHwnd);"
$new7 = @"
    // GUI-FIXES-5: Popup player window
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
            ImVec2 sz  = ImGui::GetContentRegionAvail();
            float popW = sz.x, popH = sz.y;
            if (mainHwnd && popW > 10 && popH > 10) {
                ImVec2 cPos = ImGui::GetCursorScreenPos();
                int ppX = (int)cPos.x, ppY = (int)cPos.y;
                int ppW = (int)popW,   ppH = (int)popH;
                static int ppCX=-1, ppCY=-1, ppCW=-1, ppCH=-1;
                // Reuse mainHwnd as parent - popup is a child of the main window
                VD_TickPanelAndOverlay(mainHwnd, ppX, ppY, ppW, ppH,
                    vds.popup.videoChild, vds.popup.overlayHwnd,
                    ppCX, ppCY, ppCW, ppCH, nullptr);
                if (!vds.popup.player) {
                    vds.popup.player = new VLCPlayer();
                    vds.popup.panelUD.player = vds.popup.player;
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
Apply "FIX 7: popup player draw (mainHwnd, correct PopupPanelUD)" $old7 $new7

# -----------------------------------------------------------------------
# FIX 8: Fix ImGuiCol_ScrollbarGrabHover -> ImGuiCol_ScrollbarGrabHovered
#        (was already broken in original code before our patch)
# -----------------------------------------------------------------------
Apply "FIX 8: ScrollbarGrabHover -> ScrollbarGrabHovered" `
    "ImGuiCol_ScrollbarGrabHover," `
    "ImGuiCol_ScrollbarGrabHovered,"

# -----------------------------------------------------------------------
# FIX 9: Update header comment
# -----------------------------------------------------------------------
Apply "FIX 9: header comment" `
    "// GUI-FIXES-4:" `
    "// GUI-FIXES-5: keyboard shortcuts, Share+Browser btns, popup player,`r`n//              prevVol in struct, like icon, shimmer skeleton, rel cap 50.`r`n// GUI-FIXES-4:"

# -----------------------------------------------------------------------
# Write result
# -----------------------------------------------------------------------
[System.IO.File]::WriteAllText((Resolve-Path $file).Path, $content, [System.Text.Encoding]::UTF8)
Write-Host "`n$fixes fix(es) applied to $file" -ForegroundColor Cyan
