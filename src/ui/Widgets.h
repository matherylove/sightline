#pragma once
#include "imgui.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include <string>

namespace Widgets {

// Full-width flat button styled like NewPipe drawer item
inline bool DrawerItem(const char* icon, const char* label, bool selected) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 40.0f;
    if (selected) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + w, p.y + h),
            ImGui::ColorConvertFloat4ToU32(Theme::COL_SELECTED));
    }
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_CONTRAST_V4);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_ACCENT_V4);
    // Use a single space + label to avoid uneven icon-text gap
    char buf[128]; snprintf(buf, sizeof(buf), "%s %s", icon, label);
    bool clicked = ImGui::Button(buf, ImVec2(w, h));
    ImGui::PopStyleColor(3);
    return clicked;
}

// Tab button with accent underline indicator (NewPipe style)
// Height unified to 30px across all usages.
inline bool TabButton(const char* label, bool active, float w) {
    const float TAB_H = 30.0f;
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        Theme::COL_ACCENT_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_ACCENT_HOV_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_ACCENT_HOV_V4);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1,1,1,1));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::COL_CONTRAST_V4);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Theme::COL_CONTRAST_V4);
        ImGui::PushStyleColor(ImGuiCol_Text,          Theme::COL_TEXT_DIM_V4);
    }
    bool clicked = ImGui::Button(label, ImVec2(w, TAB_H));
    ImGui::PopStyleColor(4);
    if (active) {
        ImVec2 p  = ImGui::GetItemRectMin();
        ImVec2 pb = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(p.x, pb.y - 3), ImVec2(pb.x, pb.y),
            ImGui::ColorConvertFloat4ToU32(Theme::COL_ACCENT_V4));
    }
    return clicked;
}

// Placeholder for unimplemented sections
inline void ComingSoon(const char* label) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.35f);
    float textW = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX((avail.x - textW) * 0.5f);
    ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", label);
}

// Returns a copy of `text` truncated with "..." so it fits within `maxWidth`
// pixels. Binary-search walks UTF-8 multibyte character boundaries.
static inline std::string TruncateText(const char* text, float maxWidth) {
    if (!text || maxWidth <= 0.0f) return text ? text : "";
    ImVec2 full = ImGui::CalcTextSize(text);
    if (full.x <= maxWidth) return text;
    const float ellipsisW = ImGui::CalcTextSize("...").x;
    const float budget    = maxWidth - ellipsisW;
    if (budget <= 0.0f) return "...";
    std::string s(text);
    std::vector<int> charStarts;
    charStarts.reserve(s.size());
    for (int i = 0; i < (int)s.size(); ) {
        charStarts.push_back(i);
        unsigned char c = (unsigned char)s[i];
        if      (c < 0x80)  i += 1;
        else if (c < 0xE0)  i += 2;
        else if (c < 0xF0)  i += 3;
        else                i += 4;
    }
    if (charStarts.empty()) return "...";
    int lo = 0, hi = (int)charStarts.size();
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        int byteOff = charStarts[mid];
        if (ImGui::CalcTextSize(s.c_str(), s.c_str() + byteOff).x <= budget)
            lo = mid;
        else
            hi = mid - 1;
    }
    int cutByte = (lo < (int)charStarts.size()) ? charStarts[lo] : (int)s.size();
    return s.substr(0, cutByte) + "...";
}

// -----------------------------------------------------------------------
// NewPipe-style video card with async thumbnail
// 3-level text hierarchy: title (COL_TEXT) > channel (COL_TEXT_DIM_V4) > meta (COL_TEXT_FAINT)
// -----------------------------------------------------------------------
inline bool VideoCard(int id,
                      const char* title,
                      const char* channel,
                      const char* duration,
                      const char* views,
                      bool        selected,
                      const char* videoId = nullptr) {
    const float THUMB_W = 160.0f;
    const float THUMB_H = 90.0f;
    const float PAD     = 10.0f;
    const float CARD_H  = THUMB_H + PAD * 2;

    ImGui::PushID(id);
    float cardW = ImGui::GetContentRegionAvail().x;

    ImVec2 cardPos = ImGui::GetCursorScreenPos();
    ImU32 bgCol = selected
        ? ImGui::ColorConvertFloat4ToU32(Theme::COL_SELECTED)
        : ImGui::ColorConvertFloat4ToU32(Theme::COL_CARD);
    ImGui::GetWindowDrawList()->AddRectFilled(
        cardPos, ImVec2(cardPos.x + cardW, cardPos.y + CARD_H), bgCol, 4.0f);

    bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardW, CARD_H));
    if (ImGui::IsItemHovered() && !selected)
        ImGui::GetWindowDrawList()->AddRectFilled(
            cardPos, ImVec2(cardPos.x + cardW, cardPos.y + CARD_H),
            IM_COL32(255,255,255,10), 4.0f);

    ImVec2 tMin = ImVec2(cardPos.x + PAD, cardPos.y + PAD);
    ImVec2 tMax = ImVec2(tMin.x + THUMB_W, tMin.y + THUMB_H);

    IDirect3DTexture9* thumbTex = nullptr;
    if (videoId && videoId[0] && ThumbnailCache::s_instance)
        thumbTex = ThumbnailCache::s_instance->Get(videoId);

    if (thumbTex) {
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(uintptr_t)thumbTex,
            tMin, tMax, ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,255));
        ImGui::GetWindowDrawList()->AddRect(
            tMin, tMax,
            ImGui::ColorConvertFloat4ToU32(Theme::COL_CARD), 3.0f, 0, 2.0f);
    } else {
        ImGui::GetWindowDrawList()->AddRectFilled(
            tMin, tMax, IM_COL32(30,30,30,255), 3.0f);
        if (videoId && videoId[0]) {
            int t = (int)(ImGui::GetTime() * 3.0) % 3;
            const char* dots[] = { ".  ", ".. ", "..." };
            ImVec2 dsz = ImGui::CalcTextSize(dots[t]);
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(tMin.x + (THUMB_W - dsz.x) * 0.5f,
                       tMin.y + (THUMB_H - dsz.y) * 0.5f),
                IM_COL32(120,120,120,255), dots[t]);
        } else {
            ImVec2 ip = ImVec2(tMin.x + THUMB_W*0.5f - 7, tMin.y + THUMB_H*0.5f - 8);
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                ip, ImVec2(ip.x, ip.y + 16), ImVec2(ip.x + 13, ip.y + 8),
                IM_COL32(255,255,255,80));
        }
    }

    // Duration badge — teal accent, bottom-right of thumb
    if (duration && duration[0]) {
        ImVec2 dsz  = ImGui::CalcTextSize(duration);
        float  bw   = dsz.x + 8, bh = dsz.y + 4;
        ImVec2 bMin = ImVec2(tMax.x - bw - 4, tMax.y - bh - 4);
        ImVec2 bMax = ImVec2(tMax.x - 4, tMax.y - 4);
        ImGui::GetWindowDrawList()->AddRectFilled(
            bMin, bMax,
            ImGui::ColorConvertFloat4ToU32(Theme::COL_ACCENT_V4), 3.0f);
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(bMin.x + 4, bMin.y + 2), IM_COL32(255,255,255,255), duration);
    }

    // Text area: 3-level hierarchy
    //   title   -> COL_TEXT         (primary)
    //   channel -> COL_TEXT_DIM_V4  (secondary)
    //   meta    -> COL_TEXT_FAINT   (tertiary)
    ImFont* font     = ImGui::GetFont();
    float   fontSize = ImGui::GetFontSize();
    float lineH = ImGui::GetTextLineHeight();
    float tx      = tMax.x + PAD;
    float ty      = cardPos.y + PAD;
    float maxTxtW = cardPos.x + cardW - tx - PAD;

    std::string titleStr   = TruncateText(title,   maxTxtW);
    std::string channelStr = TruncateText(channel, maxTxtW);

    char metaBuf[256];
    snprintf(metaBuf, sizeof(metaBuf), "%s", views ? views : "");
    std::string metaStr = TruncateText(metaBuf, maxTxtW);

    // Row 1: title (primary)
    ImGui::GetWindowDrawList()->AddText(
        font, fontSize, ImVec2(tx, ty),
        ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT), titleStr.c_str());
    // Row 2: channel (secondary)
    ImGui::GetWindowDrawList()->AddText(
        font, fontSize, ImVec2(tx, ty + lineH * 1.4f),
        ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT_DIM_V4), channelStr.c_str());
    // Row 3: views / meta (tertiary)
    ImGui::GetWindowDrawList()->AddText(
        font, fontSize, ImVec2(tx, ty + lineH * 2.8f),
        ImGui::ColorConvertFloat4ToU32(Theme::COL_TEXT_FAINT), metaStr.c_str());

    ImGui::SetCursorScreenPos(ImVec2(cardPos.x, cardPos.y + CARD_H));
    // Tighter inter-card gap (4px instead of 6px)
    ImGui::Dummy(ImVec2(cardW, 4.0f));

    ImGui::PopID();
    return clicked;
}

// Legacy flat table row (kept for compat)
inline bool VideoRow(int id, const char* title, const char* channel,
                     const char* duration, const char* views, bool selected) {
    ImGui::PushID(id);
    float w = ImGui::GetContentRegionAvail().x;
    float c0 = w*0.50f, c1 = w*0.75f, c2 = w*0.87f;
    bool clicked = ImGui::Selectable(title, selected,
        ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 22));
    ImGui::SameLine(c0); ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", channel);
    ImGui::SameLine(c1); ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", duration);
    ImGui::SameLine(c2); ImGui::TextColored(Theme::COL_TEXT_DIM_V4, "%s", views);
    ImGui::PopID();
    return clicked;
}

} // namespace Widgets
